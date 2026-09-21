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
namespace Kolf::Session { class SessionController; }

namespace Kolf::Online {
class OnlineWindow : public QWidget {
    Q_OBJECT
public:
    explicit OnlineWindow(QWidget *parent = nullptr);
    void startAutomation(const QJsonObject &config);

private:
    void showEntry();
    void showLobby(const QJsonObject &state);
    void showResults(const QJsonObject &state);
    void savePreferences();
    void closeMatch();
    void advanceAutomation(const QJsonObject &state);
    void failAutomation(const QString &reason);

    OnlineCoordinator m_coordinator;
    QStackedWidget *m_pages;
    QLineEdit *m_endpoint;
    QLabel *m_connectStatus;
    QLineEdit *m_name;
    QLineEdit *m_color;
    QLineEdit *m_joinCode;
    QComboBox *m_createCourse;
    QLabel *m_entryStatus;
    QLabel *m_lobbySummary;
    QLabel *m_lobbyStatus;
    QListWidget *m_members;
    QListWidget *m_players;
    QComboBox *m_lobbyCourse;
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
    Kolf::Session::SessionController *m_matchController = nullptr;
};
}
