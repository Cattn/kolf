// SPDX-License-Identifier: GPL-2.0-or-later
#include "onlineautomation.h"

#include "online/onlinecoordinator.h"
#include "online/onlinewidget.h"
#include "online/onlinematchview.h"
#include "ui/resultsview.h"
#include "session/sessioncontroller.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QTimer>

using namespace Kolf::Testing;
using Kolf::Session::SessionController;
using Kolf::Session::ShotIntent;

OnlineAutomation::OnlineAutomation(const QJsonObject &config, Online::OnlineWidget *widget, UiSnapshot uiSnapshot, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_widget(widget)
    , m_uiSnapshot(std::move(uiSnapshot))
{
}

void OnlineAutomation::start()
{
    const auto role = m_config.value(QStringLiteral("role")).toString();
    const auto endpoint = m_config.value(QStringLiteral("endpoint")).toString();
    const auto joinCodeFile = m_config.value(QStringLiteral("joinCodeFile")).toString();
    const auto logDirectory = m_config.value(QStringLiteral("logDirectory")).toString();
    const int expectedMembers = m_config.value(QStringLiteral("expectedMembers")).toInt();
    const int expectedPlayers = m_config.value(QStringLiteral("expectedPlayers")).toInt();
    const int matches = m_config.value(QStringLiteral("matches")).toInt();
    if ((role != QLatin1String("owner") && role != QLatin1String("joiner")) || endpoint.isEmpty()
        || joinCodeFile.isEmpty() || logDirectory.isEmpty() || expectedMembers < 2 || expectedMembers > 8
        || expectedPlayers < expectedMembers || expectedPlayers > 8
        || matches < 1 || matches > 2 || !m_config.value(QStringLiteral("scriptedShots")).isArray()) {
        fail(QStringLiteral("Invalid online automation configuration."));
        return;
    }
    m_valid = true;
    auto &coordinator = m_widget->coordinator();
    connect(&coordinator, &Online::OnlineCoordinator::serviceChanged, this, &OnlineAutomation::createOrJoin);
    connect(&coordinator, &Online::OnlineCoordinator::lobbyChanged, this, &OnlineAutomation::advance);
    connect(&coordinator, &Online::OnlineCoordinator::failed, this, &OnlineAutomation::fail);
    m_widget->setEndpointAddress(endpoint);
    m_widget->setProfile(m_config.value(QStringLiteral("displayName")).toString(), QStringLiteral("custom"),
        m_config.value(QStringLiteral("color")).toString());
    QDir().mkpath(logDirectory);
    coordinator.connectToService(endpoint);
}

void OnlineAutomation::fail(const QString &reason)
{
    qCritical() << reason;
    if (m_valid) {
        QFile error(QDir(m_config.value(QStringLiteral("logDirectory")).toString())
            .filePath(QStringLiteral("automation-error.txt")));
        if (error.open(QIODevice::WriteOnly | QIODevice::Truncate)) error.write(reason.toUtf8());
    }
    QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(3); });
}

void OnlineAutomation::createOrJoin()
{
    if (m_createOrJoinSent) return;
    m_createOrJoinSent = true;
    auto &coordinator = m_widget->coordinator();
    if (m_config.value(QStringLiteral("role")) == QLatin1String("owner")) {
        coordinator.createLobby(m_widget->profileName(), m_widget->profileColorMode(), m_widget->profileCustomColor(),
            m_config.value(QStringLiteral("courseId")).toString());
        return;
    }
    auto *timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this, timer] {
        QFile file(m_config.value(QStringLiteral("joinCodeFile")).toString());
        if (!file.open(QIODevice::ReadOnly)) return;
        const auto joinCode = QString::fromUtf8(file.readAll()).trimmed();
        if (joinCode.isEmpty()) return;
        timer->stop(); timer->deleteLater();
        m_widget->coordinator().joinLobby(joinCode, m_widget->profileName(), m_widget->profileColorMode(),
            m_widget->profileCustomColor());
    });
    timer->start();
}

