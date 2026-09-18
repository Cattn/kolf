// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QWebSocket>

namespace Kolf::Net {
class NetworkClient : public QObject {
    Q_OBJECT
public:
    explicit NetworkClient(QObject *parent = nullptr);
    void open(const QUrl &url, const QJsonObject &hello);
    void send(const QJsonObject &message, bool visual = false);
    void close();
    qint64 bufferedBytes() const { return m_socket.bytesToWrite(); }
    quint64 coalescedFrames() const { return m_coalesced; }
Q_SIGNALS:
    void received(const QJsonObject &message);
    void failed(const QString &reason);
private:
    QWebSocket m_socket;
    QTimer m_flush;
    QTimer m_watchdog;
    QElapsedTimer m_lastReceived;
    QElapsedTimer m_blocked;
    QString m_frame;
    quint64 m_coalesced = 0;
    bool m_closed = false;
};
}
