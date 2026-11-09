// SPDX-License-Identifier: GPL-2.0-or-later
#include "powerups.h"

#include <QJsonArray>
#include <QTest>

class PowerUpsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void offRuleHoldsNothing()
    {
        Kolf::PowerUpState state;
        state.reset(Kolf::PowerUpRule::Off, 2);
        state.give(0, QStringLiteral("wobble"));
        QVERIFY(state.player(0).slots.isEmpty());
    }

    void thirdPickupReplacesOldest()
    {
        Kolf::PowerUpState state;
        state.reset(Kolf::PowerUpRule::Casual, 2);
        state.give(0, QStringLiteral("wobble"));
        state.give(0, QStringLiteral("shield"));
        state.give(0, QStringLiteral("fog"));
        QCOMPARE(state.player(0).slots, (QStringList{QStringLiteral("shield"), QStringLiteral("fog")}));
        QCOMPARE(state.take(0, 0), QStringLiteral("shield"));
        QCOMPARE(state.player(0).slots, QStringList{QStringLiteral("fog")});
        QCOMPARE(state.take(0, 5), QString());
    }

    void statusesRefreshAndExpire()
    {
        Kolf::PowerUpState state;
        state.reset(Kolf::PowerUpRule::Chaos, 3);
        state.addStatus(1, QStringLiteral("heavy"), 1);
        state.addStatus(1, QStringLiteral("fog"), 2);
        state.addStatus(1, QStringLiteral("fog"), 2);
        state.finishTurn(1);
        QVERIFY(!state.hasStatus(1, QStringLiteral("heavy")));
        QVERIFY(state.hasStatus(1, QStringLiteral("fog")));
        state.finishTurn(1);
        QVERIFY(!state.hasStatus(1, QStringLiteral("fog")));
        state.addStatus(2, QStringLiteral("shield"), 99);
        QCOMPARE(state.player(2).statuses.value(QStringLiteral("shield")), Kolf::PowerUpState::MaximumStatusTurns);
        QVERIFY(state.consumeStatus(2, QStringLiteral("shield")));
        QVERIFY(!state.consumeStatus(2, QStringLiteral("shield")));
    }

    void jsonRoundTripsAndRejectsBadInput()
    {
        Kolf::PowerUpState state;
        state.reset(Kolf::PowerUpRule::Casual, 2);
        state.give(1, QStringLiteral("mulligan"));
        state.addStatus(0, QStringLiteral("wobble"), 1);
        state.nextDraw();
        const QJsonObject json = state.toJson();
        Kolf::PowerUpState copy;
        QVERIFY(copy.fromJson(json, 2));
        QCOMPARE(copy.toJson(), json);
        QVERIFY(!copy.fromJson(json, 3));
        QJsonObject bad = json;
        bad.insert(QStringLiteral("rule"), QStringLiteral("turbo"));
        QVERIFY(!copy.fromJson(bad, 2));
        bad = json;
        QJsonArray players = bad.value(QStringLiteral("players")).toArray();
        QJsonObject first = players.at(0).toObject();
        first.insert(QStringLiteral("slots"), QJsonArray{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
        players.replace(0, first);
        bad.insert(QStringLiteral("players"), players);
        QVERIFY(!copy.fromJson(bad, 2));
        QCOMPARE(copy.toJson(), json);
    }
};

QTEST_GUILESS_MAIN(PowerUpsTest)
#include "powerupstest.moc"
