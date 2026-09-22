// SPDX-License-Identifier: GPL-2.0-or-later
#include "onlinewidget.h"
#include "color.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QComboBox>
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QFont>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QInputDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QUrl>
#include <QPixmap>

using namespace Kolf::Online;

OnlineWidget::OnlineWidget(QWidget *parent)
    : QWidget(parent)
    , m_coordinator(this)
{
    auto *root = new QVBoxLayout(this);
    m_pages = new QStackedWidget(this);
    root->addWidget(m_pages);

    auto *connectPage = new QWidget(this);
    auto *connectLayout = new QVBoxLayout(connectPage);
    auto *connectForm = new QFormLayout;
    m_endpoint = new QLineEdit(connectPage);
    KConfigGroup onlineConfig(KSharedConfig::openConfig(), QStringLiteral("Online"));
    const auto configuredEndpoint = qEnvironmentVariable("KOLF_ONLINE_DEFAULT_ENDPOINT").trimmed();
    m_endpoint->setText(onlineConfig.readEntry("lastSuccessfulEndpoint",
        onlineConfig.readEntry("endpoint", configuredEndpoint)));
    m_serverLabel = new QLabel(connectPage);
    m_serverLabel->setWordWrap(true);
    connectLayout->addWidget(m_serverLabel);
    m_serverControls = new QWidget(connectPage);
    auto *serverLayout = new QVBoxLayout(m_serverControls);
    serverLayout->setContentsMargins(0, 0, 0, 0);
    m_recentServers = new QComboBox(m_serverControls);
    m_recentServers->setAccessibleName(i18n("Recent servers"));
    for (const auto &server : onlineConfig.readEntry("recentServers", QStringList()))
        m_recentServers->addItem(server);
    serverLayout->addWidget(m_recentServers);
    connectForm->addRow(i18nc("@label", "Server address:"), m_endpoint);
    serverLayout->addLayout(connectForm);
    connectLayout->addWidget(m_serverControls);
    m_serverControls->hide();
    m_connectStatus = new QLabel(i18n("Choose a server to play online."), connectPage);
    m_connectStatus->setWordWrap(true);
    connectLayout->addWidget(m_connectStatus);
    auto *connectButtons = new QHBoxLayout;
    m_connectButton = new QPushButton(i18nc("@action:button", "Connect"), connectPage);
    m_changeServerButton = new QPushButton(i18nc("@action:button", "Change Server"), connectPage);
    auto *cancelButton = new QPushButton(i18nc("@action:button", "Cancel"), connectPage);
    connectButtons->addStretch(); connectButtons->addWidget(m_changeServerButton);
    connectButtons->addWidget(m_connectButton); connectButtons->addWidget(cancelButton);
    connectLayout->addStretch(); connectLayout->addLayout(connectButtons);
    m_pages->addWidget(connectPage);
    m_connectTimeout = new QTimer(this);
    m_connectTimeout->setSingleShot(true);
    m_connectTimeout->setInterval(10000);
    setConnectionState(ConnectionState::Disconnected);

    auto *entryPage = new QWidget(this);
    auto *entryLayout = new QVBoxLayout(entryPage);
    auto *profileForm = new QFormLayout;
    m_name = new QLineEdit(onlineConfig.readEntry("displayName", QStringLiteral("Player")), entryPage);
    m_name->setMaxLength(32);
    m_color = new QLineEdit(onlineConfig.readEntry("color", QStringLiteral("#3daee9ff")), entryPage);
    m_color->setMaxLength(9);
    profileForm->addRow(i18nc("@label", "Display name:"), m_name);
    profileForm->addRow(i18nc("@label", "Ball color (#RRGGBBAA):"), m_color);
    m_createCourse = new QComboBox(entryPage);
    profileForm->addRow(i18nc("@label", "Course:"), m_createCourse);
    entryLayout->addLayout(profileForm);
    m_createButton = new QPushButton(i18nc("@action:button", "Create Lobby"), entryPage);
    entryLayout->addWidget(m_createButton);
    auto *joinRow = new QHBoxLayout;
    m_joinCode = new QLineEdit(entryPage); m_joinCode->setMaxLength(12);
    m_joinCode->setPlaceholderText(i18nc("@info:placeholder", "Join code"));
    m_joinButton = new QPushButton(i18nc("@action:button", "Join Lobby"), entryPage);
    joinRow->addWidget(m_joinCode); joinRow->addWidget(m_joinButton);
    entryLayout->addLayout(joinRow);
    m_entryStatus = new QLabel(i18n("Create a lobby or enter a friend's join code."), entryPage);
    m_entryStatus->setWordWrap(true);
    entryLayout->addWidget(m_entryStatus);
    auto *entryDisconnect = new QPushButton(i18nc("@action:button", "Disconnect"), entryPage);
    entryLayout->addStretch(); entryLayout->addWidget(entryDisconnect);
    m_pages->addWidget(entryPage);

    auto *lobbyPage = new QWidget(this);
    auto *lobbyLayout = new QVBoxLayout(lobbyPage);
    m_lobbySummary = new QLabel(lobbyPage); m_lobbySummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *heading = new QHBoxLayout;
    heading->addWidget(m_lobbySummary, 1);
    auto *copyInvite = new QPushButton(i18nc("@action:button", "Copy Join Code"), lobbyPage);
    heading->addWidget(copyInvite);
    lobbyLayout->addLayout(heading);
    auto *columns = new QHBoxLayout;
    auto *rosterColumn = new QVBoxLayout;
    rosterColumn->addWidget(new QLabel(i18n("People and players"), lobbyPage));
    m_players = new QListWidget(lobbyPage); rosterColumn->addWidget(m_players, 1);
    auto *playerButtons = new QHBoxLayout;
    m_addPlayer = new QPushButton(i18nc("@action:button", "Add local player"), lobbyPage);
    m_editPlayer = new QPushButton(i18nc("@action:button", "Edit player"), lobbyPage);
    m_removePlayer = new QPushButton(i18nc("@action:button", "Remove player"), lobbyPage);
    playerButtons->addWidget(m_addPlayer); playerButtons->addWidget(m_editPlayer); playerButtons->addWidget(m_removePlayer);
    rosterColumn->addLayout(playerButtons);
    columns->addLayout(rosterColumn, 2);
    auto *setupColumn = new QVBoxLayout;
    setupColumn->addWidget(new QLabel(i18n("Match setup"), lobbyPage));
    auto *courseRow = new QFormLayout;
    m_lobbyCourse = new QComboBox(lobbyPage);
    courseRow->addRow(i18nc("@label", "Course:"), m_lobbyCourse); setupColumn->addLayout(courseRow);
    m_lobbyStatus = new QLabel(lobbyPage); m_lobbyStatus->setWordWrap(true); setupColumn->addWidget(m_lobbyStatus);
    setupColumn->addStretch();
    columns->addLayout(setupColumn, 1);
    lobbyLayout->addLayout(columns, 1);
    auto *lobbyButtons = new QHBoxLayout;
    auto *lobbyDisconnect = new QPushButton(i18nc("@action:button", "Disconnect"), lobbyPage);
    m_ready = new QPushButton(lobbyPage); m_start = new QPushButton(i18nc("@action:button", "Start"), lobbyPage);
    lobbyButtons->addWidget(lobbyDisconnect); lobbyButtons->addStretch(); lobbyButtons->addWidget(m_ready); lobbyButtons->addWidget(m_start);
    lobbyLayout->addLayout(lobbyButtons);
    m_pages->addWidget(lobbyPage);

    auto *resultsPage = new QWidget(this);
    auto *resultsLayout = new QVBoxLayout(resultsPage);
    m_result = new QTextEdit(resultsPage); m_result->setReadOnly(true); resultsLayout->addWidget(m_result);
    auto *returnButton = new QPushButton(i18nc("@action:button", "Return to Lobby"), resultsPage);
    resultsLayout->addWidget(returnButton);
    m_pages->addWidget(resultsPage);

    connect(m_connectButton, &QPushButton::clicked, this, &OnlineWidget::beginConnection);
    connect(copyInvite, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_state.value(QStringLiteral("joinCode")).toString());
    });
    connect(m_changeServerButton, &QPushButton::clicked, this, [this] {
        m_serverControls->setVisible(true);
        m_endpoint->setFocus();
    });
    connect(m_recentServers, &QComboBox::activated, this, [this](int index) {
        if (index >= 0) m_endpoint->setText(m_recentServers->itemText(index));
    });
    connect(m_endpoint, &QLineEdit::textChanged, this, [this] {
        if (m_connectionState != ConnectionState::Connecting) setConnectionState(m_connectionState);
    });
    connect(cancelButton, &QPushButton::clicked, this, [this] {
        m_connectTimeout->stop();
        m_coordinator.disconnectFromService();
        setConnectionState(ConnectionState::Disconnected);
        Q_EMIT leaveRequested();
    });
    connect(m_connectTimeout, &QTimer::timeout, this, [this] {
        setConnectionState(ConnectionState::Failed, i18n("The server did not respond in time. Check its address and try again."));
        m_coordinator.disconnectFromService();
    });
    connect(m_createButton, &QPushButton::clicked, this, [this] {
        if (m_entryRequestPending) return;
        m_entryRequestPending = true;
        m_createButton->setEnabled(false); m_joinButton->setEnabled(false);
        savePreferences(); m_coordinator.createLobby(m_name->text(), m_color->text(), m_createCourse->currentData().toString());
    });
    connect(m_joinButton, &QPushButton::clicked, this, [this] {
        if (m_entryRequestPending) return;
        m_entryRequestPending = true;
        m_createButton->setEnabled(false); m_joinButton->setEnabled(false);
        savePreferences(); m_coordinator.joinLobby(m_joinCode->text(), m_name->text(), m_color->text());
    });
    connect(entryDisconnect, &QPushButton::clicked, this, [this] {
        m_coordinator.disconnectFromService();
        Q_EMIT leaveRequested();
    });
    connect(lobbyDisconnect, &QPushButton::clicked, this, [this] {
        m_coordinator.disconnectFromService();
        Q_EMIT leaveRequested();
    });
    connect(m_ready, &QPushButton::clicked, this, [this] {
        bool ready = false;
        for (const auto &value : m_state.value(QStringLiteral("members")).toArray()) {
            const auto member = value.toObject();
            if (member.value(QStringLiteral("memberId")).toString() == m_coordinator.memberId()) ready = member.value(QStringLiteral("ready")).toBool();
        }
        m_coordinator.setReady(!ready);
    });
    connect(m_start, &QPushButton::clicked, &m_coordinator, &OnlineCoordinator::startMatch);
    connect(m_addPlayer, &QPushButton::clicked, this, [this] {
        bool accepted = false;
        const auto name = QInputDialog::getText(this, i18nc("@title:window", "Add local player"),
            i18nc("@label", "Player name:"), QLineEdit::Normal, m_name->text(), &accepted).trimmed();
        if (!accepted || name.isEmpty()) return;
        const auto color = QInputDialog::getText(this, i18nc("@title:window", "Add local player"),
            i18nc("@label", "Ball color (#RRGGBBAA):"), QLineEdit::Normal, m_color->text(), &accepted).trimmed();
        if (accepted) m_coordinator.addPlayer(name, color);
    });
    connect(m_editPlayer, &QPushButton::clicked, this, [this] {
        const auto *item = m_players->currentItem();
        if (!item) return;
        bool accepted = false;
        const auto name = QInputDialog::getText(this, i18nc("@title:window", "Edit player"),
            i18nc("@label", "Player name:"), QLineEdit::Normal, item->data(Qt::UserRole + 1).toString(), &accepted).trimmed();
        if (!accepted || name.isEmpty()) return;
        const auto color = QInputDialog::getText(this, i18nc("@title:window", "Edit player"),
            i18nc("@label", "Ball color (#RRGGBBAA):"), QLineEdit::Normal, item->data(Qt::UserRole + 2).toString(), &accepted).trimmed();
        if (accepted) m_coordinator.updatePlayer(item->data(Qt::UserRole).toString(), name, color);
    });
    connect(m_removePlayer, &QPushButton::clicked, this, [this] {
        if (const auto *item = m_players->currentItem()) m_coordinator.removePlayer(item->data(Qt::UserRole).toString());
    });
    connect(m_players, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        const bool local = item && item->data(Qt::UserRole + 3).toString() == m_coordinator.memberId();
        m_editPlayer->setEnabled(local); m_removePlayer->setEnabled(local && item->data(Qt::UserRole + 4).toInt() > 1);
    });
    connect(m_lobbyCourse, &QComboBox::activated, this, [this](int) {
        if (!m_state.isEmpty()) m_coordinator.setCourse(m_lobbyCourse->currentData().toString());
    });
    connect(returnButton, &QPushButton::clicked, &m_coordinator, &OnlineCoordinator::returnToLobby);
    connect(&m_coordinator, &OnlineCoordinator::connected, this, [this] {
        setConnectionState(ConnectionState::Connecting, i18n("Verifying the server…"));
    });
    connect(&m_coordinator, &OnlineCoordinator::serviceChanged, this, [this](const QJsonObject &hello) {
        m_connectTimeout->stop();
        setConnectionState(ConnectionState::Connected);
        m_createCourse->clear(); m_lobbyCourse->clear();
        for (const auto &value : hello.value(QStringLiteral("courses")).toArray()) {
            const auto course = value.toObject();
            m_createCourse->addItem(course.value(QStringLiteral("displayName")).toString(), course.value(QStringLiteral("courseId")));
            m_lobbyCourse->addItem(course.value(QStringLiteral("displayName")).toString(), course.value(QStringLiteral("courseId")));
        }
        showEntry();
    });
    connect(&m_coordinator, &OnlineCoordinator::connectionClosed, this, [this] {
        if (m_matchActive) {
            m_matchActive = false;
            Q_EMIT matchEnded();
        }
        m_connectTimeout->stop();
        m_entryRequestPending = false;
        m_createButton->setEnabled(true); m_joinButton->setEnabled(true);
        if (m_connectionState != ConnectionState::Failed)
            setConnectionState(ConnectionState::Failed, i18n("The connection closed. Retry or choose another server."));
        m_pages->setCurrentIndex(0);
    });
    connect(&m_coordinator, &OnlineCoordinator::lobbyChanged, this, &OnlineWidget::showLobby);
    connect(&m_coordinator, &OnlineCoordinator::lobbyClosed, this, [this](const QString &reason) {
        if (m_matchActive) {
            m_matchActive = false;
            Q_EMIT matchEnded();
        }
        showEntry(); m_entryStatus->setText(reason);
    });
    connect(&m_coordinator, &OnlineCoordinator::matchPrepared, this, [this](const QJsonObject &config) {
        auto sessionConfig = config;
        if (!m_automation.isEmpty()) {
            sessionConfig[QStringLiteral("scriptedShots")] = m_automation.value(QStringLiteral("scriptedShots"));
            if (m_automation.contains(QStringLiteral("scriptedHazardAction")))
                sessionConfig[QStringLiteral("scriptedHazardAction")] = m_automation.value(QStringLiteral("scriptedHazardAction"));
            sessionConfig[QStringLiteral("capture")] = true;
            sessionConfig[QStringLiteral("verifySnapshots")] = true;
            sessionConfig[QStringLiteral("logFrames")] = true;
            sessionConfig[QStringLiteral("exitWhenFinished")] = false;
            sessionConfig[QStringLiteral("logDirectory")] = QDir(m_automation.value(QStringLiteral("logDirectory")).toString())
                .filePath(config.value(QStringLiteral("matchId")).toString());
        }
        m_matchActive = true;
        Q_EMIT matchRequested(sessionConfig);
    });
    connect(&m_coordinator, &OnlineCoordinator::statusChanged, this, [this](const QString &status) {
        m_lobbyStatus->setText(status);
        Q_EMIT statusChanged(status);
    });
    connect(&m_coordinator, &OnlineCoordinator::failed, this, [this](const QString &reason) {
        if (m_connectionState == ConnectionState::Connecting) {
            m_connectTimeout->stop();
            setConnectionState(ConnectionState::Failed, reason);
        }
        m_entryRequestPending = false;
        m_createButton->setEnabled(true); m_joinButton->setEnabled(true);
        m_entryStatus->setText(reason); m_lobbyStatus->setText(reason);
        if (!m_automation.isEmpty()) failAutomation(reason);
    });
}

