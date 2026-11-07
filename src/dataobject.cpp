// SPDX-License-Identifier: GPL-2.0-or-later
#include "dataobject.h"

#include "ball.h"
#include "ballstep.h"
#include "game.h"
#include "overlay.h"
#include "shape.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QXmlStreamReader>
#include <QtMath>
#include <cmath>

namespace
{
constexpr qsizetype MaximumDefinitionBytes = 64 * 1024;
constexpr qsizetype MaximumSpriteBytes = 256 * 1024;
constexpr qsizetype MaximumEmbeddedImageBytes = 1024 * 1024;

const QStringList &componentTypes()
{
	static const QStringList types{QStringLiteral("surface"), QStringLiteral("force"), QStringLiteral("boost"),
		QStringLiteral("hazard"), QStringLiteral("sound"), QStringLiteral("decor"), QStringLiteral("solid"),
		QStringLiteral("trigger"), QStringLiteral("receiver"), QStringLiteral("motion")};
	return types;
}

const QStringList &plannedComponentTypes()
{
	static const QStringList types{QStringLiteral("animation"), QStringLiteral("teleport"),
		QStringLiteral("capture"), QStringLiteral("body")};
	return types;
}

struct ParameterRule
{
	QString name;
	enum Kind { Number, Integer, Angle, Choice, Text, Bool } kind;
	double minimum = 0;
	double maximum = 0;
	QStringList choices;
	bool required = false;
};

QList<ParameterRule> parameterRules(const QString &type)
{
	if (type == QLatin1String("surface"))
		return {{QStringLiteral("friction"), ParameterRule::Number, 0.05, 20, {}, true},
			{QStringLiteral("priority"), ParameterRule::Integer, 0, 10, {}, false}};
	if (type == QLatin1String("force"))
		return {{QStringLiteral("direction"), ParameterRule::Angle, 0, 359, {}, false},
			{QStringLiteral("strength"), ParameterRule::Number, 0, 20, {}, true}};
	if (type == QLatin1String("boost"))
		return {{QStringLiteral("direction"), ParameterRule::Angle, 0, 359, {}, false},
			{QStringLiteral("impulse"), ParameterRule::Number, 0, 10, {}, true},
			{QStringLiteral("cooldown"), ParameterRule::Integer, 0, 600, {}, false}};
	if (type == QLatin1String("hazard"))
		return {{QStringLiteral("outcome"), ParameterRule::Choice, 0, 0, {QStringLiteral("water")}, true}};
	if (type == QLatin1String("sound"))
		return {{QStringLiteral("on"), ParameterRule::Choice, 0, 0, {QStringLiteral("boost"), QStringLiteral("hazard")}, true},
			{QStringLiteral("key"), ParameterRule::Choice, 0, 0, {QStringLiteral("blackhole"), QStringLiteral("bumper"),
				QStringLiteral("hit"), QStringLiteral("puddle"), QStringLiteral("wall"), QStringLiteral("woohoo")}, true}};
	if (type == QLatin1String("trigger"))
		return {{QStringLiteral("on"), ParameterRule::Choice, 0, 0, {QStringLiteral("occupied"), QStringLiteral("enter"),
				QStringLiteral("exit"), QStringLiteral("stop")}, true},
			{QStringLiteral("channel"), ParameterRule::Text, 0, 0, {}, true}};
	if (type == QLatin1String("receiver"))
		return {{QStringLiteral("channel"), ParameterRule::Text, 0, 0, {}, true},
			{QStringLiteral("action"), ParameterRule::Choice, 0, 0, {QStringLiteral("open"), QStringLiteral("close"),
				QStringLiteral("toggle"), QStringLiteral("start"), QStringLiteral("stop")}, true},
			{QStringLiteral("mode"), ParameterRule::Choice, 0, 0, {QStringLiteral("latch"), QStringLiteral("momentary")}, false}};
	if (type == QLatin1String("motion"))
		return {{QStringLiteral("dx"), ParameterRule::Number, -1000, 1000, {}, false},
			{QStringLiteral("dy"), ParameterRule::Number, -1000, 1000, {}, false},
			{QStringLiteral("speed"), ParameterRule::Number, 0, 10, {}, true},
			{QStringLiteral("autostart"), ParameterRule::Bool, 0, 0, {}, false}};
	return {};
}

QString categoryError(const QString &category, Kolf::ObjectCategory &result)
{
	static const QList<Kolf::ObjectCategory> all{Kolf::ObjectCategory::Course, Kolf::ObjectCategory::Terrain,
		Kolf::ObjectCategory::Obstacle, Kolf::ObjectCategory::Hazard, Kolf::ObjectCategory::Mechanism,
		Kolf::ObjectCategory::Decoration};
	for (const auto candidate : all) {
		if (Kolf::categoryId(candidate) == category) {
			result = candidate;
			return {};
		}
	}
	return QStringLiteral("category must be terrain, obstacle, hazard, mechanism, decoration or course");
}

bool safeReference(const QString &value)
{
	if (value.startsWith(QLatin1Char('#')))
		return true;
	static const QRegularExpression image(QStringLiteral("^data:image/(png|jpeg|gif);base64,([A-Za-z0-9+/=\\s]*)$"));
	const auto match = image.match(value);
	return match.hasMatch() && QByteArray::fromBase64(match.captured(2).toLatin1()).size() <= MaximumEmbeddedImageBytes;
}

bool safeStyle(const QString &style)
{
	const QString lower = style.toLower();
	if (lower.contains(QLatin1String("@import")) || lower.contains(QLatin1String("expression(")))
		return false;
	static const QRegularExpression url(QStringLiteral("url\\(\\s*['\"]?([^'\")]*)"));
	auto it = url.globalMatch(style);
	while (it.hasNext())
		if (!safeReference(it.next().captured(1).trimmed()))
			return false;
	return true;
}
}

