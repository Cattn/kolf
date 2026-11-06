// SPDX-License-Identifier: GPL-2.0-or-later
#include "online/serverlist.h"

#include <KConfig>
#include <KConfigGroup>

#include <QTemporaryDir>
#include <QTest>

using namespace Kolf::Online;

class ServerListTest : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    int m_configCount = 0;

    KConfig *freshConfig()
    {
        return new KConfig(m_dir.filePath(QStringLiteral("kolfrc-%1").arg(++m_configCount)), KConfig::SimpleConfig);
    }

private Q_SLOTS:
    void init()
    {
        qunsetenv("KOLF_ONLINE_DEFAULT_ENDPOINT");
        qunsetenv("KOLF_ONLINE_DEFAULT_SERVER_NAME");
    }

    void normalizes_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("bare public host") << "kolf.pro" << "wss://kolf.pro/ws";
        QTest::newRow("public host with port") << "games.example.org:8443" << "wss://games.example.org:8443/ws";
        QTest::newRow("public host with path") << "example.org/kolf" << "wss://example.org/kolf";
        QTest::newRow("explicit wss keeps path") << "wss://kolf.pro/ws" << "wss://kolf.pro/ws";
        QTest::newRow("explicit wss adds path") << "wss://Kolf.PRO" << "wss://kolf.pro/ws";
        QTest::newRow("https becomes wss") << "https://kolf.pro" << "wss://kolf.pro/ws";
        QTest::newRow("localhost") << "localhost" << "ws://localhost:3011";
        QTest::newRow("loopback with port") << "127.0.0.1:4000" << "ws://127.0.0.1:4000";
        QTest::newRow("lan") << "192.168.1.20" << "ws://192.168.1.20:3011";
        QTest::newRow("lan 172") << "172.20.0.5" << "ws://172.20.0.5:3011";
        QTest::newRow("lan 10") << "10.0.0.2:3011" << "ws://10.0.0.2:3011";
        QTest::newRow("mdns") << "golf.local" << "ws://golf.local:3011";
        QTest::newRow("explicit ws lan") << "ws://127.0.0.1:3011" << "ws://127.0.0.1:3011";
        QTest::newRow("ipv6 loopback") << "::1" << "ws://[::1]:3011";
        QTest::newRow("whitespace") << "  kolf.pro  " << "wss://kolf.pro/ws";
    }

    void normalizes()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        const auto result = normalizeServerAddress(input);
        QVERIFY2(result.isValid(), qPrintable(result.error));
        QCOMPARE(result.url.toString(), expected);
    }

    void rejects_data()
    {
        QTest::addColumn<QString>("input");
        QTest::newRow("empty") << "  ";
        QTest::newRow("plaintext public") << "ws://kolf.pro/ws";
        QTest::newRow("plaintext public 172.32") << "ws://172.32.0.1:3011";
        QTest::newRow("other scheme") << "ftp://kolf.pro";
        QTest::newRow("credentials") << "wss://user:pass@kolf.pro/ws";
        QTest::newRow("query") << "wss://kolf.pro/ws?x=1";
        QTest::newRow("fragment") << "wss://kolf.pro/ws#a";
    }

    void rejects()
    {
        QFETCH(QString, input);
        const auto result = normalizeServerAddress(input);
        QVERIFY(!result.isValid());
        QVERIFY(!result.error.isEmpty());
    }

    void privateHosts()
    {
        QVERIFY(isPrivateHost(QStringLiteral("localhost")));
        QVERIFY(isPrivateHost(QStringLiteral("169.254.3.4")));
        QVERIFY(isPrivateHost(QStringLiteral("fd00::1")));
        QVERIFY(isPrivateHost(QStringLiteral("172.31.255.255")));
        QVERIFY(!isPrivateHost(QStringLiteral("172.15.0.1")));
        QVERIFY(!isPrivateHost(QStringLiteral("8.8.8.8")));
        QVERIFY(!isPrivateHost(QStringLiteral("kolf.pro")));
    }

    void freshInstallDefaultsToBuiltIn()
    {
        std::unique_ptr<KConfig> config(freshConfig());
        KConfigGroup group(config.get(), QStringLiteral("Online"));
        const auto list = ServerList::load(group);
        QCOMPARE(list.servers().size(), 1);
        QVERIFY(list.servers().first().builtIn);
        QCOMPARE(list.defaultUrl(), QStringLiteral("wss://kolf.pro/ws"));
        QCOMPARE(list.defaultServer().name, QStringLiteral("kolf.pro"));
        QVERIFY(group.hasKey("servers"));
        QVERIFY(!group.hasKey("defaultServer"));
    }

    void migratesLegacyKeys()
    {
        std::unique_ptr<KConfig> config(freshConfig());
        KConfigGroup group(config.get(), QStringLiteral("Online"));
        group.writeEntry("lastSuccessfulEndpoint", QStringLiteral("127.0.0.1:3011"));
        group.writeEntry("endpoint", QStringLiteral("ws://127.0.0.1:3011"));
        group.writeEntry("recentServers", QStringList{QStringLiteral("127.0.0.1:3011"), QStringLiteral("192.168.0.9"),
            QStringLiteral("kolf.pro"), QStringLiteral("ws://public.example.org:3011"), QStringLiteral("192.168.0.9:3011")});
        const auto list = ServerList::load(group);
        const auto servers = list.servers();
        QCOMPARE(servers.size(), 3);
        QCOMPARE(servers[1].url, QStringLiteral("ws://127.0.0.1:3011"));
        QCOMPARE(servers[1].name, QStringLiteral("127.0.0.1"));
        QCOMPARE(servers[2].url, QStringLiteral("ws://192.168.0.9:3011"));
        QCOMPARE(list.defaultUrl(), QStringLiteral("ws://127.0.0.1:3011"));
        QVERIFY(group.hasKey("recentServers"));
        QVERIFY(group.hasKey("lastSuccessfulEndpoint"));
        QCOMPARE(group.readEntry("defaultServer", QString()), QStringLiteral("ws://127.0.0.1:3011"));
    }

    void migrationWithoutLastSuccessfulKeepsBuiltInDefault()
    {
        std::unique_ptr<KConfig> config(freshConfig());
        KConfigGroup group(config.get(), QStringLiteral("Online"));
        group.writeEntry("recentServers", QStringList{QStringLiteral("10.1.1.1")});
        const auto list = ServerList::load(group);
        QCOMPARE(list.servers().size(), 2);
        QCOMPARE(list.defaultUrl(), QStringLiteral("wss://kolf.pro/ws"));
    }

    void savesAndReloads()
    {
        std::unique_ptr<KConfig> config(freshConfig());
        KConfigGroup group(config.get(), QStringLiteral("Online"));
        auto list = ServerList::load(group);
        QString error;
        QVERIFY(list.add(QStringLiteral("Living room"), QStringLiteral("192.168.1.5"), &error));
        QVERIFY(!list.add(QString(), QStringLiteral("ws://192.168.1.5:3011"), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!list.add(QString(), QStringLiteral("ws://example.org"), &error));
        QVERIFY(list.add(QString(), QStringLiteral("games.example.org")));
        QVERIFY(list.setDefault(QStringLiteral("ws://192.168.1.5:3011")));
        list.save(group);

        auto reloaded = ServerList::load(group);
        const auto servers = reloaded.servers();
        QCOMPARE(servers.size(), 3);
        QCOMPARE(servers[1].name, QStringLiteral("Living room"));
        QCOMPARE(servers[2].name, QStringLiteral("games.example.org"));
        QCOMPARE(reloaded.defaultUrl(), QStringLiteral("ws://192.168.1.5:3011"));
        QCOMPARE(reloaded.displayName(QStringLiteral("ws://192.168.1.5:3011")), QStringLiteral("Living room"));
    }

    void builtInCannotBeRemoved()
    {
        std::unique_ptr<KConfig> config(freshConfig());
        KConfigGroup group(config.get(), QStringLiteral("Online"));
        auto list = ServerList::load(group);
        QVERIFY(!list.remove(QStringLiteral("wss://kolf.pro/ws")));
        QVERIFY(!list.update(QStringLiteral("wss://kolf.pro/ws"), QStringLiteral("x"), QStringLiteral("example.org")));
        QVERIFY(list.add(QStringLiteral("LAN"), QStringLiteral("10.0.0.4")));
        QVERIFY(list.setDefault(QStringLiteral("ws://10.0.0.4:3011")));
        QVERIFY(list.update(QStringLiteral("ws://10.0.0.4:3011"), QStringLiteral("LAN 2"), QStringLiteral("10.0.0.5")));
        QCOMPARE(list.defaultUrl(), QStringLiteral("ws://10.0.0.5:3011"));
        QVERIFY(list.remove(QStringLiteral("ws://10.0.0.5:3011")));
        QCOMPARE(list.defaultUrl(), QStringLiteral("wss://kolf.pro/ws"));
    }

    void environmentOverrideWins()
    {
        std::unique_ptr<KConfig> config(freshConfig());
        KConfigGroup group(config.get(), QStringLiteral("Online"));
        group.writeEntry("servers", QStringLiteral("[{\"name\":\"LAN\",\"url\":\"ws://10.0.0.4:3011\"}]"));
        group.writeEntry("defaultServer", QStringLiteral("ws://10.0.0.4:3011"));
        qputenv("KOLF_ONLINE_DEFAULT_ENDPOINT", "ws://127.0.0.1:4100");
        qputenv("KOLF_ONLINE_DEFAULT_SERVER_NAME", "Test server");
        const auto list = ServerList::load(group);
        QCOMPARE(list.servers().first().url, QStringLiteral("ws://127.0.0.1:4100"));
        QCOMPARE(list.servers().first().name, QStringLiteral("Test server"));
        QCOMPARE(list.defaultUrl(), QStringLiteral("ws://127.0.0.1:4100"));
        QCOMPARE(list.servers().size(), 2);
    }
};

QTEST_GUILESS_MAIN(ServerListTest)
#include "serverlisttest.moc"
