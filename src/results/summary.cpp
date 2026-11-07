// SPDX-License-Identifier: GPL-2.0-or-later
#include "summary.h"

#include "online/color.h"

#include <QJsonArray>
#include <algorithm>

using namespace Kolf::Results;

namespace {
QList<int> intList(const QJsonValue &value)
{
    QList<int> result;
    for (const auto &item : value.toArray()) result.append(item.toInt());
    return result;
}

std::optional<HoleHighlight> highlightFromJson(const QJsonValue &value)
{
    if (!value.isObject()) return std::nullopt;
    const auto object = value.toObject();
    HoleHighlight highlight;
    highlight.hole = object.value(QStringLiteral("hole")).toInt();
    highlight.strokes = object.value(QStringLiteral("strokes")).toInt();
    if (object.value(QStringLiteral("par")).isDouble()) highlight.par = object.value(QStringLiteral("par")).toInt();
    if (object.value(QStringLiteral("relativeToPar")).isDouble())
        highlight.relativeToPar = object.value(QStringLiteral("relativeToPar")).toInt();
    return highlight;
}

QList<int> winnersOf(const QList<Standing> &standings)
{
    QList<int> winners;
    for (const auto &standing : standings)
        if (standing.rank == 1 && standing.completedHoles > 0) winners.append(standing.player);
    return winners;
}
}

QList<Standing> Kolf::Results::rankPlayers(const QList<QList<int>> &scores, const QList<int> &par,
    const QList<int> &skippedHoles, const std::optional<QList<QList<int>>> &completedHoles,
    const std::optional<QList<int>> &completedHoleCounts)
{
    QList<Standing> rows;
    for (int index = 0; index < scores.size(); ++index) {
        const auto &scoreRow = scores.at(index);
        QList<HoleHighlight> completed;
        for (int holeIndex = 0; holeIndex < scoreRow.size(); ++holeIndex) {
            const int hole = holeIndex + 1;
            if (completedHoles) {
                if (index >= completedHoles->size() || !completedHoles->at(index).contains(hole)) continue;
            } else if (completedHoleCounts && index < completedHoleCounts->size()
                && holeIndex >= completedHoleCounts->at(index)) {
                continue;
            }
            if (skippedHoles.contains(hole)) continue;
            const int strokes = scoreRow.at(holeIndex);
            if (strokes <= 0) continue;
            HoleHighlight highlight{hole, strokes, std::nullopt, std::nullopt};
            if (holeIndex < par.size() && par.at(holeIndex) > 0) {
                highlight.par = par.at(holeIndex);
                highlight.relativeToPar = strokes - par.at(holeIndex);
            }
            completed.append(highlight);
        }
        const bool allRated = std::all_of(completed.cbegin(), completed.cend(),
            [](const HoleHighlight &hole) { return hole.relativeToPar.has_value(); });
        const auto value = [allRated](const HoleHighlight &hole) {
            return allRated ? *hole.relativeToPar : hole.strokes;
        };
        Standing row;
        row.player = index;
        for (int strokes : scoreRow) row.total += strokes;
        row.completedHoles = int(completed.size());
        int relative = 0, rated = 0;
        for (const auto &hole : completed) {
            if (hole.relativeToPar) { relative += *hole.relativeToPar; ++rated; }
            if (hole.strokes == 1) ++row.holesInOne;
        }
        if (rated) row.relativeToPar = relative;
        if (!completed.isEmpty()) {
            auto best = completed, worst = completed;
            std::stable_sort(best.begin(), best.end(), [&](const HoleHighlight &a, const HoleHighlight &b) {
                return value(a) != value(b) ? value(a) < value(b) : a.hole < b.hole;
            });
            std::stable_sort(worst.begin(), worst.end(), [&](const HoleHighlight &a, const HoleHighlight &b) {
                return value(a) != value(b) ? value(a) > value(b) : a.hole < b.hole;
            });
            row.bestHole = best.first();
            row.worstHole = worst.first();
        }
        rows.append(row);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Standing &a, const Standing &b) {
        if (a.completedHoles != b.completedHoles) return a.completedHoles > b.completedHoles;
        if (a.total != b.total) return a.total < b.total;
        return a.player < b.player;
    });
    for (int index = 0; index < rows.size(); ++index) {
        const bool tied = index > 0 && rows.at(index - 1).completedHoles == rows.at(index).completedHoles
            && rows.at(index - 1).total == rows.at(index).total;
        rows[index].rank = tied ? rows.at(index - 1).rank : index + 1;
    }
    return rows;
}