OnlineWidget::~OnlineWidget()
{
}

void OnlineWidget::enterOnline()
{
    if (!m_automation.isEmpty()) return;
    m_pages->setCurrentIndex(0);
    if (!m_endpoint->text().trimmed().isEmpty()) {
        beginConnection();
    } else {
        setConnectionState(ConnectionState::Disconnected, i18n("Choose a private server to play online."));
        m_serverControls->show();
    }
}

void OnlineWidget::setConnectionState(ConnectionState state, const QString &message)
{
    m_connectionState = state;
    const auto address = m_endpoint->text().trimmed();
    const QUrl url(address.contains(QStringLiteral("://")) ? address : QStringLiteral("ws://") + address);
    const bool plaintextRemote = url.scheme() == QLatin1String("ws")
        && !url.host().isEmpty() && url.host() != QLatin1String("localhost")
        && url.host() != QLatin1String("127.0.0.1") && url.host() != QLatin1String("::1");
    const auto configuredAddress = qEnvironmentVariable("KOLF_ONLINE_DEFAULT_ENDPOINT").trimmed();
    const auto friendlyName = address == configuredAddress
        ? qEnvironmentVariable("KOLF_ONLINE_DEFAULT_SERVER_NAME").trimmed() : QString();
    m_serverLabel->setText(address.isEmpty() ? i18n("No server selected")
        : i18n("Server: %1 (%2)%3", friendlyName.isEmpty() ? i18n("Private server") : friendlyName, address,
            plaintextRemote ? i18n(" (unencrypted; use only on a trusted network)") : QString()));
    m_connectStatus->setText(!message.isEmpty() ? message : state == ConnectionState::Connecting ? i18n("Connecting…")
        : state == ConnectionState::Failed ? i18n("Connection failed. Retry or change server.")
        : state == ConnectionState::Connected ? i18n("Connected.") : i18n("Ready to connect."));
    m_connectButton->setEnabled(state != ConnectionState::Connecting && !address.isEmpty());
    m_connectButton->setText(state == ConnectionState::Failed ? i18nc("@action:button", "Retry")
        : i18nc("@action:button", "Connect"));
    m_changeServerButton->setEnabled(state != ConnectionState::Connecting);
    m_endpoint->setEnabled(state != ConnectionState::Connecting);
}