QString Kolf::sanitizeSvg(const QByteArray &svg)
{
	if (svg.isEmpty())
		return QStringLiteral("sprite is empty");
	if (svg.size() > MaximumSpriteBytes)
		return QStringLiteral("sprite is larger than 256 KiB");
	static const QStringList forbidden{QStringLiteral("script"), QStringLiteral("foreignobject"), QStringLiteral("iframe"),
		QStringLiteral("object"), QStringLiteral("embed"), QStringLiteral("audio"), QStringLiteral("video"),
		QStringLiteral("handler"), QStringLiteral("listener")};
	QXmlStreamReader reader(svg);
	bool inStyle = false;
	bool sawRoot = false;
	while (!reader.atEnd()) {
		switch (reader.readNext()) {
		case QXmlStreamReader::DTD:
			return QStringLiteral("sprite must not contain a DOCTYPE or entities");
		case QXmlStreamReader::EntityReference:
			return QStringLiteral("sprite must not use entities");
		case QXmlStreamReader::ProcessingInstruction:
			return QStringLiteral("sprite must not contain processing instructions");
		case QXmlStreamReader::StartElement: {
			const QString name = reader.name().toString().toLower();
			if (!sawRoot && name != QLatin1String("svg"))
				return QStringLiteral("sprite root element must be <svg>");
			sawRoot = true;
			if (forbidden.contains(name))
				return QStringLiteral("sprite must not contain <%1>").arg(reader.name().toString());
			inStyle = name == QLatin1String("style");
			for (const auto &attribute : reader.attributes()) {
				const QString attributeName = attribute.name().toString().toLower();
				const QString value = attribute.value().toString();
				if (attributeName.startsWith(QLatin1String("on")))
					return QStringLiteral("sprite must not contain event handlers (%1)").arg(attribute.qualifiedName().toString());
				if (attributeName == QLatin1String("href") && !safeReference(value.trimmed()))
					return QStringLiteral("sprite must not reference external files or URLs");
				if (!safeStyle(value))
					return QStringLiteral("sprite must not reference external files or URLs");
			}
			break;
		}
		case QXmlStreamReader::Characters:
			if (inStyle && !safeStyle(reader.text().toString()))
				return QStringLiteral("sprite must not reference external files or URLs");
			break;
		case QXmlStreamReader::EndElement:
			inStyle = false;
			break;
		default:
			break;
		}
	}
	if (reader.hasError())
		return QStringLiteral("sprite is not well-formed XML: %1").arg(reader.errorString());
	if (!sawRoot)
		return QStringLiteral("sprite has no <svg> element");
	return {};
}

