// SPDX-License-Identifier: GPL-2.0-or-later
#include "replayharness.h"

#include "game.h"
#include "landscape.h"
#include "objectregistry.h"
#include "objects.h"
#include "obstacles.h"
#include "session/gamesessionadapter.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <KConfig>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace Kolf::Session;

namespace Kolf::Testing {
namespace {

constexpr int MaximumStepsPerShot = 30000;
QList<qint64> profileStepNs;
QList<qint64> profileRenderNs;

QString profileSummary(QList<qint64> samples)
{
    if (samples.isEmpty()) return QStringLiteral("none");
    std::sort(samples.begin(), samples.end());
    qint64 total = 0;
    for (const auto value : std::as_const(samples)) total += value;
    const auto ms = [](qint64 ns) { return QString::number(double(ns) / 1e6, 'f', 3); };
    return QStringLiteral("%1 samples, mean %2 ms, p95 %3 ms, max %4 ms").arg(samples.size())
        .arg(ms(total / samples.size()), ms(samples.at(int(samples.size() * 0.95))), ms(samples.last()));
}
constexpr double MaximumMagnitude = 55.5 / 8.0;

struct FixedShot {
    double offsetDegrees;
    double power;
};
constexpr FixedShot FixedShots[] = {{0, 0.6}, {25, 0.35}, {0, 0.2}};

double rounded(double value)
{
    return std::round(value * 1000.0) / 1000.0;
}

QJsonObject trackedFields(const QJsonObject &visual)
{
    return {{QStringLiteral("x"), rounded(visual[QStringLiteral("x")].toDouble())},
        {QStringLiteral("y"), rounded(visual[QStringLiteral("y")].toDouble())},
        {QStringLiteral("rotation"), rounded(visual[QStringLiteral("rotation")].toDouble())},
        {QStringLiteral("visible"), visual[QStringLiteral("visible")].toBool()}};
}

QMap<QString, QJsonObject> objectState(const GameSessionAdapter &adapter)
{
    QMap<QString, QJsonObject> result;
    const auto objects = adapter.capture(1, 1, 1, QStringLiteral("AwaitingShot"))[QStringLiteral("objects")].toArray();
    for (const auto value : objects) {
        const auto object = value.toObject();
        result.insert(object[QStringLiteral("id")].toString(), trackedFields(object));
    }
    return result;
}

QPointF cupPosition(KolfGame *game, const QPointF &fallback)
{
    QPointF best = fallback;
    double bestDistance = -1;
    const auto items = game->scene()->items();
    for (auto *item : items) {
        auto *cup = dynamic_cast<Kolf::Cup *>(item);
        if (!cup) continue;
        const double distance = QLineF(fallback, cup->pos()).length();
        if (bestDistance < 0 || distance < bestDistance) {
            bestDistance = distance;
            best = cup->pos();
        }
    }
    return best;
}

ShotIntent fixedIntent(const FixedShot &shot, const QPointF &ball, const QPointF &cup)
{
    double direction = std::atan2(cup.y() - ball.y(), cup.x() - ball.x()) + shot.offsetDegrees * M_PI / 180.0;
    while (direction > M_PI) direction -= 2 * M_PI;
    while (direction < -M_PI) direction += 2 * M_PI;
    ShotIntent intent;
    intent.directionRadians = rounded(direction);
    intent.launchMagnitude = rounded(shot.power * MaximumMagnitude);
    return intent;
}

QStringList courseFiles(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isDir()) return {info.absoluteFilePath()};
    QStringList result;
    const auto entries = QDir(path).entryInfoList(QDir::Files, QDir::Name);
    for (const auto &entry : entries) {
        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        if (file.read(4096).contains("-course@")) result.append(entry.absoluteFilePath());
    }
    return result;
}

QString firstDifference(const QJsonValue &expected, const QJsonValue &actual, const QString &path)
{
    if (expected == actual) return {};
    if (expected.isObject() && actual.isObject()) {
        const auto a = expected.toObject(), b = actual.toObject();
        auto keys = a.keys();
        for (const auto &key : b.keys()) if (!keys.contains(key)) keys.append(key);
        for (const auto &key : std::as_const(keys)) {
            const auto difference = firstDifference(a.value(key), b.value(key), path + QLatin1Char('.') + key);
            if (!difference.isEmpty()) return difference;
        }
    }
    if (expected.isArray() && actual.isArray()) {
        const auto a = expected.toArray(), b = actual.toArray();
        for (int i = 0; i < qMax(a.size(), b.size()); ++i) {
            const auto difference = firstDifference(a.at(i), b.at(i), QStringLiteral("%1[%2]").arg(path).arg(i));
            if (!difference.isEmpty()) return difference;
        }
    }
    const auto text = [](const QJsonValue &value) {
        if (value.isUndefined()) return QStringLiteral("(missing)");
        if (value.isObject()) return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
        if (value.isArray()) return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
        return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
    };
    return QStringLiteral("%1: expected %2, got %3").arg(path, text(expected), text(actual));
}

QJsonObject replayHole(const QString &courseFile, int hole, quint32 seed, int &holeCount, QString &error)
{
    PlayerList players;
    Player player;
    player.setId(1);
    player.setName(QStringLiteral("Replay"));
    player.ball()->setColor(Qt::white);
    players.append(player);

    QWidget host;
    auto *game = new KolfGame(&players, courseFile, &host, Role::Authority);
    game->setManualStepping(true);
    game->setSound(false);
    auto *adapter = new GameSessionAdapter(game);
    QString phase;
    QObject::connect(adapter, &GameSessionAdapter::transition, adapter, [&phase](const QString &next) { phase = next; });

    QJsonArray shots;
    if (!adapter->loadHole(hole)) {
        error = QStringLiteral("hole %1 could not be loaded").arg(hole);
        delete game;
        return {};
    }
    holeCount = adapter->highestHole();
    game->setRandomSeed(seed + quint32(hole));
    if (!qEnvironmentVariableIsEmpty("KOLF_REPLAY_POWERUPS"))
        game->setPowerUpRule(Kolf::powerUpRuleFromId(qEnvironmentVariable("KOLF_REPLAY_POWERUPS")));
    adapter->enableSimulation(true);
    const auto initialObjects = objectState(*adapter);
    for (const auto &fixed : FixedShots) {
        Ball *ball = players.first().ball();
        if (adapter->hole() != hole || ball->curState() == Holed) break;
        if (!qEnvironmentVariableIsEmpty("KOLF_REPLAY_STATUS"))
            game->powerUps().addStatus(0, qEnvironmentVariable("KOLF_REPLAY_STATUS"), 1);
        const auto intent = fixedIntent(fixed, ball->pos(), cupPosition(game, ball->pos()));
        QJsonObject shot{{QStringLiteral("direction"), intent.directionRadians}, {QStringLiteral("magnitude"), intent.launchMagnitude}};
        if (!adapter->shoot(intent)) {
            shot[QStringLiteral("rejected")] = adapter->failure();
            shots.append(shot);
            break;
        }
        phase.clear();
        int steps = 0;
        int choices = 0;
        const bool profile = qEnvironmentVariable("KOLF_REPLAY_PROFILE") == QLatin1String("1");
        while (steps < MaximumStepsPerShot) {
            QElapsedTimer stepClock;
            stepClock.start();
            game->stepSimulation();
            game->timeout();
            QCoreApplication::processEvents();
            if (profile) {
                profileStepNs.append(stepClock.nsecsElapsed());
                if (steps % 10 == 0) {
                    QElapsedTimer renderClock;
                    renderClock.start();
                    game->grab();
                    profileRenderNs.append(renderClock.nsecsElapsed());
                }
            }
            ++steps;
            if (phase == QLatin1String("AwaitingHazardChoice")) {
                ++choices;
                phase.clear();
                if (!adapter->choose(QStringLiteral("drop"))) break;
                QCoreApplication::processEvents();
            }
            if (phase == QLatin1String("Simulating")) phase.clear();
            if (!phase.isEmpty()) break;
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        shot[QStringLiteral("steps")] = steps;
        if (choices) shot[QStringLiteral("hazardDrops")] = choices;
        shot[QStringLiteral("strokes")] = players.first().score(hole);
        if (game->powerUps().enabled())
            shot[QStringLiteral("powerups")] = game->powerUps().toJson();
        if (phase.isEmpty()) {
            shot[QStringLiteral("unsettled")] = true;
            shots.append(shot);
            break;
        }
        if (adapter->hole() != hole || phase == QLatin1String("Finished")) {
            shot[QStringLiteral("holed")] = true;
            shots.append(shot);
            break;
        }
        shot[QStringLiteral("ball")] = QJsonArray{rounded(ball->x()), rounded(ball->y())};
        shot[QStringLiteral("state")] = int(ball->curState());
        const auto objects = objectState(*adapter);
        QJsonObject moved;
        for (auto it = objects.cbegin(); it != objects.cend(); ++it)
            if (initialObjects.value(it.key()) != it.value()) moved.insert(it.key(), it.value());
        if (!moved.isEmpty()) shot[QStringLiteral("objects")] = moved;
        shots.append(shot);
    }
    delete game;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return {{QStringLiteral("hole"), hole}, {QStringLiteral("shots"), shots}};
}

}

QJsonObject replayCourse(const QString &courseFile, quint32 seed, QString &error)
{
    QJsonArray holes;
    int holeCount = 1;
    for (int hole = 1; hole <= holeCount && error.isEmpty(); ++hole) {
        const auto result = replayHole(courseFile, hole, seed, holeCount, error);
        if (error.isEmpty()) holes.append(result);
    }
    return {{QStringLiteral("course"), QFileInfo(courseFile).fileName()}, {QStringLiteral("seed"), double(seed)},
        {QStringLiteral("holes"), holes}};
}

int runReplayFromEnvironment()
{
    const QString courses = qEnvironmentVariable("KOLF_REPLAY_COURSES");
    const QString golden = qEnvironmentVariable("KOLF_REPLAY_GOLDEN");
    const QString output = qEnvironmentVariable("KOLF_REPLAY_OUTPUT");
    const bool update = qEnvironmentVariable("KOLF_REPLAY_UPDATE") == QLatin1String("1");
    const quint32 seed = qEnvironmentVariableIsEmpty("KOLF_REPLAY_SEED") ? 1 : qEnvironmentVariable("KOLF_REPLAY_SEED").toUInt();
    const int runs = qMax(1, qEnvironmentVariableIntValue("KOLF_REPLAY_RUNS"));
    const auto files = courseFiles(courses);
    if (files.isEmpty()) {
        std::cerr << "replay: no course files in " << qPrintable(courses) << std::endl;
        return 2;
    }
    int failures = 0;
    for (const auto &file : files) {
        const QString name = QFileInfo(file).completeBaseName();
        QString error;
        profileStepNs.clear();
        profileRenderNs.clear();
        const auto result = replayCourse(file, seed, error);
        if (!profileStepNs.isEmpty()) {
            std::cout << "profile: " << qPrintable(name) << ": step " << qPrintable(profileSummary(profileStepNs)) << std::endl;
            std::cout << "profile: " << qPrintable(name) << ": render " << qPrintable(profileSummary(profileRenderNs)) << std::endl;
        }
        if (!error.isEmpty()) {
            std::cerr << "replay: " << qPrintable(name) << ": " << qPrintable(error) << std::endl;
            ++failures;
            continue;
        }
        for (int run = 1; run < runs; ++run) {
            QString repeatError;
            const auto repeat = replayCourse(file, seed, repeatError);
            const auto difference = firstDifference(result, repeat, name);
            if (!difference.isEmpty()) {
                std::cerr << "replay: run " << run + 1 << " differs: " << qPrintable(difference) << std::endl;
                ++failures;
            }
        }
        const QByteArray json = QJsonDocument(result).toJson(QJsonDocument::Indented);
        if (!output.isEmpty()) {
            QDir().mkpath(output);
            QFile out(QDir(output).filePath(name + QStringLiteral(".json")));
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) out.write(json);
        }
        if (golden.isEmpty()) continue;
        QFile goldenFile(QDir(golden).filePath(name + QStringLiteral(".json")));
        if (update) {
            QDir().mkpath(golden);
            if (!goldenFile.open(QIODevice::WriteOnly | QIODevice::Truncate) || goldenFile.write(json) != json.size()) {
                std::cerr << "replay: could not write " << qPrintable(goldenFile.fileName()) << std::endl;
                ++failures;
            }
            continue;
        }
        if (!goldenFile.open(QIODevice::ReadOnly)) {
            std::cerr << "replay: missing golden file " << qPrintable(goldenFile.fileName()) << std::endl;
            ++failures;
            continue;
        }
        const auto expected = QJsonDocument::fromJson(goldenFile.readAll()).object();
        const auto difference = firstDifference(expected, result, name);
        if (!difference.isEmpty()) {
            std::cerr << "replay: " << qPrintable(difference) << std::endl;
            ++failures;
        } else {
            std::cout << "replay: " << qPrintable(name) << " matches" << std::endl;
        }
    }
    return failures ? 1 : 0;
}

