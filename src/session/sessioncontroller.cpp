// SPDX-License-Identifier: GPL-2.0-or-later
#include "sessioncontroller.h"
#include "landscape.h"
#include "objects.h"
#include "obstacles.h"
#include "prototype_build.h"
#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QUuid>
#include <QVBoxLayout>
#include <KConfig>
#include <QRegularExpression>

using namespace Kolf::Session;

SessionController::SessionController(const QJsonObject &config)
    : m_config(config), m_role(config[QStringLiteral("role")] == QLatin1String("authority") ? Role::Authority : Role::Guest) {
    setWindowTitle(QStringLiteral("Kolf prototype — %1").arg(config[QStringLiteral("role")].toString()));
    m_layout = new QVBoxLayout(this);
    m_status = new QLabel(QStringLiteral("Connecting")); m_status->setWordWrap(true); m_layout->addWidget(m_status);
    m_notice = new QLabel; m_notice->setWordWrap(true); m_layout->addWidget(m_notice);
    auto *controls = new QHBoxLayout;
    auto *resync = new QPushButton(QStringLiteral("Resync")); controls->addWidget(resync);
    auto *advanced = new QCheckBox(QStringLiteral("Advanced putting")); controls->addWidget(advanced);
    auto *mouse = new QCheckBox(QStringLiteral("Mouse aiming")); mouse->setChecked(true); controls->addWidget(mouse);
    m_drop = new QPushButton(QStringLiteral("Drop outside hazard")); controls->addWidget(m_drop);
    m_rehit = new QPushButton(QStringLiteral("Rehit")); controls->addWidget(m_rehit);
    m_drop->setEnabled(false); m_rehit->setEnabled(false); m_layout->addLayout(controls);
    m_scores = new QTableWidget(2, 0); m_scores->setMaximumHeight(120); m_scores->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_scores->setVerticalHeaderLabels({QStringLiteral("Authority"), QStringLiteral("Guest")}); m_layout->addWidget(m_scores);
    connect(advanced, &QCheckBox::toggled, this, [this](bool on) { if (m_game) m_game->setUseAdvancedPutting(on); });
    connect(mouse, &QCheckBox::toggled, this, [this](bool on) { if (m_game) m_game->setUseMouse(on); });
    connect(resync, &QPushButton::clicked, this, [this] { if (!m_interrupted) {
        m_ready = false; m_awaitingResync = true; refresh(); m_network.send(envelope(QStringLiteral("RequestResync")));
    } });
    const auto choose = [this](const QString &action) {
        m_drop->setEnabled(false); m_rehit->setEnabled(false);
        m_network.send(envelope(QStringLiteral("ChooseHazardAction"), {{QStringLiteral("choiceId"), m_choiceId},
            {QStringLiteral("stateRevision"), m_revision}, {QStringLiteral("syncId"), m_syncId}, {QStringLiteral("action"), action}}));
    };
    connect(m_drop, &QPushButton::clicked, this, [choose] { choose(QStringLiteral("drop")); });
    connect(m_rehit, &QPushButton::clicked, this, [choose] { choose(QStringLiteral("rehit")); });
    connect(&m_network, &Net::NetworkClient::received, this, &SessionController::receive);
    connect(&m_network, &Net::NetworkClient::failed, this, &SessionController::interrupt);
    m_presentation.setDelay(config[QStringLiteral("presentationDelayMs")].toInt(100));
    connect(&m_presentation, &Replication::PresentationController::present, this, [this](const QJsonObject &s) {
        if (!m_adapter || m_interrupted) return;
        QString error;
        if (!m_adapter->apply(s, error)) interrupt(error);
    });
    m_clock.start();
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (m_pending.isEmpty() || m_interrupted) return;
        if (m_pendingClock.elapsed() > 15000) { interrupt(QStringLiteral("Shot acceptance timed out")); return; }
        m_network.send(m_pendingMessage); // Same ID and content: never a speculative new shot.
    });
    m_retry.start(1500);
    const auto dir = config[QStringLiteral("logDirectory")].toString();
    QDir().mkpath(dir); m_log.setFileName(QDir(dir).filePath(QStringLiteral("session.jsonl"))); m_log.open(QIODevice::WriteOnly | QIODevice::Truncate);
    QFile course(config[QStringLiteral("course")].toString());
    if (!course.open(QIODevice::ReadOnly) || course.size() > 4 * 1024 * 1024) { interrupt(QStringLiteral("Cannot read bounded course file")); return; }
    m_hash = QString::fromLatin1(QCryptographicHash::hash(course.readAll(), QCryptographicHash::Sha256).toHex());
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
    const QUrl url(config[QStringLiteral("endpoint")].toString());
    if ((url.scheme() != QLatin1String("ws") && url.scheme() != QLatin1String("wss")) || url.host().isEmpty()) { interrupt(QStringLiteral("Invalid WebSocket endpoint")); return; }
    log(QStringLiteral("connect"), {{QStringLiteral("courseHash"), m_hash}, {QStringLiteral("buildId"), QStringLiteral(KOLF_PROTOTYPE_BUILD)},
        {QStringLiteral("processId"), double(QCoreApplication::applicationPid())}});
    m_network.open(url, envelope(QStringLiteral("Hello"), {{QStringLiteral("role"), config[QStringLiteral("role")]},
        {QStringLiteral("credential"), config[QStringLiteral("credential")]}, {QStringLiteral("courseHash"), m_hash},
        {QStringLiteral("buildId"), QStringLiteral(KOLF_PROTOTYPE_BUILD)}}));
    connect(&m_frames, &QTimer::timeout, this, [this] {
        if (!m_adapter || !m_ready || m_role != Role::Authority || m_interrupted || m_phase == QLatin1String("Finished")) return;
        const auto s = state();
        const auto bytes = QJsonDocument(s).toJson(QJsonDocument::Compact).size();
        m_frameBytes += bytes; ++m_frameCount;
        if (m_config[QStringLiteral("logFrames")].toBool()) log(QStringLiteral("frame"), {{QStringLiteral("state"), s}, {QStringLiteral("frameSeq"), m_frameSeq + 1}});
        m_network.send(envelope(QStringLiteral("StateFrame"), {{QStringLiteral("state"), s}, {QStringLiteral("syncId"), m_syncId},
            {QStringLiteral("frameSeq"), ++m_frameSeq}, {QStringLiteral("hostMs"), double(m_clock.elapsed())}}), true);
    });
    m_frames.start(qBound(25, config[QStringLiteral("frameIntervalMs")].toInt(67), 1000));
    resize(850, 700);
}
SessionController::~SessionController() {
    log(QStringLiteral("shutdown"), {{QStringLiteral("frames"), double(m_frameCount)},
        {QStringLiteral("frameBytes"), double(m_frameBytes)}, {QStringLiteral("coalesced"), double(m_network.coalescedFrames())}});
    m_network.close();
    delete m_game;
    log(QStringLiteral("sceneDestroyed"));
}
void SessionController::log(const QString &event, QJsonObject data) {
    data[QStringLiteral("event")] = event; data[QStringLiteral("ms")] = double(m_clock.elapsed());
    data[QStringLiteral("revision")] = m_revision;
    m_log.write(QJsonDocument(data).toJson(QJsonDocument::Compact) + '\n'); m_log.flush();
}
void SessionController::load() {
    if (m_game) { interrupt(QStringLiteral("Repeated course load")); return; }
    for (int i = 0; i < 2; ++i) {
        Player p; p.setId(i + 1); p.setName(i ? QStringLiteral("Guest") : QStringLiteral("Authority"));
        p.ball()->setColor(i ? QColor(Qt::cyan) : QColor(Qt::yellow)); m_players.append(p);
    }
    m_game = new KolfGame(m_factory, &m_players, m_config[QStringLiteral("course")].toString(), this, m_role);
    m_game->setSound(false);
    m_adapter = new GameSessionAdapter(m_game); m_layout->insertWidget(1, m_game, 1);
    connect(m_game, &KolfGame::shotIntentReady, this, &SessionController::submit);
    connect(m_adapter, &GameSessionAdapter::transition, this, &SessionController::commit);
    connect(m_adapter, &GameSessionAdapter::failed, this, &SessionController::interrupt);
    if (!m_adapter->prepareCourse()) { interrupt(QStringLiteral("Scene registry failed")); return; }
    m_network.send(envelope(QStringLiteral("CourseReady"), {{QStringLiteral("manifestHash"), m_adapter->manifestHash()}}));
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
    if (phase == QLatin1String("AwaitingShot") && m_revision > 1) ++m_turn;
    if (m_adapter->hole() != m_lastHole) { m_lastHole = m_adapter->hole(); ++m_generation; }
    m_adapter->enableInput(false);
    // Input is barred until both peers acknowledge; pending teleport callbacks
    // and moving obstacles continue on the authority during resynchronization.
    m_network.send(envelope(m_revision == 1 ? QStringLiteral("InitialState") : QStringLiteral("CommitTransition"), {{QStringLiteral("state"), state()}}));
    log(QStringLiteral("commit"), {{QStringLiteral("state"), state()}});
    refresh();
}
void SessionController::submit(const ShotIntent &intent) {
    if (!m_ready || m_interrupted || !m_adapter || m_phase != QLatin1String("AwaitingShot") || m_adapter->activeSlot() != m_slot || !intent.valid() || !m_pending.isEmpty()) return;
    m_pending = QUuid::createUuid().toString(QUuid::WithoutBraces); m_pendingClock.start(); m_ready = false;
    m_pendingMessage = envelope(QStringLiteral("SubmitShot"), {{QStringLiteral("commandId"), m_pending},
        {QStringLiteral("holeGeneration"), m_generation}, {QStringLiteral("turnId"), m_turn}, {QStringLiteral("playerSlot"), m_slot},
        {QStringLiteral("puttingMode"), intent.advanced ? QStringLiteral("advanced") : QStringLiteral("normal")},
        {QStringLiteral("directionRadians"), intent.directionRadians}, {QStringLiteral("launchMagnitude"), intent.launchMagnitude}});
    m_network.send(m_pendingMessage);
    refresh();
}
void SessionController::receive(const QJsonObject &m) {
    if (m_interrupted) return;
    const auto type = m[QStringLiteral("type")].toString();
    if (type == QLatin1String("Welcome")) { m_slot = m[QStringLiteral("playerSlot")].toInt(-1); refresh(); return; }
    if (type == QLatin1String("LoadCourse")) { load(); return; }
    if (type == QLatin1String("MatchInterrupted")) { interrupt(m[QStringLiteral("reason")].toString()); return; }
    if (!m_adapter) { interrupt(QStringLiteral("State arrived before course load")); return; }
    if (type == QLatin1String("StartMatch") && m_role == Role::Authority) { commit(QStringLiteral("AwaitingShot")); return; }
    if (type == QLatin1String("AdmitShot") && m_role == Role::Authority) {
        ShotCommand c;
        if (!decodeShot(m, c)) { interrupt(QStringLiteral("Invalid admitted shot")); return; }
        if (m_admitted.contains(c.commandId)) {
            if (m_admitted[c.commandId] != m) { interrupt(QStringLiteral("Conflicting admitted command")); return; }
            m_network.send(envelope(QStringLiteral("ShotAccepted"), {{QStringLiteral("commandId"), c.commandId}})); return;
        }
        const bool valid = m_phase == QLatin1String("AwaitingShot") && c.turnId == m_turn && c.holeGeneration == m_generation && c.playerSlot == m_adapter->activeSlot();
        if (!valid || !m_adapter->shoot(c.intent)) {
            m_network.send(envelope(QStringLiteral("ShotRejected"), {{QStringLiteral("commandId"), c.commandId}, {QStringLiteral("reason"), QStringLiteral("engine state changed")}}));
            // Preparation can itself discover a hazard. Do not reopen uncertain gameplay.
            interrupt(QStringLiteral("Admitted shot could not be applied")); return;
        }
        m_admitted.insert(c.commandId, m);
        m_network.send(envelope(QStringLiteral("ShotAccepted"), {{QStringLiteral("commandId"), c.commandId}}));
        commit(QStringLiteral("Simulating")); return;
    }
    if (type == QLatin1String("ShotAccepted")) {
        if (m[QStringLiteral("commandId")].toString() == m_pending) {
            log(QStringLiteral("accepted"), {{QStringLiteral("latencyMs"), double(m_pendingClock.elapsed())}}); m_pending.clear();
        }
        return;
    }
    if (type == QLatin1String("ShotPending") || type == QLatin1String("ShotResolved")) return;
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
        m_network.send(envelope(QStringLiteral("FullState"), {{QStringLiteral("state"), state()}, {QStringLiteral("syncId"), m_syncId}})); return;
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
            m_awaitingResync = true; m_ready = false; m_network.send(envelope(QStringLiteral("RequestResync"))); return;
        }
        if (frame && m[QStringLiteral("frameSeq")].toInt() <= m_receivedFrame) return;
        if (s[QStringLiteral("courseHash")].toString() != m_hash) { interrupt(QStringLiteral("Snapshot course mismatch")); return; }
        if (m_role == Role::Guest && frame) {
            m_receivedFrame = m[QStringLiteral("frameSeq")].toInt();
            if (!m[QStringLiteral("hostMs")].isDouble()) { interrupt(QStringLiteral("Invalid frame timestamp")); return; }
            m_presentation.push(s, m[QStringLiteral("hostMs")].toDouble());
            if (m_config[QStringLiteral("logFrames")].toBool()) log(QStringLiteral("receivedFrame"), {{QStringLiteral("state"), s}, {QStringLiteral("frameSeq"), m_receivedFrame}});
            return;
        }
        m_presentation.clear();
        if (m_role == Role::Guest) {
            QString error;
            if (!m_adapter->apply(s, error)) { interrupt(error); return; }
        }
        m_revision = revision; m_generation = generation; m_turn = s[QStringLiteral("turnId")].toInt();
        m_syncId = syncId;
        m_phase = s[QStringLiteral("phase")].toString(); m_choiceId = s[QStringLiteral("choiceId")].toString(); m_choiceSlot = s[QStringLiteral("choiceSlot")].toInt(-1);
        if (frame) m_receivedFrame = m[QStringLiteral("frameSeq")].toInt();
        else {
            m_receivedFrame = 0; m_ready = false; m_awaitingResync = false;
            m_network.send(envelope(QStringLiteral("StateApplied"), {{QStringLiteral("stateRevision"), m_revision},
                {QStringLiteral("syncId"), m_syncId}, {QStringLiteral("manifestHash"), m_adapter->manifestHash()}}));
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
            QTimer::singleShot(0, this, [this, path] { grab().save(path); });
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
                    m_network.close(); interrupt(QStringLiteral("Injected transport disconnect"));
                } else {
                    m_ready = false; m_awaitingResync = true; refresh();
                    log(QStringLiteral("requestResync"));
                    m_network.send(envelope(QStringLiteral("RequestResync")));
                }
            });
        }
        // Development-only scripted canonical intents, read locally; never accepted as setup commands over the network.
        if (m_phase == QLatin1String("AwaitingShot") && m_adapter->activeSlot() == m_slot && !m_scriptedTurns.contains(m_turn)) {
            const auto shots = m_config[QStringLiteral("scriptedShots")].toArray();
            if (m_turn <= shots.size()) {
                m_scriptedTurns.insert(m_turn); const auto shot = shots[m_turn - 1].toObject();
                const int expectedTurn = m_turn;
                QTimer::singleShot(150, this, [this, shot, expectedTurn] {
                    if (m_turn == expectedTurn) submit({shot[QStringLiteral("directionRadians")].toDouble(), shot[QStringLiteral("launchMagnitude")].toDouble(), shot[QStringLiteral("advanced")].toBool()});
                });
            }
        }
        if (m_phase == QLatin1String("AwaitingHazardChoice") && m_choiceSlot == m_slot && m_config.contains(QStringLiteral("scriptedHazardAction"))) {
            m_network.send(envelope(QStringLiteral("ChooseHazardAction"), {{QStringLiteral("choiceId"), m_choiceId},
                {QStringLiteral("stateRevision"), m_revision}, {QStringLiteral("syncId"), m_syncId},
                {QStringLiteral("action"), m_config[QStringLiteral("scriptedHazardAction")]}}));
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
    m_frames.stop(); m_presentation.clear(); m_network.send(envelope(QStringLiteral("MatchInterrupted"))); m_network.close();
    m_notice->setText(reason); log(QStringLiteral("interrupted"), {{QStringLiteral("reason"), reason}}); refresh();
    if (m_config[QStringLiteral("exitWhenFinished")].toBool()) QTimer::singleShot(500, qApp, [] { QCoreApplication::exit(3); });
}
void SessionController::refresh() {
    m_status->setText(QStringLiteral("%1 | slot %2 | %3 | turn %4 | generation %5 | revision %6 | frame %7 | %8")
        .arg(m_role == Role::Authority ? QStringLiteral("Authority") : QStringLiteral("Guest")).arg(m_slot)
        .arg(m_phase).arg(m_turn).arg(m_generation).arg(m_revision).arg(m_receivedFrame)
        .arg(!m_pending.isEmpty() ? QStringLiteral("Pending") : m_ready ? QStringLiteral("Ready") : QStringLiteral("Waiting")));
    if (!m_adapter) return;
    const bool canAim = m_ready && !m_interrupted && m_phase == QLatin1String("AwaitingShot") && m_adapter->activeSlot() == m_slot && m_pending.isEmpty();
    // Do not cancel an ongoing local power stroke on every visual frame.
    if (!canAim || m_game->inputIgnored()) m_adapter->enableInput(canAim);
    const bool canChoose = m_ready && !m_interrupted && m_phase == QLatin1String("AwaitingHazardChoice") && m_choiceSlot == m_slot;
    m_drop->setEnabled(canChoose); m_rehit->setEnabled(canChoose);
    m_scores->setColumnCount(m_adapter->hole());
    for (int p = 0; p < 2; ++p) for (int h = 1; h <= m_adapter->hole(); ++h)
        m_scores->setItem(p, h - 1, new QTableWidgetItem(QString::number(m_players[p].score(h))));
    if (m_phase == QLatin1String("Finished")) m_notice->setText(QStringLiteral("Match complete. The final scorecard remains here until you close the window."));
}

#include "moc_sessioncontroller.cpp"
