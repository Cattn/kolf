// SPDX-License-Identifier: GPL-2.0-or-later
#include "onlinewindow.h"
#include "session/sessioncontroller.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTextEdit>
#include <QVBoxLayout>

using namespace Kolf::Online;

OnlineWindow::OnlineWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_coordinator(this)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(i18nc("@title:window", "Kolf Online"));
    resize(520, 430);
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
    m_createCourse->addItem(i18n("Classic"), QStringLiteral("classic"));
    m_createCourse->addItem(i18n("Easy"), QStringLiteral("easy"));
    m_createCourse->addItem(i18n("Practice"), QStringLiteral("practice"));
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
    m_members = new QListWidget(lobbyPage); lobbyLayout->addWidget(m_members);
    auto *courseRow = new QFormLayout;
    m_lobbyCourse = new QComboBox(lobbyPage);
    m_lobbyCourse->addItem(i18n("Classic"), QStringLiteral("classic"));
    m_lobbyCourse->addItem(i18n("Easy"), QStringLiteral("easy"));
    m_lobbyCourse->addItem(i18n("Practice"), QStringLiteral("practice"));
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
    connect(cancelButton, &QPushButton::clicked, this, &QWidget::close);
    connect(createButton, &QPushButton::clicked, this, [this] {
        savePreferences(); m_coordinator.createLobby(m_name->text(), m_color->text(), m_createCourse->currentData().toString());
    });
    connect(joinButton, &QPushButton::clicked, this, [this] {
        savePreferences(); m_coordinator.joinLobby(m_joinCode->text(), m_name->text(), m_color->text());
    });
    connect(entryDisconnect, &QPushButton::clicked, &m_coordinator, &OnlineCoordinator::disconnectFromService);
    connect(lobbyDisconnect, &QPushButton::clicked, &m_coordinator, &OnlineCoordinator::disconnectFromService);
    connect(m_ready, &QPushButton::clicked, this, [this] {
        bool ready = false;
        for (const auto &value : m_state.value(QStringLiteral("members")).toArray()) {
            const auto member = value.toObject();
            if (member.value(QStringLiteral("memberId")).toString() == m_coordinator.memberId()) ready = member.value(QStringLiteral("ready")).toBool();
        }
        m_coordinator.setReady(!ready);
    });
    connect(m_start, &QPushButton::clicked, &m_coordinator, &OnlineCoordinator::startMatch);
    connect(m_lobbyCourse, &QComboBox::activated, this, [this](int) {
        if (!m_state.isEmpty()) m_coordinator.setCourse(m_lobbyCourse->currentData().toString());
    });
    connect(returnButton, &QPushButton::clicked, &m_coordinator, &OnlineCoordinator::returnToLobby);
    connect(&m_coordinator, &OnlineCoordinator::connected, this, &OnlineWindow::showEntry);
    connect(&m_coordinator, &OnlineCoordinator::connectionClosed, this, [this] {
        closeMatch();
        m_connectStatus->setText(i18n("Disconnected from the online service."));
        m_pages->setCurrentIndex(0);
    });
    connect(&m_coordinator, &OnlineCoordinator::lobbyChanged, this, &OnlineWindow::showLobby);
    connect(&m_coordinator, &OnlineCoordinator::lobbyClosed, this, [this](const QString &reason) {
        closeMatch(); showEntry(); m_entryStatus->setText(reason);
    });
    connect(&m_coordinator, &OnlineCoordinator::matchPrepared, this, [this](const QJsonObject &config) {
        closeMatch();
        m_matchController = new Kolf::Session::SessionController(config, m_coordinator.networkClient(), m_pages);
        resize(900, 760);
        m_pages->addWidget(m_matchController);
        m_pages->setCurrentWidget(m_matchController);
    });
    connect(&m_coordinator, &OnlineCoordinator::statusChanged, m_lobbyStatus, &QLabel::setText);
    connect(&m_coordinator, &OnlineCoordinator::failed, this, [this](const QString &reason) {
        m_connectStatus->setText(reason); m_entryStatus->setText(reason); m_lobbyStatus->setText(reason);
    });
}

void OnlineWindow::showEntry()
{
    KConfigGroup onlineConfig(KSharedConfig::openConfig(), QStringLiteral("Online"));
    onlineConfig.writeEntry("endpoint", m_endpoint->text().trimmed());
    onlineConfig.sync();
    m_entryStatus->setText(i18n("Connected. Create a lobby or enter a friend's join code."));
    m_pages->setCurrentIndex(1);
}

void OnlineWindow::showLobby(const QJsonObject &state)
{
    m_state = state;
    if (state.value(QStringLiteral("phase")) == QLatin1String("Results")) { showResults(state); return; }
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
        m_members->addItem(i18n("%1 — %2", member.value(QStringLiteral("displayName")).toString(), ready ? i18n("Ready") : i18n("Not ready")));
    }
    const bool open = state.value(QStringLiteral("phase")) == QLatin1String("Open");
    const bool owner = state.value(QStringLiteral("ownerMemberId")).toString() == m_coordinator.memberId();
    m_ready->setText(localReady ? i18nc("@action:button", "Unready") : i18nc("@action:button", "Ready"));
    m_ready->setEnabled(open);
    m_start->setEnabled(open && owner && members.size() == 2 && allReady);
    const auto courseId = state.value(QStringLiteral("selectedCourseId")).toString();
    const auto index = m_lobbyCourse->findData(courseId);
    if (index >= 0) {
        const QSignalBlocker blocker(m_lobbyCourse);
        m_lobbyCourse->setCurrentIndex(index);
    }
    m_lobbyCourse->setEnabled(open && owner);
    if (open) m_lobbyStatus->setText(i18n("Ready both players on this revision, then the owner can start."));
}

void OnlineWindow::showResults(const QJsonObject &state)
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

void OnlineWindow::closeMatch()
{
    if (!m_matchController) return;
    m_pages->removeWidget(m_matchController);
    delete m_matchController;
    m_matchController = nullptr;
}

void OnlineWindow::savePreferences()
{
    KConfigGroup onlineConfig(KSharedConfig::openConfig(), QStringLiteral("Online"));
    onlineConfig.writeEntry("displayName", m_name->text().trimmed());
    onlineConfig.writeEntry("color", m_color->text().trimmed());
    onlineConfig.sync();
}