int checkPropertyDefaults(KolfGame *game)
{
    int failures = 0;
    for (const auto &type : Kolf::ObjectRegistry::instance().types()) {
        QStringList runtimeFields;
        const auto values = game->newObjectProperties(type.id(), &runtimeFields);
        QStringList problems;
        for (const auto &spec : type.propertySpecs()) {
            if (!values.contains(spec.key())) problems << QStringLiteral("%1 is not reported").arg(spec.key());
            else if (!spec.sameValue(values.value(spec.key()), spec.defaultValue()))
                problems << QStringLiteral("%1 is %2, schema default %3").arg(spec.key(), values.value(spec.key()).toString(), spec.defaultValue().toString());
        }
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            if (!type.propertySpec(it.key())) problems << QStringLiteral("%1 is missing from the schema").arg(it.key());
        auto declared = type.runtimeFields();
        declared.sort();
        runtimeFields.sort();
        if (declared != runtimeFields)
            problems << QStringLiteral("runtime fields are %1, catalog declares %2").arg(runtimeFields.join(QLatin1Char(',')), declared.join(QLatin1Char(',')));
        for (const auto &problem : std::as_const(problems)) std::cerr << "defaults: " << qPrintable(type.id()) << ": " << qPrintable(problem) << std::endl;
        failures += problems.size();
    }
    if (!failures) std::cout << "defaults: every object type matches its schema" << std::endl;
    return failures;
}

