// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace Kolf::Session {
struct OnlineEnvelope {
    QString type;
    QString requestId;
    QString lobbyId;
    QString matchId;
    QJsonObject payload;
};

bool decodeOnlineEnvelope(const QByteArray &raw, OnlineEnvelope &envelope, QString &errorCode);
QJsonObject onlineEnvelope(const QString &type, const QJsonObject &payload = {}, const QString &requestId = {},
                       const QString &lobbyId = {}, const QString &matchId = {});
int runOnlineProtocolFixtures(const QString &path);
}
