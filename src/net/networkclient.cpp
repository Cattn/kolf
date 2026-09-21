// SPDX-License-Identifier: GPL-2.0-or-later
#include "networkclient.h"
#include "session/protocolv3.h"
#include <QJsonDocument>

using namespace Kolf::Net;
static constexpr qint64 MaxBytes = 512 * 1024;
NetworkClient::NetworkClient(QObject *parent) : QObject(parent) {
    m_socket.setMaxAllowedIncomingFrameSize(MaxBytes);
    m_socket.setMaxAllowedIncomingMessageSize(MaxBytes);
    connect(&m_socket, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
        m_lastReceived.restart();
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(text.toUtf8(), &error);
        const auto raw = text.toUtf8();
        const auto m = document.object();
        bool valid = raw.size() <= MaxBytes && error.error == QJsonParseError::NoError && document.isObject();
        if (valid && m_protocolVersion == 1)
            valid = m[QStringLiteral("v")] == 1 && m[QStringLiteral("matchId")] == QLatin1String("prototype");
        else if (valid && m_protocolVersion == 3) {
            Session::EnvelopeV3 decoded;
            QString errorCode;
            valid = Session::decodeEnvelopeV3(raw, decoded, errorCode);
        } else valid = false;
        if (!valid) {
            Q_EMIT failed(QStringLiteral("Malformed protocol envelope")); close(); return;
        }
        Q_EMIT received(m);
    });
    connect(&m_socket, &QWebSocket::binaryMessageReceived, this, [this] { Q_EMIT failed(QStringLiteral("Unexpected binary message")); close(); });
    connect(&m_socket, &QWebSocket::connected, this, [this] {
        m_lastReceived.restart();
        Q_EMIT connected();
        if (!m_initialMessage.isEmpty()) send(m_initialMessage);
    });
    connect(&m_socket, &QWebSocket::disconnected, this, [this] {
        const bool unexpected = !m_closed;
        m_watchdog.stop(); m_flush.stop(); m_frame.clear();
        if (unexpected) Q_EMIT failed(QStringLiteral("Transport disconnected"));
        Q_EMIT disconnected();
    });
    connect(&m_socket, &QWebSocket::errorOccurred, this, [this] {
        if (!m_closed) { const auto reason = m_socket.errorString(); Q_EMIT failed(reason); close(); }
    });
    connect(&m_socket, &QWebSocket::pong, this, [this] { m_lastReceived.restart(); });
    connect(&m_flush, &QTimer::timeout, this, [this] {
        if (m_frame.isEmpty()) return;
        if (m_socket.bytesToWrite() < MaxBytes / 2) {
            m_socket.sendTextMessage(m_frame); m_frame.clear(); m_blocked.invalidate();
        } else if (m_blocked.isValid() && m_blocked.elapsed() > 5000) {
            Q_EMIT failed(QStringLiteral("Slow relay")); close();
        }
    });
    m_flush.start(30);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        if (m_lastReceived.elapsed() > 15000) { Q_EMIT failed(QStringLiteral("Transport heartbeat expired")); close(); }
        else m_socket.ping();
    });
}
void NetworkClient::open(const QUrl &url, const QJsonObject &initialMessage, int protocolVersion) {
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        Q_EMIT failed(QStringLiteral("Transport is already connected"));
        return;
    }
    m_closed = false; m_protocolVersion = protocolVersion; m_initialMessage = initialMessage;
    m_frame.clear(); m_blocked.invalidate(); m_lastReceived.start(); m_flush.start(30); m_watchdog.start(5000);
    m_socket.open(url);
}
void NetworkClient::send(const QJsonObject &m, bool visual) {
    if (m_closed || m_socket.state() != QAbstractSocket::ConnectedState) return;
    const auto data = QJsonDocument(m).toJson(QJsonDocument::Compact);
    if (data.size() > MaxBytes || m_socket.bytesToWrite() > 2 * MaxBytes) {
        Q_EMIT failed(QStringLiteral("Outgoing queue limit exceeded")); close(); return;
    }
    if (visual && m_socket.bytesToWrite() > MaxBytes / 2) {
        if (!m_frame.isEmpty()) ++m_coalesced;
        m_frame = QString::fromUtf8(data); if (!m_blocked.isValid()) m_blocked.start(); return;
    }
    if (!visual) m_frame.clear();
    m_socket.sendTextMessage(QString::fromUtf8(data));
}
void NetworkClient::close() {
    m_closed = true; m_watchdog.stop(); m_flush.stop(); m_frame.clear(); m_initialMessage = {};
    m_socket.close();
}