std::shared_ptr<const Kolf::DataObjectDefinition> Kolf::DataObjectLibrary::parse(const QByteArray &json, const QString &file, const QByteArray &sprite, QString &error)
{
	const auto fail = [&](const QString &path, const QString &reason) {
		error = path.isEmpty() ? QStringLiteral("%1: %2").arg(file, reason) : QStringLiteral("%1: %2: %3").arg(file, path, reason);
		return nullptr;
	};
	if (json.size() > MaximumDefinitionBytes)
		return fail({}, QStringLiteral("definition is larger than 64 KiB"));
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
	if (parseError.error != QJsonParseError::NoError)
		return fail({}, QStringLiteral("not valid JSON (%1 at offset %2)").arg(parseError.errorString()).arg(parseError.offset));
	if (!document.isObject())
		return fail({}, QStringLiteral("the definition must be a JSON object"));
	const QJsonObject root = document.object();
	static const QStringList rootKeys{QStringLiteral("kolfObject"), QStringLiteral("id"), QStringLiteral("name"),
		QStringLiteral("category"), QStringLiteral("help"), QStringLiteral("affectsGameplay"), QStringLiteral("shape"),
		QStringLiteral("visual"), QStringLiteral("properties"), QStringLiteral("components")};
	for (const auto &key : root.keys())
		if (!rootKeys.contains(key))
			return fail(key, QStringLiteral("unknown key"));
	if (root.value(QStringLiteral("kolfObject")).toInt(-1) != 1 || !root.value(QStringLiteral("kolfObject")).isDouble())
		return fail(QStringLiteral("kolfObject"), QStringLiteral("must be 1"));

	auto definition = std::make_shared<DataObjectDefinition>();
	definition->file = file;
	definition->id = root.value(QStringLiteral("id")).toString();
	if (!ObjectRegistry::isValidId(definition->id))
		return fail(QStringLiteral("id"), QStringLiteral("must be lower case letters, digits and underscores, starting with a letter"));
	definition->name = root.value(QStringLiteral("name")).toString();
	if (definition->name.trimmed().isEmpty() || definition->name.size() > 64)
		return fail(QStringLiteral("name"), QStringLiteral("must be 1 to 64 characters"));
	definition->help = root.value(QStringLiteral("help")).toString();
	if (!root.value(QStringLiteral("help")).isUndefined() && (!root.value(QStringLiteral("help")).isString() || definition->help.size() > 500))
		return fail(QStringLiteral("help"), QStringLiteral("must be text of at most 500 characters"));
	ObjectCategory category = ObjectCategory::Obstacle;
	if (const QString problem = categoryError(root.value(QStringLiteral("category")).toString(), category); !problem.isEmpty())
		return fail(QStringLiteral("category"), problem);
	const QJsonValue gameplayValue = root.value(QStringLiteral("affectsGameplay"));
	if (!gameplayValue.isUndefined() && !gameplayValue.isBool())
		return fail(QStringLiteral("affectsGameplay"), QStringLiteral("must be true or false"));
	const bool affectsGameplay = gameplayValue.toBool(true);

	const QJsonObject shape = root.value(QStringLiteral("shape")).toObject();
	if (!root.value(QStringLiteral("shape")).isObject())
		return fail(QStringLiteral("shape"), QStringLiteral("must be an object"));
	definition->shape = shape.value(QStringLiteral("type")).toString();
	if (definition->shape != QLatin1String("rect") && definition->shape != QLatin1String("ellipse"))
		return fail(QStringLiteral("shape.type"), QStringLiteral("must be rect or ellipse"));
	const QJsonArray size = shape.value(QStringLiteral("size")).toArray();
	if (size.size() != 2 || !size.at(0).isDouble() || !size.at(1).isDouble()
		|| size.at(0).toDouble() < 1 || size.at(0).toDouble() > 2000 || size.at(1).toDouble() < 1 || size.at(1).toDouble() > 2000)
		return fail(QStringLiteral("shape.size"), QStringLiteral("must be [width, height] from 1 to 2000"));
	definition->size = QSizeF(size.at(0).toDouble(), size.at(1).toDouble());
	definition->resizable = shape.value(QStringLiteral("resizable")).toBool(false);
	definition->rotatable = shape.value(QStringLiteral("rotatable")).toBool(false);

	const QJsonObject visual = root.value(QStringLiteral("visual")).toObject();
	const QString spriteName = visual.value(QStringLiteral("sprite")).toString();
	static const QRegularExpression spritePattern(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}\\.svg$"));
	if (!spritePattern.match(spriteName).hasMatch())
		return fail(QStringLiteral("visual.sprite"), QStringLiteral("must be an .svg file name next to the definition"));
	definition->layer = visual.value(QStringLiteral("layer")).toString(QStringLiteral("ground"));
	if (!QStringList{QStringLiteral("ground"), QStringLiteral("raised"), QStringLiteral("canopy")}.contains(definition->layer))
		return fail(QStringLiteral("visual.layer"), QStringLiteral("must be ground, raised or canopy"));
	if (const QString problem = sanitizeSvg(sprite); !problem.isEmpty())
		return fail(QStringLiteral("visual.sprite"), problem);
	definition->sprite = std::make_shared<QSvgRenderer>(sprite);
	if (!definition->sprite->isValid())
		return fail(QStringLiteral("visual.sprite"), QStringLiteral("is not a valid SVG image"));

	ObjectType type = ObjectType::withPlainName(definition->id, definition->name);
	type.category(category);
	type.plainHelp(definition->help);
	{
		const auto renderer = definition->sprite;
		type.iconFactory([renderer](int size) {
			QPixmap pixmap(size, size);
			pixmap.fill(Qt::transparent);
			QPainter painter(&pixmap);
			renderer->render(&painter, QRectF(2, 2, size - 4, size - 4));
			return QIcon(pixmap);
		});
	}
	if (!affectsGameplay)
		type.decoration();
	QMap<QString, PropertyType> propertyTypes;
	QMap<QString, QStringList> propertyChoices;
	if (definition->resizable) {
		type.property(PropertySpec::number(QStringLiteral("width"), kli18n("Width"), definition->size.width(), 1, 2000, 1));
		type.property(PropertySpec::number(QStringLiteral("height"), kli18n("Height"), definition->size.height(), 1, 2000, 1));
		propertyTypes.insert(QStringLiteral("width"), PropertyType::Number);
		propertyTypes.insert(QStringLiteral("height"), PropertyType::Number);
	}
	if (definition->rotatable) {
		type.property(PropertySpec::angle(QStringLiteral("rotation"), kli18n("Rotation"), 0));
		propertyTypes.insert(QStringLiteral("rotation"), PropertyType::Angle);
	}
	const QJsonValue propertiesValue = root.value(QStringLiteral("properties"));
	if (!propertiesValue.isUndefined() && !propertiesValue.isObject())
		return fail(QStringLiteral("properties"), QStringLiteral("must be an object"));
	const QJsonObject properties = propertiesValue.toObject();
	if (properties.size() > 32)
		return fail(QStringLiteral("properties"), QStringLiteral("at most 32 properties"));
	static const QRegularExpression propertyName(QStringLiteral("^[a-z][A-Za-z0-9_]{0,31}$"));
	for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
		const QString path = QStringLiteral("properties.%1").arg(it.key());
		if (!propertyName.match(it.key()).hasMatch())
			return fail(path, QStringLiteral("property names start with a lower case letter and use letters, digits and underscores"));
		if (QStringList{QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("rotation"), QStringLiteral("dummykey")}.contains(it.key()))
			return fail(path, QStringLiteral("is reserved; use shape.resizable or shape.rotatable"));
		if (!it.value().isObject())
			return fail(path, QStringLiteral("must be an object"));
		QString problem;
		const auto spec = PropertySpec::fromJson(it.key(), it.value().toObject(), problem);
		if (!spec)
			return fail(path, problem);
		type.property(*spec);
		propertyTypes.insert(it.key(), spec->type());
		propertyChoices.insert(it.key(), spec->choices());
	}

	const QJsonValue componentsValue = root.value(QStringLiteral("components"));
	const QJsonArray components = componentsValue.toArray();
	if (!componentsValue.isArray() || components.isEmpty() || components.size() > 16)
		return fail(QStringLiteral("components"), QStringLiteral("must list 1 to 16 components"));
	bool gameplayComponent = false;
	bool boost = false;
	for (int i = 0; i < components.size(); ++i) {
		const QString path = QStringLiteral("components[%1]").arg(i);
		if (!components.at(i).isObject())
			return fail(path, QStringLiteral("must be an object"));
		const QJsonObject component = components.at(i).toObject();
		const QString componentType = component.value(QStringLiteral("type")).toString();
		if (plannedComponentTypes().contains(componentType))
			return fail(path + QStringLiteral(".type"), QStringLiteral("'%1' is not available yet").arg(componentType));
		if (!componentTypes().contains(componentType))
			return fail(path + QStringLiteral(".type"), QStringLiteral("unknown component '%1'").arg(componentType));
		const auto rules = parameterRules(componentType);
		for (const auto &key : component.keys()) {
			if (key == QLatin1String("type"))
				continue;
			const auto rule = std::find_if(rules.cbegin(), rules.cend(), [&key](const ParameterRule &r) { return r.name == key; });
			if (rule == rules.cend())
				return fail(path + QLatin1Char('.') + key, QStringLiteral("unknown parameter"));
			const QJsonValue value = component.value(key);
			if (value.isString() && value.toString().startsWith(QLatin1Char('$'))) {
				const QString bound = value.toString().mid(1);
				if (!propertyTypes.contains(bound))
					return fail(path + QLatin1Char('.') + key, QStringLiteral("'%1' is not a property").arg(value.toString()));
				const PropertyType boundType = propertyTypes.value(bound);
				bool compatible = false;
				switch (rule->kind) {
				case ParameterRule::Number:
				case ParameterRule::Integer:
				case ParameterRule::Angle:
					compatible = boundType == PropertyType::Number || boundType == PropertyType::Int || boundType == PropertyType::Angle;
					break;
				case ParameterRule::Choice: {
					compatible = boundType == PropertyType::Choice;
					for (const auto &choice : propertyChoices.value(bound))
						compatible = compatible && rule->choices.contains(choice);
					break;
				}
				case ParameterRule::Text:
					compatible = boundType == PropertyType::Text || boundType == PropertyType::Choice;
					break;
				case ParameterRule::Bool:
					compatible = boundType == PropertyType::Bool;
					break;
				}
				if (!compatible)
					return fail(path + QLatin1Char('.') + key, QStringLiteral("'%1' has the wrong type for this parameter").arg(value.toString()));
				continue;
			}
			if (rule->kind == ParameterRule::Choice) {
				if (!rule->choices.contains(value.toString()))
					return fail(path + QLatin1Char('.') + key, QStringLiteral("must be one of %1").arg(rule->choices.join(QStringLiteral(", "))));
				continue;
			}
			if (rule->kind == ParameterRule::Text) {
				static const QRegularExpression channel(QStringLiteral("^[A-Za-z0-9_-]{1,32}$"));
				if (!value.isString() || !channel.match(value.toString()).hasMatch())
					return fail(path + QLatin1Char('.') + key, QStringLiteral("must be a channel name (letters, digits, - and _) or a $property"));
				continue;
			}
			if (rule->kind == ParameterRule::Bool) {
				if (!value.isBool())
					return fail(path + QLatin1Char('.') + key, QStringLiteral("must be true or false or a $property"));
				continue;
			}
			const double number = value.toDouble(qQNaN());
			if (!value.isDouble() || !std::isfinite(number) || number < rule->minimum || number > rule->maximum
				|| (rule->kind != ParameterRule::Number && number != std::floor(number)))
				return fail(path + QLatin1Char('.') + key, QStringLiteral("must be a %1 from %2 to %3 or a $property")
					.arg(rule->kind == ParameterRule::Number ? QStringLiteral("number") : QStringLiteral("whole number"))
					.arg(rule->minimum).arg(rule->maximum));
		}
		for (const auto &rule : rules)
			if (rule.required && !component.contains(rule.name))
				return fail(path + QLatin1Char('.') + rule.name, QStringLiteral("is required"));
		if ((componentType == QLatin1String("force") || componentType == QLatin1String("boost"))
			&& !component.contains(QStringLiteral("direction")) && !definition->rotatable)
			return fail(path + QStringLiteral(".direction"), QStringLiteral("is required unless the shape is rotatable"));
		if (componentType != QLatin1String("decor") && componentType != QLatin1String("sound"))
			gameplayComponent = true;
		boost = boost || componentType == QLatin1String("boost");
		definition->components.append({componentType, component});
	}
	if (affectsGameplay && !gameplayComponent)
		return fail(QStringLiteral("affectsGameplay"), QStringLiteral("must be false when no component affects the ball"));
	if (!affectsGameplay && gameplayComponent)
		return fail(QStringLiteral("affectsGameplay"), QStringLiteral("must be true when a component affects the ball"));
	QStringList runtime;
	if (boost)
		runtime << QStringLiteral("lastBoost");
	const auto uses = [&definition](const char *component) {
		return std::any_of(definition->components.cbegin(), definition->components.cend(),
			[component](const DataComponent &c) { return c.type == QLatin1String(component); });
	};
	if (uses("trigger"))
		runtime << QStringLiteral("occupied");
	if (uses("receiver"))
		runtime << QStringLiteral("open");
	if (uses("motion"))
		runtime << QStringLiteral("t") << QStringLiteral("direction") << QStringLiteral("running");
	type.runtime(runtime);

	std::shared_ptr<const DataObjectDefinition> shared = definition;
	type.creator([shared](QGraphicsItem *parent, b2World *world) -> QGraphicsItem * {
		return new DataObject(shared, parent, world);
	});
	if (ObjectRegistry::instance().find(definition->id))
		return fail(QStringLiteral("id"), QStringLiteral("'%1' is already registered").arg(definition->id));
	if (!ObjectRegistry::instance().add(type))
		return fail(QStringLiteral("id"), QStringLiteral("could not be registered"));
	return shared;
}

