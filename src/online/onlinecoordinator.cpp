// SPDX-License-Identifier: GPL-2.0-or-later
#include "onlinecoordinator.h"
#include "serverlist.h"
#include "color.h"
#include "coursefile.h"

#include "session/onlineprotocol.h"
#include "rules_build_id.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLocale>
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
        if (m_resuming) { if (!m_reconnectTimer.isActive()) scheduleReconnect(); return; }
        if (canResume()) { beginResume(); return; }
        clearSession();
        Q_EMIT connectionClosed();
    });
    connect(&m_network, &Net::NetworkClient::failed, this, [this](const QString &reason) {
        if (m_resuming) { if (!m_reconnectTimer.isActive()) scheduleReconnect(); return; }
        if (canResume()) return;
        Q_EMIT failed(reason);
    });
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (!m_resuming) return;
        Q_EMIT statusChanged(tr("Reconnecting to the server…"));
        m_network.open(QUrl(m_endpoint));
    });
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

static QString canonicalColor(const QString &text)
{
    const auto parsed = Kolf::Online::colorFromRgba(text.trimmed());
    return parsed.isValid() ? Kolf::Online::rgbaFromColor(parsed) : text.trimmed();
}

void OnlineCoordinator::connectToService(const QString &endpoint)
{
    const auto address = normalizeServerAddress(endpoint);
    if (!address.isValid()) {
        Q_EMIT failed(address.error);
        return;
    }
    const QUrl url = address.url;
    Q_EMIT statusChanged(tr("Connecting to %1…").arg(url.toDisplayString()));
    m_endpoint = url.toString();
    m_network.open(url);
}

void OnlineCoordinator::disconnectFromService()
{
    const bool wasResuming = m_resuming;
    clearSession();
    m_network.close();
    if (wasResuming) Q_EMIT connectionClosed();
}

void OnlineCoordinator::clearSession()
{
    if (m_resuming) { m_resuming = false; Q_EMIT reconnectingChanged(false); }
    m_reconnectTimer.stop(); m_reconnectAttempt = 0; m_resumeToken.clear();
    m_state = {}; m_serviceHello = {}; m_memberId.clear(); m_lobbyId.clear(); m_matchId.clear(); m_preparedMatchId.clear(); m_coursePath.clear();
    m_uploadTimer.stop(); m_downloadTimer.stop(); m_uploadBytes.clear(); m_downloadBytes.clear();
    m_uploadId.clear(); m_downloadHash.clear();
}

bool OnlineCoordinator::canResume() const
{
    const auto phase = m_state.value(QStringLiteral("phase")).toString();
    return !m_resumeToken.isEmpty() && !m_lobbyId.isEmpty()
        && (phase == QLatin1String("Open") || phase == QLatin1String("Results"));
}

void OnlineCoordinator::beginResume()
{
    m_resuming = true;
    m_reconnectAttempt = 0;
    m_resumeDeadline = QDateTime::currentMSecsSinceEpoch() + 115000;
    m_uploadTimer.stop(); m_uploadId.clear(); m_uploadBytes.clear();
    Q_EMIT reconnectingChanged(true);
    scheduleReconnect();
}

void OnlineCoordinator::scheduleReconnect()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now >= m_resumeDeadline) {
        clearSession();
        Q_EMIT connectionClosed();
        return;
    }
    const int delay = 1000 * (1 << qMin(m_reconnectAttempt, 3));
    ++m_reconnectAttempt;
    const int seconds = int((m_resumeDeadline - now) / 1000);
    Q_EMIT statusChanged(tr("Lost the connection to the server. Reconnecting… (%n seconds left to rejoin)", nullptr, seconds));
    m_reconnectTimer.start(int(qMin<qint64>(delay, m_resumeDeadline - now)));
}

void OnlineCoordinator::finishResume(bool resumed)
{
    m_resuming = false;
    m_reconnectTimer.stop();
    m_reconnectAttempt = 0;
    Q_EMIT reconnectingChanged(false);
    if (resumed) Q_EMIT statusChanged(tr("Reconnected."));
}

static QJsonObject colorChoice(const QString &mode, const QString &color)
{
    QJsonObject choice{{QStringLiteral("colorMode"), mode}};
    if (mode == QLatin1String("custom")) choice[QStringLiteral("customColor")] = canonicalColor(color);
    return choice;
}

static QJsonObject clientInfo()
{
    QString version;
    for (const QChar character : QCoreApplication::applicationVersion())
        if (character.isLetterOrNumber() || QStringLiteral(".+_-").contains(character)) version.append(character);
    version = version.left(40);
    return {{QStringLiteral("appVersion"), version.isEmpty() ? QStringLiteral("unknown") : version},
            {QStringLiteral("rulesBuildId"), QStringLiteral(KOLF_RULES_BUILD_ID)}};
}

