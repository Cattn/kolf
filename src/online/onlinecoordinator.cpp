// SPDX-License-Identifier: GPL-2.0-or-later
#include "onlinecoordinator.h"
#include "color.h"
#include "coursefile.h"

#include "session/onlineprotocol.h"
#include "rules_build_id.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QStandardPaths>
#include <QSaveFile>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>

using namespace Kolf::Online;

OnlineCoordinator::OnlineCoordinator(QObject *parent)
    : QObject(parent)
{
    connect(&m_network, &Net::NetworkClient::connected, this, &OnlineCoordinator::connected);
    connect(&m_network, &Net::NetworkClient::disconnected, this, [this] {
        m_state = {}; m_serviceHello = {}; m_memberId.clear(); m_lobbyId.clear();
        m_matchId.clear(); m_preparedMatchId.clear(); m_coursePath.clear();
        m_uploadTimer.stop(); m_downloadTimer.stop(); m_uploadBytes.clear(); m_downloadBytes.clear();
        m_uploadId.clear(); m_downloadHash.clear();
        Q_EMIT connectionClosed();
    });
    connect(&m_network, &Net::NetworkClient::failed, this, &OnlineCoordinator::failed);
    connect(&m_network, &Net::NetworkClient::received, this, &OnlineCoordinator::receive);
    m_uploadTimer.setSingleShot(true);
    m_downloadTimer.setSingleShot(true);
    connect(&m_uploadTimer, &QTimer::timeout, this, [this] {
        m_uploadBytes.clear(); m_uploadId.clear();
        Q_EMIT failed(tr("Custom course upload timed out. Choose the file again to retry."));
    });
    connect(&m_downloadTimer, &QTimer::timeout, this, [this] {
        failPreparation(tr("Custom course download timed out."));
    });
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

static QString canonicalColor(const QString &text)
{
    const auto parsed = Kolf::Online::colorFromRgba(text.trimmed());
    return parsed.isValid() ? Kolf::Online::rgbaFromColor(parsed) : text.trimmed();
}

void OnlineCoordinator::connectToService(const QString &endpoint)
{
    const QUrl url = parseServiceEndpoint(endpoint);
    if (!url.isValid() || (url.scheme() != QLatin1String("ws") && url.scheme() != QLatin1String("wss"))
        || url.host().isEmpty() || !url.userName().isEmpty() || !url.password().isEmpty()
        || url.hasQuery() || url.hasFragment()) {
        Q_EMIT failed(tr("Enter a ws:// or wss:// server address without credentials or a query string."));
        return;
    }
    Q_EMIT statusChanged(tr("Connecting to %1…").arg(url.toDisplayString()));
    m_endpoint = url.toString();
    m_network.open(url);
}

void OnlineCoordinator::disconnectFromService()
{
    m_network.close();
    m_state = {}; m_serviceHello = {}; m_memberId.clear(); m_lobbyId.clear(); m_matchId.clear(); m_preparedMatchId.clear(); m_coursePath.clear();
    m_uploadTimer.stop(); m_downloadTimer.stop(); m_uploadBytes.clear(); m_downloadBytes.clear();
    m_uploadId.clear(); m_downloadHash.clear();
}

static QJsonObject colorChoice(const QString &mode, const QString &color)
{
    QJsonObject choice{{QStringLiteral("colorMode"), mode}};
    if (mode == QLatin1String("custom")) choice[QStringLiteral("customColor")] = canonicalColor(color);
    return choice;
}

void OnlineCoordinator::createLobby(const QString &displayName, const QString &colorMode, const QString &customColor, const QString &courseId)
{
    auto payload = colorChoice(colorMode, customColor);
    payload[QStringLiteral("displayName")] = displayName.trimmed();
    payload[QStringLiteral("courseId")] = courseId;
    send(QStringLiteral("CreateLobby"), payload);
}

void OnlineCoordinator::joinLobby(const QString &joinCode, const QString &displayName, const QString &colorMode, const QString &customColor)
{
    auto payload = colorChoice(colorMode, customColor);
    payload[QStringLiteral("joinCode")] = joinCode.trimmed().toUpper();
    payload[QStringLiteral("displayName")] = displayName.trimmed();
    send(QStringLiteral("JoinLobby"), payload);
}

void OnlineCoordinator::setReady(bool ready)
{
    send(QStringLiteral("SetReady"), {{QStringLiteral("lobbyRevision"), m_state.value(QStringLiteral("lobbyRevision")).toInt()},
         {QStringLiteral("ready"), ready}});
}

void OnlineCoordinator::addPlayer(const QString &displayName, const QString &colorMode, const QString &customColor)
{
    auto payload = colorChoice(colorMode, customColor);
    payload[QStringLiteral("displayName")] = displayName.trimmed();
    send(QStringLiteral("AddPlayer"), payload);
}

void OnlineCoordinator::updatePlayer(const QString &playerId, const QString &displayName, const QString &colorMode, const QString &customColor)
{
    auto payload = colorChoice(colorMode, customColor);
    payload[QStringLiteral("playerId")] = playerId;
    payload[QStringLiteral("displayName")] = displayName.trimmed();
    send(QStringLiteral("UpdatePlayer"), payload);
}

void OnlineCoordinator::removePlayer(const QString &playerId)
{
    send(QStringLiteral("RemovePlayer"), {{QStringLiteral("playerId"), playerId}});
}

void OnlineCoordinator::setCourse(const QString &courseId)
{
    send(QStringLiteral("SetCourse"), {{QStringLiteral("courseId"), courseId}});
}

void OnlineCoordinator::uploadCourse(const QString &path)
{
    if (m_state.value(QStringLiteral("phase")) != QLatin1String("Open")
        || m_state.value(QStringLiteral("ownerMemberId")).toString() != m_memberId || !m_uploadId.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 1 || file.size() > MaximumCourseBytes) {
        Q_EMIT failed(tr("Choose a readable Kolf course smaller than 4 MiB.")); return;
    }
    const auto bytes = file.readAll();
    QString error;
    if (!validateCourseBytes(bytes, error)) { Q_EMIT failed(error); return; }
    const auto hash = rawCourseHash(bytes);
    if (cacheCourse(bytes, hash).isEmpty()) { Q_EMIT failed(tr("Could not cache the chosen course.")); return; }
    m_uploadBytes = bytes; m_uploadHash = hash;
    m_uploadId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_uploadNextIndex = 0;
    send(QStringLiteral("CourseUploadBegin"), {{QStringLiteral("uploadId"), m_uploadId},
        {QStringLiteral("sha256"), hash}, {QStringLiteral("byteSize"), bytes.size()}});
    m_uploadTimer.start(30000);
    Q_EMIT statusChanged(tr("Uploading custom course…"));
}

void OnlineCoordinator::startMatch()
{
    send(QStringLiteral("StartMatch"), {{QStringLiteral("lobbyRevision"), m_state.value(QStringLiteral("lobbyRevision")).toInt()}});
}

void OnlineCoordinator::returnToLobby(bool rematch)
{
    send(QStringLiteral("ReturnToLobby"), {{QStringLiteral("rematch"), rematch}}, true);
}

QString OnlineCoordinator::requestId()
{
    return QStringLiteral("request_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

void OnlineCoordinator::send(const QString &type, const QJsonObject &payload, bool matchScoped)
{
    m_network.send(Session::onlineEnvelope(type, payload, requestId(), m_lobbyId, matchScoped ? m_matchId : QString()));
}

void OnlineCoordinator::receive(const QJsonObject &message)
{
    const auto type = message.value(QStringLiteral("type")).toString();
    const auto payload = message.value(QStringLiteral("payload")).toObject();
    if (type == QLatin1String("ServiceHello")) {
        m_serviceHello = payload;
        Q_EMIT serviceChanged(m_serviceHello);
        return;
    }
    if (type == QLatin1String("UnsupportedProtocol") || type == QLatin1String("RequestRejected")) {
        const auto code = payload.value(QStringLiteral("code")).toString();
        const auto message = type == QLatin1String("UnsupportedProtocol")
            ? tr("This server uses a different Kolf online protocol version.")
            : code == QLatin1String("ServiceFull") ? tr("The server is full. Try again later or choose another server.")
            : code == QLatin1String("LobbyNotFound") ? tr("That join code was not found. Check the code and try again.")
            : code == QLatin1String("LobbyFull") ? tr("That lobby is full.")
            : payload.value(QStringLiteral("message")).toString(tr("The online request was rejected."));
        if (!m_downloadHash.isEmpty()) failPreparation(message);
        else {
            if (!m_uploadId.isEmpty()) { m_uploadTimer.stop(); m_uploadId.clear(); m_uploadBytes.clear(); }
            else if (code == QLatin1String("WrongPhase") || code == QLatin1String("StaleRevision")) return;
            Q_EMIT failed(message);
        }
        return;
    }
    if (type == QLatin1String("CourseUploadReady") && payload.value(QStringLiteral("uploadId")).toString() == m_uploadId) {
        m_uploadTimer.start(30000);
        const auto descriptor = payload.value(QStringLiteral("descriptor")).toObject();
        if (!descriptor.isEmpty()) {
            m_uploadTimer.stop(); m_uploadId.clear(); m_uploadBytes.clear();
            setCourse(descriptor.value(QStringLiteral("courseId")).toString());
            return;
        }
        m_uploadNextIndex = payload.value(QStringLiteral("nextIndex")).toInt(-1);
        sendNextUploadChunk(); return;
    }
    if (type == QLatin1String("CourseChunkAccepted") && payload.value(QStringLiteral("uploadId")).toString() == m_uploadId) {
        m_uploadNextIndex = payload.value(QStringLiteral("nextIndex")).toInt(-1);
        m_uploadTimer.start(30000);
        const auto uploadId = m_uploadId;
        QTimer::singleShot(35, this, [this, uploadId] { if (m_uploadId == uploadId) sendNextUploadChunk(); });
        return;
    }
    if (type == QLatin1String("CourseUploaded")) {
        m_uploadTimer.stop(); m_uploadId.clear(); m_uploadBytes.clear();
        Q_EMIT statusChanged(tr("Custom course is ready in the lobby."));
        return;
    }
    if (type == QLatin1String("CourseChunkData") && !m_downloadHash.isEmpty()) {
        if (message.value(QStringLiteral("matchId")).toString() != m_matchId
            || payload.value(QStringLiteral("sha256")).toString() != m_downloadHash
            || payload.value(QStringLiteral("index")).toInt(-1) != m_downloadNextIndex
            || payload.value(QStringLiteral("byteSize")).toInt(-1) != m_downloadSize) {
            failPreparation(tr("Custom course chunks did not match the selected match.")); return;
        }
        const auto encoded = payload.value(QStringLiteral("data")).toString().toLatin1();
        const auto bytes = QByteArray::fromBase64(encoded);
        const int expected = qMin(int(CourseChunkBytes), m_downloadSize - m_downloadBytes.size());
        if (expected <= 0 || bytes.size() != expected || bytes.toBase64() != encoded) {
            failPreparation(tr("Custom course chunk was malformed.")); return;
        }
        m_downloadBytes += bytes;
        ++m_downloadNextIndex;
        m_downloadTimer.start(30000);
        const auto matchId = m_matchId;
        QTimer::singleShot(35, this, [this, matchId] { if (m_matchId == matchId) requestNextDownloadChunk(); });
        return;
    }
    if (type == QLatin1String("LobbyClosed")) {
        const auto reason = payload.value(QStringLiteral("reason")).toString(tr("The lobby closed."));
        m_state = {}; m_memberId.clear(); m_lobbyId.clear(); m_matchId.clear(); m_preparedMatchId.clear(); m_coursePath.clear();
        m_uploadTimer.stop(); m_downloadTimer.stop(); m_uploadBytes.clear(); m_downloadBytes.clear();
        m_uploadId.clear(); m_downloadHash.clear();
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
        QJsonArray localPlayerIds;
        for (const auto &value : roster) {
            const auto player = value.toObject();
            if (player.value(QStringLiteral("ownerMemberId")).toString() == m_memberId)
                localPlayerIds.append(player.value(QStringLiteral("playerId")));
        }
        const auto logDirectory = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath(QStringLiteral("online/%1").arg(m_matchId));
        Q_EMIT statusChanged(tr("Every member verified the course. Loading the match scene…"));
        Q_EMIT matchPrepared({{QStringLiteral("protocolVersion"), 3}, {QStringLiteral("endpoint"), m_endpoint},
            {QStringLiteral("lobbyId"), m_lobbyId}, {QStringLiteral("matchId"), m_matchId},
            {QStringLiteral("course"), m_coursePath}, {QStringLiteral("roster"), roster},
            {QStringLiteral("courseSource"), match.value(QStringLiteral("course")).toObject().value(QStringLiteral("source"))},
            {QStringLiteral("localPlayerIds"), localPlayerIds},
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
    if (state.value(QStringLiteral("phase")) == QLatin1String("Open")) {
        m_preparedMatchId.clear(); m_downloadTimer.stop(); m_downloadHash.clear(); m_downloadBytes.clear();
    }
    Q_EMIT lobbyChanged(m_state);
    if (state.value(QStringLiteral("phase")) == QLatin1String("Preparing")) prepareCourse();
}

void OnlineCoordinator::prepareCourse()
{
    if (m_matchId.isEmpty() || m_preparedMatchId == m_matchId) return;
    m_preparedMatchId = m_matchId;
    const auto course = m_state.value(QStringLiteral("match")).toObject().value(QStringLiteral("course")).toObject();
    const auto courseId = course.value(QStringLiteral("courseId")).toString();
    if (course.value(QStringLiteral("source")) == QLatin1String("uploaded")) {
        m_downloadHash = course.value(QStringLiteral("sha256")).toString();
        m_downloadSize = course.value(QStringLiteral("byteSize")).toInt(-1);
        if (courseCachePath(m_downloadHash).isEmpty() || m_downloadSize < 1 || m_downloadSize > MaximumCourseBytes) {
            failPreparation(tr("The custom course descriptor is invalid.")); return;
        }
        const auto path = courseCachePath(m_downloadHash);
        QFile cached(path);
        if (!qEnvironmentVariableIsSet("KOLF_ONLINE_TEST_FORCE_DOWNLOAD")
            && cached.open(QIODevice::ReadOnly) && cached.size() == m_downloadSize) {
            const auto bytes = cached.readAll();
            QString error;
            if (rawCourseHash(bytes) == m_downloadHash && validateCourseBytes(bytes, error)) {
                m_downloadHash.clear(); sendCourseReady(path, rawCourseHash(bytes)); return;
            }
        }
        m_downloadBytes.clear(); m_downloadNextIndex = 0;
        m_downloadTimer.start(30000);
        requestNextDownloadChunk();
        Q_EMIT statusChanged(tr("Downloading and verifying the custom course…"));
        return;
    }
    QString fileName;
    for (const auto &value : m_serviceHello.value(QStringLiteral("courses")).toArray()) {
        const auto catalogCourse = value.toObject();
        if (catalogCourse.value(QStringLiteral("courseId")).toString() == courseId) {
            fileName = catalogCourse.value(QStringLiteral("resourceName")).toString();
            break;
        }
    }
    const auto testCourse = qEnvironmentVariable("KOLF_ONLINE_TEST_COURSE");
    static const QRegularExpression resource(QStringLiteral("^[A-Za-z0-9_.-]+$"));
    const auto path = !testCourse.isEmpty() && courseId == QLatin1String("test") ? testCourse
        : QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("kolf/courses/%1").arg(fileName));
    QFile file(path);
    if (fileName.isEmpty() || !resource.match(fileName).hasMatch() || fileName == QLatin1String("..")
        || path.isEmpty() || !file.open(QIODevice::ReadOnly) || file.size() > MaximumCourseBytes) {
        failPreparation(tr("The selected online course could not be opened locally."));
        return;
    }
    const auto hash = Session::onlineCourseHash(file.readAll());
    sendCourseReady(path, hash);
}

void OnlineCoordinator::sendCourseReady(const QString &path, const QString &hash)
{
    const auto course = m_state.value(QStringLiteral("match")).toObject().value(QStringLiteral("course")).toObject();
    if (hash != course.value(QStringLiteral("expectedHash")).toString()) {
        failPreparation(tr("The selected course does not match the server's course hash.")); return;
    }
    m_coursePath = path;
    send(QStringLiteral("CourseReady"), {{QStringLiteral("courseHash"), hash},
         {QStringLiteral("compatibilityId"), QStringLiteral(KOLF_RULES_BUILD_ID)}}, true);
    Q_EMIT statusChanged(tr("Course verified. Waiting for the other members…"));
}

void OnlineCoordinator::failPreparation(const QString &reason)
{
    m_downloadTimer.stop(); m_downloadHash.clear(); m_downloadBytes.clear();
    if (!m_matchId.isEmpty()) send(QStringLiteral("PreparationFailed"), {{QStringLiteral("reason"), reason.left(160)}}, true);
    Q_EMIT failed(reason);
}

QString OnlineCoordinator::cacheCourse(const QByteArray &bytes, const QString &sha256)
{
    const auto path = courseCachePath(sha256);
    if (path.isEmpty() || rawCourseHash(bytes) != sha256 || !QDir().mkpath(QFileInfo(path).absolutePath())) return {};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return {};
    return path;
}

void OnlineCoordinator::sendNextUploadChunk()
{
    if (m_uploadId.isEmpty() || m_uploadNextIndex < 0 || m_uploadNextIndex > (m_uploadBytes.size() + CourseChunkBytes - 1) / CourseChunkBytes) {
        m_uploadTimer.stop(); m_uploadId.clear(); m_uploadBytes.clear();
        Q_EMIT failed(tr("Custom course upload sequence was invalid.")); return;
    }
    if (m_uploadNextIndex * CourseChunkBytes >= m_uploadBytes.size()) {
        send(QStringLiteral("CourseUploadFinish"), {{QStringLiteral("uploadId"), m_uploadId}});
        return;
    }
    const auto chunk = m_uploadBytes.mid(m_uploadNextIndex * CourseChunkBytes, CourseChunkBytes).toBase64();
    send(QStringLiteral("CourseUploadChunk"), {{QStringLiteral("uploadId"), m_uploadId},
        {QStringLiteral("index"), m_uploadNextIndex}, {QStringLiteral("data"), QString::fromLatin1(chunk)}});
}

void OnlineCoordinator::requestNextDownloadChunk()
{
    if (m_downloadHash.isEmpty()) return;
    if (m_downloadBytes.size() == m_downloadSize) {
        QString error;
        if (rawCourseHash(m_downloadBytes) != m_downloadHash || !validateCourseBytes(m_downloadBytes, error)) {
            failPreparation(error.isEmpty() ? tr("Custom course hash mismatch.") : error); return;
        }
        const auto path = cacheCourse(m_downloadBytes, m_downloadHash);
        if (path.isEmpty()) { failPreparation(tr("Could not cache the downloaded course.")); return; }
        const auto hash = m_downloadHash;
        m_downloadTimer.stop(); m_downloadHash.clear(); m_downloadBytes.clear();
        sendCourseReady(path, hash);
        return;
    }
    if (m_downloadBytes.size() > m_downloadSize || m_downloadNextIndex >= (m_downloadSize + CourseChunkBytes - 1) / CourseChunkBytes) {
        failPreparation(tr("Custom course download exceeded its bounds.")); return;
    }
    send(QStringLiteral("GetCourseChunk"), {{QStringLiteral("sha256"), m_downloadHash},
        {QStringLiteral("index"), m_downloadNextIndex}}, true);
}
