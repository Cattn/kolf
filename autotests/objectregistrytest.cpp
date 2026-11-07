// SPDX-License-Identifier: GPL-2.0-or-later
#include "objectregistry.h"

#include <QGraphicsRectItem>
#include <QJsonArray>
#include <QTest>

namespace
{
QGraphicsItem *createRect(QGraphicsItem *parent, b2World *)
{
	return new QGraphicsRectItem(parent);
}
}

class ObjectRegistryTest : public QObject
{
	Q_OBJECT
private Q_SLOTS:
	void builtInTypesRegisterOnce()
	{
		QStringList ids;
		for (const auto &type : Kolf::ObjectRegistry::instance().types())
			ids << type.id();
		QCOMPARE(ids, QStringList({QStringLiteral("blackhole"), QStringLiteral("bridge"), QStringLiteral("bumper"),
			QStringLiteral("cup"), QStringLiteral("floater"), QStringLiteral("puddle"), QStringLiteral("sand"),
			QStringLiteral("sign"), QStringLiteral("slope"), QStringLiteral("wall"), QStringLiteral("windmill")}));
		QVERIFY(Kolf::ObjectRegistry::instance().find(QStringLiteral("cup"))->isAddedOnNewHole());
		QVERIFY(!Kolf::ObjectRegistry::instance().find(QStringLiteral("wall"))->isAddedOnNewHole());
	}

	void rejectsInvalidRegistrations()
	{
		Kolf::ObjectRegistry registry;
		const auto type = [](const char *id) {
			return Kolf::ObjectType(QString::fromLatin1(id), kli18n("Test")).creator(&createRect);
		};
		QVERIFY(registry.add(type("speed_pad")));
		QVERIFY(!registry.add(type("speed_pad")));
		QVERIFY(!registry.add(type("ball")));
		QVERIFY(!registry.add(type("hole")));
		QVERIFY(!registry.add(type("Speed")));
		QVERIFY(!registry.add(type("9lives")));
		QVERIFY(!registry.add(type("")));
		QVERIFY(!registry.add(Kolf::ObjectType(QStringLiteral("nocreator"), kli18n("Test"))));
		QVERIFY(registry.isKnownGroup(QStringLiteral("speed_pad")));
		QVERIFY(registry.isKnownGroup(QStringLiteral("course")));
		QVERIFY(!registry.isKnownGroup(QStringLiteral("windmill")));
	}

	void catalogHashCoversContent()
	{
		const auto catalog = Kolf::ObjectRegistry::instance().catalog();
		QCOMPARE(catalog.value(QStringLiteral("catalogHash")).toString(), Kolf::ObjectRegistry::catalogHash(catalog));
		auto changed = catalog;
		auto types = changed.value(QStringLiteral("types")).toArray();
		auto first = types.at(0).toObject();
		first.insert(QStringLiteral("affectsGameplay"), false);
		types.replace(0, first);
		changed.insert(QStringLiteral("types"), types);
		QVERIFY(Kolf::ObjectRegistry::catalogHash(changed) != catalog.value(QStringLiteral("catalogHash")).toString());
	}

	void committedCatalogIsCurrent()
	{
		QString difference;
		QVERIFY2(Kolf::ObjectRegistry::instance().catalogMatchesFile(QStringLiteral(KOLF_OBJECT_CATALOG), difference),
			qPrintable(difference));
	}
};

QTEST_GUILESS_MAIN(ObjectRegistryTest)

#include "objectregistrytest.moc"
