// SPDX-License-Identifier: GPL-2.0-or-later
#include "objectregistry.h"

#include <QCryptographicHash>
#include <QFile>
#include <KConfigGroup>
#include <QPointF>
#include <cmath>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>

QString Kolf::categoryId(ObjectCategory category)
{
	switch (category) {
	case ObjectCategory::Course: return QStringLiteral("course");
	case ObjectCategory::Terrain: return QStringLiteral("terrain");
	case ObjectCategory::Obstacle: return QStringLiteral("obstacle");
	case ObjectCategory::Hazard: return QStringLiteral("hazard");
	case ObjectCategory::Mechanism: return QStringLiteral("mechanism");
	case ObjectCategory::Decoration: return QStringLiteral("decoration");
	}
	return QStringLiteral("obstacle");
}

Kolf::ObjectRegistry &Kolf::ObjectRegistry::instance()
{
	static ObjectRegistry registry;
	return registry;
}

QStringList Kolf::ObjectRegistry::structuralGroups()
{
	return {QStringLiteral("ball"), QStringLiteral("course"), QStringLiteral("hole")};
}

bool Kolf::ObjectRegistry::isValidId(const QString &id)
{
	static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9_]{0,31}$"));
	return pattern.match(id).hasMatch() && !structuralGroups().contains(id);
}

bool Kolf::ObjectRegistry::add(const ObjectType &type)
{
	if (!isValidId(type.id()) || find(type.id()) || !type.hasCreator())
		return false;
	auto position = std::lower_bound(m_types.begin(), m_types.end(), type,
		[](const ObjectType &a, const ObjectType &b) { return a.id() < b.id(); });
	m_types.insert(position, type);
	return true;
}

QList<Kolf::ObjectType> Kolf::ObjectRegistry::types() const
{
	return m_types;
}

const Kolf::ObjectType *Kolf::ObjectRegistry::find(const QString &id) const
{
	for (const auto &type : m_types)
		if (type.id() == id)
			return &type;
	return nullptr;
}

QGraphicsItem *Kolf::ObjectRegistry::create(const QString &id, QGraphicsItem *parent, b2World *world) const
{
	const auto *type = find(id);
	return type ? type->create(parent, world) : nullptr;
}

bool Kolf::ObjectRegistry::isKnownGroup(const QString &type) const
{
	return structuralGroups().contains(type) || find(type);
}

QJsonObject Kolf::ObjectRegistry::catalog() const
{
	QJsonArray types;
	for (const auto &type : m_types)
		types.append(type.toJson());
	QJsonObject result{
		{QStringLiteral("catalogVersion"), 1},
		{QStringLiteral("structuralGroups"), QJsonArray::fromStringList(structuralGroups())},
		{QStringLiteral("types"), types},
	};
	result.insert(QStringLiteral("catalogHash"), catalogHash(result));
	return result;
}

QString Kolf::ObjectRegistry::catalogHash(const QJsonObject &catalog)
{
	QJsonObject hashed = catalog;
	hashed.remove(QStringLiteral("catalogHash"));
	const QByteArray canonical = QJsonDocument(hashed).toJson(QJsonDocument::Compact);
	return QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

QByteArray Kolf::ObjectRegistry::catalogJson() const
{
	return QJsonDocument(catalog()).toJson(QJsonDocument::Indented);
}

bool Kolf::ObjectRegistry::writeCatalog(const QString &path) const
{
	QFile file(path);
	const QByteArray json = catalogJson();
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(json) == json.size();
}

bool Kolf::ObjectRegistry::catalogMatchesFile(const QString &path, QString &difference) const
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		difference = QStringLiteral("Cannot read %1").arg(path);
		return false;
	}
	QJsonParseError error;
	const auto committed = QJsonDocument::fromJson(file.readAll(), &error).object();
	if (error.error != QJsonParseError::NoError) {
		difference = QStringLiteral("%1 is not valid JSON: %2").arg(path, error.errorString());
		return false;
	}
	const auto current = catalog();
	if (committed == current)
		return true;
	difference = QStringLiteral("%1 is out of date (committed hash %2, current %3). Regenerate it with kolf --dump-object-catalog %1")
		.arg(path, committed.value(QStringLiteral("catalogHash")).toString(), current.value(QStringLiteral("catalogHash")).toString());
	return false;
}

