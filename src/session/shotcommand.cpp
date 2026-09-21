// SPDX-License-Identifier: GPL-2.0-or-later
#include "shotcommand.h"
#include <QRegularExpression>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>

namespace Kolf::Session {
int runProtocolFixtures(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return 2;
    const auto doc = QJsonDocument::fromJson(file.readAll()).object();
    const auto cases = doc[QStringLiteral("cases")].toArray();
    if (cases.isEmpty()) return 2;
    for (const auto value : cases) {
        const auto test = value.toObject(), patch = test[QStringLiteral("patch")].toObject();
        auto message = doc[QStringLiteral("base")].toObject();
        for (auto it = patch.begin(); it != patch.end(); ++it) message[it.key()] = it.value();
        ShotCommand command;
        if (decodeShot(message, command) != test[QStringLiteral("valid")].toBool()) {
            qCritical() << "FAIL" << test[QStringLiteral("name")].toString(); return 1;
        }
    }
    qInfo() << "PASS native golden protocol cases:" << cases.size();
    return 0;
}
bool counter(const QJsonValue &v, int lo, int hi) {
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= lo
        && v.toDouble() <= hi && v.toDouble() == std::floor(v.toDouble());
}
QJsonObject envelope(const QString &type, QJsonObject payload) {
    payload[QStringLiteral("v")] = 1;
    payload[QStringLiteral("matchId")] = QStringLiteral("prototype");
    payload[QStringLiteral("type")] = type;
    return payload;
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
    return c.intent.valid();
}
}
