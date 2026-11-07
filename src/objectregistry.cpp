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
	for (const auto &type : m_types) {
		QJsonArray properties;
		for (const auto &spec : type.propertySpecs())
			properties.append(spec.toJson());
		types.append(QJsonObject{
			{QStringLiteral("id"), type.id()},
			{QStringLiteral("name"), type.untranslatedName()},
			{QStringLiteral("category"), categoryId(type.objectCategory())},
			{QStringLiteral("affectsGameplay"), type.affectsGameplay()},
			{QStringLiteral("addOnNewHole"), type.isAddedOnNewHole()},
			{QStringLiteral("properties"), properties},
			{QStringLiteral("runtime"), QJsonArray()},
		});
	}
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
		{QStringLiteral("label"), QString::fromUtf8(m_label.untranslatedText())},
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