QString Kolf::propertyTypeId(PropertyType type)
{
	switch (type) {
	case PropertyType::Number: return QStringLiteral("number");
	case PropertyType::Int: return QStringLiteral("int");
	case PropertyType::Bool: return QStringLiteral("bool");
	case PropertyType::Choice: return QStringLiteral("enum");
	case PropertyType::Angle: return QStringLiteral("angle");
	case PropertyType::Point: return QStringLiteral("point");
	case PropertyType::PointF: return QStringLiteral("pointf");
	case PropertyType::Text: return QStringLiteral("text");
	}
	return QStringLiteral("text");
}

Kolf::PropertySpec Kolf::PropertySpec::number(const QString &key, const KLazyLocalizedString &label, double defaultValue, double minimum, double maximum, double step)
{
	PropertySpec spec(key, label, PropertyType::Number, defaultValue);
	spec.m_minimum = minimum;
	spec.m_maximum = maximum;
	spec.m_step = step;
	return spec;
}

Kolf::PropertySpec Kolf::PropertySpec::integer(const QString &key, const KLazyLocalizedString &label, int defaultValue, int minimum, int maximum)
{
	PropertySpec spec(key, label, PropertyType::Int, defaultValue);
	spec.m_minimum = minimum;
	spec.m_maximum = maximum;
	spec.m_step = 1;
	return spec;
}

Kolf::PropertySpec Kolf::PropertySpec::boolean(const QString &key, const KLazyLocalizedString &label, bool defaultValue)
{
	return PropertySpec(key, label, PropertyType::Bool, defaultValue);
}

Kolf::PropertySpec Kolf::PropertySpec::choice(const QString &key, const KLazyLocalizedString &label, const QStringList &choices, const QString &defaultValue)
{
	PropertySpec spec(key, label, PropertyType::Choice, defaultValue);
	spec.m_choices = choices;
	return spec;
}

Kolf::PropertySpec Kolf::PropertySpec::angle(const QString &key, const KLazyLocalizedString &label, int defaultValue)
{
	PropertySpec spec(key, label, PropertyType::Angle, defaultValue);
	spec.m_minimum = 0;
	spec.m_maximum = 359;
	spec.m_step = 1;
	return spec;
}

Kolf::PropertySpec Kolf::PropertySpec::point(const QString &key, const KLazyLocalizedString &label, const QPoint &defaultValue)
{
	PropertySpec spec(key, label, PropertyType::Point, defaultValue);
	spec.m_minimum = -MaximumCoordinate;
	spec.m_maximum = MaximumCoordinate;
	return spec;
}

Kolf::PropertySpec Kolf::PropertySpec::pointF(const QString &key, const KLazyLocalizedString &label, const QPointF &defaultValue)
{
	PropertySpec spec(key, label, PropertyType::PointF, defaultValue);
	spec.m_minimum = -MaximumCoordinate;
	spec.m_maximum = MaximumCoordinate;
	return spec;
}

Kolf::PropertySpec Kolf::PropertySpec::text(const QString &key, const KLazyLocalizedString &label, const QString &defaultValue, int maximumLength)
{
	PropertySpec spec(key, label, PropertyType::Text, defaultValue);
	spec.m_maximum = maximumLength;
	return spec;
}

namespace
{
double clamped(double value, double minimum, double maximum)
{
	return std::isfinite(value) ? qBound(minimum, value, maximum) : minimum;
}

bool numberText(const QString &text, double minimum, double maximum, bool integral)
{
	bool ok = false;
	const QString trimmed = text.trimmed();
	const double value = integral ? double(trimmed.toLongLong(&ok)) : trimmed.toDouble(&ok);
	return ok && std::isfinite(value) && value >= minimum && value <= maximum;
}
}