void OnlineAutomation::writeResultsEvidence(const QJsonObject &state)
{
    const auto result = state.value(QStringLiteral("latestResult")).toObject();
    if (result.isEmpty()) return;
    const auto matchId = result.value(QStringLiteral("matchId")).toString();
    QFile evidence(QDir(m_config.value(QStringLiteral("logDirectory")).toString())
        .filePath(matchId + QStringLiteral("/results.json")));
    if (evidence.open(QIODevice::WriteOnly | QIODevice::Truncate))
        evidence.write(QJsonDocument(QJsonObject{{QStringLiteral("result"), result},
            {QStringLiteral("visibleText"), m_widget->resultsText()}}).toJson());
    const auto image = QDir(m_config.value(QStringLiteral("logDirectory")).toString()).filePath(matchId + QStringLiteral("/results.png"));
    if (auto *view = m_widget->findChild<Kolf::Ui::ResultsView *>()) {
        view->resize(m_widget->size());
        view->grab().save(image);
    }
}

void OnlineAutomation::advance(const QJsonObject &state)
{
    auto &coordinator = m_widget->coordinator();
    m_mutationPending = false;
    const auto role = m_config.value(QStringLiteral("role")).toString();
    const auto phase = state.value(QStringLiteral("phase")).toString();
    const auto members = state.value(QStringLiteral("members")).toArray();
    const auto players = state.value(QStringLiteral("players")).toArray();
    if (role == QLatin1String("owner")) {
        QFile joinCode(m_config.value(QStringLiteral("joinCodeFile")).toString());
        if (joinCode.open(QIODevice::WriteOnly | QIODevice::Truncate))
            joinCode.write(state.value(QStringLiteral("joinCode")).toString().toUtf8());
    }
    if (phase == QLatin1String("Results")) {
        writeResultsEvidence(state);
        m_startRequested = false;
        const auto matchId = state.value(QStringLiteral("latestResult")).toObject().value(QStringLiteral("matchId")).toString();
        if (!matchId.isEmpty() && !m_returnedMatches.contains(matchId)) {
            m_returnedMatches.insert(matchId);
            ++m_completedMatches;
            coordinator.returnToLobby();
        }
        return;
    }
    if (phase != QLatin1String("Open")) return;
    if (m_completedMatches >= m_config.value(QStringLiteral("matches")).toInt()) {
        QTimer::singleShot(250, qApp, &QApplication::quit);
        return;
    }
    if (members.size() != m_config.value(QStringLiteral("expectedMembers")).toInt()) return;
    for (const auto &value : members)
        if (value.toObject().contains(QStringLiteral("connected")) && !value.toObject().value(QStringLiteral("connected")).toBool()) return;
    if (m_config.value(QStringLiteral("scriptedLobbyDrop")).toBool() && !m_lobbyDropDone) {
        m_lobbyDropDone = true;
        QFile marker(QDir(m_config.value(QStringLiteral("logDirectory")).toString()).filePath(QStringLiteral("lobby-drop.txt")));
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) marker.write("dropped\n");
        QTimer::singleShot(300, this, [this] { m_widget->networkClient()->dropForTest(); });
        return;
    }

    const auto rules = m_config.value(QStringLiteral("lobbyRules")).toObject();
    if (!rules.isEmpty() && role == QLatin1String("owner") && !m_rulesSent) {
        m_rulesSent = true;
        coordinator.setLobbyRules(rules.value(QStringLiteral("hostControls")).toBool(), rules.value(QStringLiteral("allowVoting")).toBool());
        return;
    }
    if (!rules.isEmpty() && (state.value(QStringLiteral("hostControls")) != rules.value(QStringLiteral("hostControls"))
        || state.value(QStringLiteral("allowVoting")) != rules.value(QStringLiteral("allowVoting")))) return;

    const auto customCourse = m_config.value(QStringLiteral("customCourse")).toString();
    if (!customCourse.isEmpty()) {
        const auto selectedId = state.value(QStringLiteral("selectedCourseId")).toString();
        if (!selectedId.startsWith(QStringLiteral("upload_"))) {
            if (role == QLatin1String("owner") && !m_customCourseSent) {
                m_customCourseSent = true;
                coordinator.uploadCourse(customCourse);
            }
            return;
        }
    }

    int localPlayers = 0;
    bool localReady = false;
    for (const auto &value : members) {
        const auto member = value.toObject();
        const bool ready = member.value(QStringLiteral("ready")).toBool();
        if (member.value(QStringLiteral("memberId")).toString() == coordinator.memberId()) localReady = ready;
    }
    for (const auto &value : players)
        if (value.toObject().value(QStringLiteral("ownerMemberId")).toString() == coordinator.memberId()) ++localPlayers;

    const auto additionalPlayers = m_config.value(QStringLiteral("additionalPlayers")).toArray();
    if (m_addedPlayers < additionalPlayers.size()) {
        if (localPlayers == m_addedPlayers + 1 && !m_mutationPending) {
            const auto player = additionalPlayers.at(m_addedPlayers).toObject();
            ++m_addedPlayers;
            m_mutationPending = true;
            coordinator.addPlayer(player.value(QStringLiteral("displayName")).toString(), QStringLiteral("custom"),
                player.value(QStringLiteral("color")).toString());
        }
        return;
    }
    if (players.size() != m_config.value(QStringLiteral("expectedPlayers")).toInt()) return;
    if (!m_lobbyCaptured) {
        if (m_lobbyCapturePending) return;
        m_lobbyCapturePending = true;
        QTimer::singleShot(900, this, [this] {
            m_lobbyCaptured = true;
            const auto directory = m_config.value(QStringLiteral("logDirectory")).toString();
            m_widget->window()->grab().save(QDir(directory).filePath(QStringLiteral("lobby.png")));
            QFile evidence(QDir(directory).filePath(QStringLiteral("lobby.json")));
            if (evidence.open(QIODevice::WriteOnly)) evidence.write(QJsonDocument(m_widget->coordinator().lobbyState()).toJson());
            advance(m_widget->coordinator().lobbyState());
        });
        return;
    }
    if (!localReady && !m_mutationPending) {
        if (role != QLatin1String("owner")) {
            m_mutationPending = true;
            if (auto *primary = m_widget->findChild<QPushButton *>(QStringLiteral("lobbyPrimary"))) primary->click();
            return;
        }
    }
    bool guestsReady = true;
    for (const auto &value : members) {
        const auto member = value.toObject();
        if (member.value(QStringLiteral("memberId")).toString() != coordinator.memberId()) guestsReady = guestsReady && member.value(QStringLiteral("ready")).toBool();
    }
    if (role == QLatin1String("owner") && guestsReady && !m_mutationPending && !m_startRequested) {
        auto *primary = m_widget->findChild<QPushButton *>(QStringLiteral("lobbyPrimary"));
        if (!primary || !primary->isEnabled()) return;
        m_mutationPending = true;
        m_startRequested = true;
        primary->click();
    }
}

