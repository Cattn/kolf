// SPDX-License-Identifier: GPL-2.0-or-later
#include "objectregistry.h"
#include "online/coursefile.h"

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

	void propertyTextFollowsTheSchema()
	{
		const auto *windmill = Kolf::ObjectRegistry::instance().find(QStringLiteral("windmill"));
		QVERIFY(windmill);
		QVERIFY(windmill->propertySpec(QStringLiteral("speed"))->acceptsText(QStringLiteral("9")));
		QVERIFY(!windmill->propertySpec(QStringLiteral("speed"))->acceptsText(QStringLiteral("50")));
		QVERIFY(!windmill->propertySpec(QStringLiteral("speed"))->acceptsText(QStringLiteral("fast")));
		QVERIFY(windmill->propertySpec(QStringLiteral("bottom"))->acceptsText(QStringLiteral("false")));
		QVERIFY(!windmill->propertySpec(QStringLiteral("bottom"))->acceptsText(QStringLiteral("maybe")));
		const auto *blackHole = Kolf::ObjectRegistry::instance().find(QStringLiteral("blackhole"));
		QVERIFY(blackHole->propertySpec(QStringLiteral("exit"))->acceptsText(QStringLiteral("-26,293")));
		QVERIFY(!blackHole->propertySpec(QStringLiteral("exit"))->acceptsText(QStringLiteral("1,2,3")));
		QVERIFY(!blackHole->propertySpec(QStringLiteral("exitDeg"))->acceptsText(QStringLiteral("400")));
		QVERIFY(blackHole->propertySpec(QStringLiteral("maxspeed"))->acceptsText(QStringLiteral("10")));
		const auto *slope = Kolf::ObjectRegistry::instance().find(QStringLiteral("slope"));
		QVERIFY(slope->propertySpec(QStringLiteral("gradient"))->acceptsText(QStringLiteral("Opposite Diagonal")));
		QVERIFY(!slope->propertySpec(QStringLiteral("gradient"))->acceptsText(QStringLiteral("Sideways")));
		QVERIFY(!slope->propertySpec(QStringLiteral("width"))->acceptsText(QStringLiteral("inf")));
		QVERIFY(!Kolf::ObjectRegistry::instance().find(QStringLiteral("cup"))->propertySpecs().size());
	}

	void courseValidationChecksProperties()
	{
		const auto course = [](const QByteArray &object) {
			return QByteArray("[0-course@-50,-50]\nName=Catalog\nauthor=test\n\n[1-hole@-50,-50|0]\npar=3\n\n"
				"[1-ball@100,100]\ndummykey=true\n\n[1-cup@200,200|1]\ndummykey=true\n\n[1-windmill@150,150|2]\n") + object + '\n';
		};
		QString error;
		QVERIFY2(Kolf::Online::validateCourseBytes(course("speed=9\nhasFinalLoad=true"), error), qPrintable(error));
		QVERIFY(!Kolf::Online::validateCourseBytes(course("speed=50"), error));
		QVERIFY(error.contains(QStringLiteral("out of range")));
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
