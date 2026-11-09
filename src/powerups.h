// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace Kolf {

enum class PowerUpRule { Off, Casual, Chaos };

QString powerUpRuleId(PowerUpRule rule);
QString powerUpName(const QString &id);
PowerUpRule powerUpRuleFromId(const QString &id);

struct PlayerPowerUps {
    QStringList slots;
    QMap<QString, int> statuses;
};

class PowerUpState {
public:
    static constexpr int SlotCount = 2;
    static constexpr int MaximumStatusTurns = 9;

    PowerUpRule rule() const { return m_rule; }
    bool enabled() const { return m_rule != PowerUpRule::Off; }
    void reset(PowerUpRule rule, int players);
    int playerCount() const { return int(m_players.size()); }
    const PlayerPowerUps &player(int slot) const;

    void give(int slot, const QString &item);
    QString take(int slot, int index);
    void addStatus(int slot, const QString &status, int turns);
    bool hasStatus(int slot, const QString &status) const;
    bool consumeStatus(int slot, const QString &status);
    void finishTurn(int slot);
    int nextDraw() { return m_draws++; }
    void resetHole();
    QString draw(int hole);
    static QStringList pool(PowerUpRule rule);
    static bool isAttack(const QString &item);
    static bool isKnown(const QString &item);
    enum class UseResult { Applied, Shielded, Invalid };
    UseResult use(int user, int index, int target);
    double wobbleDegrees();

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &json, int players);

private:
    PowerUpRule m_rule = PowerUpRule::Off;
    QList<PlayerPowerUps> m_players;
    int m_draws = 0;
};

}