bool Kolf::DataObjectLibrary::loadFile(const QString &path, QString &error)
{
	const QFileInfo info(path);
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		error = QStringLiteral("%1: cannot be read").arg(info.fileName());
		return false;
	}
	const QByteArray json = file.read(MaximumDefinitionBytes + 1);
	const QJsonObject root = QJsonDocument::fromJson(json).object();
	QByteArray sprite;
	const QString spriteName = root.value(QStringLiteral("visual")).toObject().value(QStringLiteral("sprite")).toString();
	if (!spriteName.isEmpty() && !spriteName.contains(QLatin1Char('/')) && !spriteName.contains(QLatin1Char('\\'))) {
		QFile spriteFile(info.dir().filePath(spriteName));
		if (spriteFile.open(QIODevice::ReadOnly))
			sprite = spriteFile.read(MaximumSpriteBytes + 1);
	}
	return bool(parse(json, info.fileName(), sprite, error));
}

QStringList Kolf::DataObjectLibrary::loadDirectory(const QString &directory)
{
	QStringList errors;
	const auto entries = QDir(directory).entryInfoList({QStringLiteral("*.kobj.json")}, QDir::Files, QDir::Name);
	for (const auto &entry : entries) {
		QString error;
		if (!loadFile(entry.absoluteFilePath(), error))
			errors << error;
	}
	return errors;
}

