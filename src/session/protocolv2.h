// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace Kolf::Session {
struct EnvelopeV2 {
    QString type;
    QString requestId;
    QString lobbyId;
    QString matchId;
    QJsonObject payload;
};

bool decodeEnvelopeV2(const QByteArray &raw, EnvelopeV2 &envelope, QString &errorCode);
QJsonObject envelopeV2(const QString &type, const QJsonObject &payload = {}, const QString &requestId = {},
                       const QString &lobbyId = {}, const QString &matchId = {});
int runV2ProtocolFixtures(const QString &path);
}
