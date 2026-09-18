// SPDX-License-Identifier: GPL-2.0-or-later
#include "gamesessionadapter.h"
#include "replication/snapshot.h"
#include "tagaro/board.h"
#include "obstacles.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTimer>
#include <QUuid>

using namespace Kolf::Session;
using namespace Kolf::Replication;

GameSessionAdapter::GameSessionAdapter(KolfGame *game) : QObject(game), g(game) {
    connect(g, &KolfGame::onlineSettlementRequested, this, &GameSessionAdapter::settle);
    enableInput(false);
}
int GameSessionAdapter::activeSlot() const { return int(g->curPlayer - g->players->begin()); }
int GameSessionAdapter::hole() const { return g->curHole; }
bool GameSessionAdapter::prepareCourse() {
    if (!loadHole(1)) return false;
    const int count = g->highestHole;
    if (count < 1 || count > 1000) return false;
    for (int h = 1; h <= count; ++h) {
        if (!loadHole(h)) return false;
        const auto map = registry();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            const auto kind = visualKind(it.value());
            if (kind == QLatin1String("unsupported")) return false;
            m_manifestKinds[h].insert(it.key(), kind);
        }
        m_manifestHashes[h] = manifestHash();
    }
    for (auto &p : *g->players) p.setScores({});
    return loadHole(1);
}
void GameSessionAdapter::enableSimulation(bool enabled) {
    g->m_simulationEnabled = enabled && g->m_role == Role::Authority;
    if (g->maySimulate()) { g->timer->start(g->timerMsec); g->fastTimer->start(g->fastTimerMsec); }
    else { g->timer->stop(); g->fastTimer->stop(); }
}
void GameSessionAdapter::enableInput(bool enabled) {
    g->m_ignoreEvents = !enabled;
    if (!enabled) {
        g->putting = g->stroking = false; g->putterTimer->stop(); g->strokeCircle->setVisible(false);
    }
    g->putter->setVisible(enabled);
    if (enabled) g->putter->setOrigin(g->curBall()->x(), g->curBall()->y());
}
bool GameSessionAdapter::loadHole(int number) {
    enableSimulation(false);
    g->curHole = number;
    g->curPlayer = g->players->begin();
    g->paused = true;
    g->recalcHighestHole = true;
    g->whiteBall->setPos(200, 200);
    for (auto &p : *g->players) {
        p.ball()->setPos(200, 200);
        p.ball()->setState(Stopped); p.ball()->setVelocity(Vector());
        p.ball()->setBeginningOfHole(true); p.ball()->setPlaceOnGround(false);
        p.ball()->setForceStillGoing(false); p.ball()->setAddStroke(0);
        p.ball()->setVisible(false);
        while (p.numHoles() < unsigned(number)) p.addHole();
    }
    g->openFile();
    g->paused = false;
    g->curBall()->setVisible(true);
    g->inPlay = false;
    g->putter->resetAngles();
    g->putter->setOrigin(g->curBall()->x(), g->curBall()->y());
    return !registry().isEmpty();
}
QMap<QString, QGraphicsItem *> GameSessionAdapter::registry() const {
    QMap<QString, QGraphicsItem *> map;
    for (auto *item : g->m_topLevelQItems) {
        if (dynamic_cast<Ball *>(item)) continue;
        const auto key = item->data(1).toString();
        if (key.isEmpty() || map.contains(key)) return {};
        map.insert(key, item);
        if (auto *c = dynamic_cast<CanvasItem *>(item)) {
            auto children = c->presentationChildren();
            for (auto it = children.cbegin(); it != children.cend(); ++it) map.insert(key + QLatin1Char('/') + it.key(), it.value());
        }
    }
    for (int i = 0; i < g->borderWalls.size(); ++i) map.insert(QStringLiteral("border/%1").arg(i), g->borderWalls[i]);
    return map;
}
QString GameSessionAdapter::manifestHash() const {
    QByteArray manifest;
    const auto map = registry();
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        manifest += it.key().toUtf8() + '\0' + visualKind(it.value()).toUtf8() + '\n';
    }
    return QString::fromLatin1(QCryptographicHash::hash(manifest, QCryptographicHash::Sha256).toHex());
}
QJsonObject GameSessionAdapter::capture(int revision, int generation, int turn, const QString &phase) const {
    QJsonArray objects, balls, scores;
    const auto map = registry();
    for (auto it = map.cbegin(); it != map.cend(); ++it) objects.append(captureVisual(it.key(), it.value()));
    for (int i = 0; i < g->players->size(); ++i) {
        const auto &p = (*g->players)[i];
        auto v = captureVisual(QStringLiteral("ball/%1").arg(i), p.ball());
        v[QStringLiteral("state")] = int(p.ball()->curState()); balls.append(v);
        QJsonArray row; for (int score : p.scores()) row.append(score); scores.append(row);
    }
    return {{QStringLiteral("stateRevision"), revision}, {QStringLiteral("holeGeneration"), generation},
        {QStringLiteral("turnId"), turn}, {QStringLiteral("phase"), phase}, {QStringLiteral("hole"), hole()},
        {QStringLiteral("par"), g->curPar}, {QStringLiteral("activeSlot"), activeSlot()},
        {QStringLiteral("manifestHash"), manifestHash()}, {QStringLiteral("objects"), objects},
        {QStringLiteral("balls"), balls}, {QStringLiteral("scores"), scores},
        {QStringLiteral("choiceId"), m_choiceId}, {QStringLiteral("choiceSlot"), m_choiceSlot}};
}
bool GameSessionAdapter::apply(const QJsonObject &s, QString &error) {
    if (g->m_role != Role::Guest) { error = QStringLiteral("snapshot application on authority"); return false; }
    const auto reject = [&error] { error = QStringLiteral("invalid snapshot or scene manifest"); return false; };
    for (const auto &key : {"stateRevision", "holeGeneration", "turnId", "hole"}) if (!counter(s[QLatin1String(key)], 1)) return reject();
    if (!counter(s[QStringLiteral("hole")], 1, 1000) || !counter(s[QStringLiteral("activeSlot")], 0, 1)
        || !counter(s[QStringLiteral("par")], 0, 1000)) return reject();
    if (!QStringList{QStringLiteral("AwaitingShot"), QStringLiteral("Simulating"), QStringLiteral("AwaitingHazardChoice"), QStringLiteral("Finished")}.contains(s[QStringLiteral("phase")].toString())) return reject();
    const auto objects = s[QStringLiteral("objects")].toArray(), balls = s[QStringLiteral("balls")].toArray(), scores = s[QStringLiteral("scores")].toArray();
    if (balls.size() != 2 || scores.size() != 2 || objects.size() > 4096) return reject();
    QSet<QString> ids;
    for (const auto v : objects) {
        const auto obj = v.toObject(); const auto id = obj[QStringLiteral("id")].toString();
        if (!validateVisual(obj) || ids.contains(id)) return reject(); ids.insert(id);
    }
    for (int i = 0; i < 2; ++i) {
        const auto b = balls[i].toObject();
        if (!validateVisual(b) || b[QStringLiteral("id")] != QStringLiteral("ball/%1").arg(i)
            || !counter(b[QStringLiteral("state")], 0, 2) || scores[i].toArray().size() != s[QStringLiteral("hole")].toInt()) return reject();
        for (const auto score : scores[i].toArray()) if (!counter(score, 0, 10000)) return reject();
    }
    const int requestedHole = s[QStringLiteral("hole")].toInt();
    const auto expected = m_manifestKinds.value(requestedHole);
    if (expected.isEmpty() || objects.size() != expected.size()
        || s[QStringLiteral("manifestHash")].toString() != m_manifestHashes.value(requestedHole)) return reject();
    for (const auto value : objects) {
        const auto object = value.toObject();
        if (expected.value(object[QStringLiteral("id")].toString()) != object[QStringLiteral("kind")].toString()) return reject();
    }
    if (s[QStringLiteral("phase")] == QLatin1String("AwaitingHazardChoice")
        && (s[QStringLiteral("choiceId")].toString().isEmpty() || !counter(s[QStringLiteral("choiceSlot")], 0, 1))) return reject();
    const bool changedHole = hole() != s[QStringLiteral("hole")].toInt();
    g->setUpdatesEnabled(false);
    if (changedHole && !loadHole(s[QStringLiteral("hole")].toInt())) { g->setUpdatesEnabled(true); return reject(); }
    const auto map = registry();
    if (objects.size() != map.size() || s[QStringLiteral("manifestHash")].toString() != manifestHash()) { g->setUpdatesEnabled(true); return reject(); }
    for (const auto v : objects) {
        const auto obj = v.toObject(); const auto item = map.value(obj[QStringLiteral("id")].toString(), nullptr);
        if (!item || obj[QStringLiteral("kind")].toString() != visualKind(item)) { g->setUpdatesEnabled(true); return reject(); }
    }
    const auto beforeSteps = g->physicsSteps(), beforeCollisions = g->collisionCalls();
    for (const auto v : objects) { const auto obj = v.toObject(); applyVisual(obj, map.value(obj[QStringLiteral("id")].toString())); }
    for (int i = 0; i < 2; ++i) {
        auto &p = (*g->players)[i];
        applyVisual(balls[i].toObject(), p.ball());
        p.ball()->state = BallState(balls[i].toObject()[QStringLiteral("state")].toInt());
        QList<int> row; for (const auto score : scores[i].toArray()) row.append(score.toInt()); p.setScores(row);
    }
    g->curPlayer = g->players->begin() + s[QStringLiteral("activeSlot")].toInt();
    g->curPar = s[QStringLiteral("par")].toInt();
    m_choiceId = s[QStringLiteral("choiceId")].toString();
    m_choiceSlot = s[QStringLiteral("choiceSlot")].toInt(-1);
    m_finished = s[QStringLiteral("phase")] == QLatin1String("Finished");
    g->inPlay = s[QStringLiteral("phase")] == QLatin1String("Simulating");
    // Keep local aim/power untouched on visual frames while the owned turn is active.
    if (!g->putting && !g->stroking) g->putter->setOrigin(g->curBall()->x(), g->curBall()->y());
    g->setUpdatesEnabled(true);
    Q_ASSERT(beforeSteps == g->physicsSteps() && beforeCollisions == g->collisionCalls());
    return true;
}
bool GameSessionAdapter::shoot(const ShotIntent &intent) {
    m_scored = false; m_resolutionIndex = 0;
    return g->applyAcceptedShot(intent);
}
void GameSessionAdapter::settle() {
    if (!g->maySimulate() || !g->m_onlineShot || m_choiceSlot >= 0) return;
    for (const auto &p : std::as_const(*g->players)) if (p.ball()->forceStillGoing() || p.ball()->curState() == Rolling) return;
    if (!m_scored) {
        if (!g->dontAddStroke) g->curPlayer->addStrokeToHole(hole());
        g->dontAddStroke = false;
        for (auto &p : *g->players) {
            for (int i = 0; i < p.ball()->addStroke(); ++i) p.addStrokeToHole(hole());
            p.ball()->setAddStroke(0);
        }
        m_scored = true;
    }
    continueResolution();
}
void GameSessionAdapter::continueResolution() {
    for (; m_resolutionIndex < g->players->size(); ++m_resolutionIndex) {
        auto &p = (*g->players)[m_resolutionIndex];
        if (p.ball()->curState() != Holed && p.ball()->placeOnGround(m_hazardVelocity)) {
            m_choiceSlot = m_resolutionIndex;
            m_choiceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            Q_EMIT transition(QStringLiteral("AwaitingHazardChoice")); return;
        }
    }
    for (auto &p : *g->players) {
        if (g->holeInfo.hasMaxStrokes() && p.score(hole()) >= g->holeInfo.maxStrokes()) {
            p.ball()->setState(Holed); p.ball()->setVisible(false);
        }
    }
    g->inPlay = g->m_onlineShot = false;
    bool allDone = true;
    for (const auto &p : std::as_const(*g->players)) if (p.ball()->curState() != Holed) allDone = false;
    if (allDone) {
        if (hole() >= g->highestHole) {
            m_finished = true; enableSimulation(false); Q_EMIT transition(QStringLiteral("Finished")); return;
        }
        int starter = 0;
        for (int i = 1; i < g->players->size(); ++i) {
            for (int h = hole(); h > 0; --h) {
                const int a = (*g->players)[i].score(h), b = (*g->players)[starter].score(h);
                if (a != b) { if (a < b) starter = i; break; }
            }
        }
        loadHole(hole() + 1);
        g->curBall()->setVisible(false); g->curPlayer = g->players->begin() + starter;
    } else {
        do { ++g->curPlayer; if (g->curPlayer == g->players->end()) g->curPlayer = g->players->begin(); }
        while (g->curBall()->curState() == Holed);
    }
    g->curBall()->setVisible(true);
    g->putter->setAngle(g->curBall());
    Q_EMIT transition(QStringLiteral("AwaitingShot"));
}
bool GameSessionAdapter::choose(const QString &action) {
    if (!g->maySimulate() || m_choiceSlot < 0 || (action != QLatin1String("drop") && action != QLatin1String("rehit"))) return false;
    auto *ball = (*g->players)[m_choiceSlot].ball();
    if (action == QLatin1String("rehit")) {
        const auto saved = g->ballStateList[m_choiceSlot];
        ball->setPos(saved.beginningOfHole ? g->whiteBall->pos() : QPointF(saved.spot));
    } else {
        Vector direction = m_hazardVelocity;
        if (direction.magnitude() < 0.000001) direction = Vector(1, 0);
        direction /= direction.magnitude();
        bool outside = false;
        for (int step = 0; step < 1000; ++step) {
            bool inHazard = false;
            for (auto *item : ball->collidingItems()) if (item->data(0) == Rtti_DontPlaceOn) inHazard = true;
            if (!inHazard) { outside = true; break; }
            ball->setPos(ball->pos() - direction * 3.0);
        }
        if (!outside) { m_failure = QStringLiteral("hazard drop exceeded search limit"); return false; }
    }
    ball->setPlaceOnGround(false); ball->setVisible(true); ball->setState(Stopped); ball->setVelocity(Vector());
    m_choiceSlot = -1; m_choiceId.clear(); ++m_resolutionIndex;
    continueResolution();
    return true;
}
