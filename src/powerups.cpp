// SPDX-License-Identifier: GPL-2.0-or-later
#include "powerups.h"

#include <KLazyLocalizedString>
#include <KLocalizedString>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>

namespace Kolf {
namespace {
bool validId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9_]{0,31}$"));
    return pattern.match(id).hasMatch();
}
}

QString powerUpRuleId(PowerUpRule rule)
{
    switch (rule) {
    case PowerUpRule::Casual: return QStringLiteral("casual");
    case PowerUpRule::Chaos: return QStringLiteral("chaos");
    case PowerUpRule::Off: break;
    }
    return QStringLiteral("off");
}

QString powerUpName(const QString &id)
{
    static const QMap<QString, KLazyLocalizedString> names{
        {QStringLiteral("wobble"), kli18nc("@item power-up", "Wobble")},
        {QStringLiteral("heavy"), kli18nc("@item power-up", "Heavy Ball")},
        {QStringLiteral("fog"), kli18nc("@item power-up", "Fog")},
        {QStringLiteral("shield"), kli18nc("@item power-up", "Shield")},
        {QStringLiteral("mulligan"), kli18nc("@item power-up", "Mulligan")},
        {QStringLiteral("butterfingers"), kli18nc("@item power-up", "Butterfingers")},
        {QStringLiteral("mirror"), kli18nc("@item power-up", "Mirror")},
        {QStringLiteral("slick"), kli18nc("@item power-up", "Slick")},
        {QStringLiteral("steady"), kli18nc("@item power-up", "Steady Hand")},
        {QStringLiteral("bigcup"), kli18nc("@item power-up", "Big Cup")},
        {QStringLiteral("guide"), kli18nc("@item power-up", "Guide")},
        {QStringLiteral("swap"), kli18nc("@item power-up", "Swap")}};
    const auto it = names.constFind(id);
    return it == names.cend() ? id : it->toString();
}

PowerUpRule powerUpRuleFromId(const QString &id)
{
    if (id == QLatin1String("casual")) return PowerUpRule::Casual;
    if (id == QLatin1String("chaos")) return PowerUpRule::Chaos;
    return PowerUpRule::Off;
}

void PowerUpState::reset(PowerUpRule rule, int players)
{
    m_rule = rule;
    m_players = QList<PlayerPowerUps>(qBound(0, players, 8));
    m_draws = 0;
}

const PlayerPowerUps &PowerUpState::player(int slot) const
{
    static const PlayerPowerUps empty;
    return slot >= 0 && slot < m_players.size() ? m_players.at(slot) : empty;
}

void PowerUpState::give(int slot, const QString &item)
{
    if (!enabled() || slot < 0 || slot >= m_players.size() || !validId(item))
        return;
    auto &slots = m_players[slot].slots;
    if (slots.size() >= SlotCount)
        slots.removeFirst();
    slots.append(item);
}

QString PowerUpState::take(int slot, int index)
{
    if (slot < 0 || slot >= m_players.size() || index < 0 || index >= m_players.at(slot).slots.size())
        return {};
    return m_players[slot].slots.takeAt(index);
}

void PowerUpState::addStatus(int slot, const QString &status, int turns)
{
    if (slot < 0 || slot >= m_players.size() || !validId(status))
        return;
    m_players[slot].statuses.insert(status, qBound(1, turns, MaximumStatusTurns));
}

bool PowerUpState::hasStatus(int slot, const QString &status) const
{
    return slot >= 0 && slot < m_players.size() && m_players.at(slot).statuses.contains(status);
}

bool PowerUpState::consumeStatus(int slot, const QString &status)
{
    return slot >= 0 && slot < m_players.size() && m_players[slot].statuses.remove(status) > 0;
}

void PowerUpState::finishTurn(int slot)
{
    if (slot < 0 || slot >= m_players.size())
        return;
    auto &statuses = m_players[slot].statuses;
    for (auto it = statuses.begin(); it != statuses.end();) {
        if (--it.value() <= 0)
            it = statuses.erase(it);
        else
            ++it;
    }
}

QStringList PowerUpState::pool(PowerUpRule rule)
{
    QStringList items{QStringLiteral("wobble"), QStringLiteral("heavy"), QStringLiteral("fog"), QStringLiteral("butterfingers"),
        QStringLiteral("mirror"), QStringLiteral("slick"), QStringLiteral("shield"), QStringLiteral("mulligan"),
        QStringLiteral("steady"), QStringLiteral("bigcup")};
    if (rule == PowerUpRule::Chaos)
        items << QStringLiteral("wobble") << QStringLiteral("heavy") << QStringLiteral("fog") << QStringLiteral("slick")
              << QStringLiteral("butterfingers") << QStringLiteral("mirror") << QStringLiteral("swap") << QStringLiteral("swap");
    return items;
}

