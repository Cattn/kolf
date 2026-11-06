// SPDX-License-Identifier: GPL-2.0-or-later
#include "serverprobe.h"

#include "serverlist.h"
#include "session/onlineprotocol.h"

#include <KLocalizedString>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Kolf::Online {

ServerProbe::ServerProbe(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(6000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        ProbeResult result;
        result.message = i18n("The server did not answer in time.");
        finish(result);
    });
}

void ServerProbe::connectSocket(QWebSocket *socket)
{
    connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &text) {
        if (socket != m_socket) return;
        const auto message = QJsonDocument::fromJson(text.toUtf8()).object();
        const auto type = message.value(QStringLiteral("type")).toString();
        if (type != QLatin1String("ServiceHello") && type != QLatin1String("UnsupportedProtocol")) return;
        const auto payload = message.value(QStringLiteral("payload")).toObject();
        ProbeResult result;
        result.roundTripMs = int(m_clock.elapsed());
        result.protocolVersion = payload.value(QStringLiteral("protocolVersion")).toInt(message.value(QStringLiteral("protocolVersion")).toInt());
        result.serverName = payload.value(QStringLiteral("serverName")).toString();
        result.serverVersion = payload.value(QStringLiteral("serverVersion")).toString();
        result.courseCount = int(payload.value(QStringLiteral("courses")).toArray().size());
        if (type == QLatin1String("ServiceHello") && result.protocolVersion == Session::onlineProtocolVersion) {
            result.status = ProbeResult::Status::Online;
        } else {
            result.status = ProbeResult::Status::Incompatible;
            result.message = result.protocolVersion > Session::onlineProtocolVersion
                ? i18n("This server needs a newer version of Kolf.")
                : i18n("This server runs an older online protocol than this Kolf.");
        }
        finish(result);
    });
    connect(socket, &QWebSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError error) {
        if (socket != m_socket) return;
        ProbeResult result;
        result.message = error == QAbstractSocket::HostNotFoundError ? i18n("The server address could not be found.")
            : error == QAbstractSocket::ConnectionRefusedError ? i18n("The server refused the connection.")
            : error == QAbstractSocket::SslHandshakeFailedError ? i18n("The secure connection failed. Check the server certificate.")
            : i18n("Couldn't connect (%1).", socket->errorString());
        finish(result);
    });
    connect(socket, &QWebSocket::disconnected, this, [this, socket] {
        if (socket != m_socket) return;
        ProbeResult result;
        result.message = i18n("The server closed the connection.");
        finish(result);
    });
}

void ServerProbe::probe(const QString &url)
{
    if (m_socket) {
        auto *old = m_socket.data();
        m_socket.clear();
        old->abort();
        old->deleteLater();
    }
    m_url = url;
    m_busy = true;
    m_timeout.start();
    const auto address = normalizeServerAddress(url);
    if (!address.isValid()) {
        ProbeResult result;
        result.message = address.error;
        finish(result);
        return;
    }
    auto *socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_socket = socket;
    connectSocket(socket);
    m_clock.start();
    socket->open(address.url);
}

void ServerProbe::finish(ProbeResult result)
{
    m_timeout.stop();
    result.url = m_url;
    if (m_socket) {
        auto *old = m_socket.data();
        m_socket.clear();
        old->abort();
        old->deleteLater();
    }
    QTimer::singleShot(0, this, [this, result] {
        if (result.url != m_url || !m_busy) return;
        m_busy = false;
        Q_EMIT finished(result);
    });
}

QString ServerProbe::describe(const ProbeResult &result)
{
    if (result.status == ProbeResult::Status::Unreachable) return result.message;
    QStringList parts;
    const QString name = result.serverName.isEmpty() ? i18n("Kolf server") : result.serverName;
    parts << (result.serverVersion.isEmpty() ? name : i18nc("server name and version", "%1 %2", name, result.serverVersion));
    parts << i18n("protocol %1", result.protocolVersion);
    if (result.courseCount > 0) parts << i18np("1 course", "%1 courses", result.courseCount);
    parts << i18n("%1 ms", result.roundTripMs);
    const QString details = parts.join(QStringLiteral(" · "));
    return result.status == ProbeResult::Status::Online ? i18n("Compatible. %1", details)
        : i18n("Not compatible: %1 %2", result.message, details);
}

}
