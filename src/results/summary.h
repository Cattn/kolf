// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace Kolf::Results {
struct HoleHighlight {
    int hole = 0;
    int strokes = 0;
    std::optional<int> par;
    std::optional<int> relativeToPar;
};

struct Standing {
    int player = 0;
    int rank = 0;
    int total = 0;
    int completedHoles = 0;
    std::optional<int> relativeToPar;
    int holesInOne = 0;
    std::optional<HoleHighlight> bestHole;
    std::optional<HoleHighlight> worstHole;
};

// Mirrors server/service/result-stats.ts. A zero score is an unfinished hole.
QList<Standing> rankPlayers(const QList<QList<int>> &scores, const QList<int> &par,
    const QList<int> &skippedHoles = {},
    const std::optional<QList<QList<int>>> &completedHoles = std::nullopt,
    const std::optional<QList<int>> &completedHoleCounts = std::nullopt);

struct PlayerInfo {
    QString name;
    QColor color;
    QList<int> scores;
    int acceptedShots = -1;
    int hazardChoices = -1;
};

struct Summary {
    QString courseName;
    bool completed = true;
    QString interruptionReason;
    qint64 durationMs = -1;
    QList<int> par;
    QList<int> skippedHoles;
    QList<int> navigatedHoles;
    QList<PlayerInfo> players;
    QList<Standing> standings;
    QList<int> winners;

    bool hasPartialHoles() const { return !skippedHoles.isEmpty() || !navigatedHoles.isEmpty(); }
    int holeCount() const;
};

Summary summarizeLocalGame(const QString &courseName, const QList<PlayerInfo> &players, const QList<int> &par);
Summary summarizeMatchResult(const QJsonObject &result);
}
