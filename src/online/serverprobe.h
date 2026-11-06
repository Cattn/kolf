// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWebSocket>

namespace Kolf::Online {

struct ProbeResult {
    enum class Status { Online, Incompatible, Unreachable };
    QString url;
    Status status = Status::Unreachable;
    int roundTripMs = -1;
    int protocolVersion = 0;
    QString serverName;
    QString serverVersion;
    int courseCount = 0;
    QString message;
};

class ServerProbe : public QObject {
    Q_OBJECT
public:
    explicit ServerProbe(QObject *parent = nullptr);
    void probe(const QString &url);
    bool isRunning() const { return m_busy; }
    QString url() const { return m_url; }

    static QString describe(const ProbeResult &result);

Q_SIGNALS:
    void finished(const Kolf::Online::ProbeResult &result);

private:
    void finish(ProbeResult result);
    void connectSocket(QWebSocket *socket);

    QPointer<QWebSocket> m_socket;
    QTimer m_timeout;
    QElapsedTimer m_clock;
    QString m_url;
    bool m_busy = false;
};

}