std::optional<QVariant> Kolf::PropertySpec::read(const KConfigGroup &group) const
{
	if (!group.hasKey(m_key))
		return std::nullopt;
	switch (m_type) {
	case PropertyType::Number:
		return QVariant(clamped(group.readEntry(m_key, m_default.toDouble()), m_minimum, m_maximum));
	case PropertyType::Int:
	case PropertyType::Angle:
		return QVariant(int(clamped(group.readEntry(m_key, m_default.toInt()), m_minimum, m_maximum)));
	case PropertyType::Bool:
		return QVariant(group.readEntry(m_key, m_default.toBool()));
	case PropertyType::Choice: {
		const QString value = group.readEntry(m_key, QString());
		if (!m_choices.contains(value))
			return std::nullopt;
		return QVariant(value);
	}
	case PropertyType::Point: {
		const QPoint value = group.readEntry(m_key, m_default.toPoint());
		return QVariant(QPoint(int(clamped(value.x(), m_minimum, m_maximum)), int(clamped(value.y(), m_minimum, m_maximum))));
	}
	case PropertyType::PointF: {
		const QPointF value = group.readEntry(m_key, m_default.toPointF());
		return QVariant(QPointF(clamped(value.x(), m_minimum, m_maximum), clamped(value.y(), m_minimum, m_maximum)));
	}
	case PropertyType::Text:
		return QVariant(group.readEntry(m_key, m_default.toString()).left(int(m_maximum)));
	}
	return std::nullopt;
}

bool Kolf::PropertySpec::acceptsText(const QString &text) const
{
	switch (m_type) {
	case PropertyType::Number:
		return numberText(text, m_minimum, m_maximum, false);
	case PropertyType::Int:
	case PropertyType::Angle:
		return numberText(text, m_minimum, m_maximum, true);
	case PropertyType::Bool: {
		static const QStringList values{QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("on"), QStringLiteral("off"),
			QStringLiteral("yes"), QStringLiteral("no"), QStringLiteral("1"), QStringLiteral("0")};
		return values.contains(text.trimmed().toLower());
	}
	case PropertyType::Choice:
		return m_choices.contains(text.trimmed());
	case PropertyType::Point:
	case PropertyType::PointF: {
		const auto parts = text.split(QLatin1Char(','));
		return parts.size() == 2 && numberText(parts.at(0), m_minimum, m_maximum, m_type == PropertyType::Point)
			&& numberText(parts.at(1), m_minimum, m_maximum, m_type == PropertyType::Point);
	}
	case PropertyType::Text:
		return text.size() <= int(m_maximum);
	}
	return false;
}

bool Kolf::PropertySpec::sameValue(const QVariant &a, const QVariant &b) const
{
	switch (m_type) {
	case PropertyType::Number:
		return qFuzzyCompare(1.0 + a.toDouble(), 1.0 + b.toDouble());
	case PropertyType::Int:
	case PropertyType::Angle:
		return a.toInt() == b.toInt();
	case PropertyType::Bool:
		return a.toBool() == b.toBool();
	case PropertyType::Point:
		return a.toPoint() == b.toPoint();
	case PropertyType::PointF:
		return qFuzzyCompare(1.0 + a.toPointF().x(), 1.0 + b.toPointF().x()) && qFuzzyCompare(1.0 + a.toPointF().y(), 1.0 + b.toPointF().y());
	case PropertyType::Choice:
	case PropertyType::Text:
		return a.toString() == b.toString();
	}
	return false;
}