QJsonObject OnlineAutomation::sessionConfig(const QJsonObject &matchConfig) const
{
    auto config = matchConfig;
    config[QStringLiteral("scriptedShots")] = m_config.value(QStringLiteral("scriptedShots"));
    for (const auto &key : {QStringLiteral("scriptedHazardAction"), QStringLiteral("scriptedHostResetTurn"),
             QStringLiteral("scriptedHostUndoTurn"), QStringLiteral("scriptedHostSkipTurn"),
             QStringLiteral("scriptedHostGo"), QStringLiteral("scriptedHostProposal")}) {
        if (m_config.contains(key)) config[key] = m_config.value(key);
    }
    if (m_config.value(QStringLiteral("scriptedHostCombined")).toBool() && m_completedMatches == 0) {
        config[QStringLiteral("scriptedHostResetTurn")] = 3;
        config[QStringLiteral("scriptedHostUndoTurn")] = 6;
        config[QStringLiteral("scriptedHostSkipTurn")] = 12;
        config[QStringLiteral("scriptedHostGo")] = QJsonArray{
            QJsonObject{{QStringLiteral("turn"), 8}, {QStringLiteral("action"), QStringLiteral("goLast")}},
            QJsonObject{{QStringLiteral("turn"), 9}, {QStringLiteral("action"), QStringLiteral("goFirst")}}};
    }
    config[QStringLiteral("capture")] = true;
    config[QStringLiteral("verifySnapshots")] = true;
    config[QStringLiteral("logFrames")] = true;
    config[QStringLiteral("exitWhenFinished")] = false;
    config[QStringLiteral("logDirectory")] = QDir(m_config.value(QStringLiteral("logDirectory")).toString())
        .filePath(matchConfig.value(QStringLiteral("matchId")).toString());
    return config;
}