void OnlineWidget::beginConnection()
{
    if (m_connectionState == ConnectionState::Connecting) return;
    setConnectionState(ConnectionState::Connecting);
    m_connectTimeout->start();
    m_coordinator.connectToService(m_endpoint->text());
}

void OnlineWidget::startAutomation(const QJsonObject &config)
{
    const auto role = config.value(QStringLiteral("role")).toString();
    const auto endpoint = config.value(QStringLiteral("endpoint")).toString();
    const auto joinCodeFile = config.value(QStringLiteral("joinCodeFile")).toString();
    const auto logDirectory = config.value(QStringLiteral("logDirectory")).toString();
    const int expectedMembers = config.value(QStringLiteral("expectedMembers")).toInt();
    const int expectedPlayers = config.value(QStringLiteral("expectedPlayers")).toInt();
    const int matches = config.value(QStringLiteral("matches")).toInt();
    if ((role != QLatin1String("owner") && role != QLatin1String("joiner")) || endpoint.isEmpty()
        || joinCodeFile.isEmpty() || logDirectory.isEmpty() || expectedMembers < 2 || expectedMembers > 8
        || expectedPlayers < expectedMembers || expectedPlayers > 8
        || matches < 1 || matches > 2 || !config.value(QStringLiteral("scriptedShots")).isArray()) {
        failAutomation(QStringLiteral("Invalid online automation configuration."));
        return;
    }
    m_automation = config;
    m_endpoint->setText(endpoint);
    m_name->setText(config.value(QStringLiteral("displayName")).toString());
    m_color->setText(config.value(QStringLiteral("color")).toString());
    QDir().mkpath(logDirectory);
    m_coordinator.connectToService(endpoint);
}