QJsonObject Kolf::PropertySpec::toJson() const
{
	QJsonObject result{
		{QStringLiteral("key"), m_key},
		{QStringLiteral("label"), m_plainLabel.isEmpty() ? QString::fromUtf8(m_label.untranslatedText()) : m_plainLabel},
		{QStringLiteral("type"), propertyTypeId(m_type)},
	};
	switch (m_type) {
	case PropertyType::Number:
		result.insert(QStringLiteral("default"), m_default.toDouble());
		result.insert(QStringLiteral("min"), m_minimum);
		result.insert(QStringLiteral("max"), m_maximum);
		result.insert(QStringLiteral("step"), m_step);
		break;
	case PropertyType::Int:
	case PropertyType::Angle:
		result.insert(QStringLiteral("default"), m_default.toInt());
		result.insert(QStringLiteral("min"), m_minimum);
		result.insert(QStringLiteral("max"), m_maximum);
		break;
	case PropertyType::Bool:
		result.insert(QStringLiteral("default"), m_default.toBool());
		break;
	case PropertyType::Choice:
		result.insert(QStringLiteral("default"), m_default.toString());
		result.insert(QStringLiteral("values"), QJsonArray::fromStringList(m_choices));
		break;
	case PropertyType::Point:
		result.insert(QStringLiteral("default"), QJsonArray{m_default.toPoint().x(), m_default.toPoint().y()});
		result.insert(QStringLiteral("min"), m_minimum);
		result.insert(QStringLiteral("max"), m_maximum);
		break;
	case PropertyType::PointF:
		result.insert(QStringLiteral("default"), QJsonArray{m_default.toPointF().x(), m_default.toPointF().y()});
		result.insert(QStringLiteral("min"), m_minimum);
		result.insert(QStringLiteral("max"), m_maximum);
		break;
	case PropertyType::Text:
		result.insert(QStringLiteral("default"), m_default.toString());
		result.insert(QStringLiteral("maxLength"), m_maximum);
		break;
	}
	return result;
}

const Kolf::PropertySpec *Kolf::ObjectType::propertySpec(const QString &key) const
{
	for (const auto &spec : m_properties)
		if (spec.key() == key)
			return &spec;
	return nullptr;
}

QVariantMap Kolf::ObjectType::defaults() const
{
	QVariantMap result;
	for (const auto &spec : m_properties)
		result.insert(spec.key(), spec.defaultValue());
	return result;
}

QVariantMap Kolf::ObjectType::readProperties(const KConfigGroup &group) const
{
	QVariantMap result;
	for (const auto &spec : m_properties)
		if (const auto value = spec.read(group))
			result.insert(spec.key(), *value);
	return result;
}

void Kolf::ObjectType::writeProperties(KConfigGroup &group, const QVariantMap &values) const
{
	for (const auto &spec : m_properties)
		if (values.contains(spec.key()))
			group.writeEntry(spec.key(), values.value(spec.key()));
}

QJsonObject Kolf::ObjectType::toJson() const
{
	QJsonArray properties;
	for (const auto &spec : m_properties)
		properties.append(spec.toJson());
	return {
		{QStringLiteral("id"), m_id},
		{QStringLiteral("name"), untranslatedName()},
		{QStringLiteral("category"), categoryId(m_category)},
		{QStringLiteral("affectsGameplay"), m_affectsGameplay},
		{QStringLiteral("addOnNewHole"), m_addOnNewHole},
		{QStringLiteral("properties"), properties},
		{QStringLiteral("runtime"), QJsonArray::fromStringList(m_runtime)},
	};
}

QString Kolf::ObjectType::signature() const
{
	const QByteArray json = QJsonDocument(toJson()).toJson(QJsonDocument::Compact);
	return QString::fromLatin1(QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex().left(16));
}

bool Kolf::ObjectRegistry::remove(const QString &id)
{
	for (int i = 0; i < m_types.size(); ++i) {
		if (m_types.at(i).id() == id) {
			m_types.removeAt(i);
			return true;
		}
	}
	return false;
}

