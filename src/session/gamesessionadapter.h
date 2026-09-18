// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
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
    void enableSimulation(bool enabled);
    void enableInput(bool enabled);
    int activeSlot() const;
    int hole() const;
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
    int m_choiceSlot = -1;
    int m_resolutionIndex = 0;
    QString m_choiceId;
    Vector m_hazardVelocity;
    QString m_failure;
    QMap<int, QMap<QString, QString>> m_manifestKinds;
    QMap<int, QString> m_manifestHashes;
};
}