QStringList Kolf::DataObjectLibrary::loadInstalled()
{
	QStringList directories = QStandardPaths::locateAll(QStandardPaths::AppDataLocation, QStringLiteral("objects"), QStandardPaths::LocateDirectory);
	for (const auto &extra : qEnvironmentVariable("KOLF_OBJECT_DIRS").split(QDir::listSeparator(), Qt::SkipEmptyParts))
		directories.prepend(extra);
	QStringList errors;
	for (const auto &directory : std::as_const(directories))
		errors << loadDirectory(directory);
	return errors;
}

namespace
{
class DataObjectOverlay : public Kolf::Overlay
{
public:
	explicit DataObjectOverlay(Kolf::DataObject *object)
		: Kolf::Overlay(object, object, true)
		, m_object(object)
	{
		if (object->isResizable()) {
			for (int i = 0; i < 4; ++i) {
				auto *handle = new Kolf::OverlayHandle(Kolf::OverlayHandle::SquareShape, this);
				m_corners << handle;
				addHandle(handle);
				QObject::connect(handle, &Kolf::OverlayHandle::moveRequest, this, [this](const QPointF &scenePos) { resizeTo(scenePos); });
			}
		}
		if (object->isRotatable()) {
			m_rotate = new Kolf::OverlayHandle(Kolf::OverlayHandle::CircleShape, this);
			addHandle(m_rotate);
			QObject::connect(m_rotate, &Kolf::OverlayHandle::moveRequest, this, [this](const QPointF &scenePos) { rotateTowards(scenePos); });
		}
	}