std::optional<Kolf::PropertySpec> Kolf::PropertySpec::fromJson(const QString &key, const QJsonObject &json, QString &error)
{
	const QString type = json.value(QStringLiteral("type")).toString();
	const QString label = json.value(QStringLiteral("label")).toString(key);
	if (label.isEmpty() || label.size() > 64) {
		error = QStringLiteral("label must be 1 to 64 characters");
		return std::nullopt;
	}
	const auto readNumber = [&](const char *name, double fallback, bool &ok) {
		const QJsonValue value = json.value(QLatin1String(name));
		if (value.isUndefined()) return fallback;
		if (!value.isDouble() || !std::isfinite(value.toDouble())) { ok = false; error = QStringLiteral("%1 must be a number").arg(QLatin1String(name)); }
		return value.toDouble();
	};
	bool ok = true;
	std::optional<PropertySpec> spec;
	if (type == QLatin1String("number") || type == QLatin1String("int")) {
		const double minimum = readNumber("min", 0, ok), maximum = readNumber("max", 100, ok), step = readNumber("step", 1, ok);
		const double fallback = readNumber("default", minimum, ok);
		if (!ok) return std::nullopt;
		if (minimum > maximum || fallback < minimum || fallback > maximum || std::abs(minimum) > MaximumCoordinate || std::abs(maximum) > MaximumCoordinate) {
			error = QStringLiteral("needs min <= default <= max within +-100000");
			return std::nullopt;
		}
		if (type == QLatin1String("int")) {
			if (minimum != std::floor(minimum) || maximum != std::floor(maximum) || fallback != std::floor(fallback)) {
				error = QStringLiteral("int properties need whole numbers");
				return std::nullopt;
			}
			spec = integer(key, KLazyLocalizedString(), int(fallback), int(minimum), int(maximum));
		} else {
			spec = number(key, KLazyLocalizedString(), fallback, minimum, maximum, step);
		}
	} else if (type == QLatin1String("bool")) {
		const QJsonValue value = json.value(QStringLiteral("default"));
		if (!value.isUndefined() && !value.isBool()) { error = QStringLiteral("default must be true or false"); return std::nullopt; }
		spec = boolean(key, KLazyLocalizedString(), value.toBool(false));
	} else if (type == QLatin1String("angle")) {
		const double fallback = readNumber("default", 0, ok);
		if (!ok) return std::nullopt;
		if (fallback < 0 || fallback > 359 || fallback != std::floor(fallback)) { error = QStringLiteral("default must be a whole angle from 0 to 359"); return std::nullopt; }
		spec = angle(key, KLazyLocalizedString(), int(fallback));
	} else if (type == QLatin1String("enum")) {
		QStringList values;
		for (const auto value : json.value(QStringLiteral("values")).toArray()) {
			if (!value.isString() || value.toString().isEmpty() || value.toString().size() > 64) { error = QStringLiteral("values must be short strings"); return std::nullopt; }
			values << value.toString();
		}
		const QString fallback = json.value(QStringLiteral("default")).toString(values.value(0));
		if (values.isEmpty() || values.size() > 32 || !values.contains(fallback)) { error = QStringLiteral("needs 1 to 32 values including the default"); return std::nullopt; }
		spec = choice(key, KLazyLocalizedString(), values, fallback);
	} else if (type == QLatin1String("text")) {
		const double maximumLength = readNumber("maxLength", 200, ok);
		if (!ok) return std::nullopt;
		const QString fallback = json.value(QStringLiteral("default")).toString();
		if (maximumLength < 1 || maximumLength > 1000 || fallback.size() > maximumLength) { error = QStringLiteral("maxLength must be 1 to 1000 and fit the default"); return std::nullopt; }
		spec = text(key, KLazyLocalizedString(), fallback, int(maximumLength));
	} else {
		error = QStringLiteral("type must be number, int, bool, angle, enum or text");
		return std::nullopt;
	}
	spec->m_plainLabel = label;
	return spec;
}