bool resaveCourse(const QString &courseFile, const QString &outputFile, QString &error)
{
    QFile::remove(outputFile);
    PlayerList players;
    Player player;
    player.setId(1);
    player.setName(QStringLiteral("Resave"));
    players.append(player);
    QWidget host;
    auto *game = new KolfGame(&players, courseFile, &host, Role::Authority);
    game->setManualStepping(true);
    game->setSound(false);
    auto *adapter = new GameSessionAdapter(game);
    KConfig target(outputFile, KConfig::SimpleConfig);
    int holeCount = 1;
    for (int hole = 1; hole <= holeCount; ++hole) {
        if (!adapter->loadHole(hole)) {
            error = QStringLiteral("hole %1 could not be loaded").arg(hole);
            break;
        }
        holeCount = adapter->highestHole();
        game->writeHole(&target);
    }
    KolfGame::updateCourseMetadata(&target);
    delete game;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return error.isEmpty() && target.sync();
}

int runCollisionProbeFromEnvironment()
{
    const QString courseFile = qEnvironmentVariable("KOLF_COLLISION_PROBE");
    const double power = qEnvironmentVariableIsEmpty("KOLF_COLLISION_POWER") ? 0.4 : qEnvironmentVariable("KOLF_COLLISION_POWER").toDouble();
    const QString cupCourse = qEnvironmentVariable("KOLF_COLLISION_CUP_COURSE");
    int wrong = 0;
    const QList<double> offsets = cupCourse.isEmpty() ? QList<double>{-7.0, -5.0, -3.0, -1.0, 0.0, 1.0, 3.0, 5.0, 7.0} : QList<double>{0.0};
    for (const double offset : offsets) {
        PlayerList players;
        for (int id = 1; id <= 2; ++id) {
            Player player;
            player.setId(id);
            player.setName(QStringLiteral("Probe %1").arg(id));
            player.ball()->setColor(Qt::white);
            players.append(player);
        }
        QWidget host;
        auto *game = new KolfGame(&players, cupCourse.isEmpty() ? courseFile : cupCourse, &host, Role::Authority);
        game->setManualStepping(true);
        game->setSound(false);
        auto *adapter = new GameSessionAdapter(game);
        QString phase;
        QObject::connect(adapter, &GameSessionAdapter::transition, adapter, [&phase](const QString &next) { phase = next; });
        if (!adapter->loadHole(1)) { std::cerr << "collision: could not load hole" << std::endl; return 2; }
        adapter->enableSimulation(true);
        Ball *striker = players[0].ball();
        Ball *target = players[1].ball();
        target->setPos(striker->pos() + QPointF(40, offset));
        target->setVisible(true);
        target->setBeginningOfHole(false);
        target->setState(Stopped);
        ShotIntent intent;
        intent.directionRadians = 0;
        intent.launchMagnitude = power * MaximumMagnitude;
        if (!adapter->shoot(intent)) { std::cerr << "collision: shot rejected" << std::endl; return 2; }
        const QPointF targetStart = target->pos();
        QPointF normal;
        QPointF strikerAtContact;
        bool contact = false;
        for (int step = 0; step < 3000; ++step) {
            const QPointF strikerBefore = striker->pos();
            game->stepSimulation();
            game->timeout();
            QCoreApplication::processEvents();
            if (!contact && (target->pos() != targetStart || !target->velocity().isNull())) {
                contact = true;
                strikerAtContact = strikerBefore;
                normal = targetStart - strikerBefore;
            }
            if (phase == QLatin1String("AwaitingShot") || phase == QLatin1String("Finished")) break;
        }
        if (!cupCourse.isEmpty()) {
            const bool holed = target->curState() == Holed;
            const bool strikerNext = game->curBall() == striker;
            std::cout << "collision: pushed into cup: holed " << holed << ", phase " << qPrintable(phase)
                      << ", striker plays next " << strikerNext << ", target strokes " << players[1].score(1) << std::endl;
            if (!holed || !strikerNext) ++wrong;
            delete game;
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            continue;
        }
        const QString screenshot = qEnvironmentVariable("KOLF_COLLISION_SCREENSHOT");
        if (!screenshot.isEmpty() && offset == 3.0) {
            game->resize(500, 420);
            game->grab().save(screenshot);
        }
        const QPointF moved = target->pos() - targetStart;
        const auto degrees = [](QPointF v) { return std::atan2(v.y(), v.x()) * 180.0 / M_PI; };
        double difference = degrees(moved) - degrees(normal);
        while (difference > 180) difference -= 360;
        while (difference < -180) difference += 360;
        const bool sideways = contact && qAbs(offset) > 0.5;
        const bool opposite = sideways && ((offset > 0) != (moved.y() > 0));
        if (!contact || opposite || qAbs(difference) > 20 || (offset == 0 && QLineF(QPointF(), moved).length() < 50)) ++wrong;
        std::cout << "collision: offset " << offset << " contact " << contact << " normal " << degrees(normal)
                  << " target moved " << degrees(moved) << " (" << QLineF(QPointF(), moved).length() << " px)"
                  << " difference " << difference << (opposite ? " OPPOSITE" : "") << std::endl;
        delete game;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    return wrong ? 1 : 0;
}

namespace {

struct SolveResult {
    bool holed = false;
    double distance = 1e9;
    int strokes = 0;
};

SolveResult playShots(const QString &courseFile, int hole, const QList<ShotIntent> &shots)
{
    PlayerList players;
    Player player;
    player.setId(1);
    player.setName(QStringLiteral("Solver"));
    player.ball()->setColor(Qt::white);
    players.append(player);
    QWidget host;
    auto *game = new KolfGame(&players, courseFile, &host, Role::Authority);
    game->setManualStepping(true);
    game->setSound(false);
    auto *adapter = new GameSessionAdapter(game);
    QString phase;
    QObject::connect(adapter, &GameSessionAdapter::transition, adapter, [&phase](const QString &next) { phase = next; });
    SolveResult result;
    if (!adapter->loadHole(hole)) {
        delete game;
        return result;
    }
    game->setRandomSeed(1 + quint32(hole));
    adapter->enableSimulation(true);
    Ball *ball = players.first().ball();
    const QPointF cup = cupPosition(game, ball->pos());
    for (const auto &intent : shots) {
        if (adapter->hole() != hole || ball->curState() == Holed) break;
        if (!adapter->shoot(intent)) break;
        phase.clear();
        for (int steps = 0; steps < MaximumStepsPerShot; ++steps) {
            game->stepSimulation();
            game->timeout();
            QCoreApplication::processEvents();
            if (phase == QLatin1String("AwaitingHazardChoice")) {
                phase.clear();
                if (!adapter->choose(QStringLiteral("drop"))) break;
                QCoreApplication::processEvents();
            }
            if (phase == QLatin1String("Simulating")) phase.clear();
            if (!phase.isEmpty()) break;
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (phase.isEmpty()) break;
    }
    result.strokes = players.first().score(hole);
    result.holed = adapter->hole() != hole || phase == QLatin1String("Finished") || ball->curState() == Holed;
    result.distance = result.holed ? 0 : QLineF(ball->pos(), cup).length();
    delete game;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return result;
}

}

int runSolverFromEnvironment()
{
    const QString courseFile = qEnvironmentVariable("KOLF_SOLVE_COURSE");
    int holeCount = 1;
    {
        PlayerList players;
        Player player;
        player.setId(1);
        players.append(player);
        QWidget host;
        auto *game = new KolfGame(&players, courseFile, &host, Role::Authority);
        auto *adapter = new GameSessionAdapter(game);
        adapter->loadHole(1);
        holeCount = adapter->highestHole();
        delete game;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    int unsolved = 0;
    for (int hole = 1; hole <= holeCount; ++hole) {
        QList<ShotIntent> chosen;
        SolveResult best;
        int par = 0;
        {
            KConfig config(courseFile, KConfig::SimpleConfig);
            par = KConfigGroup(&config, QStringLiteral("%1-hole@-50,-50|0").arg(hole)).readEntry("par", 3);
        }
        for (int depth = 0; depth < par + 2 && !best.holed; ++depth) {
            SolveResult bestStep;
            ShotIntent bestShot;
            for (int angle = 0; angle < 48 && !bestStep.holed; ++angle) {
                for (const double power : {0.15, 0.25, 0.35, 0.5, 0.7, 0.9}) {
                    if (bestStep.holed) break;
                    ShotIntent intent;
                    intent.directionRadians = rounded(angle * 2 * M_PI / 48 - M_PI);
                    intent.launchMagnitude = rounded(power * MaximumMagnitude);
                    auto shots = chosen;
                    shots.append(intent);
                    const auto result = playShots(courseFile, hole, shots);
                    const bool better = (result.holed && (!bestStep.holed || result.strokes < bestStep.strokes))
                        || (!bestStep.holed && !result.holed && result.distance < bestStep.distance);
                    if (better) {
                        bestStep = result;
                        bestShot = intent;
                    }
                }
            }
            chosen.append(bestShot);
            best = bestStep;
        }
        std::cout << "solve: hole " << hole << " par " << par << ": "
                  << (best.holed ? "holed in " : "not holed after ") << best.strokes << " strokes"
                  << (best.holed ? "" : qPrintable(QStringLiteral(", %1 px from the cup").arg(best.distance, 0, 'f', 0))) << std::endl;
        if (!best.holed) ++unsolved;
    }
    return unsolved ? 1 : 0;
}

int runResaveFromEnvironment()
{
    const QString courses = qEnvironmentVariable("KOLF_RESAVE_COURSES");
    const QString output = qEnvironmentVariable("KOLF_RESAVE_OUTPUT");
    const auto files = courseFiles(courses);
    if (files.isEmpty() || output.isEmpty()) {
        std::cerr << "resave: set KOLF_RESAVE_COURSES and KOLF_RESAVE_OUTPUT" << std::endl;
        return 2;
    }
    QDir().mkpath(output);
    int failures = 0;
    {
        PlayerList players;
        Player player;
        player.setId(1);
        players.append(player);
        QWidget host;
        auto *game = new KolfGame(&players, files.first(), &host, Role::Authority);
        game->setManualStepping(true);
        failures += checkPropertyDefaults(game);
        delete game;
    }
    for (const auto &file : files) {
        const QString name = QFileInfo(file).completeBaseName();
        const QString first = QDir(output).filePath(name + QStringLiteral(".kolf"));
        const QString second = QDir(output).filePath(name + QStringLiteral(".roundtrip.kolf"));
        QString error;
        if (!resaveCourse(file, first, error) || !resaveCourse(first, second, error)) {
            std::cerr << "resave: " << qPrintable(name) << ": " << qPrintable(error) << std::endl;
            ++failures;
            continue;
        }
        QFile a(first), b(second);
        if (!a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly) || a.readAll() != b.readAll()) {
            std::cerr << "resave: " << qPrintable(name) << ": saving the saved course changed it" << std::endl;
            ++failures;
            continue;
        }
        std::cout << "resave: " << qPrintable(name) << " round-trips" << std::endl;
    }
    return failures ? 1 : 0;
}

}
