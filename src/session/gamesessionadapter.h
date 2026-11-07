// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QSet>
#include "game.h"
#include <QJsonObject>
#include <QObject>

namespace Kolf::Session {
class GameSessionAdapter : public QObject {
    Q_OBJECT
public:
    explicit GameSessionAdapter(KolfGame *game);
    bool loadHole(int hole);
    bool prepareCourse();
    QString manifestHash() const;
    QJsonObject capture(int revision, int generation, int turn, const QString &phase) const;
    bool apply(const QJsonObject &state, QString &error);
    bool shoot(const ShotIntent &intent);
    bool choose(const QString &action);
    bool resetCurrentHole();
    bool skipCurrentHole();
    bool navigateToHole(int targetHole);
    void rememberUndoCheckpoint(const QJsonObject &state);
    bool canUndoShot() const;
    bool undoShot();
    QString removeSlots(const QList<int> &slots);
    bool isDeparted(int slot) const { return m_departed.contains(slot); }
    void enableSimulation(bool enabled);
    void enableInput(bool enabled);
    int activeSlot() const;
    int hole() const;
    int highestHole() const;
    int par() const;
    int choiceSlot() const { return m_choiceSlot; }
    QString choiceId() const { return m_choiceId; }
    bool finished() const { return m_finished; }
    QString failure() const { return m_failure; }
Q_SIGNALS:
    void transition(const QString &phase);
    void failed(const QString &reason);
private:
    void settle();
    void continueResolution();
    QMap<QString, QGraphicsItem *> registry() const;
    KolfGame *g;
    bool m_scored = false;
    bool m_finished = false;
    QSet<int> m_departed;
    void applyDepartures();
    QString advanceTurn();
    int m_choiceSlot = -1;
    int m_resolutionIndex = 0;
    QString m_choiceId;
    Vector m_hazardVelocity;
    QString m_failure;
    QMap<int, QMap<QString, QString>> m_manifestKinds;
    QMap<int, QString> m_manifestHashes;
    QJsonObject m_undoState;
};
}
