// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <cmath>

namespace Kolf::Session {
enum class Role { Offline, Authority, Guest };
struct ShotIntent {
    double directionRadians = 0;
    double launchMagnitude = 1;
    bool advanced = false;
    bool valid() const {
        return std::isfinite(directionRadians) && std::abs(directionRadians) <= 3.14159265358979323846
            && std::isfinite(launchMagnitude) && launchMagnitude > 0
            && launchMagnitude <= (advanced ? 66.7 : 55.5) / 8.0;
    }
};
struct ShotCommand {
    QString commandId;
    int holeGeneration = 0;
    int turnId = 0;
    int playerSlot = -1;
    ShotIntent intent;
};
bool decodeShot(const QJsonObject &message, ShotCommand &command);
int runProtocolFixtures(const QString &path);
QJsonObject envelope(const QString &type, QJsonObject payload = {});
bool counter(const QJsonValue &value, int minimum = 0, int maximum = 1000000000);
class GameSessionAdapter;
}
Q_DECLARE_METATYPE(Kolf::Session::ShotIntent)
