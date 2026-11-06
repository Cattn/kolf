// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QJsonObject>
#include <QWidget>

class QAction;
class QMenu;
class QToolButton;
class QGridLayout;
class QFrame;
class KolfGame;
namespace Kolf::Ui { class GameHud; }
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QVBoxLayout;
class ScoreBoard;
namespace Kolf::Net { class NetworkClient; }
namespace Kolf::Session { class SessionController; }

namespace Kolf::Online {
class OnlineMatchView : public QWidget {
    Q_OBJECT
public:
    explicit OnlineMatchView(QWidget *parent = nullptr);
    Session::SessionController *startMatch(const QJsonObject &config, Net::NetworkClient *network);
    void finishMatch();
    bool confirmHostActions() const;
    void setConfirmHostActions(bool confirm);
    QJsonObject uiSnapshot() const;

Q_SIGNALS:
    void changeServerRequested();
    void leaveRequested();

private:
    void updateProposalUi(const QJsonObject &state);
    void setHazardChoiceAvailable(bool available);
    void updateCountdown();
    void showNotice(const QString &notice);

    Session::SessionController *m_controller = nullptr;
    QVBoxLayout *m_layout = nullptr;
    ScoreBoard *m_scoreboard = nullptr;
    QLabel *m_notice = nullptr;
    QWidget *m_hazardPanel = nullptr;
    QPushButton *m_dropButton = nullptr;
    QPushButton *m_rehitButton = nullptr;
    QAction *m_hostControls = nullptr;
    QAction *m_allowVoting = nullptr;
    QAction *m_confirmHostActions = nullptr;
    QList<QAction *> m_actions;
    QMenu *m_matchMenu = nullptr;
    QMenu *m_rulesMenu = nullptr;
    QMenu *m_historyMenu = nullptr;
    QToolButton *m_matchButton = nullptr;
    QToolButton *m_scoreToggle = nullptr;
    Ui::GameHud *m_hud = nullptr;
    QWidget *m_canvas = nullptr;
    QGridLayout *m_canvasLayout = nullptr;
    QFrame *m_toast = nullptr;
    KolfGame *m_game = nullptr;
    int m_highestHole = 1;
    int m_lastHole = 1;
    QString m_voteSummary;
    QLabel *m_proposalLabel = nullptr;
    QWidget *m_proposalVotes = nullptr;
    QPushButton *m_voteYesButton = nullptr;
    QPushButton *m_voteNoButton = nullptr;
    QPushButton *m_approveButton = nullptr;
    QPushButton *m_rejectButton = nullptr;
    QPushButton *m_withdrawButton = nullptr;
    QJsonObject m_proposal;
    QString m_localMemberId;
    bool m_isHost = false;
};
}