void OnlineWidget::advanceAutomation(const QJsonObject &state)
{
    if (m_automation.isEmpty()) return;
    m_automationMutationPending = false;
    const auto role = m_automation.value(QStringLiteral("role")).toString();
    const auto phase = state.value(QStringLiteral("phase")).toString();
    const auto members = state.value(QStringLiteral("members")).toArray();
    const auto players = state.value(QStringLiteral("players")).toArray();
    if (role == QLatin1String("owner")) {
        QFile joinCode(m_automation.value(QStringLiteral("joinCodeFile")).toString());
        if (joinCode.open(QIODevice::WriteOnly | QIODevice::Truncate))
            joinCode.write(state.value(QStringLiteral("joinCode")).toString().toUtf8());
    }
    if (phase == QLatin1String("Results")) {
        m_automationStartRequested = false;
        const auto matchId = state.value(QStringLiteral("latestResult")).toObject().value(QStringLiteral("matchId")).toString();
        if (!matchId.isEmpty() && !m_automationReturnedMatches.contains(matchId)) {
            m_automationReturnedMatches.insert(matchId);
            ++m_automationCompletedMatches;
            m_coordinator.returnToLobby();
        }
        return;
    }
    if (phase != QLatin1String("Open")) return;
    if (m_automationCompletedMatches >= m_automation.value(QStringLiteral("matches")).toInt()) {
        QTimer::singleShot(250, qApp, &QApplication::quit);
        return;
    }
    if (members.size() != m_automation.value(QStringLiteral("expectedMembers")).toInt()) return;

    int localPlayers = 0;
    bool localReady = false;
    bool allReady = true;
    for (const auto &value : members) {
        const auto member = value.toObject();
        const bool ready = member.value(QStringLiteral("ready")).toBool();
        allReady = allReady && ready;
        if (member.value(QStringLiteral("memberId")).toString() == m_coordinator.memberId()) localReady = ready;
    }
    for (const auto &value : players)
        if (value.toObject().value(QStringLiteral("ownerMemberId")).toString() == m_coordinator.memberId()) ++localPlayers;

    const auto additionalPlayers = m_automation.value(QStringLiteral("additionalPlayers")).toArray();
    if (m_automationAddedPlayers < additionalPlayers.size()) {
        if (localPlayers == m_automationAddedPlayers + 1 && !m_automationMutationPending) {
            const auto player = additionalPlayers.at(m_automationAddedPlayers).toObject();
            ++m_automationAddedPlayers;
            m_automationMutationPending = true;
            m_coordinator.addPlayer(player.value(QStringLiteral("displayName")).toString(), player.value(QStringLiteral("color")).toString());
        }
        return;
    }
    if (players.size() != m_automation.value(QStringLiteral("expectedPlayers")).toInt()) return;
    if (!localReady && !m_automationMutationPending) {
        m_automationMutationPending = true;
        m_coordinator.setReady(true);
        return;
    }
    if (role == QLatin1String("owner") && allReady && !m_automationMutationPending && !m_automationStartRequested) {
        m_automationMutationPending = true;
        m_automationStartRequested = true;
        m_coordinator.startMatch();
    }
}

