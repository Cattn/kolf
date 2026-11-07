// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <KLazyLocalizedString>
#include <QJsonObject>
#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <optional>

class KConfigGroup;
class QGraphicsItem;
class b2World;

namespace Kolf
{
enum class ObjectCategory { Course, Terrain, Obstacle, Hazard, Mechanism, Decoration };
enum class PropertyType { Number, Int, Bool, Choice, Angle, Point, PointF, Text };

QString categoryId(ObjectCategory category);
QString propertyTypeId(PropertyType type);

constexpr double MaximumCoordinate = 100000;

class PropertySpec
{
public:
	static PropertySpec number(const QString &key, const KLazyLocalizedString &label, double defaultValue, double minimum, double maximum, double step);
	static PropertySpec integer(const QString &key, const KLazyLocalizedString &label, int defaultValue, int minimum, int maximum);
	static PropertySpec boolean(const QString &key, const KLazyLocalizedString &label, bool defaultValue);
	static PropertySpec choice(const QString &key, const KLazyLocalizedString &label, const QStringList &choices, const QString &defaultValue);
	static PropertySpec angle(const QString &key, const KLazyLocalizedString &label, int defaultValue);
	static PropertySpec point(const QString &key, const KLazyLocalizedString &label, const QPoint &defaultValue);
	static PropertySpec pointF(const QString &key, const KLazyLocalizedString &label, const QPointF &defaultValue);
	static PropertySpec text(const QString &key, const KLazyLocalizedString &label, const QString &defaultValue, int maximumLength);

	QString key() const { return m_key; }
	QString label() const { return m_label.toString(); }
	PropertyType type() const { return m_type; }
	QVariant defaultValue() const { return m_default; }
	double minimum() const { return m_minimum; }
	double maximum() const { return m_maximum; }
	QStringList choices() const { return m_choices; }

	std::optional<QVariant> read(const KConfigGroup &group) const;
	bool acceptsText(const QString &text) const;
	bool sameValue(const QVariant &a, const QVariant &b) const;
	QJsonObject toJson() const;

private:
	PropertySpec(const QString &key, const KLazyLocalizedString &label, PropertyType type, const QVariant &defaultValue)
		: m_key(key), m_label(label), m_type(type), m_default(defaultValue) {}

	QString m_key;
	KLazyLocalizedString m_label;
	PropertyType m_type;
	QVariant m_default;
	double m_minimum = 0;
	double m_maximum = 0;
	double m_step = 0;
	QStringList m_choices;
};

class ObjectType
{
public:
	using Creator = QGraphicsItem *(*)(QGraphicsItem *parent, b2World *world);

	ObjectType(const QString &id, const KLazyLocalizedString &name) : m_id(id), m_name(name) {}

	ObjectType &category(ObjectCategory category) { m_category = category; return *this; }
	ObjectType &decoration() { m_affectsGameplay = false; return *this; }
	ObjectType &addOnNewHole() { m_addOnNewHole = true; return *this; }
	ObjectType &creator(Creator creator) { m_creator = creator; return *this; }
	ObjectType &property(const PropertySpec &spec) { m_properties.append(spec); return *this; }
	ObjectType &properties(const QList<PropertySpec> &specs) { m_properties.append(specs); return *this; }

	QString id() const { return m_id; }
	QString name() const { return m_name.toString(); }
	QString untranslatedName() const { return QString::fromUtf8(m_name.untranslatedText()); }
	ObjectCategory objectCategory() const { return m_category; }
	bool affectsGameplay() const { return m_affectsGameplay; }
	bool isAddedOnNewHole() const { return m_addOnNewHole; }
	bool hasCreator() const { return m_creator; }
	QGraphicsItem *create(QGraphicsItem *parent, b2World *world) const { return m_creator ? m_creator(parent, world) : nullptr; }
	QList<PropertySpec> propertySpecs() const { return m_properties; }
	const PropertySpec *propertySpec(const QString &key) const;
	QVariantMap defaults() const;
	QVariantMap readProperties(const KConfigGroup &group) const;
	void writeProperties(KConfigGroup &group, const QVariantMap &values) const;

private:
	QString m_id;
	KLazyLocalizedString m_name;
	ObjectCategory m_category = ObjectCategory::Obstacle;
	bool m_affectsGameplay = true;
	bool m_addOnNewHole = false;
	Creator m_creator = nullptr;
	QList<PropertySpec> m_properties;
};

class ObjectRegistry
{
public:
	static ObjectRegistry &instance();
	static QStringList structuralGroups();
	static bool isValidId(const QString &id);

	bool add(const ObjectType &type);
	QList<ObjectType> types() const;
	const ObjectType *find(const QString &id) const;
	QGraphicsItem *create(const QString &id, QGraphicsItem *parent, b2World *world) const;
	bool isKnownGroup(const QString &type) const;

	QJsonObject catalog() const;
	QByteArray catalogJson() const;
	bool writeCatalog(const QString &path) const;
	bool catalogMatchesFile(const QString &path, QString &difference) const;
	static QString catalogHash(const QJsonObject &catalog);

private:
	QList<ObjectType> m_types;
};

template<typename T> QGraphicsItem *createObject(QGraphicsItem *parent, b2World *world)
{
	return new T(parent, world);
}
}

#define KOLF_OBJECT_CONCAT_INNER(a, b) a##b
#define KOLF_OBJECT_CONCAT(a, b) KOLF_OBJECT_CONCAT_INNER(a, b)
#define KOLF_REGISTER_OBJECT(Class, description) \
	namespace { \
	const bool KOLF_OBJECT_CONCAT(kolfObjectRegistered, __LINE__) = \
		::Kolf::ObjectRegistry::instance().add(description.creator(&::Kolf::createObject<Class>)); \
	}