	void update() override
	{
		Kolf::Overlay::update();
		const QRectF rect = m_object->boundingRect();
		const QList<QPointF> corners{rect.topLeft(), rect.topRight(), rect.bottomLeft(), rect.bottomRight()};
		for (int i = 0; i < m_corners.size(); ++i)
			m_corners[i]->setPos(mapFromItem(m_object, corners[i]));
		if (m_rotate)
			m_rotate->setPos(mapFromItem(m_object, QPointF(0, rect.top() - 18)));
	}

private:
	void resizeTo(const QPointF &scenePos)
	{
		const QPointF local = m_object->mapFromScene(scenePos);
		const qreal minimum = Kolf::Overlay::MinimumObjectDimension;
		m_object->setSize(QSizeF(qMax(minimum, 2 * std::abs(local.x())), qMax(minimum, 2 * std::abs(local.y()))));
		update();
	}

	void rotateTowards(const QPointF &scenePos)
	{
		const QPointF delta = scenePos - m_object->scenePos();
		int degrees = qRound(qRadiansToDegrees(std::atan2(delta.y(), delta.x())) + 90);
		degrees = ((degrees % 360) + 360) % 360;
		m_object->applyProperties({{QStringLiteral("rotation"), degrees}});
		update();
	}

	Kolf::DataObject *m_object;
	QList<Kolf::OverlayHandle *> m_corners;
	Kolf::OverlayHandle *m_rotate = nullptr;
};
}

Kolf::DataObject::DataObject(std::shared_ptr<const DataObjectDefinition> definition, QGraphicsItem *parent, b2World *world)
	: QGraphicsObject(parent)
	, CanvasItem(world)
	, m_definition(std::move(definition))
	, m_size(m_definition->size)
{
	if (const auto *type = ObjectRegistry::instance().find(m_definition->id))
		m_values = type->defaults();
	if (m_definition->layer == QLatin1String("raised"))
		setZBehavior(CanvasItem::IsRaisedByStrut, 4);
	else if (m_definition->layer == QLatin1String("canopy"))
		setZBehavior(CanvasItem::FixedZValue, 5000);
	else
		setZBehavior(CanvasItem::FixedZValue, 2);
	if (hasComponent(QStringLiteral("solid"))) {
		m_solidShape = new Kolf::RectShape(boundingRect());
		addShape(m_solidShape);
		setSimulationType(CanvasItem::CollisionSimulation);
	} else {
		setSimulationType(CanvasItem::NoSimulation);
	}
	for (const auto &component : m_definition->components)
		if (component.type == QLatin1String("motion"))
			m_running = parameter(component, QStringLiteral("autostart"), true).toBool();
	updateGeometry();
}

QStringList Kolf::DataObject::channels(const QString &componentType) const
{
	QStringList result;
	for (const auto &component : m_definition->components)
		if (component.type == componentType)
			result << parameter(component, QStringLiteral("channel"), QString()).toString();
	return result;
}

bool Kolf::DataObject::hasComponent(const QString &type) const
{
	for (const auto &component : m_definition->components)
		if (component.type == type)
			return true;
	return false;
}

void Kolf::DataObject::setGame(KolfGame *game)
{
	CanvasItem::setGame(game);
	if (game && (hasComponent(QStringLiteral("trigger")) || hasComponent(QStringLiteral("motion"))))
		connect(game, &KolfGame::simulationStepped, this, &DataObject::step, Qt::UniqueConnection);
}

void Kolf::DataObject::step()
{
	if (!game || !game->maySimulate())
		return;
	if (!m_baseSet) {
		m_basePosition = pos();
		m_baseSet = true;
	}
	for (const auto &component : m_definition->components) {
		if (component.type != QLatin1String("motion") || !m_running)
			continue;
		const QPointF offset(parameter(component, QStringLiteral("dx"), 0).toDouble(), parameter(component, QStringLiteral("dy"), 0).toDouble());
		const double length = std::hypot(offset.x(), offset.y());
		if (length <= 0)
			continue;
		m_t += m_direction * parameter(component, QStringLiteral("speed"), 0).toDouble() / length;
		if (m_t >= 1) { m_t = 1; m_direction = -1; }
		if (m_t <= 0) { m_t = 0; m_direction = 1; }
		const QPointF target = m_basePosition + offset * m_t;
		moveBy(target.x() - x(), target.y() - y());
	}
	if (!hasComponent(QStringLiteral("trigger")))
		return;
	QList<Ball *> inside, stopped;
	for (Ball *ball : game->balls()) {
		if (!ball->isVisible() || ball->curState() == Holed || !containsPoint(ball->pos()))
			continue;
		inside << ball;
		if (ball->curState() == Stopped)
			stopped << ball;
	}
	if (m_resyncInside) {
		m_resyncInside = false;
		m_insideBalls = inside;
		m_stoppedBalls = stopped;
		m_occupied = int(inside.size());
		return;
	}
	for (const auto &component : m_definition->components) {
		if (component.type != QLatin1String("trigger"))
			continue;
		const QString on = parameter(component, QStringLiteral("on"), QString()).toString();
		const QString channel = parameter(component, QStringLiteral("channel"), QString()).toString();
		if (on == QLatin1String("occupied")) {
			if (m_insideBalls.isEmpty() != inside.isEmpty())
				game->emitChannel(channel, !inside.isEmpty(), false);
		} else if (on == QLatin1String("enter")) {
			for (Ball *ball : std::as_const(inside))
				if (!m_insideBalls.contains(ball))
					game->emitChannel(channel, true, true);
		} else if (on == QLatin1String("exit")) {
			for (Ball *ball : std::as_const(m_insideBalls))
				if (!inside.contains(ball))
					game->emitChannel(channel, true, true);
		} else if (on == QLatin1String("stop")) {
			for (Ball *ball : std::as_const(stopped))
				if (!m_stoppedBalls.contains(ball))
					game->emitChannel(channel, true, true);
		}
	}
	m_insideBalls = inside;
	m_stoppedBalls = stopped;
	m_occupied = int(inside.size());
}