void OnlineWidget::failAutomation(const QString &reason)
{
    qCritical() << reason;
    if (!m_automation.isEmpty()) {
        QFile error(QDir(m_automation.value(QStringLiteral("logDirectory")).toString())
            .filePath(QStringLiteral("automation-error.txt")));
        if (error.open(QIODevice::WriteOnly | QIODevice::Truncate)) error.write(reason.toUtf8());
    }
    QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(3); });
}

void OnlineWidget::showEntry()
{
    setConnectionState(ConnectionState::Connected);
    KConfigGroup onlineConfig(KSharedConfig::openConfig(), QStringLiteral("Online"));
    const auto endpoint = m_endpoint->text().trimmed();
    onlineConfig.writeEntry("lastSuccessfulEndpoint", endpoint);
    onlineConfig.deleteEntry("endpoint");
    auto recent = onlineConfig.readEntry("recentServers", QStringList());
    recent.removeAll(endpoint);
    recent.prepend(endpoint);
    while (recent.size() > 5) recent.removeLast();
    onlineConfig.writeEntry("recentServers", recent);
    m_recentServers->clear();
    m_recentServers->addItems(recent);
    onlineConfig.sync();
    m_entryRequestPending = false;
    m_createButton->setEnabled(true); m_joinButton->setEnabled(true);
    m_entryStatus->setText(i18n("Connected. Create a lobby or enter a friend's join code."));
    m_pages->setCurrentIndex(1);
    if (m_automation.isEmpty() || m_automationCreateOrJoinSent) return;
    m_automationCreateOrJoinSent = true;
    if (m_automation.value(QStringLiteral("role")) == QLatin1String("owner")) {
        m_coordinator.createLobby(m_name->text(), m_color->text(), m_automation.value(QStringLiteral("courseId")).toString());
        return;
    }
    auto *timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this, timer] {
        QFile file(m_automation.value(QStringLiteral("joinCodeFile")).toString());
        if (!file.open(QIODevice::ReadOnly)) return;
        const auto joinCode = QString::fromUtf8(file.readAll()).trimmed();
        if (joinCode.isEmpty()) return;
        timer->stop(); timer->deleteLater();
        m_coordinator.joinLobby(joinCode, m_name->text(), m_color->text());
    });
    timer->start();
}

