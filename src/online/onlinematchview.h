// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QJsonObject>
#include <QWidget>

class QCheckBox;
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
    QJsonObject uiSnapshot() const;

Q_SIGNALS:
    void changeServerRequested();
    void leaveRequested();

private:
    void updateProposalUi(const QJsonObject &state);
    void setHazardChoiceAvailable(bool available);
    void showNotice(const QString &notice);

    Session::SessionController *m_controller = nullptr;
    QVBoxLayout *m_layout = nullptr;
    ScoreBoard *m_scoreboard = nullptr;
    QLabel *m_notice = nullptr;
    QWidget *m_hazardPanel = nullptr;
    QPushButton *m_dropButton = nullptr;
    QPushButton *m_rehitButton = nullptr;
    QWidget *m_hostOptions = nullptr;
    QCheckBox *m_hostControls = nullptr;
    QCheckBox *m_allowVoting = nullptr;
    QCheckBox *m_confirmHostActions = nullptr;
    QLabel *m_guestStatus = nullptr;
    QWidget *m_proposalComposer = nullptr;
    QComboBox *m_proposalAction = nullptr;
    QSpinBox *m_proposalTarget = nullptr;
    QPushButton *m_proposeButton = nullptr;
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
