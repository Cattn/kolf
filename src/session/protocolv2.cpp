// SPDX-License-Identifier: GPL-2.0-or-later
#include "protocolv2.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

namespace Kolf::Session {
namespace {
constexpr qsizetype MaximumMessageBytes = 512 * 1024;

bool identifier(const QJsonValue &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,96}$"));
    return value.isString() && pattern.match(value.toString()).hasMatch();
}
}

bool decodeEnvelopeV2(const QByteArray &raw, EnvelopeV2 &envelope, QString &errorCode)
{
    errorCode.clear();
    if (raw.size() > MaximumMessageBytes) {
        errorCode = QStringLiteral("MessageTooLarge");
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        errorCode = QStringLiteral("MalformedJson");
        return false;
    }
    if (!document.isObject()) {
        errorCode = QStringLiteral("InvalidEnvelope");
        return false;
    }
    const auto object = document.object();
    const auto version = object.value(QStringLiteral("protocolVersion"));
    if (!version.isDouble() || version.toDouble() != 2.0) {
        errorCode = QStringLiteral("UnsupportedProtocol");
        return false;
    }
    if (!identifier(object.value(QStringLiteral("type"))) || !object.value(QStringLiteral("payload")).isObject()) {
        errorCode = QStringLiteral("InvalidEnvelope");
        return false;
    }
    for (const auto &key : {QStringLiteral("requestId"), QStringLiteral("lobbyId"), QStringLiteral("matchId")}) {
        if (object.contains(key) && !identifier(object.value(key))) {
            errorCode = QStringLiteral("InvalidEnvelope");
            return false;
        }
    }
    envelope = {
        object.value(QStringLiteral("type")).toString(),
        object.value(QStringLiteral("requestId")).toString(),
        object.value(QStringLiteral("lobbyId")).toString(),
        object.value(QStringLiteral("matchId")).toString(),
        object.value(QStringLiteral("payload")).toObject(),
    };
    return true;
}

int runV2ProtocolFixtures(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return 2;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const auto cases = document.object().value(QStringLiteral("cases")).toArray();
    if (parseError.error != QJsonParseError::NoError || cases.isEmpty()) return 2;
    for (const auto &value : cases) {
        const auto test = value.toObject();
        const auto raw = QJsonDocument(test.value(QStringLiteral("message")).toObject()).toJson(QJsonDocument::Compact);
        EnvelopeV2 envelope;
        QString errorCode;
        const bool valid = decodeEnvelopeV2(raw, envelope, errorCode);
        if (valid != test.value(QStringLiteral("valid")).toBool()
            || (!valid && errorCode != test.value(QStringLiteral("error")).toString())) {
            qCritical() << "FAIL v2 envelope" << test.value(QStringLiteral("name")).toString() << errorCode;
            return 1;
        }
    }
    qInfo() << "PASS native v2 envelope cases:" << cases.size();
    return 0;
}
}
