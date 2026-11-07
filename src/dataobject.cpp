// SPDX-License-Identifier: GPL-2.0-or-later
#include "dataobject.h"

#include "ball.h"
#include "ballstep.h"
#include "game.h"
#include "overlay.h"

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
#include <cmath>

namespace
{
constexpr qsizetype MaximumDefinitionBytes = 64 * 1024;
constexpr qsizetype MaximumSpriteBytes = 256 * 1024;
constexpr qsizetype MaximumEmbeddedImageBytes = 1024 * 1024;

const QStringList &componentTypes()
{
	static const QStringList types{QStringLiteral("surface"), QStringLiteral("force"), QStringLiteral("boost"),
		QStringLiteral("hazard"), QStringLiteral("sound"), QStringLiteral("decor")};
	return types;
}

const QStringList &plannedComponentTypes()
{
	static const QStringList types{QStringLiteral("solid"), QStringLiteral("animation"), QStringLiteral("teleport"),
		QStringLiteral("capture"), QStringLiteral("motion"), QStringLiteral("body"), QStringLiteral("trigger"),
		QStringLiteral("receiver")};
	return types;
}

struct ParameterRule
{
	QString name;
	enum Kind { Number, Integer, Angle, Choice } kind;
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
	if (!affectsGameplay)
		type.decoration();
	QMap<QString, PropertyType> propertyTypes;
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
			if (value.isString() && value.toString().startsWith(QLatin1Char('$')) && rule->kind != ParameterRule::Choice) {
				const QString bound = value.toString().mid(1);
				if (!propertyTypes.contains(bound))
					return fail(path + QLatin1Char('.') + key, QStringLiteral("'%1' is not a property").arg(value.toString()));
				const PropertyType boundType = propertyTypes.value(bound);
				if (boundType != PropertyType::Number && boundType != PropertyType::Int && boundType != PropertyType::Angle)
					return fail(path + QLatin1Char('.') + key, QStringLiteral("'%1' is not a numeric property").arg(value.toString()));
				continue;
			}
			if (rule->kind == ParameterRule::Choice) {
				if (!rule->choices.contains(value.toString()))
					return fail(path + QLatin1Char('.') + key, QStringLiteral("must be one of %1").arg(rule->choices.join(QStringLiteral(", "))));
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
	if (boost)
		type.runtime({QStringLiteral("lastBoost")});

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
	setSimulationType(CanvasItem::NoSimulation);
	updateGeometry();
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
	for (const auto &component : m_definition->components)
		if (component.type == QLatin1String("boost"))
			return {{QStringLiteral("lastBoost"), double(m_lastBoost)}};
	return {};
}

void Kolf::DataObject::setRuntimeState(const QVariantMap &state)
{
	m_lastBoost = qint64(state.value(QStringLiteral("lastBoost"), double(m_lastBoost)).toDouble());
}

void Kolf::DataObject::updateGeometry()
{
	prepareGeometryChange();
	if (m_definition->resizable)
		m_size = QSizeF(m_values.value(QStringLiteral("width")).toDouble(), m_values.value(QStringLiteral("height")).toDouble());
	if (m_definition->rotatable)
		setRotation(m_values.value(QStringLiteral("rotation")).toInt());
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
	return new Kolf::Overlay(this, this);
}

#include "moc_dataobject.cpp"
