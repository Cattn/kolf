// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace Kolf::Session {
struct EnvelopeV3 {
    QString type;
    QString requestId;
    QString lobbyId;
    QString matchId;
    QJsonObject payload;
};

bool decodeEnvelopeV3(const QByteArray &raw, EnvelopeV3 &envelope, QString &errorCode);
QJsonObject envelopeV3(const QString &type, const QJsonObject &payload = {}, const QString &requestId = {},
                       const QString &lobbyId = {}, const QString &matchId = {});
int runV3ProtocolFixtures(const QString &path);
}