void Kolf::DataObject::receiveChannel(const QString &channel, bool level, bool pulse)
{
	for (const auto &component : m_definition->components) {
		if (component.type != QLatin1String("receiver") || parameter(component, QStringLiteral("channel"), QString()).toString() != channel)
			continue;
		const QString action = parameter(component, QStringLiteral("action"), QString()).toString();
		const bool momentary = parameter(component, QStringLiteral("mode"), QStringLiteral("latch")).toString() == QLatin1String("momentary");
		if (pulse || level) {
			perform(action);
		} else if (momentary) {
			static const QMap<QString, QString> inverse{{QStringLiteral("open"), QStringLiteral("close")},
				{QStringLiteral("close"), QStringLiteral("open")}, {QStringLiteral("start"), QStringLiteral("stop")},
				{QStringLiteral("stop"), QStringLiteral("start")}, {QStringLiteral("toggle"), QStringLiteral("toggle")}};
			perform(inverse.value(action));
		}
	}
}

void Kolf::DataObject::perform(const QString &action)
{
	if (action == QLatin1String("open"))
		setOpen(true);
	else if (action == QLatin1String("close"))
		setOpen(false);
	else if (action == QLatin1String("toggle"))
		setOpen(!m_open);
	else if (action == QLatin1String("start"))
		m_running = true;
	else if (action == QLatin1String("stop"))
		m_running = false;
}

void Kolf::DataObject::setOpen(bool open)
{
	m_open = open;
	if (m_solidShape)
		setSimulationType(open ? CanvasItem::NoSimulation : CanvasItem::CollisionSimulation);
	setOpacity(open ? 0.35 : 1.0);
}

QRectF Kolf::DataObject::boundingRect() const
{
	return QRectF(-m_size.width() / 2, -m_size.height() / 2, m_size.width(), m_size.height());
}

QPainterPath Kolf::DataObject::shape() const
{
	QPainterPath path;
	if (m_definition->shape == QLatin1String("ellipse"))
		path.addEllipse(boundingRect());
	else
		path.addRect(boundingRect());
	return path;
}

void Kolf::DataObject::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
	m_definition->sprite->render(painter, boundingRect());
}

void Kolf::DataObject::moveBy(double dx, double dy)
{
	QGraphicsObject::moveBy(dx, dy);
	CanvasItem::moveBy(dx, dy);
}

void Kolf::DataObject::setSize(const QSizeF &size)
{
	if (!m_definition->resizable || size.isEmpty())
		return;
	m_values.insert(QStringLiteral("width"), qBound(1.0, size.width(), 2000.0));
	m_values.insert(QStringLiteral("height"), qBound(1.0, size.height(), 2000.0));
	updateGeometry();
}

QVariantMap Kolf::DataObject::properties() const
{
	return m_values;
}

void Kolf::DataObject::applyProperties(const QVariantMap &values)
{
	for (auto it = values.cbegin(); it != values.cend(); ++it)
		if (m_values.contains(it.key()))
			m_values.insert(it.key(), it.value());
	updateGeometry();
}

QVariantMap Kolf::DataObject::runtimeState() const
{
	QVariantMap state;
	const auto *type = ObjectRegistry::instance().find(m_definition->id);
	const QStringList fields = type ? type->runtimeFields() : QStringList();
	if (fields.contains(QStringLiteral("lastBoost")))
		state.insert(QStringLiteral("lastBoost"), double(m_lastBoost));
	if (fields.contains(QStringLiteral("occupied")))
		state.insert(QStringLiteral("occupied"), m_occupied);
	if (fields.contains(QStringLiteral("open")))
		state.insert(QStringLiteral("open"), m_open);
	if (fields.contains(QStringLiteral("running")))
		state.insert(QStringLiteral("running"), m_running);
	if (fields.contains(QStringLiteral("t"))) {
		state.insert(QStringLiteral("t"), m_t);
		state.insert(QStringLiteral("direction"), m_direction);
	}
	return state;
}