int Summary::holeCount() const
{
    qsizetype count = par.size();
    for (const auto &player : players) count = qMax(count, player.scores.size());
    return int(count);
}

Summary Kolf::Results::summarizeLocalGame(const QString &courseName, const QList<PlayerInfo> &players, const QList<int> &par)
{
    Summary summary;
    summary.courseName = courseName;
    summary.players = players;
    summary.par = par;
    const auto played = [&summary](int hole) {
        if (hole < summary.par.size() && summary.par.at(hole) > 0) return true;
        return std::any_of(summary.players.cbegin(), summary.players.cend(),
            [hole](const PlayerInfo &player) { return player.scores.value(hole) > 0; });
    };
    int holes = summary.holeCount();
    while (holes > 0 && !played(holes - 1)) --holes;
    summary.par = summary.par.mid(0, holes);
    for (auto &player : summary.players) player.scores = player.scores.mid(0, holes);
    QList<QList<int>> scores;
    for (const auto &player : std::as_const(summary.players)) scores.append(player.scores);
    summary.standings = rankPlayers(scores, summary.par);
    summary.winners = winnersOf(summary.standings);
    return summary;
}

Summary Kolf::Results::summarizeMatchResult(const QJsonObject &result)
{
    Summary summary;
    summary.courseName = result.value(QStringLiteral("courseName")).toString(result.value(QStringLiteral("courseId")).toString());
    summary.completed = result.value(QStringLiteral("status")) == QLatin1String("Completed");
    summary.interruptionReason = result.value(QStringLiteral("reason")).toString();
    if (result.value(QStringLiteral("durationMs")).isDouble())
        summary.durationMs = qint64(result.value(QStringLiteral("durationMs")).toDouble());
    summary.par = intList(result.value(QStringLiteral("par")));
    summary.skippedHoles = intList(result.value(QStringLiteral("skippedHoles")));
    summary.navigatedHoles = intList(result.value(QStringLiteral("navigatedHoles")));
    const auto roster = result.value(QStringLiteral("roster")).toArray();
    const auto scores = result.value(QStringLiteral("scores")).toArray();
    QStringList playerIds;
    for (int index = 0; index < roster.size(); ++index) {
        const auto player = roster.at(index).toObject();
        PlayerInfo info;
        info.name = player.value(QStringLiteral("displayName")).toString();
        info.color = Kolf::Online::colorFromRgba(player.value(QStringLiteral("resolvedColor")).toString());
        info.scores = index < scores.size() ? intList(scores.at(index)) : QList<int>();
        summary.players.append(info);
        playerIds.append(player.value(QStringLiteral("playerId")).toString());
    }
    for (const auto &value : result.value(QStringLiteral("standings")).toArray()) {
        const auto row = value.toObject();
        const int player = int(playerIds.indexOf(row.value(QStringLiteral("playerId")).toString()));
        if (player < 0) continue;
        Standing standing;
        standing.player = player;
        standing.rank = row.value(QStringLiteral("rank")).toInt();
        standing.total = row.value(QStringLiteral("total")).toInt();
        standing.completedHoles = row.value(QStringLiteral("completedHoles")).toInt();
        if (row.value(QStringLiteral("relativeToPar")).isDouble())
            standing.relativeToPar = row.value(QStringLiteral("relativeToPar")).toInt();
        standing.holesInOne = row.value(QStringLiteral("holesInOne")).toInt();
        standing.bestHole = highlightFromJson(row.value(QStringLiteral("bestHole")));
        standing.worstHole = highlightFromJson(row.value(QStringLiteral("worstHole")));
        if (row.value(QStringLiteral("leftOnHole")).isDouble()) standing.leftOnHole = row.value(QStringLiteral("leftOnHole")).toInt();
        summary.players[player].acceptedShots = row.value(QStringLiteral("acceptedShots")).toInt();
        summary.players[player].hazardChoices = row.value(QStringLiteral("hazardChoices")).toInt();
        summary.standings.append(standing);
    }
    if (summary.completed) {
        for (const auto &id : result.value(QStringLiteral("winnerPlayerIds")).toArray()) {
            const int player = int(playerIds.indexOf(id.toString()));
            if (player >= 0) summary.winners.append(player);
        }
    }
    return summary;
}