void OnlineAutomation::attach(SessionController *controller, const QJsonObject &sessionConfig)
{
    const auto size = m_config.value(QStringLiteral("windowSize")).toArray();
    if (size.size() == 2) m_widget->window()->resize(size[0].toInt(), size[1].toInt());
    m_session = controller;
    m_sessionConfig = sessionConfig;
    m_uiLogDirectory = sessionConfig.value(QStringLiteral("logDirectory")).toString();
    m_lastUiSnapshot.clear();
    m_resetDone = m_undoDone = m_skipDone = false;
    m_proposalSent = m_proposalDecided = m_proposalVoted = false;
    m_goIndex = 0;
    m_shotTurns.clear();
    connect(controller, &SessionController::messageProcessed, this, &OnlineAutomation::sessionMessage);
    connect(controller, &SessionController::proposalUiChanged, this, &OnlineAutomation::recordUi);
    connect(controller, &SessionController::statusChanged, this, [this] {
        auto *session = m_session.data();
        if (!session || !session->isPaused() || m_pauseCaptured || m_uiLogDirectory.isEmpty()) return;
        m_pauseCaptured = true;
        const auto image = QDir(m_uiLogDirectory).filePath(QStringLiteral("paused.png"));
        QTimer::singleShot(300, m_widget, [widget = m_widget, image] { widget->window()->grab().save(image); });
    });
}

void OnlineAutomation::recordUi(const QJsonObject &proposalUi)
{
    if (!m_session || m_uiLogDirectory.isEmpty() || !m_uiSnapshot) return;
    auto snapshot = m_uiSnapshot();
    snapshot[QStringLiteral("role")] = m_session->isAuthority() ? QStringLiteral("host") : QStringLiteral("guest");
    snapshot[QStringLiteral("controlsEnabled")] = proposalUi.value(QStringLiteral("controlsEnabled")).toBool();
    snapshot[QStringLiteral("votingEnabled")] = proposalUi.value(QStringLiteral("votingEnabled")).toBool();
    snapshot[QStringLiteral("proposalActive")] = !proposalUi.value(QStringLiteral("proposal")).toObject().isEmpty();
    const auto encoded = QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
    if (encoded == m_lastUiSnapshot) return;
    m_lastUiSnapshot = encoded;
    const auto image = QDir(m_uiLogDirectory).filePath(snapshot.value(QStringLiteral("proposalActive")).toBool()
        ? QStringLiteral("request.png") : QStringLiteral("hud.png"));
    if (snapshot.value(QStringLiteral("proposalActive")).toBool() || (m_session->isReady() && m_session->phase() == QLatin1String("AwaitingShot")))
        QTimer::singleShot(0, m_widget, [widget = m_widget, image] { widget->window()->grab().save(image); });
    QFile output(QDir(m_uiLogDirectory).filePath(QStringLiteral("ui-state.jsonl")));
    if (output.open(QIODevice::WriteOnly | QIODevice::Append)) { output.write(encoded); output.write("\n"); }
}

