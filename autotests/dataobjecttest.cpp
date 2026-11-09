// SPDX-License-Identifier: GPL-2.0-or-later
#include "dataobject.h"
#include "objectregistry.h"

#include <QFile>
#include <QJsonDocument>
#include <QTest>

namespace
{
const QByteArray Sprite = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\"><rect width=\"10\" height=\"10\" fill=\"#8cf\"/></svg>";

QByteArray definition(const QString &id, const QString &components, const QString &extra = QString())
{
	return QStringLiteral(R"({"kolfObject": 1, "id": "%1", "name": "Test", "category": "terrain",
		"shape": {"type": "rect", "size": [40, 20], "resizable": true, "rotatable": true},
		"visual": {"sprite": "test.svg"}, %3 "components": %2})").arg(id, components, extra).toUtf8();
}
}

class DataObjectTest : public QObject
{
	Q_OBJECT
private Q_SLOTS:
	void shippedIceLoads()
	{
		QString error;
		QVERIFY2(Kolf::DataObjectLibrary::loadFile(QStringLiteral(KOLF_SOURCE_OBJECTS "/ice.kobj.json"), error), qPrintable(error));
		const auto *ice = Kolf::ObjectRegistry::instance().find(QStringLiteral("ice"));
		QVERIFY(ice);
		QCOMPARE(ice->name(), QStringLiteral("Ice"));
		QStringList keys;
		for (const auto &spec : ice->propertySpecs())
			keys << spec.key();
		QCOMPARE(keys, QStringList({QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("rotation"), QStringLiteral("slipperiness")}));
		QVERIFY(ice->affectsGameplay());
		QVERIFY(ice->runtimeFields().isEmpty());
		QVERIFY(!Kolf::DataObjectLibrary::loadFile(QStringLiteral(KOLF_SOURCE_OBJECTS "/ice.kobj.json"), error));
		QVERIFY2(error.contains(QStringLiteral("'ice' is already registered")), qPrintable(error));
	}