QString PowerUpState::draw(int hole)
{
    const QStringList items = pool(m_rule);
    const quint32 value = qHash(QStringLiteral("kolf-powerup/%1/%2").arg(hole).arg(nextDraw()), 0x6b6f6c66u);
    return items.at(int(value % quint32(items.size())));
}

bool PowerUpState::isAttack(const QString &item)
{
    static const QStringList attacks{QStringLiteral("wobble"), QStringLiteral("heavy"), QStringLiteral("fog"),
        QStringLiteral("butterfingers"), QStringLiteral("mirror"), QStringLiteral("slick"), QStringLiteral("swap")};
    return attacks.contains(item);
}

bool PowerUpState::isKnown(const QString &item)
{
    return isAttack(item) || item == QLatin1String("shield") || item == QLatin1String("mulligan")
        || item == QLatin1String("steady") || item == QLatin1String("bigcup");
}

PowerUpState::UseResult PowerUpState::use(int user, int index, int target)
{
    if (!enabled() || user < 0 || user >= m_players.size() || index < 0 || index >= m_players.at(user).slots.size())
        return UseResult::Invalid;
    const QString item = m_players.at(user).slots.at(index);
    if (!isKnown(item))
        return UseResult::Invalid;
    if (isAttack(item)) {
        if (target < 0 || target >= m_players.size() || target == user)
            return UseResult::Invalid;
        m_players[user].slots.removeAt(index);
        if (consumeStatus(target, QStringLiteral("shield")))
            return UseResult::Shielded;
        if (item != QLatin1String("swap"))
            addStatus(target, item, 1);
        return UseResult::Applied;
    }
    m_players[user].slots.removeAt(index);
    addStatus(user, item, item == QLatin1String("shield") ? MaximumStatusTurns : 1);
    return UseResult::Applied;
}

double PowerUpState::wobbleDegrees()
{
    const quint32 value = qHash(QStringLiteral("kolf-wobble/%1").arg(nextDraw()), 0x776f6262u);
    return (double(value % 1601u) - 800.0) / 100.0;
}

void PowerUpState::resetHole()
{
    for (auto &player : m_players)
        player.statuses.clear();
}

QJsonObject PowerUpState::toJson() const
{
    QJsonArray players;
    for (const auto &player : m_players) {
        QJsonObject statuses;
        for (auto it = player.statuses.cbegin(); it != player.statuses.cend(); ++it)
            statuses.insert(it.key(), it.value());
        players.append(QJsonObject{{QStringLiteral("slots"), QJsonArray::fromStringList(player.slots)},
            {QStringLiteral("statuses"), statuses}});
    }
    return {{QStringLiteral("rule"), powerUpRuleId(m_rule)}, {QStringLiteral("draws"), m_draws},
        {QStringLiteral("players"), players}};
}

bool PowerUpState::fromJson(const QJsonObject &json, int players)
{
    const QString ruleId = json.value(QStringLiteral("rule")).toString();
    if (ruleId != QLatin1String("off") && ruleId != QLatin1String("casual") && ruleId != QLatin1String("chaos"))
        return false;
    const auto list = json.value(QStringLiteral("players")).toArray();
    const double draws = json.value(QStringLiteral("draws")).toDouble(-1);
    if (list.size() != players || draws < 0 || draws > 1e9)
        return false;
    QList<PlayerPowerUps> parsed;
    for (const auto &value : list) {
        const auto object = value.toObject();
        PlayerPowerUps player;
        const auto slots = object.value(QStringLiteral("slots")).toArray();
        if (slots.size() > SlotCount)
            return false;
        for (const auto &slot : slots) {
            if (!validId(slot.toString()))
                return false;
            player.slots.append(slot.toString());
        }
        const auto statuses = object.value(QStringLiteral("statuses")).toObject();
        if (statuses.size() > 16)
            return false;
        for (auto it = statuses.constBegin(); it != statuses.constEnd(); ++it) {
            const int turns = it.value().toInt(-1);
            if (!validId(it.key()) || turns < 1 || turns > MaximumStatusTurns)
                return false;
            player.statuses.insert(it.key(), turns);
        }
        parsed.append(player);
    }
    m_rule = powerUpRuleFromId(ruleId);
    m_players = parsed;
    m_draws = int(draws);
    return true;
}

}
