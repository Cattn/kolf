// SPDX-License-Identifier: GPL-2.0-or-later
#include "onlinecoordinator.h"

#include "session/protocolv2.h"
#include "prototype_build.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

using namespace Kolf::Online;

OnlineCoordinator::OnlineCoordinator(QObject *parent)
    : QObject(parent)
{
    connect(&m_network, &Net::NetworkClient::connected, this, &OnlineCoordinator::connected);
    connect(&m_network, &Net::NetworkClient::disconnected, this, &OnlineCoordinator::connectionClosed);
    connect(&m_network, &Net::NetworkClient::failed, this, &OnlineCoordinator::failed);
    connect(&m_network, &Net::NetworkClient::received, this, &OnlineCoordinator::receive);
}

static QUrl parseServiceEndpoint(QString text)
{
    text = text.trimmed();
    if (text.isEmpty()) return {};
    if (!text.contains(QStringLiteral("://"))) {
        if (!text.contains(QLatin1Char(':'))) text.append(QStringLiteral(":3011"));
        text.prepend(QStringLiteral("ws://"));
    }
    return QUrl(text);
}

void OnlineCoordinator::connectToService(const QString &endpoint)
{
    const QUrl url = parseServiceEndpoint(endpoint);
    if (!url.isValid() || (url.scheme() != QLatin1String("ws") && url.scheme() != QLatin1String("wss")) || url.host().isEmpty()) {
        Q_EMIT failed(tr("Enter a valid ws:// or wss:// endpoint."));
        return;
    }
    Q_EMIT statusChanged(tr("Connecting to %1…").arg(url.toDisplayString()));
    m_endpoint = url.toString();
    m_network.open(url, {}, 2);
}

void OnlineCoordinator::disconnectFromService()
{
    m_network.close();
    m_state = {}; m_memberId.clear(); m_lobbyId.clear(); m_matchId.clear(); m_preparedMatchId.clear(); m_coursePath.clear();
}

void OnlineCoordinator::createLobby(const QString &displayName, const QString &color, const QString &courseId)
{
    send(QStringLiteral("CreateLobby"), {{QStringLiteral("displayName"), displayName.trimmed()},
         {QStringLiteral("color"), color.trimmed()}, {QStringLiteral("courseId"), courseId}});
}

void OnlineCoordinator::joinLobby(const QString &joinCode, const QString &displayName, const QString &color)
{
    send(QStringLiteral("JoinLobby"), {{QStringLiteral("joinCode"), joinCode.trimmed().toUpper()},
         {QStringLiteral("displayName"), displayName.trimmed()}, {QStringLiteral("color"), color.trimmed()}});
}

void OnlineCoordinator::setReady(bool ready)
{
    send(QStringLiteral("SetReady"), {{QStringLiteral("lobbyRevision"), m_state.value(QStringLiteral("lobbyRevision")).toInt()},
         {QStringLiteral("ready"), ready}});
}

void OnlineCoordinator::setCourse(const QString &courseId)
{
    send(QStringLiteral("SetCourse"), {{QStringLiteral("courseId"), courseId}});
}

void OnlineCoordinator::startMatch()
{
    send(QStringLiteral("StartMatch"), {{QStringLiteral("lobbyRevision"), m_state.value(QStringLiteral("lobbyRevision")).toInt()}});
}

void OnlineCoordinator::returnToLobby()
{
    send(QStringLiteral("ReturnToLobby"), {}, true);
}

