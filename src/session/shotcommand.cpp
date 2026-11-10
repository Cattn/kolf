// SPDX-License-Identifier: GPL-2.0-or-later
#include "shotcommand.h"
#include <QRegularExpression>

namespace Kolf::Session {
bool counter(const QJsonValue &v, int lo, int hi) {
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= lo
        && v.toDouble() <= hi && v.toDouble() == std::floor(v.toDouble());
}
bool decodeShot(const QJsonObject &m, ShotCommand &c, int maximumPlayerSlot) {
    static const QRegularExpression ids(QStringLiteral("^[A-Za-z0-9_.:-]{1,96}$"));
    if (!ids.match(m[QStringLiteral("commandId")].toString()).hasMatch()
        || !counter(m[QStringLiteral("holeGeneration")], 1) || !counter(m[QStringLiteral("turnId")], 1)
        || !counter(m[QStringLiteral("playerSlot")], 0, maximumPlayerSlot)
        || !m[QStringLiteral("directionRadians")].isDouble() || !m[QStringLiteral("launchMagnitude")].isDouble()) return false;
    const auto mode = m[QStringLiteral("puttingMode")].toString();
    if (mode != QLatin1String("normal") && mode != QLatin1String("advanced")) return false;
    c = {m[QStringLiteral("commandId")].toString(), m[QStringLiteral("holeGeneration")].toInt(),
         m[QStringLiteral("turnId")].toInt(), m[QStringLiteral("playerSlot")].toInt(),
         {m[QStringLiteral("directionRadians")].toDouble(), m[QStringLiteral("launchMagnitude")].toDouble(), mode == QLatin1String("advanced")}};
    if (m.contains(QStringLiteral("powerUp"))) {
        const auto powerUp = m[QStringLiteral("powerUp")].toObject();
        if (!counter(powerUp[QStringLiteral("slot")], 0, 1) || !powerUp[QStringLiteral("target")].isDouble()) return false;
        const int target = powerUp[QStringLiteral("target")].toInt(-2);
        if (target < -1 || target > maximumPlayerSlot) return false;
        c.intent.powerUpSlot = powerUp[QStringLiteral("slot")].toInt();
        c.intent.powerUpTarget = target;
    }
    return c.intent.valid();
}
}