	void reportsErrorsWithPaths_data()
	{
		QTest::addColumn<QByteArray>("json");
		QTest::addColumn<QByteArray>("sprite");
		QTest::addColumn<QString>("expected");
		QTest::newRow("bad binding") << definition(QStringLiteral("pad_a"), QStringLiteral(R"([{"type": "boost", "impulse": "$strenght"}])"))
			<< Sprite << QStringLiteral("components[0].impulse: '$strenght' is not a property");
		QTest::newRow("unknown component") << definition(QStringLiteral("pad_b"), QStringLiteral(R"([{"type": "lava"}])"))
			<< Sprite << QStringLiteral("components[0].type: unknown component 'lava'");
		QTest::newRow("planned component") << definition(QStringLiteral("pad_c"), QStringLiteral(R"([{"type": "capture"}])"))
			<< Sprite << QStringLiteral("components[0].type: 'capture' is not available yet");
		QTest::newRow("out of range") << definition(QStringLiteral("pad_d"), QStringLiteral(R"([{"type": "surface", "friction": 50}])"))
			<< Sprite << QStringLiteral("components[0].friction: must be a number from 0.05 to 20");
		QTest::newRow("unknown parameter") << definition(QStringLiteral("pad_e"), QStringLiteral(R"([{"type": "surface", "friction": 2, "grip": 1}])"))
			<< Sprite << QStringLiteral("components[0].grip: unknown parameter");
		QTest::newRow("reserved property") << definition(QStringLiteral("pad_f"), QStringLiteral(R"([{"type": "surface", "friction": 2}])"),
			QStringLiteral(R"("properties": {"rotation": {"type": "angle"}},)"))
			<< Sprite << QStringLiteral("properties.rotation: is reserved");
		QTest::newRow("decor in gameplay") << definition(QStringLiteral("pad_g"), QStringLiteral(R"([{"type": "decor"}])"))
			<< Sprite << QStringLiteral("affectsGameplay: must be false");
		QTest::newRow("unknown key") << definition(QStringLiteral("pad_h"), QStringLiteral(R"([{"type": "surface", "friction": 2}])"),
			QStringLiteral(R"("script": "x",)"))
			<< Sprite << QStringLiteral("script: unknown key");
		QTest::newRow("script in sprite") << definition(QStringLiteral("pad_i"), QStringLiteral(R"([{"type": "surface", "friction": 2}])"))
			<< QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\"><script>alert(1)</script></svg>")
			<< QStringLiteral("visual.sprite: sprite must not contain <script>");
		QTest::newRow("event handler") << definition(QStringLiteral("pad_j"), QStringLiteral(R"([{"type": "surface", "friction": 2}])"))
			<< QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" onload=\"x()\"/>")
			<< QStringLiteral("visual.sprite: sprite must not contain event handlers");
		QTest::newRow("external image") << definition(QStringLiteral("pad_k"), QStringLiteral(R"([{"type": "surface", "friction": 2}])"))
			<< QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\"><image xlink:href=\"https://example.com/a.png\"/></svg>")
			<< QStringLiteral("visual.sprite: sprite must not reference external files or URLs");
		QTest::newRow("external style") << definition(QStringLiteral("pad_l"), QStringLiteral(R"([{"type": "surface", "friction": 2}])"))
			<< QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\"><rect fill=\"url(file:///etc/passwd)\"/></svg>")
			<< QStringLiteral("visual.sprite: sprite must not reference external files or URLs");
		QTest::newRow("doctype") << definition(QStringLiteral("pad_m"), QStringLiteral(R"([{"type": "surface", "friction": 2}])"))
			<< QByteArray("<!DOCTYPE svg [<!ENTITY x SYSTEM \"file:///etc/passwd\">]><svg xmlns=\"http://www.w3.org/2000/svg\">&x;</svg>")
			<< QStringLiteral("visual.sprite: sprite must not contain a DOCTYPE or entities");
		QTest::newRow("bad json") << QByteArray("{\"kolfObject\": 1,") << Sprite << QStringLiteral("test.kobj.json: not valid JSON");
	}

	void reportsErrorsWithPaths()
	{
		QFETCH(QByteArray, json);
		QFETCH(QByteArray, sprite);
		QFETCH(QString, expected);
		QString error;
		QVERIFY(!Kolf::DataObjectLibrary::parse(json, QStringLiteral("test.kobj.json"), sprite, error));
		QVERIFY2(error.startsWith(QStringLiteral("test.kobj.json: ")), qPrintable(error));
		QVERIFY2(error.contains(expected), qPrintable(error));
	}

	void validDefinitionRegistersWithRuntime()
	{
		QString error;
		const auto json = definition(QStringLiteral("speed_pad"),
			QStringLiteral(R"([{"type": "boost", "impulse": "$strength", "cooldown": 20}, {"type": "sound", "on": "boost", "key": "bumper"}])"),
			QStringLiteral(R"("properties": {"strength": {"type": "number", "label": "Strength", "min": 0.5, "max": 6, "default": 2.5}},)"));
		QVERIFY2(Kolf::DataObjectLibrary::parse(json, QStringLiteral("speed_pad.kobj.json"), Sprite, error), qPrintable(error));
		const auto *pad = Kolf::ObjectRegistry::instance().find(QStringLiteral("speed_pad"));
		QVERIFY(pad);
		QCOMPARE(pad->runtimeFields(), QStringList{QStringLiteral("lastBoost")});
		QVERIFY(pad->propertySpec(QStringLiteral("strength"))->acceptsText(QStringLiteral("3")));
		QVERIFY(!pad->propertySpec(QStringLiteral("strength"))->acceptsText(QStringLiteral("7")));
		QVERIFY(Kolf::ObjectRegistry::instance().remove(QStringLiteral("speed_pad")));
	}
};

QTEST_MAIN(DataObjectTest)

#include "dataobjecttest.moc"