QString OnlineCoordinator::incompatibleBuildMessage(const QJsonObject &payload)
{
    const QString hostVersion = payload.value(QStringLiteral("hostVersion")).toString().left(40);
    const QString yourVersion = payload.value(QStringLiteral("yourVersion")).toString().left(40);
    if (hostVersion.isEmpty() || yourVersion.isEmpty())
        return tr("This lobby was created with a different version of Kolf. You both need the same version to play together.");
    if (hostVersion != yourVersion) {
        if (isNewerClientVersion(hostVersion, yourVersion))
            return tr("This lobby uses Kolf %1, and you have Kolf %2. Update Kolf to join: kolf.pro/download").arg(hostVersion, yourVersion);
        return tr("This lobby uses Kolf %1, and you have Kolf %2. Ask the host to update Kolf, then join again.").arg(hostVersion, yourVersion);
    }
    return tr("This lobby uses a different build of Kolf %1 (build %2; yours is %3). You both need the same build to play together.")
        .arg(hostVersion, payload.value(QStringLiteral("hostBuild")).toString().left(8),
             payload.value(QStringLiteral("yourBuild")).toString().left(8));
}

void OnlineCoordinator::createLobby(const QString &displayName, const QString &colorMode, const QString &customColor, const QString &courseId)
{
    auto payload = colorChoice(colorMode, customColor);
    payload[QStringLiteral("displayName")] = displayName.trimmed();
    payload[QStringLiteral("courseId")] = courseId;
    payload[QStringLiteral("clientInfo")] = clientInfo();
    send(QStringLiteral("CreateLobby"), payload);
}

void OnlineCoordinator::joinLobby(const QString &joinCode, const QString &displayName, const QString &colorMode, const QString &customColor)
{
    auto payload = colorChoice(colorMode, customColor);
    payload[QStringLiteral("joinCode")] = joinCode.trimmed().toUpper();
    payload[QStringLiteral("displayName")] = displayName.trimmed();
    payload[QStringLiteral("clientInfo")] = clientInfo();
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
    Q_EMIT courseUploadProgress(0);
    Q_EMIT statusChanged(tr("Uploading custom course…"));
}

void OnlineCoordinator::startMatch()
{
    send(QStringLiteral("StartMatch"), {{QStringLiteral("lobbyRevision"), m_state.value(QStringLiteral("lobbyRevision")).toInt()}});
}

void OnlineCoordinator::setLobbyRules(bool hostControls, bool allowVoting)
{
    send(QStringLiteral("SetLobbyRules"), {{QStringLiteral("hostControls"), hostControls},
        {QStringLiteral("allowVoting"), allowVoting}});
}

