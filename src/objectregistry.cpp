// SPDX-License-Identifier: GPL-2.0-or-later
#include "objectregistry.h"

#include <QCryptographicHash>
#include <QFile>
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
		types.append(QJsonObject{
			{QStringLiteral("id"), type.id()},
			{QStringLiteral("name"), type.untranslatedName()},
			{QStringLiteral("category"), categoryId(type.objectCategory())},
			{QStringLiteral("affectsGameplay"), type.affectsGameplay()},
			{QStringLiteral("addOnNewHole"), type.isAddedOnNewHole()},
			{QStringLiteral("properties"), QJsonObject()},
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