QString OnlineCoordinator::requestId()
{
    return QStringLiteral("request_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

void OnlineCoordinator::send(const QString &type, const QJsonObject &payload, bool matchScoped)
{
    m_network.send(Session::envelopeV2(type, payload, requestId(), m_lobbyId, matchScoped ? m_matchId : QString()));
}

void OnlineCoordinator::receive(const QJsonObject &message)
{
    const auto type = message.value(QStringLiteral("type")).toString();
    const auto payload = message.value(QStringLiteral("payload")).toObject();
    if (type == QLatin1String("UnsupportedProtocol") || type == QLatin1String("RequestRejected")) {
        Q_EMIT failed(payload.value(QStringLiteral("message")).toString(tr("The online request was rejected.")));
        return;
    }
    if (type == QLatin1String("LobbyClosed")) {
        const auto reason = payload.value(QStringLiteral("reason")).toString(tr("The lobby closed."));
        m_state = {}; m_memberId.clear(); m_lobbyId.clear(); m_matchId.clear(); m_preparedMatchId.clear(); m_coursePath.clear();
        Q_EMIT lobbyClosed(reason);
        return;
    }
    if (type == QLatin1String("LobbyCreated"))
        m_memberId = payload.value(QStringLiteral("memberId")).toString();
    else if (type == QLatin1String("LobbyState") && m_memberId.isEmpty())
        m_memberId = payload.value(QStringLiteral("joinedMemberId")).toString();

    const auto state = payload.value(QStringLiteral("state")).toObject();
    if (state.contains(QStringLiteral("lobbyId"))) acceptState(state);
    if (type == QLatin1String("PreparationReady")) {
        const auto match = m_state.value(QStringLiteral("match")).toObject();
        const auto roster = match.value(QStringLiteral("roster")).toArray();
        QString localPlayerId;
        for (const auto &value : roster) {
            const auto player = value.toObject();
            if (player.value(QStringLiteral("memberId")).toString() == m_memberId)
                localPlayerId = player.value(QStringLiteral("playerId")).toString();
        }
        const auto logDirectory = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath(QStringLiteral("online/%1").arg(m_matchId));
        Q_EMIT statusChanged(tr("Both players verified the course. Loading the match scene…"));
        Q_EMIT matchPrepared({{QStringLiteral("protocolVersion"), 2}, {QStringLiteral("endpoint"), m_endpoint},
            {QStringLiteral("lobbyId"), m_lobbyId}, {QStringLiteral("matchId"), m_matchId},
            {QStringLiteral("course"), m_coursePath}, {QStringLiteral("roster"), roster},
            {QStringLiteral("localPlayerId"), localPlayerId},
            {QStringLiteral("role"), match.value(QStringLiteral("authorityMemberId")).toString() == m_memberId
                ? QStringLiteral("authority") : QStringLiteral("guest")},
            {QStringLiteral("logDirectory"), logDirectory}});
    }
    else if (type == QLatin1String("MatchStarted"))
        Q_EMIT statusChanged(tr("Match input is open."));
    else if (type == QLatin1String("PreparationAborted"))
        Q_EMIT failed(payload.value(QStringLiteral("reason")).toString(tr("Match preparation failed.")));
}

void OnlineCoordinator::acceptState(const QJsonObject &state)
{
    m_state = state;
    m_lobbyId = state.value(QStringLiteral("lobbyId")).toString();
    const auto match = state.value(QStringLiteral("match")).toObject();
    m_matchId = match.value(QStringLiteral("matchId")).toString();
    Q_EMIT lobbyChanged(m_state);
    if (state.value(QStringLiteral("phase")) == QLatin1String("Preparing")) prepareCourse();
}

void OnlineCoordinator::prepareCourse()
{
    if (m_matchId.isEmpty() || m_preparedMatchId == m_matchId) return;
    m_preparedMatchId = m_matchId;
    const auto course = m_state.value(QStringLiteral("match")).toObject().value(QStringLiteral("course")).toObject();
    const auto courseId = course.value(QStringLiteral("courseId")).toString();
    const QHash<QString, QString> files{{QStringLiteral("classic"), QStringLiteral("Classic.kolf")},
                                       {QStringLiteral("easy"), QStringLiteral("Easy.kolf")},
                                       {QStringLiteral("practice"), QStringLiteral("Practice")}};
    const auto fileName = files.value(courseId);
    const auto path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("kolf/courses/%1").arg(fileName));
    QFile file(path);
    if (fileName.isEmpty() || path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        send(QStringLiteral("PreparationFailed"), {{QStringLiteral("reason"), tr("The selected course is not installed.")}}, true);
        Q_EMIT failed(tr("The selected online course could not be opened locally."));
        return;
    }
    m_coursePath = path;
    const auto hash = QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
    send(QStringLiteral("CourseReady"), {{QStringLiteral("courseHash"), hash},
         {QStringLiteral("compatibilityId"), QStringLiteral(KOLF_PROTOTYPE_BUILD)}}, true);
    Q_EMIT statusChanged(tr("Course verified. Waiting for the other player…"));
}
