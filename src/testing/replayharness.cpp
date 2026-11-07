// SPDX-License-Identifier: GPL-2.0-or-later
#include "replayharness.h"

#include "game.h"
#include "itemfactory.h"
#include "landscape.h"
#include "objects.h"
#include "obstacles.h"
#include "session/gamesessionadapter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QWidget>
#include <cmath>
#include <iostream>

using namespace Kolf::Session;

namespace Kolf::Testing {
namespace {

constexpr int MaximumStepsPerShot = 30000;
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

void registerTypes(Kolf::ItemFactory &factory)
{
    factory.registerType<Kolf::Slope>(QStringLiteral("slope"), QStringLiteral("Slope"));
    factory.registerType<Kolf::Puddle>(QStringLiteral("puddle"), QStringLiteral("Puddle"));
    factory.registerType<Kolf::Wall>(QStringLiteral("wall"), QStringLiteral("Wall"));
    factory.registerType<Kolf::Cup>(QStringLiteral("cup"), QStringLiteral("Cup"));
    factory.registerType<Kolf::Sand>(QStringLiteral("sand"), QStringLiteral("Sand"));
    factory.registerType<Kolf::Windmill>(QStringLiteral("windmill"), QStringLiteral("Windmill"));
    factory.registerType<Kolf::BlackHole>(QStringLiteral("blackhole"), QStringLiteral("Black hole"));
    factory.registerType<Kolf::Floater>(QStringLiteral("floater"), QStringLiteral("Floater"));
    factory.registerType<Kolf::Bridge>(QStringLiteral("bridge"), QStringLiteral("Bridge"));
    factory.registerType<Kolf::Sign>(QStringLiteral("sign"), QStringLiteral("Sign"));
    factory.registerType<Kolf::Bumper>(QStringLiteral("bumper"), QStringLiteral("Bumper"));
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

QJsonObject replayHole(const Kolf::ItemFactory &factory, const QString &courseFile, int hole, quint32 seed, int &holeCount, QString &error)
{
    PlayerList players;
    Player player;
    player.setId(1);
    player.setName(QStringLiteral("Replay"));
    player.ball()->setColor(Qt::white);
    players.append(player);

    QWidget host;
    auto *game = new KolfGame(factory, &players, courseFile, &host, Role::Authority);
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
    adapter->enableSimulation(true);
    const auto initialObjects = objectState(*adapter);
    for (const auto &fixed : FixedShots) {
        Ball *ball = players.first().ball();
        if (adapter->hole() != hole || ball->curState() == Holed) break;
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
        while (steps < MaximumStepsPerShot) {
            game->stepSimulation();
            game->timeout();
            QCoreApplication::processEvents();
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
    Kolf::ItemFactory factory;
    registerTypes(factory);
    QJsonArray holes;
    int holeCount = 1;
    for (int hole = 1; hole <= holeCount && error.isEmpty(); ++hole) {
        const auto result = replayHole(factory, courseFile, hole, seed, holeCount, error);
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
        const auto result = replayCourse(file, seed, error);
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

}
