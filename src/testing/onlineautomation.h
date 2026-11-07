// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <functional>

namespace Kolf::Online { class OnlineWidget; }
namespace Kolf::Session { class SessionController; }

namespace Kolf::Testing {
class OnlineAutomation : public QObject {
    Q_OBJECT
public:
    using UiSnapshot = std::function<QJsonObject()>;

    OnlineAutomation(const QJsonObject &config, Online::OnlineWidget *widget, UiSnapshot uiSnapshot, QObject *parent = nullptr);
    void start();
    QJsonObject sessionConfig(const QJsonObject &matchConfig) const;
    void attach(Session::SessionController *controller, const QJsonObject &sessionConfig);
    void recordUi(const QJsonObject &proposalUi);

private:
    void fail(const QString &reason);
    void createOrJoin();
    void advance(const QJsonObject &state);
    void writeResultsEvidence(const QJsonObject &state);
    void sessionMessage(const QString &type, const QJsonObject &message);
    void sessionInputReady();
    void scheduleShot(int turn, bool requireReady);

    QJsonObject m_config;
    Online::OnlineWidget *m_widget;
    UiSnapshot m_uiSnapshot;
    bool m_valid = false;
    bool m_createOrJoinSent = false;
    bool m_mutationPending = false;
    bool m_startRequested = false;
    bool m_lobbyDropDone = false;
    bool m_customCourseSent = false;
    bool m_rulesSent = false;
    bool m_lobbyCaptured = false;
    bool m_lobbyCapturePending = false;
    int m_completedMatches = 0;
    int m_addedPlayers = 0;
    QSet<QString> m_returnedMatches;

    QPointer<Session::SessionController> m_session;
    QJsonObject m_sessionConfig;
    QString m_uiLogDirectory;
    QByteArray m_lastUiSnapshot;
    bool m_resetDone = false;
    bool m_undoDone = false;
    bool m_skipDone = false;
    bool m_proposalSent = false;
    bool m_proposalDecided = false;
    bool m_proposalVoted = false;
    int m_goIndex = 0;
    QSet<int> m_shotTurns;
};
}
