// SPDX-License-Identifier: GPL-2.0-or-later
#include "online/invitelink.h"

#include <QTest>

using namespace Kolf::Online;

class InviteLinkTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void buildsLinksForTheConnectedServer()
    {
        QCOMPARE(inviteLinkFor(QStringLiteral("wss://kolf.pro/ws"), QStringLiteral("K7QF2MXA")), QStringLiteral("https://kolf.pro/join/K7QF2MXA"));
        QCOMPARE(inviteLinkFor(QStringLiteral("wss://games.example.org:8443/ws"), QStringLiteral("k7qf-2mxa")),
            QStringLiteral("https://games.example.org:8443/join/K7QF2MXA"));
        QCOMPARE(inviteLinkFor(QStringLiteral("ws://192.168.1.5:3011"), QStringLiteral("K7QF2MXA")), QStringLiteral("http://192.168.1.5:3011/join/K7QF2MXA"));
        QVERIFY(inviteLinkFor(QStringLiteral("wss://kolf.pro/ws"), QStringLiteral("nope")).isEmpty());
    }

    void parsesWebLinks()
    {
        const auto web = parseInviteLink(QStringLiteral("https://kolf.pro/join/k7qf-2mxa"));
        QVERIFY(web);
        QCOMPARE(web->code, QStringLiteral("K7QF2MXA"));
        QCOMPARE(web->serverUrl, QStringLiteral("wss://kolf.pro/ws"));
        const auto lan = parseInviteLink(QStringLiteral("http://192.168.1.5:3011/join/K7QF2MXA"));
        QVERIFY(lan);
        QCOMPARE(lan->serverUrl, QStringLiteral("ws://192.168.1.5:3011"));
        QVERIFY(!parseInviteLink(QStringLiteral("http://example.org/join/K7QF2MXA")));
        QVERIFY(!parseInviteLink(QStringLiteral("https://kolf.pro/status")));
        QVERIFY(!parseInviteLink(QStringLiteral("K7QF2MXA")));
    }

    void parsesAppLinks()
    {
        const auto app = parseInviteLink(QStringLiteral("kolf://join/K7QF2MXA?server=wss%3A%2F%2Fkolf.pro%2Fws"));
        QVERIFY(app);
        QCOMPARE(app->code, QStringLiteral("K7QF2MXA"));
        QCOMPARE(app->serverUrl, QStringLiteral("wss://kolf.pro/ws"));
        const auto bare = parseInviteLink(QStringLiteral("kolf://join/K7QF2MXA"));
        QVERIFY(bare);
        QVERIFY(bare->serverUrl.isEmpty());
        QVERIFY(!parseInviteLink(QStringLiteral("kolf://join/K7QF2MXA?server=ws%3A%2F%2Fevil.example%2Fws")));
        QVERIFY(!parseInviteLink(QStringLiteral("kolf://open/K7QF2MXA")));
        QVERIFY(!parseInviteLink(QStringLiteral("kolf://join/OOOO")));
    }

    void roundTrips()
    {
        const auto link = inviteLinkFor(QStringLiteral("wss://kolf.pro/ws"), QStringLiteral("ABCDEFGH"));
        const auto parsed = parseInviteLink(link);
        QVERIFY(parsed);
        QCOMPARE(parsed->serverUrl, QStringLiteral("wss://kolf.pro/ws"));
        QCOMPARE(parsed->code, QStringLiteral("ABCDEFGH"));
    }
};

QTEST_GUILESS_MAIN(InviteLinkTest)
#include "invitelinktest.moc"
