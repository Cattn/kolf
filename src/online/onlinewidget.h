// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "onlinecoordinator.h"

#include <QHash>
#include <QStringList>
#include <QJsonObject>
#include <QSet>
#include <QWidget>

class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QToolButton;
class QPushButton;
class QStackedWidget;
class QTimer;
namespace Kolf::Online { class PlayerProfileEditor; }
namespace Kolf::Ui { class ResultsView; }
namespace Kolf { class ItemFactory; }

namespace Kolf::Online {
class LobbyView;
class OnlineWidget : public QWidget {
    Q_OBJECT
public:
    OnlineWidget(const Kolf::ItemFactory &factory, QWidget *parent = nullptr);
    ~OnlineWidget() override;
    void leaveOnline();
    void enterOnline();
    void changeServer();
    Net::NetworkClient *networkClient() { return m_coordinator.networkClient(); }
    OnlineCoordinator &coordinator() { return m_coordinator; }
    void setEndpointAddress(const QString &endpoint);
    void serversChanged();
    void setProfile(const QString &name, const QString &colorMode, const QString &customColor);
    QString profileName() const;
    QString profileColorMode() const;
    QString profileCustomColor() const;
    QString resultsText() const;

Q_SIGNALS:
    void leaveRequested();
    void matchRequested(const QJsonObject &config);
    void matchEnded();
    void statusChanged(const QString &status);
    void manageServersRequested();
    void serviceNotice(const QString &text);
    void matchPauseChanged(bool paused, const QString &reason);

private:
    enum class ConnectionState { Disconnected, Connecting, Disconnecting, Connected, InLobby, InMatch, Failed };
    void showEntry();
    void showLobby(const QJsonObject &state);
    void showResults(const QJsonObject &state);
    void announceRematch(const QJsonObject &state);
    void savePreferences();
    void updateProfileChip();
    static QString normalizeJoinCode(const QString &input);
    void beginConnection();
    void reloadServers();
    void rebuildServerMenu();
    void switchToServer(const QString &url);
    void showServerNotice(const QJsonObject &hello);
    void setConnectionState(ConnectionState state, const QString &message = {});

    OnlineCoordinator m_coordinator;
    QStackedWidget *m_pages;
    QLineEdit *m_endpoint;
    QLabel *m_connectStatus;
    QToolButton *m_serverButton;
    QMenu *m_serverMenu;
    QStringList m_serverOrder;
    QHash<QString, QString> m_servers;
    QString m_defaultServer;
    QString m_sessionServer;
    QString m_serverDisplayName;
    QLabel *m_serverNotice;
    QString m_restartNotice;
    bool m_connectAfterDisconnect = false;
    QGroupBox *m_serverControls;
    QGroupBox *m_hostCard;
    QGroupBox *m_joinCard;
    QLabel *m_profileChip;
    QPushButton *m_connectButton;
    QPushButton *m_createButton;
    QPushButton *m_joinButton;
    QTimer *m_connectTimeout;
    PlayerProfileEditor *m_profile;
    QLineEdit *m_joinCode;
    QComboBox *m_createCourse;
    QLabel *m_entryStatus;
    LobbyView *m_lobby;
    Kolf::Ui::ResultsView *m_result;
    QJsonObject m_state;
    bool m_matchActive = false;
    bool m_rematchRequested = false;
    bool m_rematchReadySent = false;
    bool m_entryRequestPending = false;
    bool m_disconnectRequested = false;
    ConnectionState m_connectionState = ConnectionState::Disconnected;
};
}