void OnlineAutomation::scheduleShot(int turn, bool requireReady)
{
    auto *session = m_session.data();
    const auto shots = m_sessionConfig.value(QStringLiteral("scriptedShots")).toArray();
    if (!session || turn > shots.size()) return;
    m_shotTurns.insert(turn);
    const auto shot = shots[turn - 1].toObject();
    QTimer::singleShot(150, session, [this, session, shot, turn, requireReady] {
        if (session->turn() != turn || (requireReady && !session->isReady())) return;
        if (session->isPaused()) { m_shotTurns.remove(turn); return; }
        session->submitShot({shot[QStringLiteral("directionRadians")].toDouble(),
            shot[QStringLiteral("launchMagnitude")].toDouble(), shot[QStringLiteral("advanced")].toBool()});
    });
}

void OnlineAutomation::sessionMessage(const QString &type, const QJsonObject &message)
{
    auto *session = m_session.data();
    if (!session) return;
    const auto &config = m_sessionConfig;
    const auto proposal = config.value(QStringLiteral("scriptedHostProposal")).toObject();
    const bool proposalTurn = !proposal.isEmpty() && proposal.value(QStringLiteral("turn")).toInt() == session->turn();

    if (type == QLatin1String("FullState")) m_resyncSeen = true;
    if (type == QLatin1String("InputReady")) {
        sessionInputReady();
        return;
    }
    if (type == QLatin1String("HostControlsChanged")) {
        if (session->isAuthority() && session->hostControlsEnabled() && !m_resetDone
            && config.value(QStringLiteral("scriptedHostResetTurn")).toInt() == session->turn() && session->isReady()) {
            m_resetDone = true;
            session->resetOnlineHole();
        }
        if (session->isAuthority() && session->hostControlsEnabled() && !m_undoDone
            && config.value(QStringLiteral("scriptedHostUndoTurn")).toInt() == session->turn() && session->isReady()) {
            m_undoDone = true;
            session->undoOnlineShot();
        }
        if (session->isAuthority() && session->hostControlsEnabled() && !m_skipDone
            && config.value(QStringLiteral("scriptedHostSkipTurn")).toInt() == session->turn() && session->isReady()) {
            m_skipDone = true;
            session->skipOnlineHole();
        }
        const auto go = config.value(QStringLiteral("scriptedHostGo")).toArray();
        if (session->isAuthority() && session->hostControlsEnabled() && session->isReady() && m_goIndex < go.size()) {
            const auto move = go[m_goIndex].toObject();
            if (move.value(QStringLiteral("turn")).toInt() == session->turn()) {
                ++m_goIndex;
                session->navigateOnline(move.value(QStringLiteral("action")).toString(),
                    move.value(QStringLiteral("targetHole")).toInt());
            }
        }
        return;
    }
    if (type == QLatin1String("HostProposalChanged")) {
        if (proposalTurn && proposal.contains(QStringLiteral("vote")) && !m_proposalVoted
            && !session->proposalRequestPending()) {
            m_proposalVoted = true;
            session->voteHostProposal(proposal.value(QStringLiteral("vote")).toBool());
        }
        if (session->isAuthority() && proposalTurn && !m_proposalDecided
            && proposal.value(QStringLiteral("action")) == message.value(QStringLiteral("action"))) {
            m_proposalDecided = true;
            const bool approve = proposal.value(QStringLiteral("approve")).toBool();
            QTimer::singleShot(400, session, [session, approve] {
                if (session->hasHostProposal() && !session->isInterrupted()) session->decideHostProposal(approve);
            });
        }
        return;
    }
    if (type == QLatin1String("HostProposalClosed")) {
        if (proposalTurn && message.value(QStringLiteral("reason")) == QLatin1String("rejected by host")
            && session->ownsSlot(session->activeSlot()) && !m_shotTurns.contains(session->turn()))
            scheduleShot(session->turn(), true);
        return;
    }
    if (type == QLatin1String("HostProposalAccepted")) {
        if (session->hasHostProposal() && !m_proposalVoted && proposalTurn && proposal.contains(QStringLiteral("vote"))) {
            m_proposalVoted = true;
            session->voteHostProposal(proposal.value(QStringLiteral("vote")).toBool());
        }
    }
}