void OnlineWidget::showLobby(const QJsonObject &state)
{
    m_state = state;
    setConnectionState(state.value(QStringLiteral("phase")) == QLatin1String("Playing")
        ? ConnectionState::InMatch : ConnectionState::InLobby);
    m_entryRequestPending = false;
    m_createButton->setEnabled(true); m_joinButton->setEnabled(true);
    if (state.value(QStringLiteral("phase")) == QLatin1String("Results")) {
        showResults(state);
        advanceAutomation(state);
        return;
    }
    if (!m_matchActive) m_pages->setCurrentIndex(2);
    m_lobbySummary->setText(i18n("Join code: %1", state.value(QStringLiteral("joinCode")).toString()));
    m_players->clear();
    bool localReady = false, allReady = true;
    const auto members = state.value(QStringLiteral("members")).toArray();
    for (const auto &value : members) {
        const auto member = value.toObject();
        const bool ready = member.value(QStringLiteral("ready")).toBool();
        allReady = allReady && ready;
        if (member.value(QStringLiteral("memberId")).toString() == m_coordinator.memberId()) localReady = ready;
        const bool memberOwner = member.value(QStringLiteral("memberId")).toString() == state.value(QStringLiteral("ownerMemberId")).toString();
        auto *headingItem = new QListWidgetItem(i18n("%1%2 — %3", member.value(QStringLiteral("displayName")).toString(),
            memberOwner ? i18n(" (owner)") : QString(), ready ? i18n("Ready") : i18n("Not ready")), m_players);
        headingItem->setFlags(Qt::ItemIsEnabled);
        QFont font = headingItem->font(); font.setBold(true); headingItem->setFont(font);
        for (const auto &playerValue : state.value(QStringLiteral("players")).toArray()) {
            const auto player = playerValue.toObject();
            if (player.value(QStringLiteral("ownerMemberId")).toString() != member.value(QStringLiteral("memberId")).toString()) continue;
            auto *item = new QListWidgetItem(i18n("    %1 — %2", player.value(QStringLiteral("displayName")).toString(),
                player.value(QStringLiteral("color")).toString()), m_players);
            const QColor color = colorFromRgba(player.value(QStringLiteral("color")).toString());
            if (color.isValid()) {
                QPixmap swatch(16, 16); swatch.fill(color); item->setIcon(QIcon(swatch));
            }
            item->setData(Qt::UserRole, player.value(QStringLiteral("playerId")));
            item->setData(Qt::UserRole + 1, player.value(QStringLiteral("displayName")));
            item->setData(Qt::UserRole + 2, player.value(QStringLiteral("color")));
            item->setData(Qt::UserRole + 3, member.value(QStringLiteral("memberId")));
        }
    }
    const auto players = state.value(QStringLiteral("players")).toArray();
    int localPlayers = 0;
    for (const auto &value : players) {
        const auto player = value.toObject();
        if (player.value(QStringLiteral("ownerMemberId")).toString() == m_coordinator.memberId()) ++localPlayers;
    }
    for (int i = 0; i < m_players->count(); ++i) m_players->item(i)->setData(Qt::UserRole + 4, localPlayers);
    const bool open = state.value(QStringLiteral("phase")) == QLatin1String("Open");
    const bool owner = state.value(QStringLiteral("ownerMemberId")).toString() == m_coordinator.memberId();
    m_ready->setText(localReady ? i18nc("@action:button", "Unready") : i18nc("@action:button", "Ready"));
    m_ready->setEnabled(open);
    m_start->setEnabled(open && owner && members.size() >= 2 && players.size() >= 2 && players.size() <= 8 && allReady);
    m_addPlayer->setEnabled(open && players.size() < 8);
    m_editPlayer->setEnabled(false); m_removePlayer->setEnabled(false);
    const auto courseId = state.value(QStringLiteral("selectedCourseId")).toString();
    const auto index = m_lobbyCourse->findData(courseId);
    if (index >= 0) {
        const QSignalBlocker blocker(m_lobbyCourse);
        m_lobbyCourse->setCurrentIndex(index);
    }
    m_lobbyCourse->setEnabled(open && owner);
    if (open) m_lobbyStatus->setText(players.size() < 2
        ? i18n("2–8 players are required. Add another player or invite another member.")
        : i18n("Ready every member on this revision, then the owner can start."));
    advanceAutomation(state);
}

