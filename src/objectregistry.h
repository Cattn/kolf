// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <KLazyLocalizedString>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class QGraphicsItem;
class b2World;

namespace Kolf
{
enum class ObjectCategory { Course, Terrain, Obstacle, Hazard, Mechanism, Decoration };

QString categoryId(ObjectCategory category);

class ObjectType
{
public:
	using Creator = QGraphicsItem *(*)(QGraphicsItem *parent, b2World *world);

	ObjectType(const QString &id, const KLazyLocalizedString &name) : m_id(id), m_name(name) {}

	ObjectType &category(ObjectCategory category) { m_category = category; return *this; }
	ObjectType &decoration() { m_affectsGameplay = false; return *this; }
	ObjectType &addOnNewHole() { m_addOnNewHole = true; return *this; }
	ObjectType &creator(Creator creator) { m_creator = creator; return *this; }

	QString id() const { return m_id; }
	QString name() const { return m_name.toString(); }
	QString untranslatedName() const { return QString::fromUtf8(m_name.untranslatedText()); }
	ObjectCategory objectCategory() const { return m_category; }
	bool affectsGameplay() const { return m_affectsGameplay; }
	bool isAddedOnNewHole() const { return m_addOnNewHole; }
	bool hasCreator() const { return m_creator; }
	QGraphicsItem *create(QGraphicsItem *parent, b2World *world) const { return m_creator ? m_creator(parent, world) : nullptr; }

private:
	QString m_id;
	KLazyLocalizedString m_name;
	ObjectCategory m_category = ObjectCategory::Obstacle;
	bool m_affectsGameplay = true;
	bool m_addOnNewHole = false;
	Creator m_creator = nullptr;
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
