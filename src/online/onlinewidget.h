// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "onlinecoordinator.h"

#include <QJsonObject>
#include <QSet>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTextEdit;
class QTimer;
namespace Kolf::Online { class PlayerProfileEditor; }

namespace Kolf::Online {
class OnlineWidget : public QWidget {
    Q_OBJECT
public:
    explicit OnlineWidget(QWidget *parent = nullptr);
    ~OnlineWidget() override;
    void startAutomation(const QJsonObject &config);
    void leaveOnline();
    void enterOnline();
    void changeServer();
    Net::NetworkClient *networkClient() { return m_coordinator.networkClient(); }

Q_SIGNALS:
    void leaveRequested();
    void matchRequested(const QJsonObject &config);
    void matchEnded();
    void statusChanged(const QString &status);

private:
    enum class ConnectionState { Disconnected, Connecting, Disconnecting, Connected, InLobby, InMatch, Failed };
    void showEntry();
    void showLobby(const QJsonObject &state);
    void showResults(const QJsonObject &state);
    void announceRematch(const QJsonObject &state);
    void savePreferences();
    void beginConnection();
    void setConnectionState(ConnectionState state, const QString &message = {});
    void advanceAutomation(const QJsonObject &state);
    void failAutomation(const QString &reason);

    OnlineCoordinator m_coordinator;
    QStackedWidget *m_pages;
    QLineEdit *m_endpoint;
    QLabel *m_connectStatus;
    QLabel *m_serverLabel;
    QComboBox *m_recentServers;
    QWidget *m_serverControls;
    QPushButton *m_connectButton;
    QPushButton *m_changeServerButton;
    QPushButton *m_createButton;
    QPushButton *m_joinButton;
    QTimer *m_connectTimeout;
    PlayerProfileEditor *m_profile;
    QLineEdit *m_joinCode;
    QComboBox *m_createCourse;
    QLabel *m_entryStatus;
    QLabel *m_lobbySummary;
    QLabel *m_lobbyStatus;
    QLabel *m_colorWarning;
    QListWidget *m_players;
    QComboBox *m_lobbyCourse;
    QPushButton *m_customCourse;
    QPushButton *m_ready;
    QPushButton *m_start;
    QPushButton *m_addPlayer;
    QPushButton *m_editPlayer;
    QPushButton *m_removePlayer;
    QTextEdit *m_result;
    QJsonObject m_state;
    QJsonObject m_automation;
    QSet<QString> m_automationReturnedMatches;
    int m_automationCompletedMatches = 0;
    int m_automationAddedPlayers = 0;
    bool m_automationCreateOrJoinSent = false;
    bool m_automationMutationPending = false;
    bool m_automationStartRequested = false;
    bool m_automationCustomCourseSent = false;
    bool m_matchActive = false;
    bool m_rematchRequested = false;
    bool m_rematchReadySent = false;
    bool m_entryRequestPending = false;
    bool m_disconnectRequested = false;
    ConnectionState m_connectionState = ConnectionState::Disconnected;
};
}
