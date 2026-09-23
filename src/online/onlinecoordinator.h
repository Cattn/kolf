// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "net/networkclient.h"

#include <QJsonObject>
#include <QObject>
#include <QTimer>

namespace Kolf::Online {
class OnlineCoordinator : public QObject {
    Q_OBJECT
public:
    explicit OnlineCoordinator(QObject *parent = nullptr);
    const QJsonObject &lobbyState() const { return m_state; }
    const QJsonObject &serviceHello() const { return m_serviceHello; }
    const QString &memberId() const { return m_memberId; }
    Net::NetworkClient *networkClient() { return &m_network; }

public Q_SLOTS:
    void connectToService(const QString &endpoint);
    void disconnectFromService();
    void createLobby(const QString &displayName, const QString &colorMode, const QString &customColor, const QString &courseId);
    void joinLobby(const QString &joinCode, const QString &displayName, const QString &colorMode, const QString &customColor);
    void setReady(bool ready);
    void addPlayer(const QString &displayName, const QString &colorMode, const QString &customColor);
    void updatePlayer(const QString &playerId, const QString &displayName, const QString &colorMode, const QString &customColor);
    void removePlayer(const QString &playerId);
    void setCourse(const QString &courseId);
    void uploadCourse(const QString &path);
    void startMatch();
    void returnToLobby();

Q_SIGNALS:
    void connected();
    void connectionClosed();
    void lobbyChanged(const QJsonObject &state);
    void serviceChanged(const QJsonObject &hello);
    void lobbyClosed(const QString &reason);
    void matchPrepared(const QJsonObject &config);
    void statusChanged(const QString &status);
    void failed(const QString &reason);

private:
    QString requestId();
    void receive(const QJsonObject &message);
    void acceptState(const QJsonObject &state);
    void prepareCourse();
    void failPreparation(const QString &reason);
    void sendCourseReady(const QString &path, const QString &hash);
    void sendNextUploadChunk();
    void requestNextDownloadChunk();
    QString cacheCourse(const QByteArray &bytes, const QString &sha256);
    void send(const QString &type, const QJsonObject &payload, bool matchScoped = false);

    Net::NetworkClient m_network;
    QJsonObject m_state;
    QJsonObject m_serviceHello;
    QString m_memberId;
    QString m_lobbyId;
    QString m_matchId;
    QString m_preparedMatchId;
    QString m_endpoint;
    QString m_coursePath;
    QByteArray m_uploadBytes;
    QByteArray m_downloadBytes;
    QString m_uploadId;
    QString m_uploadHash;
    QString m_downloadHash;
    int m_uploadNextIndex = 0;
    int m_downloadNextIndex = 0;
    int m_downloadSize = 0;
    QTimer m_uploadTimer;
    QTimer m_downloadTimer;
};
}