void Kolf::DataObject::setRuntimeState(const QVariantMap &state)
{
	m_lastBoost = qint64(state.value(QStringLiteral("lastBoost"), double(m_lastBoost)).toDouble());
	m_occupied = state.value(QStringLiteral("occupied"), m_occupied).toInt();
	m_running = state.value(QStringLiteral("running"), m_running).toBool();
	m_t = qBound(0.0, state.value(QStringLiteral("t"), m_t).toDouble(), 1.0);
	m_direction = state.value(QStringLiteral("direction"), m_direction).toInt() < 0 ? -1 : 1;
	if (state.contains(QStringLiteral("open")))
		setOpen(state.value(QStringLiteral("open")).toBool());
	m_resyncInside = true;
}

void Kolf::DataObject::updateGeometry()
{
	prepareGeometryChange();
	if (m_definition->resizable)
		m_size = QSizeF(m_values.value(QStringLiteral("width")).toDouble(), m_values.value(QStringLiteral("height")).toDouble());
	if (m_definition->rotatable)
		setRotation(m_values.value(QStringLiteral("rotation")).toInt());
	if (m_solidShape) {
		m_solidShape->setRect(boundingRect());
		m_solidShape->setAngle(rotation());
	}
	update();
	propagateUpdate();
}

QVariant Kolf::DataObject::parameter(const DataComponent &component, const QString &name, const QVariant &fallback) const
{
	const QJsonValue value = component.parameters.value(name);
	if (value.isUndefined())
		return fallback;
	if (value.isString() && value.toString().startsWith(QLatin1Char('$')))
		return m_values.value(value.toString().mid(1), fallback);
	return value.toVariant();
}

bool Kolf::DataObject::containsPoint(const QPointF &point) const
{
	return shape().contains(mapFromParent(point));
}

bool Kolf::DataObject::isHazardAt(const QPointF &point) const
{
	for (const auto &component : m_definition->components)
		if (component.type == QLatin1String("hazard"))
			return isVisible() && containsPoint(point);
	return false;
}

void Kolf::DataObject::playSound(const QString &event)
{
	if (!game)
		return;
	for (const auto &component : m_definition->components) {
		if (component.type != QLatin1String("sound") || component.parameters.value(QStringLiteral("on")).toString() != event)
			continue;
		static const QMap<QString, Sound> sounds{{QStringLiteral("blackhole"), Sound::BlackHole}, {QStringLiteral("bumper"), Sound::Bumper},
			{QStringLiteral("hit"), Sound::Hit}, {QStringLiteral("puddle"), Sound::Puddle}, {QStringLiteral("wall"), Sound::Wall},
			{QStringLiteral("woohoo"), Sound::WooHoo}};
		game->playSound(sounds.value(component.parameters.value(QStringLiteral("key")).toString(), Sound::Hit));
	}
}

void Kolf::DataObject::onBall(BallStepContext &context)
{
	if (game && !game->maySimulate())
		return;
	Ball *ball = context.ball();
	if (!containsPoint(ball->pos()))
		return;
	const QString kind = m_definition->id;
	const double rotation = m_values.value(QStringLiteral("rotation"), 0).toDouble();
	for (const auto &component : m_definition->components) {
		if (component.type == QLatin1String("surface")) {
			context.surface(kind, parameter(component, QStringLiteral("friction"), 1).toDouble(),
				parameter(component, QStringLiteral("priority"), 1).toInt());
		} else if (component.type == QLatin1String("force")) {
			const double radians = parameter(component, QStringLiteral("direction"), rotation).toDouble() * M_PI / 180.0;
			const double strength = 0.013 * parameter(component, QStringLiteral("strength"), 0).toDouble();
			context.force(kind, Vector(QPointF(std::cos(radians), std::sin(radians)) * strength));
		} else if (component.type == QLatin1String("boost")) {
			const qint64 now = game ? qint64(game->physicsSteps()) : 0;
			if (now - m_lastBoost < parameter(component, QStringLiteral("cooldown"), 30).toInt())
				continue;
			m_lastBoost = now;
			const double radians = parameter(component, QStringLiteral("direction"), rotation).toDouble() * M_PI / 180.0;
			context.impulse(Vector(QPointF(std::cos(radians), std::sin(radians)) * parameter(component, QStringLiteral("impulse"), 0).toDouble()));
			playSound(QStringLiteral("boost"));
		} else if (component.type == QLatin1String("hazard")) {
			if (!context.claimContact())
				continue;
			if (game)
				game->playSound(Sound::Puddle);
			playSound(QStringLiteral("hazard"));
			ball->setAddStroke(ball->addStroke() + 1);
			ball->setPlaceOnGround(true);
			ball->setVisible(false);
			ball->setState(Stopped);
			ball->setVelocity(Vector());
			if (game && game->curBall() == ball)
				game->stoppedBall();
			context.blockForces();
			return;
		}
	}
}

Kolf::Overlay *Kolf::DataObject::createOverlay()
{
	return new DataObjectOverlay(this);
}

#include "moc_dataobject.cpp"