void OnlineAutomation::sessionInputReady()
{
    auto *session = m_session.data();
    const auto &config = m_sessionConfig;
    const bool authority = session->isAuthority();
    const bool awaitingShot = session->phase() == QLatin1String("AwaitingShot");
    const int turn = session->turn();
    if (m_resyncSeen) {
        m_resyncSeen = false;
        m_shotTurns.remove(turn);
    }
    const int leaveTurn = m_config.value(QStringLiteral("scriptedLeaveTurn")).toInt();
    if (!authority && leaveTurn > 0 && turn >= leaveTurn && !m_leaveDone) {
        m_leaveDone = true;
        QFile marker(QDir(m_config.value(QStringLiteral("logDirectory")).toString()).filePath(QStringLiteral("left-match.txt")));
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) marker.write(QByteArray::number(turn));
        if (auto *view = m_widget->window()->findChild<Online::OnlineMatchView *>())
            QTimer::singleShot(0, view, [view] { Q_EMIT view->leaveRequested(); });
        QTimer::singleShot(1500, qApp, [] { QCoreApplication::exit(0); });
        return;
    }
    const int dropTurn = m_config.value(QStringLiteral("scriptedMatchDropTurn")).toInt();
    if (dropTurn > 0 && turn >= dropTurn && !m_matchDropDone) {
        m_matchDropDone = true;
        QFile marker(QDir(m_config.value(QStringLiteral("logDirectory")).toString()).filePath(QStringLiteral("match-drop.txt")));
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) marker.write(QByteArray::number(turn));
        QTimer::singleShot(200, this, [this] { m_widget->networkClient()->dropForTest(); });
    }

    const auto proposal = config.value(QStringLiteral("scriptedHostProposal")).toObject();
    if (authority && !proposal.isEmpty() && turn == 1 && !session->hostControlsEnabled())
        session->setHostControlsEnabled(true);
    if (!proposal.isEmpty() && turn == proposal.value(QStringLiteral("turn")).toInt()) {
        if (proposal.value(QStringLiteral("proposer")).toBool() && !m_proposalSent) {
            m_proposalSent = true;
            session->proposeHostAction(proposal.value(QStringLiteral("action")).toString(),
                proposal.value(QStringLiteral("targetHole")).toInt());
        }
        return;
    }

    const struct { bool &done; const char *key; void (SessionController::*action)(); } hostActions[] = {
        {m_resetDone, "scriptedHostResetTurn", &SessionController::resetOnlineHole},
        {m_undoDone, "scriptedHostUndoTurn", &SessionController::undoOnlineShot},
        {m_skipDone, "scriptedHostSkipTurn", &SessionController::skipOnlineHole},
    };
    for (const auto &hostAction : hostActions) {
        if (!authority || !awaitingShot || hostAction.done
            || config.value(QLatin1String(hostAction.key)).toInt() != turn) continue;
        if (session->hostControlsEnabled()) {
            hostAction.done = true;
            (session->*hostAction.action)();
        } else {
            session->setHostControlsEnabled(true);
        }
        return;
    }

    const auto go = config.value(QStringLiteral("scriptedHostGo")).toArray();
    if (authority && awaitingShot && m_goIndex < go.size()) {
        const auto move = go[m_goIndex].toObject();
        if (move.value(QStringLiteral("turn")).toInt() == turn) {
            if (session->hostControlsEnabled()) {
                ++m_goIndex;
                session->navigateOnline(move.value(QStringLiteral("action")).toString(),
                    move.value(QStringLiteral("targetHole")).toInt());
            } else {
                session->setHostControlsEnabled(true);
            }
            return;
        }
    }

    if (awaitingShot && session->ownsSlot(session->activeSlot()) && !m_shotTurns.contains(turn))
        scheduleShot(turn, false);
    if (session->phase() == QLatin1String("AwaitingHazardChoice") && session->ownsSlot(session->choiceSlot())
        && config.contains(QStringLiteral("scriptedHazardAction")))
        session->chooseHazardAction(config.value(QStringLiteral("scriptedHazardAction")).toString());
}
