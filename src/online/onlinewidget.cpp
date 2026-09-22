// SPDX-License-Identifier: GPL-2.0-or-later
#include "onlinewidget.h"
#include "session/sessioncontroller.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QComboBox>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
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
    m_endpoint->setText(onlineConfig.readEntry("endpoint", QStringLiteral("ws://127.0.0.1:3011")));
    connectForm->addRow(i18nc("@label", "Server:"), m_endpoint);
    connectLayout->addLayout(connectForm);
    m_connectStatus = new QLabel(i18n("Connect to a Kolf multiplayer service."), connectPage);
    m_connectStatus->setWordWrap(true);
    connectLayout->addWidget(m_connectStatus);
    auto *connectButtons = new QHBoxLayout;
    auto *connectButton = new QPushButton(i18nc("@action:button", "Connect"), connectPage);
    auto *cancelButton = new QPushButton(i18nc("@action:button", "Cancel"), connectPage);
    connectButtons->addStretch(); connectButtons->addWidget(connectButton); connectButtons->addWidget(cancelButton);
    connectLayout->addStretch(); connectLayout->addLayout(connectButtons);
    m_pages->addWidget(connectPage);

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
    auto *createButton = new QPushButton(i18nc("@action:button", "Create Lobby"), entryPage);
    entryLayout->addWidget(createButton);
    auto *joinRow = new QHBoxLayout;
    m_joinCode = new QLineEdit(entryPage); m_joinCode->setMaxLength(12);
    m_joinCode->setPlaceholderText(i18nc("@info:placeholder", "Join code"));
    auto *joinButton = new QPushButton(i18nc("@action:button", "Join Lobby"), entryPage);
    joinRow->addWidget(m_joinCode); joinRow->addWidget(joinButton);
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
    lobbyLayout->addWidget(m_lobbySummary);
    lobbyLayout->addWidget(new QLabel(i18n("Members"), lobbyPage));
    m_members = new QListWidget(lobbyPage); lobbyLayout->addWidget(m_members);
    lobbyLayout->addWidget(new QLabel(i18n("Players"), lobbyPage));
    m_players = new QListWidget(lobbyPage); lobbyLayout->addWidget(m_players);
    auto *playerButtons = new QHBoxLayout;
    m_addPlayer = new QPushButton(i18nc("@action:button", "Add local player"), lobbyPage);
    m_editPlayer = new QPushButton(i18nc("@action:button", "Edit player"), lobbyPage);
    m_removePlayer = new QPushButton(i18nc("@action:button", "Remove player"), lobbyPage);
    playerButtons->addWidget(m_addPlayer); playerButtons->addWidget(m_editPlayer); playerButtons->addWidget(m_removePlayer);
    lobbyLayout->addLayout(playerButtons);
    auto *courseRow = new QFormLayout;
    m_lobbyCourse = new QComboBox(lobbyPage);
    courseRow->addRow(i18nc("@label", "Course:"), m_lobbyCourse); lobbyLayout->addLayout(courseRow);
    m_lobbyStatus = new QLabel(lobbyPage); m_lobbyStatus->setWordWrap(true); lobbyLayout->addWidget(m_lobbyStatus);
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

    connect(connectButton, &QPushButton::clicked, this, [this] { m_coordinator.connectToService(m_endpoint->text()); });
    connect(cancelButton, &QPushButton::clicked, this, &OnlineWidget::leaveRequested);
    connect(createButton, &QPushButton::clicked, this, [this] {
        savePreferences(); m_coordinator.createLobby(m_name->text(), m_color->text(), m_createCourse->currentData().toString());
    });
    connect(joinButton, &QPushButton::clicked, this, [this] {
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
    connect(&m_coordinator, &OnlineCoordinator::connected, this, &OnlineWidget::showEntry);
    connect(&m_coordinator, &OnlineCoordinator::serviceChanged, this, [this](const QJsonObject &hello) {
        m_createCourse->clear(); m_lobbyCourse->clear();
        for (const auto &value : hello.value(QStringLiteral("courses")).toArray()) {
            const auto course = value.toObject();
            m_createCourse->addItem(course.value(QStringLiteral("displayName")).toString(), course.value(QStringLiteral("courseId")));
            m_lobbyCourse->addItem(course.value(QStringLiteral("displayName")).toString(), course.value(QStringLiteral("courseId")));
        }
    });
    connect(&m_coordinator, &OnlineCoordinator::connectionClosed, this, [this] {
        closeMatch();
        m_connectStatus->setText(i18n("Disconnected from the online service."));
        m_pages->setCurrentIndex(0);
    });
    connect(&m_coordinator, &OnlineCoordinator::lobbyChanged, this, &OnlineWidget::showLobby);
    connect(&m_coordinator, &OnlineCoordinator::lobbyClosed, this, [this](const QString &reason) {
        closeMatch(); showEntry(); m_entryStatus->setText(reason);
    });
    connect(&m_coordinator, &OnlineCoordinator::matchPrepared, this, [this](const QJsonObject &config) {
        closeMatch();
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
        m_matchController = new Kolf::Session::SessionController(sessionConfig, m_coordinator.networkClient(), m_pages);
        resize(900, 760);
        m_pages->addWidget(m_matchController);
        m_pages->setCurrentWidget(m_matchController);
    });
    connect(&m_coordinator, &OnlineCoordinator::statusChanged, m_lobbyStatus, &QLabel::setText);
    connect(&m_coordinator, &OnlineCoordinator::failed, this, [this](const QString &reason) {
        m_connectStatus->setText(reason); m_entryStatus->setText(reason); m_lobbyStatus->setText(reason);
        if (!m_automation.isEmpty()) failAutomation(reason);
    });
}

OnlineWidget::~OnlineWidget()
{
    closeMatch();
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
    if (role == QLatin1String("owner") && allReady && !m_automationMutationPending) {
        m_automationMutationPending = true;
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
    KConfigGroup onlineConfig(KSharedConfig::openConfig(), QStringLiteral("Online"));
    onlineConfig.writeEntry("endpoint", m_endpoint->text().trimmed());
    onlineConfig.sync();
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
    if (state.value(QStringLiteral("phase")) == QLatin1String("Results")) {
        showResults(state);
        advanceAutomation(state);
        return;
    }
    const bool showingMatch = m_matchController
        && (state.value(QStringLiteral("phase")) == QLatin1String("Preparing")
            || state.value(QStringLiteral("phase")) == QLatin1String("Playing"));
    if (showingMatch) m_pages->setCurrentWidget(m_matchController);
    else m_pages->setCurrentIndex(2);
    m_lobbySummary->setText(i18n("Join code: %1    Revision: %2    State: %3",
        state.value(QStringLiteral("joinCode")).toString(), state.value(QStringLiteral("lobbyRevision")).toInt(),
        state.value(QStringLiteral("phase")).toString()));
    m_members->clear();
    bool localReady = false, allReady = true;
    const auto members = state.value(QStringLiteral("members")).toArray();
    for (const auto &value : members) {
        const auto member = value.toObject();
        const bool ready = member.value(QStringLiteral("ready")).toBool();
        allReady = allReady && ready;
        if (member.value(QStringLiteral("memberId")).toString() == m_coordinator.memberId()) localReady = ready;
        const bool memberOwner = member.value(QStringLiteral("memberId")).toString() == state.value(QStringLiteral("ownerMemberId")).toString();
        m_members->addItem(i18n("%1%2 — %3", member.value(QStringLiteral("displayName")).toString(),
            memberOwner ? i18n(" (owner)") : QString(), ready ? i18n("Ready") : i18n("Not ready")));
    }
    m_players->clear();
    const auto players = state.value(QStringLiteral("players")).toArray();
    int localPlayers = 0;
    for (const auto &value : players) {
        const auto player = value.toObject();
        const auto ownerId = player.value(QStringLiteral("ownerMemberId")).toString();
        QString ownerName = ownerId;
        for (const auto &memberValue : members) {
            const auto member = memberValue.toObject();
            if (member.value(QStringLiteral("memberId")).toString() == ownerId) ownerName = member.value(QStringLiteral("displayName")).toString();
        }
        auto *item = new QListWidgetItem(i18n("%1 — %2 — %3", player.value(QStringLiteral("displayName")).toString(),
            player.value(QStringLiteral("color")).toString(), ownerName), m_players);
        item->setData(Qt::UserRole, player.value(QStringLiteral("playerId")));
        item->setData(Qt::UserRole + 1, player.value(QStringLiteral("displayName")));
        item->setData(Qt::UserRole + 2, player.value(QStringLiteral("color")));
        item->setData(Qt::UserRole + 3, ownerId);
        if (ownerId == m_coordinator.memberId()) ++localPlayers;
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
    closeMatch();
    m_pages->setCurrentIndex(3);
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

void OnlineWidget::closeMatch()
{
    if (!m_matchController) return;
    m_pages->removeWidget(m_matchController);
    delete m_matchController;
    m_matchController = nullptr;
}

void OnlineWidget::leaveOnline()
{
    closeMatch();
    m_coordinator.disconnectFromService();
    m_state = {};
    m_connectStatus->setText(i18n("Connect to a Kolf multiplayer service."));
    m_pages->setCurrentIndex(0);
}

void OnlineWidget::savePreferences()
{
    KConfigGroup onlineConfig(KSharedConfig::openConfig(), QStringLiteral("Online"));
    onlineConfig.writeEntry("displayName", m_name->text().trimmed());
    onlineConfig.writeEntry("color", m_color->text().trimmed());
    onlineConfig.sync();
}