void OnlineCoordinator::leaveLobby()
{
    send(QStringLiteral("LeaveLobby"), {});
    m_resumeToken.clear();
    m_state = {}; m_lobbyId.clear(); m_memberId.clear(); m_matchId.clear(); m_preparedMatchId.clear();
    m_uploadTimer.stop(); m_uploadId.clear(); m_uploadBytes.clear();
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
        if (m_resuming) {
            m_network.send(Session::onlineEnvelope(QStringLiteral("ResumeSession"),
                {{QStringLiteral("resumeToken"), m_resumeToken}}, requestId()));
            return;
        }
        Q_EMIT serviceChanged(m_serviceHello);
        return;
    }
    if (type == QLatin1String("SessionResumed")) {
        m_memberId = payload.value(QStringLiteral("memberId")).toString();
        m_resumeToken = payload.value(QStringLiteral("resumeToken")).toString(m_resumeToken);
        finishResume(true);
        acceptState(payload.value(QStringLiteral("state")).toObject());
        return;
    }
    if (m_resuming && type == QLatin1String("RequestRejected")) {
        const auto hello = m_serviceHello;
        clearSession();
        m_serviceHello = hello;
        Q_EMIT serviceChanged(m_serviceHello);
        Q_EMIT lobbyClosed(tr("You were away too long to rejoin the lobby."));
        return;
    }
    if (type == QLatin1String("ServiceNotice")) {
        if (payload.value(QStringLiteral("kind")).toString() == QLatin1String("restarting")) {
            const auto deadline = QDateTime::fromMSecsSinceEpoch(qint64(payload.value(QStringLiteral("deadline")).toDouble()));
            const QString server = m_serviceHello.value(QStringLiteral("serverName")).toString().trimmed().left(60);
            QString text = server.isEmpty() ? tr("The server is restarting for an update.")
                                            : tr("%1 is restarting for an update.").arg(server);
            if (deadline.isValid() && deadline > QDateTime::currentDateTime())
                text += QLatin1Char(' ') + tr("Matches in progress can finish until %1; new games can start after the restart.")
                    .arg(QLocale().toString(deadline.time(), QLocale::ShortFormat));
            Q_EMIT serviceNotice(text);
        }
        return;
    }
    if (type == QLatin1String("UnsupportedProtocol") || type == QLatin1String("RequestRejected")) {
        const auto code = payload.value(QStringLiteral("code")).toString();
        const auto message = type == QLatin1String("UnsupportedProtocol")
            ? tr("This server uses a different Kolf online protocol version.")
            : code == QLatin1String("ServiceFull") ? tr("The server is full. Try again later or choose another server.")
            : code == QLatin1String("LobbyNotFound") ? tr("That join code was not found. Check the code and try again.")
            : code == QLatin1String("LobbyFull") ? tr("That lobby is full.")
            : code == QLatin1String("IncompatibleBuild") ? incompatibleBuildMessage(payload)
            : code == QLatin1String("ServiceRestarting") ? tr("The server is restarting for an update. Try again in a few minutes.")
            : code == QLatin1String("TooManyAttempts") ? tr("Too many join attempts with the wrong code. Wait a minute and try again.")
            : code == QLatin1String("TooManyConnections") ? tr("Too many Kolf windows are connected from your network. Close one and try again.")
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
        Q_EMIT courseUploadProgress(qBound(0, int(qint64(m_uploadNextIndex) * CourseChunkBytes * 100 / qMax(1, m_uploadBytes.size())), 100));
        m_uploadTimer.start(30000);
        const auto uploadId = m_uploadId;
        QTimer::singleShot(35, this, [this, uploadId] { if (m_uploadId == uploadId) sendNextUploadChunk(); });
        return;
    }
    if (type == QLatin1String("CourseUploaded")) {
        Q_EMIT courseUploadProgress(100);
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
        m_resumeToken.clear();
        m_state = {}; m_memberId.clear(); m_lobbyId.clear(); m_matchId.clear(); m_preparedMatchId.clear(); m_coursePath.clear();
        m_uploadTimer.stop(); m_downloadTimer.stop(); m_uploadBytes.clear(); m_downloadBytes.clear();
        m_uploadId.clear(); m_downloadHash.clear();
        Q_EMIT lobbyClosed(reason);
        return;
    }
    if (payload.contains(QStringLiteral("resumeToken")))
        m_resumeToken = payload.value(QStringLiteral("resumeToken")).toString();
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
        Q_EMIT matchPrepared({{QStringLiteral("protocolVersion"), Session::onlineProtocolVersion}, {QStringLiteral("endpoint"), m_endpoint},
            {QStringLiteral("lobbyId"), m_lobbyId}, {QStringLiteral("matchId"), m_matchId},
            {QStringLiteral("course"), m_coursePath}, {QStringLiteral("roster"), roster},
            {QStringLiteral("courseSource"), match.value(QStringLiteral("course")).toObject().value(QStringLiteral("source"))},
            {QStringLiteral("localPlayerIds"), localPlayerIds},
            {QStringLiteral("localMemberId"), m_memberId},
            {QStringLiteral("hostControls"), m_state.value(QStringLiteral("hostControls"))},
            {QStringLiteral("allowVoting"), m_state.value(QStringLiteral("allowVoting"))},
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
        downloadCourse(course.value(QStringLiteral("sha256")).toString(), course.value(QStringLiteral("byteSize")).toInt(-1),
            tr("Downloading and verifying the custom course…"));
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
    const auto expectedHash = course.value(QStringLiteral("expectedHash")).toString();
    QFile file(path);
    if (!fileName.isEmpty() && resource.match(fileName).hasMatch() && fileName != QLatin1String("..")
        && !path.isEmpty() && file.open(QIODevice::ReadOnly) && file.size() <= MaximumCourseBytes) {
        const auto hash = Session::onlineCourseHash(file.readAll());
        if (hash == expectedHash && !qEnvironmentVariableIsSet("KOLF_ONLINE_TEST_FORCE_DOWNLOAD")) {
            sendCourseReady(path, hash);
            return;
        }
    }
    downloadCourse(expectedHash, course.value(QStringLiteral("byteSize")).toInt(-1),
        tr("Your copy of %1 differs from the server's. Downloading the server's copy…")
            .arg(course.value(QStringLiteral("displayName")).toString().left(60)));
}

void OnlineCoordinator::downloadCourse(const QString &hash, int byteSize, const QString &status)
{
    m_downloadHash = hash;
    m_downloadSize = byteSize;
    if (courseCachePath(m_downloadHash).isEmpty() || m_downloadSize < 1 || m_downloadSize > MaximumCourseBytes) {
        m_downloadHash.clear();
        failPreparation(tr("The selected online course could not be opened locally, and the server did not describe a copy to download."));
        return;
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
    Q_EMIT statusChanged(status);
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
            failPreparation(error.isEmpty() ? tr("The downloaded course did not match the server's hash.") : error); return;
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
