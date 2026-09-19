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
int runV2ProtocolFixtures(const QString &path);
}
