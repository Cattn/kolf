// SPDX-License-Identifier: GPL-2.0-or-later
#include "sessioncontroller.h"
#include "landscape.h"
#include "objects.h"
#include "obstacles.h"
#include "rules_build_id.h"
#include "onlineprotocol.h"
#include "online/color.h"
#include "online/coursefile.h"
#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <QWidget>
#include <KConfig>
#include <QRegularExpression>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QPen>
#include <cmath>

using namespace Kolf::Session;

SessionController::SessionController(const QJsonObject &config, Net::NetworkClient *network, QWidget *gameHost, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_role(config[QStringLiteral("role")] == QLatin1String("authority") ? Role::Authority : Role::Guest)
    , m_network(network)
    , m_gameHost(gameHost) {
    for (const auto &value : config[QStringLiteral("localPlayerIds")].toArray())
        if (!value.toString().isEmpty()) m_localPlayerIds.insert(value.toString());
    connect(m_network, &Net::NetworkClient::received, this, [this](const QJsonObject &message) {
        const auto type = message.value(QStringLiteral("type")).toString();
        static const QSet<QString> lobby{QStringLiteral("MatchResult"), QStringLiteral("MatchStarted"),
            QStringLiteral("LobbyState"), QStringLiteral("LobbyCreated"), QStringLiteral("LobbyClosed"),
            QStringLiteral("ServiceHello"),
            QStringLiteral("PreparationReady"), QStringLiteral("PreparationAborted"),
            QStringLiteral("RequestRejected"), QStringLiteral("UnsupportedProtocol")};
        if (message.value(QStringLiteral("matchId")).toString() == m_config.value(QStringLiteral("matchId")).toString()
            && !lobby.contains(type)) receive(message);
    });
    m_presentation.setDelay(config[QStringLiteral("presentationDelayMs")].toInt(100));
    connect(&m_presentation, &Replication::PresentationController::present, this, [this](const QJsonObject &s) {
        if (!m_adapter || m_interrupted) return;
        QString error;
        if (!m_adapter->apply(s, error)) { interrupt(error); return; }
        if (m_config[QStringLiteral("logFrames")].toBool()) {
            const auto balls = s[QStringLiteral("balls")].toArray();
            const int slot = s[QStringLiteral("activeSlot")].toInt(-1);
            if (slot >= 0 && slot < balls.size()) {
                const auto ball = balls[slot].toObject();
                log(QStringLiteral("presentedFrame"), {{QStringLiteral("x"), ball[QStringLiteral("x")]},
                    {QStringLiteral("y"), ball[QStringLiteral("y")]},
                    {QStringLiteral("phase"), s[QStringLiteral("phase")]}});
            }
        }
    });
    m_clock.start();
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (m_pending.isEmpty() || m_interrupted) return;
        if (m_pendingClock.elapsed() > 15000) { interrupt(QStringLiteral("Shot acceptance timed out")); return; }
        m_network->send(m_pendingMessage); // Same ID and content: never a speculative new shot.
    });
    m_retry.start(1500);
    const auto dir = config[QStringLiteral("logDirectory")].toString();
    QDir().mkpath(dir); m_log.setFileName(QDir(dir).filePath(QStringLiteral("session.jsonl"))); m_log.open(QIODevice::WriteOnly | QIODevice::Truncate);
    QFile course(config[QStringLiteral("course")].toString());
    if (!course.open(QIODevice::ReadOnly) || course.size() > 4 * 1024 * 1024) { interrupt(QStringLiteral("Cannot read bounded course file")); return; }
    const auto courseBytes = course.readAll();
    m_hash = config.value(QStringLiteral("courseSource")) == QLatin1String("uploaded")
        ? Online::rawCourseHash(courseBytes) : onlineCourseHash(courseBytes);
    // Every current built-in is supported. Reject unknown groups before scene construction.
    m_factory.registerType<Kolf::Slope>(QStringLiteral("slope"), QStringLiteral("Slope"));
    m_factory.registerType<Kolf::Puddle>(QStringLiteral("puddle"), QStringLiteral("Puddle"));
    m_factory.registerType<Kolf::Wall>(QStringLiteral("wall"), QStringLiteral("Wall"));
    m_factory.registerType<Kolf::Cup>(QStringLiteral("cup"), QStringLiteral("Cup"));
    m_factory.registerType<Kolf::Sand>(QStringLiteral("sand"), QStringLiteral("Sand"));
    m_factory.registerType<Kolf::Windmill>(QStringLiteral("windmill"), QStringLiteral("Windmill"));
    m_factory.registerType<Kolf::BlackHole>(QStringLiteral("blackhole"), QStringLiteral("Black hole"));
    m_factory.registerType<Kolf::Floater>(QStringLiteral("floater"), QStringLiteral("Floater"));
    m_factory.registerType<Kolf::Bridge>(QStringLiteral("bridge"), QStringLiteral("Bridge"));
    m_factory.registerType<Kolf::Sign>(QStringLiteral("sign"), QStringLiteral("Sign"));
    m_factory.registerType<Kolf::Bumper>(QStringLiteral("bumper"), QStringLiteral("Bumper"));
    KConfig cfg(config[QStringLiteral("course")].toString(), KConfig::SimpleConfig);
    QSet<QString> types{QStringLiteral("ball"), QStringLiteral("hole"), QStringLiteral("course")};
    for (const auto &type : m_factory.knownTypes()) types.insert(type.identifier);
    const QRegularExpression group(QStringLiteral("^(\\d+)-([^@]+)@-?\\d+,-?\\d+(?:\\|\\d+)?$"));
    if (cfg.groupList().size() > 4096) { interrupt(QStringLiteral("Course object limit")); return; }
    for (const auto &name : cfg.groupList()) {
        const auto match = group.match(name);
        if (!match.hasMatch() || !types.contains(match.captured(2)) || match.captured(1).toInt() > 1000) {
            interrupt(QStringLiteral("Unsupported course group: %1").arg(name)); return;
        }
    }
    log(QStringLiteral("connect"), {{QStringLiteral("courseHash"), m_hash}, {QStringLiteral("buildId"), QStringLiteral(KOLF_RULES_BUILD_ID)},
        {QStringLiteral("processId"), double(QCoreApplication::applicationPid())}});
    connect(&m_frames, &QTimer::timeout, this, [this] {
        if (!m_adapter || !m_ready || m_role != Role::Authority || m_interrupted || m_phase == QLatin1String("Finished")) return;
        const auto s = state();
        const auto bytes = QJsonDocument(s).toJson(QJsonDocument::Compact).size();
        m_frameBytes += bytes; ++m_frameCount;
        if (m_config[QStringLiteral("logFrames")].toBool()) log(QStringLiteral("frame"), {{QStringLiteral("state"), s}, {QStringLiteral("frameSeq"), m_frameSeq + 1}});
        send(QStringLiteral("StateFrame"), {{QStringLiteral("state"), s}, {QStringLiteral("syncId"), m_syncId},
            {QStringLiteral("frameSeq"), ++m_frameSeq}, {QStringLiteral("hostMs"), double(m_clock.elapsed())}}, true);
    });
    m_frames.start(qBound(25, config[QStringLiteral("frameIntervalMs")].toInt(67), 1000));
    connect(&m_aimTimer, &QTimer::timeout, this, &SessionController::sendAim);
    m_aimTimer.start(70);
    Q_EMIT statusChanged(tr("Preparing the online match…"));
}
SessionController::~SessionController() {
    log(QStringLiteral("shutdown"), {{QStringLiteral("frames"), double(m_frameCount)},
        {QStringLiteral("frameBytes"), double(m_frameBytes)}, {QStringLiteral("coalesced"), double(m_network->coalescedFrames())}});
    delete m_game;
    log(QStringLiteral("sceneDestroyed"));
}
void SessionController::log(const QString &event, QJsonObject data) {
    data[QStringLiteral("event")] = event; data[QStringLiteral("ms")] = double(m_clock.elapsed());
    data[QStringLiteral("revision")] = m_revision;
    m_log.write(QJsonDocument(data).toJson(QJsonDocument::Compact) + '\n'); m_log.flush();
}
QJsonObject SessionController::wireMessage(const QString &type, QJsonObject payload) const {
    return onlineEnvelope(type, payload, QStringLiteral("request_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)),
        m_config.value(QStringLiteral("lobbyId")).toString(), m_config.value(QStringLiteral("matchId")).toString());
}
void SessionController::send(const QString &type, QJsonObject payload, bool visual) {
    m_network->send(wireMessage(type, payload), visual);
}
QJsonObject SessionController::unwrap(const QJsonObject &message) const {
    auto payload = message.value(QStringLiteral("payload")).toObject();
    payload[QStringLiteral("type")] = message.value(QStringLiteral("type"));
    const auto playerId = payload.value(QStringLiteral("playerId")).toString();
    if (!playerId.isEmpty()) {
        for (const auto &value : m_config.value(QStringLiteral("roster")).toArray()) {
            const auto player = value.toObject();
            if (player.value(QStringLiteral("playerId")).toString() == playerId) {
                payload[QStringLiteral("playerSlot")] = player.value(QStringLiteral("engineIndex"));
                break;
            }
        }
    }
    return payload;
}
void SessionController::load() {
    if (m_game) { interrupt(QStringLiteral("Repeated course load")); return; }
    const auto roster = m_config.value(QStringLiteral("roster")).toArray();
    const int rosterSize = roster.size();
    if (rosterSize < 2 || rosterSize > 8 || m_localPlayerIds.isEmpty()) {
        interrupt(QStringLiteral("Invalid frozen player roster")); return;
    }
    for (int i = 0; i < rosterSize; ++i) {
        const auto entry = i < roster.size() ? roster.at(i).toObject() : QJsonObject();
        Player p; p.setId(i + 1); p.setName(entry.value(QStringLiteral("displayName")).toString(i ? QStringLiteral("Guest") : QStringLiteral("Authority")));
        const QColor fallback = QColor::fromHsv((i * 47) % 360, 190, 245);
        const QColor configured = Kolf::Online::colorFromRgba(entry.value(QStringLiteral("resolvedColor")).toString());
        p.ball()->setColor(configured.isValid() ? configured : fallback); m_players.append(p);
    }
    m_game = new KolfGame(m_factory, &m_players, m_config[QStringLiteral("course")].toString(), m_gameHost, m_role);
    m_game->setUseMouse(m_useMouse);
    m_game->setUseAdvancedPutting(m_useAdvancedPutting);
    m_game->setSound(m_sound);
    m_game->setShowInfo(m_showInfo);
    m_game->setShowGuideLine(m_showGuideLine);
    m_adapter = new GameSessionAdapter(m_game);
    m_remoteAim = new QGraphicsPathItem(m_game->curBall()->parentItem());
    m_remoteAim->setZValue(1000000);
    m_remoteAim->setPen(QPen(QColor(255, 255, 255), 2, Qt::DashLine));
    m_remoteAim->setVisible(false);
    Q_EMIT gameReady(m_game);
    QTimer::singleShot(0, this, [this] {
        if (!m_game) return;
        m_game->resetTransform();
        m_game->viewport()->update();
    });
    connect(m_game, &KolfGame::shotIntentReady, this, &SessionController::submit);
    connect(m_adapter, &GameSessionAdapter::transition, this, &SessionController::commit);
    connect(m_adapter, &GameSessionAdapter::failed, this, &SessionController::interrupt);
    if (!m_adapter->prepareCourse()) { interrupt(QStringLiteral("Scene registry failed")); return; }
    send(QStringLiteral("SceneReady"), {{QStringLiteral("manifestHash"), m_adapter->manifestHash()}});
    refresh();
}
QJsonObject SessionController::state() const {
    auto result = m_adapter->capture(m_revision, m_generation, m_turn, m_phase);
    result[QStringLiteral("courseHash")] = m_hash;
    return result;
}
void SessionController::commit(const QString &phase) {
    if (m_interrupted || m_role != Role::Authority) return;
    m_ready = false; m_phase = phase; ++m_revision;
    clearRemoteAim(); m_lastAim = {};
    if (phase == QLatin1String("AwaitingShot") && m_revision > 1) ++m_turn;
    if (m_adapter->hole() != m_lastHole) { m_lastHole = m_adapter->hole(); ++m_generation; }
    m_adapter->enableInput(false);
    // Input is barred until both peers acknowledge; pending teleport callbacks
    // and moving obstacles continue on the authority during resynchronization.
    send(m_revision == 1 ? QStringLiteral("InitialState") : QStringLiteral("CommitTransition"), {{QStringLiteral("state"), state()}});
    log(QStringLiteral("commit"), {{QStringLiteral("state"), state()}});
    refresh();
}
void SessionController::submit(const ShotIntent &intent) {
    if (!m_ready || m_interrupted || !m_adapter || m_phase != QLatin1String("AwaitingShot")
        || !ownsSlot(m_adapter->activeSlot()) || !intent.valid() || !m_pending.isEmpty()) return;
    m_pending = QUuid::createUuid().toString(QUuid::WithoutBraces); m_pendingClock.start(); m_ready = false;
    m_lastAim = {}; clearRemoteAim();
    QJsonObject shot{{QStringLiteral("commandId"), m_pending},
        {QStringLiteral("holeGeneration"), m_generation}, {QStringLiteral("turnId"), m_turn},
        {QStringLiteral("puttingMode"), intent.advanced ? QStringLiteral("advanced") : QStringLiteral("normal")},
        {QStringLiteral("directionRadians"), intent.directionRadians}, {QStringLiteral("launchMagnitude"), intent.launchMagnitude}};
    shot[QStringLiteral("playerId")] = playerIdForSlot(m_adapter->activeSlot());
    m_pendingMessage = wireMessage(QStringLiteral("SubmitShot"), shot);
    m_network->send(m_pendingMessage);
    refresh();
}
void SessionController::receive(const QJsonObject &message) {
    if (m_interrupted) return;
    const auto m = unwrap(message);
    const auto type = m[QStringLiteral("type")].toString();
    if (type == QLatin1String("Welcome")) {
        QSet<QString> welcomeIds;
        for (const auto &value : m[QStringLiteral("playerIds")].toArray()) welcomeIds.insert(value.toString());
        if (welcomeIds.isEmpty() || welcomeIds != m_localPlayerIds) { interrupt(QStringLiteral("Player ownership changed during preparation")); return; }
        refresh(); return;
    }
    if (type == QLatin1String("LoadCourse")) { load(); return; }
    if (type == QLatin1String("MatchInterrupted")) { interrupt(m[QStringLiteral("reason")].toString()); return; }
    if (!m_adapter) { interrupt(QStringLiteral("State arrived before course load")); return; }
    if (type == QLatin1String("HostControlsChanged")) {
        m_hostControlsEnabled = m[QStringLiteral("enabled")].toBool();
        if (m[QStringLiteral("commandId")].toString() == m_hostTogglePending) m_hostTogglePending.clear();
        Q_EMIT noticeChanged(m_hostControlsEnabled ? tr("Host controls enabled.") : tr("Host controls disabled."));
        refresh(); return;
    }
    if (type == QLatin1String("HostControlRejected")) {
        const auto commandId = m[QStringLiteral("commandId")].toString();
        if (commandId == m_hostTogglePending) m_hostTogglePending.clear();
        if (commandId == m_hostResetPending) m_hostResetPending.clear();
        Q_EMIT noticeChanged(tr("Host control rejected: %1").arg(m[QStringLiteral("reason")].toString()));
        refresh(); return;
    }
    if (type == QLatin1String("HostActionPending")) {
        m_hostActionPending = true; m_ready = false; clearRemoteAim(); m_lastAim = {};
        refresh(); return;
    }
    if (type == QLatin1String("HostActionNotice")) {
        Q_EMIT noticeChanged(tr("Host reset Hole %1. Current-hole scores were cleared.").arg(m[QStringLiteral("hole")].toInt()));
        return;
    }
    if (type == QLatin1String("AdmitHostAction") && m_role == Role::Authority) {
        if (!m_hostActionPending || !m_hostControlsEnabled || m[QStringLiteral("action")] != QLatin1String("resetHole")
            || m[QStringLiteral("stateRevision")].toInt() != m_revision || m[QStringLiteral("syncId")].toInt() != m_syncId
            || m[QStringLiteral("holeGeneration")].toInt() != m_generation || m_phase != QLatin1String("AwaitingShot")) {
            interrupt(QStringLiteral("Invalid admitted host action")); return;
        }
        if (!m_adapter->resetCurrentHole()) { interrupt(QStringLiteral("Host reset could not be applied")); return; }
        ++m_generation;
        commit(QStringLiteral("AwaitingShot")); return;
    }
    if (type == QLatin1String("AimClear")) { clearRemoteAim(); return; }
    if (type == QLatin1String("AimPreview")) { showRemoteAim(m); return; }
    if (type == QLatin1String("StartMatch") && m_role == Role::Authority) { commit(QStringLiteral("AwaitingShot")); return; }
    if (type == QLatin1String("AdmitShot") && m_role == Role::Authority) {
        ShotCommand c;
        const int maximumPlayerSlot = m_config.value(QStringLiteral("roster")).toArray().size() - 1;
        if (!decodeShot(m, c, maximumPlayerSlot)) { interrupt(QStringLiteral("Invalid admitted shot")); return; }
        if (m_admitted.contains(c.commandId)) {
            if (m_admitted[c.commandId] != m) { interrupt(QStringLiteral("Conflicting admitted command")); return; }
            send(QStringLiteral("ShotAccepted"), {{QStringLiteral("commandId"), c.commandId}}); return;
        }
        const bool valid = m_phase == QLatin1String("AwaitingShot") && c.turnId == m_turn && c.holeGeneration == m_generation && c.playerSlot == m_adapter->activeSlot();
        if (!valid || !m_adapter->shoot(c.intent)) {
            const auto reason = valid ? m_adapter->failure() : QStringLiteral("engine state changed");
            send(QStringLiteral("ShotRejected"), {{QStringLiteral("commandId"), c.commandId}, {QStringLiteral("reason"), reason}});
            // Preparation can itself discover a hazard. Do not reopen uncertain gameplay.
            interrupt(QStringLiteral("Admitted shot could not be applied: %1").arg(reason)); return;
        }
        m_admitted.insert(c.commandId, m);
        send(QStringLiteral("ShotAccepted"), {{QStringLiteral("commandId"), c.commandId}});
        commit(QStringLiteral("Simulating")); return;
    }
    if (type == QLatin1String("ShotAccepted")) {
        if (m[QStringLiteral("commandId")].toString() == m_pending) {
            log(QStringLiteral("accepted"), {{QStringLiteral("latencyMs"), double(m_pendingClock.elapsed())}}); m_pending.clear();
        }
        return;
    }
    if (type == QLatin1String("ShotPending") || type == QLatin1String("ShotResolved")) {
        clearRemoteAim(); return;
    }
    if (type == QLatin1String("CommandRejected") && m[QStringLiteral("reason")] == QLatin1String("not awaiting shot")) {
        m_pending.clear(); m_ready = false;
        Q_EMIT noticeChanged(tr("The shot arrived after the match state changed. Waiting for synchronization."));
        refresh(); return;
    }
    if (type == QLatin1String("ShotRejected") || type == QLatin1String("CommandRejected")) {
        m_pending.clear(); interrupt(QStringLiteral("Command rejected: %1").arg(m[QStringLiteral("reason")].toString())); return;
    }
    if (type == QLatin1String("AdmitHazardAction") && m_role == Role::Authority) {
        if (m_phase != QLatin1String("AwaitingHazardChoice") || m[QStringLiteral("choiceId")].toString() != m_adapter->choiceId()
            || m[QStringLiteral("stateRevision")].toInt() != m_revision || m[QStringLiteral("syncId")].toInt() != m_syncId) return;
        if (!m_adapter->choose(m[QStringLiteral("action")].toString())) interrupt(QStringLiteral("Hazard continuation failed")); return;
    }
    if (type == QLatin1String("RequestResync") && m_role == Role::Authority) {
        const int syncId = m[QStringLiteral("syncId")].toInt();
        if (syncId <= m_syncId) return;
        m_syncId = syncId; m_ready = false; refresh();
        send(QStringLiteral("FullState"), {{QStringLiteral("state"), state()}, {QStringLiteral("syncId"), m_syncId}}); return;
    }
    if (type == QLatin1String("TransitionCommitted") || type == QLatin1String("FullState") || type == QLatin1String("StateFrame")) {
        const auto s = m[QStringLiteral("state")].toObject();
        const int revision = s[QStringLiteral("stateRevision")].toInt(), generation = s[QStringLiteral("holeGeneration")].toInt();
        const bool frame = type == QLatin1String("StateFrame");
        const int syncId = m[QStringLiteral("syncId")].toInt();
        if (syncId <= 0) { interrupt(QStringLiteral("Invalid synchronization round")); return; }
        if (syncId < m_syncId || (frame && syncId != m_syncId)) return;
        if (m_awaitingResync && type == QLatin1String("TransitionCommitted")
            && syncId == m_syncId && revision == m_revision) return;
        if (revision < m_revision || generation < m_generation) return;
        if (frame && (revision != m_revision || generation != m_generation)) {
            m_awaitingResync = true; m_ready = false; send(QStringLiteral("RequestResync")); return;
        }
        if (frame && m[QStringLiteral("frameSeq")].toInt() <= m_receivedFrame) return;
        if (s[QStringLiteral("courseHash")].toString() != m_hash) { interrupt(QStringLiteral("Snapshot course mismatch")); return; }
        if (m_role == Role::Guest && frame) {
            m_receivedFrame = m[QStringLiteral("frameSeq")].toInt();
            if (!m[QStringLiteral("hostMs")].isDouble()) { interrupt(QStringLiteral("Invalid frame timestamp")); return; }
            m_presentation.push(s, m[QStringLiteral("hostMs")].toDouble());
            if (m_config[QStringLiteral("logFrames")].toBool()) log(QStringLiteral("receivedFrame"), {{QStringLiteral("state"), s},
                {QStringLiteral("frameSeq"), m_receivedFrame}, {QStringLiteral("hostMs"), m[QStringLiteral("hostMs")]}});
            return;
        }
        m_presentation.clear();
        clearRemoteAim(); m_lastAim = {};
        if (m_role == Role::Guest) {
            QString error;
            if (!m_adapter->apply(s, error)) { interrupt(error); return; }
        }
        m_revision = revision; m_generation = generation; m_turn = s[QStringLiteral("turnId")].toInt();
        m_syncId = syncId;
        m_phase = s[QStringLiteral("phase")].toString(); m_choiceId = s[QStringLiteral("choiceId")].toString(); m_choiceSlot = s[QStringLiteral("choiceSlot")].toInt(-1);
        if (!frame) { m_hostActionPending = false; m_hostResetPending.clear(); }
        if (frame) m_receivedFrame = m[QStringLiteral("frameSeq")].toInt();
        else {
            m_receivedFrame = 0; m_ready = false; m_awaitingResync = false;
            send(QStringLiteral("StateApplied"), {{QStringLiteral("stateRevision"), m_revision},
                {QStringLiteral("syncId"), m_syncId}, {QStringLiteral("manifestHash"), m_adapter->manifestHash()}});
            const auto actual = m_role == Role::Guest ? state() : s;
            if (m_role == Role::Guest && actual != s) { interrupt(QStringLiteral("Applied snapshot differs from authoritative state")); return; }
            if (m_role == Role::Guest && m_config[QStringLiteral("verifySnapshots")].toBool()) {
                QString error;
                if (!m_adapter->apply(s, error) || state() != actual || m_game->physicsSteps() || m_game->collisionCalls() || m_game->gameplayRandomCalls()) {
                    interrupt(QStringLiteral("Snapshot idempotence/mutation invariant failed")); return;
                }
            }
            log(QStringLiteral("applied"), {{QStringLiteral("state"), actual}, {QStringLiteral("physicsSteps"), double(m_game->physicsSteps())}, {QStringLiteral("collisions"), double(m_game->collisionCalls())}, {QStringLiteral("gameplayRandomCalls"), double(m_game->gameplayRandomCalls())}});
        }
        refresh();
        if (!frame && m_config[QStringLiteral("capture")].toBool()) {
            const auto path = QDir(m_config[QStringLiteral("logDirectory")].toString()).filePath(QStringLiteral("revision-%1.png").arg(m_revision));
            QTimer::singleShot(0, this, [this, path] { if (m_game) m_game->grab().save(path); });
        }
        return;
    }
    if (type == QLatin1String("InputReady")) {
        if (m_awaitingResync || m[QStringLiteral("stateRevision")].toInt() != m_revision
            || m[QStringLiteral("syncId")].toInt() != m_syncId) return;
        m_ready = true;
        m_adapter->enableSimulation(m_phase != QLatin1String("Finished")); refresh();
        const auto faultPhase = m_config[QStringLiteral("faultPhase")].toString();
        if (!m_testFaultScheduled && faultPhase == m_phase) {
            m_testFaultScheduled = true;
            QTimer::singleShot(m_config[QStringLiteral("faultAfterMs")].toInt(300), this, [this] {
                if (m_interrupted) return;
                if (m_config[QStringLiteral("fault")].toString() == QLatin1String("disconnect")) {
                    m_network->close(); interrupt(QStringLiteral("Injected transport disconnect"));
                } else {
                    m_ready = false; m_awaitingResync = true; refresh();
                    log(QStringLiteral("requestResync"));
                    send(QStringLiteral("RequestResync"));
                }
            });
        }
        // Development-only scripted canonical intents, read locally; never accepted as setup commands over the network.
        if (m_phase == QLatin1String("AwaitingShot") && ownsSlot(m_adapter->activeSlot()) && !m_scriptedTurns.contains(m_turn)) {
            const auto shots = m_config[QStringLiteral("scriptedShots")].toArray();
            if (m_turn <= shots.size()) {
                m_scriptedTurns.insert(m_turn); const auto shot = shots[m_turn - 1].toObject();
                const int expectedTurn = m_turn;
                QTimer::singleShot(150, this, [this, shot, expectedTurn] {
                    if (m_turn == expectedTurn) submit({shot[QStringLiteral("directionRadians")].toDouble(), shot[QStringLiteral("launchMagnitude")].toDouble(), shot[QStringLiteral("advanced")].toBool()});
                });
            }
        }
        if (m_phase == QLatin1String("AwaitingHazardChoice") && ownsSlot(m_choiceSlot) && m_config.contains(QStringLiteral("scriptedHazardAction"))) {
            send(QStringLiteral("ChooseHazardAction"), {{QStringLiteral("choiceId"), m_choiceId},
                {QStringLiteral("stateRevision"), m_revision}, {QStringLiteral("syncId"), m_syncId},
                {QStringLiteral("action"), m_config[QStringLiteral("scriptedHazardAction")]}});
        }
        if (m_phase == QLatin1String("Finished") && m_config[QStringLiteral("exitWhenFinished")].toBool()) QTimer::singleShot(700, qApp, &QApplication::quit);
        return;
    }
    interrupt(QStringLiteral("Unexpected server message: %1").arg(type));
}
void SessionController::interrupt(const QString &reason) {
    if (m_interrupted) return;
    m_interrupted = true; m_ready = false; m_phase = QStringLiteral("Interrupted");
    if (m_adapter) { m_adapter->enableSimulation(false); m_adapter->enableInput(false); }
    m_frames.stop(); m_retry.stop(); m_presentation.clear();
    m_aimTimer.stop(); clearRemoteAim();
    send(QStringLiteral("MatchInterrupted"), {{QStringLiteral("reason"), reason.left(160)}});
    Q_EMIT noticeChanged(reason);
    log(QStringLiteral("interrupted"), {{QStringLiteral("reason"), reason}}); refresh();
    if (m_config[QStringLiteral("exitWhenFinished")].toBool()) QTimer::singleShot(500, qApp, [] { QCoreApplication::exit(3); });
}
void SessionController::refresh() {
    const int activeSlot = m_adapter ? m_adapter->activeSlot() : -1;
    const auto activeName = activeSlot >= 0 && activeSlot < m_players.size() ? m_players[activeSlot].name() : tr("another player");
    QString status;
    if (m_interrupted)
        status = tr("The online match was interrupted.");
    else if (!m_adapter)
        status = tr("Preparing the online match…");
    else if (!m_pending.isEmpty())
        status = tr("Sending %1's shot…").arg(activeName);
    else if (m_phase == QLatin1String("AwaitingHazardChoice"))
        status = ownsSlot(m_choiceSlot) ? tr("Choose how %1 should continue.").arg(activeName)
                                        : tr("Waiting for %1 to choose how to continue…").arg(activeName);
    else if (m_phase == QLatin1String("AwaitingShot"))
        status = ownsSlot(activeSlot) && m_ready ? tr("%1: your turn.").arg(activeName)
                                                : tr("Waiting for %1…").arg(activeName);
    else if (m_phase == QLatin1String("Simulating"))
        status = tr("%1's shot is in play.").arg(activeName);
    else if (m_phase == QLatin1String("Finished"))
        status = tr("Match complete.");
    else
        status = tr("Synchronizing the online match…");
    Q_EMIT statusChanged(status);
    if (!m_adapter) return;
    const bool canAim = m_ready && !m_interrupted && m_phase == QLatin1String("AwaitingShot")
        && ownsSlot(m_adapter->activeSlot()) && m_pending.isEmpty();
    // Do not cancel an ongoing local power stroke on every visual frame.
    if (!canAim || m_game->inputIgnored()) m_adapter->enableInput(canAim);
    const bool canChoose = m_ready && !m_interrupted && m_phase == QLatin1String("AwaitingHazardChoice") && ownsSlot(m_choiceSlot);
    Q_EMIT hazardChoiceChanged(canChoose);
    const bool canHostToggle = m_role == Role::Authority && m_ready && !m_interrupted
        && m_phase == QLatin1String("AwaitingShot") && m_pending.isEmpty() && !m_hostActionPending
        && m_hostTogglePending.isEmpty();
    Q_EMIT hostControlsChanged(m_hostControlsEnabled, canHostToggle,
        canHostToggle && m_hostControlsEnabled && m_hostResetPending.isEmpty());
    while (m_pars.size() < m_adapter->hole()) m_pars.append(0);
    m_pars[m_adapter->hole() - 1] = m_adapter->par();
    QJsonArray scores;
    for (const auto &player : std::as_const(m_players)) {
        QJsonArray row;
        for (int hole = 1; hole <= m_adapter->hole(); ++hole) row.append(player.score(hole));
        scores.append(row);
    }
    Q_EMIT scorecardChanged(scores, m_pars, activeSlot, m_adapter->hole());
    if (m_phase == QLatin1String("Finished"))
        Q_EMIT noticeChanged(tr("Match complete. Results are ready."));
}

void SessionController::requestResync()
{
    if (m_interrupted) return;
    clearRemoteAim(); m_lastAim = {};
    m_ready = false;
    m_awaitingResync = true;
    refresh();
    send(QStringLiteral("RequestResync"));
}

void SessionController::chooseHazardAction(const QString &action)
{
    if (!m_adapter || !m_ready || m_interrupted || m_phase != QLatin1String("AwaitingHazardChoice")
        || !ownsSlot(m_choiceSlot)) return;
    Q_EMIT hazardChoiceChanged(false);
    send(QStringLiteral("ChooseHazardAction"), {{QStringLiteral("choiceId"), m_choiceId},
        {QStringLiteral("stateRevision"), m_revision}, {QStringLiteral("syncId"), m_syncId},
        {QStringLiteral("action"), action}});
}

void SessionController::chooseDrop() { chooseHazardAction(QStringLiteral("drop")); }
void SessionController::chooseRehit() { chooseHazardAction(QStringLiteral("rehit")); }

void SessionController::setHostControlsEnabled(bool enabled)
{
    if (m_role != Role::Authority || !m_adapter || !m_ready || m_interrupted
        || m_phase != QLatin1String("AwaitingShot") || !m_pending.isEmpty()
        || m_hostActionPending || !m_hostTogglePending.isEmpty() || enabled == m_hostControlsEnabled) return;
    m_hostTogglePending = QUuid::createUuid().toString(QUuid::WithoutBraces);
    send(QStringLiteral("SetHostControls"), {{QStringLiteral("commandId"), m_hostTogglePending},
        {QStringLiteral("stateRevision"), m_revision}, {QStringLiteral("syncId"), m_syncId},
        {QStringLiteral("enabled"), enabled}});
    refresh();
}

void SessionController::resetOnlineHole()
{
    if (m_role != Role::Authority || !m_adapter || !m_ready || m_interrupted || !m_hostControlsEnabled
        || m_phase != QLatin1String("AwaitingShot") || !m_pending.isEmpty()
        || m_hostActionPending || !m_hostResetPending.isEmpty()) return;
    m_hostResetPending = QUuid::createUuid().toString(QUuid::WithoutBraces);
    send(QStringLiteral("HostAction"), {{QStringLiteral("commandId"), m_hostResetPending},
        {QStringLiteral("stateRevision"), m_revision}, {QStringLiteral("syncId"), m_syncId},
        {QStringLiteral("holeGeneration"), m_generation}, {QStringLiteral("action"), QStringLiteral("resetHole")}});
    refresh();
}

void SessionController::setUseMouse(bool enabled)
{
    m_useMouse = enabled;
    if (m_game) m_game->setUseMouse(enabled);
}

void SessionController::setUseAdvancedPutting(bool enabled)
{
    m_useAdvancedPutting = enabled;
    if (m_game) m_game->setUseAdvancedPutting(enabled);
}

void SessionController::setSound(bool enabled)
{
    m_sound = enabled;
    if (m_game) m_game->setSound(enabled);
}

void SessionController::setShowInfo(bool enabled)
{
    m_showInfo = enabled;
    if (m_game) m_game->setShowInfo(enabled);
}

void SessionController::setShowGuideLine(bool enabled)
{
    m_showGuideLine = enabled;
    if (m_game) m_game->setShowGuideLine(enabled);
}

void SessionController::sendAim()
{
    if (!m_game || !m_adapter || !m_ready || m_interrupted || !m_pending.isEmpty()
        || m_phase != QLatin1String("AwaitingShot") || !ownsSlot(m_adapter->activeSlot())) return;
    const auto aim = m_game->currentAim();
    if (!std::isfinite(aim.directionRadians) || !std::isfinite(aim.launchMagnitude)) return;
    QJsonObject update{{QStringLiteral("playerId"), playerIdForSlot(m_adapter->activeSlot())},
        {QStringLiteral("stateRevision"), m_revision}, {QStringLiteral("syncId"), m_syncId},
        {QStringLiteral("holeGeneration"), m_generation}, {QStringLiteral("turnId"), m_turn},
        {QStringLiteral("directionRadians"), std::round(aim.directionRadians * 100.0) / 100.0},
        {QStringLiteral("strength"), std::round(aim.launchMagnitude * 50.0) / 50.0}};
    if (update == m_lastAim) return;
    m_lastAim = update;
    send(QStringLiteral("AimUpdate"), update, true);
}

void SessionController::showRemoteAim(const QJsonObject &aim)
{
    if (!m_remoteAim || !m_adapter || !m_ready || m_interrupted || m_phase != QLatin1String("AwaitingShot")
        || aim[QStringLiteral("stateRevision")].toInt() != m_revision
        || aim[QStringLiteral("syncId")].toInt() != m_syncId
        || aim[QStringLiteral("holeGeneration")].toInt() != m_generation
        || aim[QStringLiteral("turnId")].toInt() != m_turn
        || aim[QStringLiteral("playerId")].toString() != playerIdForSlot(m_adapter->activeSlot())
        || ownsSlot(m_adapter->activeSlot())) return;
    const double direction = aim[QStringLiteral("directionRadians")].toDouble();
    const double strength = aim[QStringLiteral("strength")].toDouble();
    if (!std::isfinite(direction) || !std::isfinite(strength) || std::abs(direction) > M_PI
        || strength < 0 || strength > 1) return;
    const double length = 32 + 56 * strength;
    const QPointF end(length * std::cos(direction), length * std::sin(direction));
    const QPointF side(7 * std::cos(direction + 2.5), 7 * std::sin(direction + 2.5));
    const QPointF other(7 * std::cos(direction - 2.5), 7 * std::sin(direction - 2.5));
    QPainterPath path;
    path.moveTo(0, 0); path.lineTo(end);
    path.moveTo(end + side); path.lineTo(end); path.lineTo(end + other);
    m_remoteAim->setPath(path);
    m_remoteAim->setPos(m_players[m_adapter->activeSlot()].ball()->pos());
    m_remoteAim->setVisible(true);
    if (m_config[QStringLiteral("logFrames")].toBool()) log(QStringLiteral("aimPreview"),
        {{QStringLiteral("directionRadians"), direction}, {QStringLiteral("strength"), strength}});
}

void SessionController::clearRemoteAim()
{
    if (m_remoteAim && m_remoteAim->isVisible()) {
        m_remoteAim->setVisible(false);
        if (m_config[QStringLiteral("logFrames")].toBool()) log(QStringLiteral("aimClear"));
    }
}

bool SessionController::ownsSlot(int slot) const
{
    const auto playerId = playerIdForSlot(slot);
    return !playerId.isEmpty() && m_localPlayerIds.contains(playerId);
}

QString SessionController::playerIdForSlot(int slot) const
{
    const auto roster = m_config.value(QStringLiteral("roster")).toArray();
    if (slot < 0 || slot >= roster.size()) return {};
    return roster.at(slot).toObject().value(QStringLiteral("playerId")).toString();
}

#include "moc_sessioncontroller.cpp"
