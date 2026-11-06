// SPDX-License-Identifier: GPL-2.0-or-later
// Mirrors server/test/result-stats.test.ts so offline and online rankings agree.
#include "results/summary.h"

#include <QTest>

using namespace Kolf::Results;

class ResultSummaryTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void ranksTiesAndHighlights()
    {
        const auto rows = rankPlayers({{1, 4, 2}, {2, 3, 2}, {3, 4, 2}}, {3, 4, 2});
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].player, 0); QCOMPARE(rows[0].rank, 1); QCOMPARE(rows[0].total, 7); QCOMPARE(*rows[0].relativeToPar, -2);
        QCOMPARE(rows[1].player, 1); QCOMPARE(rows[1].rank, 1); QCOMPARE(rows[1].total, 7); QCOMPARE(*rows[1].relativeToPar, -2);
        QCOMPARE(rows[2].player, 2); QCOMPARE(rows[2].rank, 3); QCOMPARE(rows[2].total, 9); QCOMPARE(*rows[2].relativeToPar, 0);
        QCOMPARE(rows[0].holesInOne, 1);
        QCOMPARE(rows[0].bestHole->hole, 1);
        QCOMPARE(rows[0].worstHole->hole, 2);
    }

    void unfinishedRowsAndMissingPar()
    {
        const auto rows = rankPlayers({{2, 0}, {1}, {}}, {0});
        QCOMPARE(rows[0].player, 1); QCOMPARE(rows[0].completedHoles, 1); QCOMPARE(rows[0].total, 1); QVERIFY(!rows[0].relativeToPar);
        QCOMPARE(rows[1].player, 0); QCOMPARE(rows[1].completedHoles, 1); QCOMPARE(rows[1].total, 2); QVERIFY(!rows[1].relativeToPar);
        QCOMPARE(rows[2].player, 2); QCOMPARE(rows[2].completedHoles, 0); QCOMPARE(rows[2].total, 0); QVERIFY(!rows[2].relativeToPar);
        QVERIFY(!rows[0].bestHole->par);
        QVERIFY(!rows[2].bestHole);
        QCOMPARE(rows[2].rank, 3);
    }

    void interruptedCurrentHole()
    {
        const auto rows = rankPlayers({{2, 1}, {3, 0}}, {3, 2}, {}, std::nullopt, QList<int>{1, 1});
        QCOMPARE(rows[0].total, 3);
        QCOMPARE(rows[0].completedHoles, 1);
        QCOMPARE(rows[0].holesInOne, 0);
        QCOMPARE(*rows[0].relativeToPar, -1);
    }

    void skippedPartialHole()
    {
        const auto rows = rankPlayers({{1, 2}, {0, 3}}, {3, 3}, {1}, std::nullopt, QList<int>{2, 2});
        QCOMPARE(rows[0].player, 0); QCOMPARE(rows[0].total, 3); QCOMPARE(rows[0].completedHoles, 1); QCOMPARE(rows[0].holesInOne, 0);
        QCOMPARE(rows[1].player, 1); QCOMPARE(rows[1].total, 3); QCOMPARE(rows[1].completedHoles, 1); QCOMPARE(rows[1].holesInOne, 0);
        for (const auto &row : rows) { QCOMPARE(row.bestHole->hole, 2); QCOMPARE(row.worstHole->hole, 2); }
    }

    void navigationHighlightsLatestVisit()
    {
        const auto rows = rankPlayers({{2, 4, 1}, {0, 3, 2}}, {3, 4, 3}, {}, QList<QList<int>>{{2}, {2, 3}});
        QCOMPARE(rows[0].player, 1); QCOMPARE(rows[0].total, 5); QCOMPARE(rows[0].completedHoles, 2); QCOMPARE(rows[0].bestHole->hole, 2);
        QCOMPARE(rows[1].player, 0); QCOMPARE(rows[1].total, 7); QCOMPARE(rows[1].completedHoles, 1); QCOMPARE(rows[1].bestHole->hole, 2);
    }

    void localSummaryNamesWinners()
    {
        const auto summary = summarizeLocalGame(QStringLiteral("Easy"),
            {{QStringLiteral("Ada"), Qt::blue, {2, 3}}, {QStringLiteral("Grace"), Qt::red, {2, 3}}, {QStringLiteral("Lin"), Qt::green, {4, 4}}},
            {3, 3});
        QCOMPARE(summary.winners, QList<int>({0, 1}));
        QCOMPARE(summary.holeCount(), 2);
    }
};

QTEST_GUILESS_MAIN(ResultSummaryTest)
#include "resultsummarytest.moc"