void OnlineWidget::showResults(const QJsonObject &state)
{
    m_pages->setCurrentIndex(3);
    if (m_matchActive) {
        m_matchActive = false;
        Q_EMIT matchEnded();
    }
    const auto result = state.value(QStringLiteral("latestResult")).toObject();
    QString text = i18n("Status: %1\nCourse: %2\n", result.value(QStringLiteral("status")).toString(),
                        result.value(QStringLiteral("courseId")).toString());
    const auto roster = result.value(QStringLiteral("roster")).toArray();
    const auto totals = result.value(QStringLiteral("totals")).toArray();
    for (qsizetype i = 0; i < roster.size(); ++i)
        text += i18n("%1: %2\n", roster[i].toObject().value(QStringLiteral("displayName")).toString(),
                     i < totals.size() ? totals.at(i).toInt() : 0);
    if (result.value(QStringLiteral("status")) == QLatin1String("Interrupted"))
        text += i18n("Reason: %1\n", result.value(QStringLiteral("reason")).toString());
    m_result->setPlainText(text);
}

void OnlineWidget::leaveOnline()
{
    m_connectTimeout->stop();
    if (m_matchActive) {
        m_matchActive = false;
        Q_EMIT matchEnded();
    }
    m_coordinator.disconnectFromService();
    m_state = {};
    setConnectionState(ConnectionState::Disconnected);
    m_pages->setCurrentIndex(0);
}

void OnlineWidget::savePreferences()
{
    KConfigGroup onlineConfig(KSharedConfig::openConfig(), QStringLiteral("Online"));
    onlineConfig.writeEntry("displayName", m_name->text().trimmed());
    onlineConfig.writeEntry("color", m_color->text().trimmed());
    onlineConfig.sync();
}
