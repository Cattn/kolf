// SPDX-License-Identifier: GPL-2.0-or-later
#include "onlinewidget.h"
#include "lobbyview.h"
#include "color.h"
#include "playerprofileeditor.h"
#include "serverlist.h"
#include "invitelink.h"
#include "results/summary.h"
#include "ui/resultsview.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <KSharedConfig>

#include <QComboBox>
#include <QActionGroup>
#include <QMenu>
#include <QToolButton>
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
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
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QUrl>
#include <QPixmap>
#include <QBuffer>
#include <QGroupBox>
#include <QPainter>
#include <QRegularExpression>

using namespace Kolf::Online;

static QString courseLabel(const QJsonObject &course)
{
    return i18n("%1 — %2 holes, par %3", course.value(QStringLiteral("displayName")).toString(),
        course.value(QStringLiteral("holes")).toInt(), course.value(QStringLiteral("totalPar")).toInt());
}

static void addCourse(QComboBox *combo, const QJsonObject &course)
{
    combo->addItem(courseLabel(course), course.value(QStringLiteral("courseId")));
    combo->setItemData(combo->count() - 1,
        i18n("%1\nBy %2\n%3 holes, par %4", course.value(QStringLiteral("displayName")).toString(),
            course.value(QStringLiteral("author")).toString().isEmpty() ? i18n("Unknown author")
                : course.value(QStringLiteral("author")).toString(),
            course.value(QStringLiteral("holes")).toInt(), course.value(QStringLiteral("totalPar")).toInt()), Qt::ToolTipRole);
}

static bool editProfile(QWidget *parent, const QString &title, QString &name, QString &mode, QString &customColor)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&dialog);
    auto *editor = new PlayerProfileEditor(&dialog);
    editor->setProfile(name, mode, customColor);
    layout->addWidget(editor);
    auto *error = new QLabel(&dialog);
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!editor->isValid()) { error->setText(i18n("Enter a player name.")); return; }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return false;
    name = editor->playerName(); mode = editor->colorMode(); customColor = editor->customColor();
    return true;
}

OnlineWidget::OnlineWidget(const Kolf::ItemFactory &factory, QWidget *parent)
    : QWidget(parent)
    , m_coordinator(this)
{
    auto *root = new QVBoxLayout(this);
    m_pages = new QStackedWidget(this);
    root->addWidget(m_pages);

    KConfigGroup onlineConfig(KSharedConfig::openConfig(), QStringLiteral("Online"));
    auto *hub = new QWidget(this);
    hub->setObjectName(QStringLiteral("onlineHub"));
    auto *hubLayout = new QVBoxLayout(hub);
    hubLayout->setContentsMargins(20, 14, 20, 14);
    hubLayout->setSpacing(10);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(i18nc("@title", "Play Online"), hub);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.6);
    titleFont.setBold(true);
    title->setFont(titleFont);
    header->addWidget(title);
    header->addStretch();
    m_serverButton = new QToolButton(hub);
    m_serverButton->setObjectName(QStringLiteral("onlineServerMenu"));
    m_serverButton->setIcon(QIcon::fromTheme(QStringLiteral("network-server")));
    m_serverButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_serverButton->setPopupMode(QToolButton::InstantPopup);
    m_serverButton->setAutoRaise(false);
    m_serverMenu = new QMenu(m_serverButton);
    m_serverMenu->setObjectName(QStringLiteral("onlineServerMenuPopup"));
    m_serverButton->setMenu(m_serverMenu);
    header->addWidget(m_serverButton);
    auto *cancelButton = new QPushButton(QIcon::fromTheme(QStringLiteral("go-previous")), i18nc("@action:button", "Back"), hub);
    cancelButton->setObjectName(QStringLiteral("onlineBack"));
    header->addWidget(cancelButton);
    hubLayout->addLayout(header);
    m_connectStatus = new QLabel(hub);
    m_connectStatus->setObjectName(QStringLiteral("onlineStatus"));
    m_connectStatus->setWordWrap(true);
    hubLayout->addWidget(m_connectStatus);
    m_serverNotice = new QLabel(hub);
    m_serverNotice->setObjectName(QStringLiteral("onlineServerNotice"));
    m_serverNotice->setWordWrap(true);
    m_serverNotice->setTextFormat(Qt::RichText);
    m_serverNotice->setOpenExternalLinks(true);
    m_serverNotice->setFrameShape(QFrame::StyledPanel);
    m_serverNotice->setMargin(8);
    m_serverNotice->hide();
    hubLayout->addWidget(m_serverNotice);

    m_serverControls = new QGroupBox(i18n("Server"), hub);
    m_serverControls->setObjectName(QStringLiteral("onlineServerControls"));
    auto *serverLayout = new QFormLayout(m_serverControls);
    m_endpoint = new QLineEdit(m_serverControls);
    m_endpoint->setObjectName(QStringLiteral("onlineAddress"));
    m_endpoint->setPlaceholderText(i18nc("@info:placeholder", "kolf.pro or 192.168.1.20"));
    auto *addressRow = new QHBoxLayout;
    addressRow->addWidget(m_endpoint, 1);
    m_connectButton = new QPushButton(i18nc("@action:button", "Connect"), m_serverControls);
    m_connectButton->setObjectName(QStringLiteral("onlineConnect"));
    addressRow->addWidget(m_connectButton);
    serverLayout->addRow(i18nc("@label", "Address:"), addressRow);
    hubLayout->addWidget(m_serverControls);

    auto *profileRow = new QHBoxLayout;
    profileRow->addWidget(new QLabel(i18nc("@label the local player", "You:"), hub));
    m_profileChip = new QLabel(hub);
    m_profileChip->setObjectName(QStringLiteral("onlineProfile"));
    profileRow->addWidget(m_profileChip);
    auto *editProfileButton = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")), i18nc("@action:button", "Edit"), hub);
    editProfileButton->setObjectName(QStringLiteral("onlineEditProfile"));
    editProfileButton->setAccessibleName(i18n("Edit your online name and color"));
    profileRow->addWidget(editProfileButton);
    profileRow->addStretch();
    hubLayout->addLayout(profileRow);
    m_profile = new PlayerProfileEditor(hub);
    m_profile->hide();
    m_profile->setProfile(onlineConfig.readEntry("displayName", QStringLiteral("Player")),
        onlineConfig.readEntry("colorMode", QStringLiteral("auto")),
        onlineConfig.readEntry("customColor", QStringLiteral("#0072b2ff")));
    m_createCourse = new QComboBox(hub);
    m_createCourse->hide();

    auto *cards = new QHBoxLayout;
    m_hostCard = new QGroupBox(i18n("Host a game"), hub);
    auto *hostLayout = new QVBoxLayout(m_hostCard);
    auto *hostText = new QLabel(i18n("Create a lobby, choose the course, and share its code with friends."), m_hostCard);
    hostText->setWordWrap(true);
    hostLayout->addWidget(hostText);
    hostLayout->addStretch();
    m_createButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action:button", "Host Game"), m_hostCard);
    m_createButton->setObjectName(QStringLiteral("onlineHost"));
    hostLayout->addWidget(m_createButton);
    cards->addWidget(m_hostCard, 1);
    m_joinCard = new QGroupBox(i18n("Join a game"), hub);
    auto *joinLayout = new QVBoxLayout(m_joinCard);
    auto *joinText = new QLabel(i18n("Enter the code or invite link your host shared."), m_joinCard);
    joinText->setWordWrap(true);
    joinLayout->addWidget(joinText);
    auto *joinRow = new QHBoxLayout;
    m_joinCode = new QLineEdit(m_joinCard);
    m_joinCode->setObjectName(QStringLiteral("onlineJoinCode"));
    m_joinCode->setMaxLength(200);
    m_joinCode->setPlaceholderText(i18nc("@info:placeholder", "e.g. K7QF-2MXA"));
    m_joinCode->setAccessibleName(i18n("Join code or invite link"));
    m_joinButton = new QPushButton(QIcon::fromTheme(QStringLiteral("go-next")), i18nc("@action:button", "Join"), m_joinCard);
    m_joinButton->setObjectName(QStringLiteral("onlineJoin"));
    joinRow->addWidget(m_joinCode, 1);
    joinRow->addWidget(m_joinButton);
    joinLayout->addLayout(joinRow);
    joinLayout->addStretch();
    cards->addWidget(m_joinCard, 1);
    hubLayout->addLayout(cards);
    m_entryStatus = new QLabel(hub);
    m_entryStatus->setObjectName(QStringLiteral("onlineEntryStatus"));
    m_entryStatus->setWordWrap(true);
    hubLayout->addWidget(m_entryStatus);
    hubLayout->addStretch();
    QWidget::setTabOrder(m_createButton, m_joinCode);
    QWidget::setTabOrder(m_joinCode, m_joinButton);
    m_pages->addWidget(hub);
    m_connectTimeout = new QTimer(this);
    m_connectTimeout->setSingleShot(true);
    m_connectTimeout->setInterval(10000);
    updateProfileChip();
    setConnectionState(ConnectionState::Disconnected);
    connect(editProfileButton, &QPushButton::clicked, this, [this] {
        QString name = m_profile->playerName(), mode = m_profile->colorMode(), customColor = m_profile->customColor();
        if (!editProfile(this, i18nc("@title:window", "Your Online Player"), name, mode, customColor)) return;
        m_profile->setProfile(name, mode, customColor);
        savePreferences();
        updateProfileChip();
    });
    connect(m_joinCode, &QLineEdit::returnPressed, this, [this] { if (m_joinButton->isEnabled()) m_joinButton->click(); });

    m_lobby = new LobbyView(factory, m_coordinator, this);
    m_pages->addWidget(m_lobby);
    connect(m_lobby, &LobbyView::leaveRequested, this, [this] {
        m_state = {};
        m_rematchRequested = m_rematchReadySent = false;
        showEntry();
    });

    auto *resultsPage = new QWidget(this);
    auto *resultsLayout = new QVBoxLayout(resultsPage);
    m_result = new Kolf::Ui::ResultsView(resultsPage); resultsLayout->addWidget(m_result, 1);
    auto *resultButtons = new QHBoxLayout;
    auto *rematchButton = new QPushButton(i18nc("@action:button", "Rematch"), resultsPage);
    auto *returnButton = new QPushButton(i18nc("@action:button", "Return to Lobby"), resultsPage);
    auto *resultsChangeServer = new QPushButton(i18nc("@action:button", "Change Server"), resultsPage);
    auto *resultsLeave = new QPushButton(i18nc("@action:button", "Leave Online"), resultsPage);
    resultButtons->addWidget(rematchButton); resultButtons->addWidget(returnButton);
    resultButtons->addStretch(); resultButtons->addWidget(resultsChangeServer); resultButtons->addWidget(resultsLeave);
    resultsLayout->addLayout(resultButtons);
    m_pages->addWidget(resultsPage);

    connect(m_connectButton, &QPushButton::clicked, this, &OnlineWidget::beginConnection);
    connect(m_serverMenu, &QMenu::aboutToShow, this, &OnlineWidget::rebuildServerMenu);
    connect(m_endpoint, &QLineEdit::textChanged, this, [this] {
        if (m_connectionState != ConnectionState::Connecting) setConnectionState(m_connectionState);
    });
    connect(cancelButton, &QPushButton::clicked, this, [this] {
        Q_EMIT leaveRequested();
    });
    connect(m_connectTimeout, &QTimer::timeout, this, [this] {
        const auto message = i18n("The server did not respond in time. Check its address and try again.");
        setConnectionState(ConnectionState::Failed, message);
        Q_EMIT statusChanged(message);
        m_coordinator.disconnectFromService();
    });
    connect(m_createButton, &QPushButton::clicked, this, [this] {
        if (m_entryRequestPending) return;
        m_entryRequestPending = true;
        m_createButton->setEnabled(false); m_joinButton->setEnabled(false);
        if (!m_profile->isValid()) { m_entryStatus->setText(i18n("Enter a player name.")); m_entryRequestPending = false; m_createButton->setEnabled(true); m_joinButton->setEnabled(true); return; }
        savePreferences(); m_coordinator.createLobby(m_profile->playerName(), m_profile->colorMode(),
            m_profile->customColor(), m_createCourse->currentData().toString());
    });
    connect(m_joinButton, &QPushButton::clicked, this, [this] {
        if (m_entryRequestPending) return;
        m_entryRequestPending = true;
        m_createButton->setEnabled(false); m_joinButton->setEnabled(false);
        if (!m_profile->isValid()) { m_entryStatus->setText(i18n("Enter a player name.")); m_entryRequestPending = false; m_createButton->setEnabled(true); m_joinButton->setEnabled(true); return; }
        const auto invite = parseInviteLink(m_joinCode->text());
        const auto current = normalizeServerAddress(m_endpoint->text()).url.toString();
        if (invite && !invite->serverUrl.isEmpty() && invite->serverUrl != current) {
            m_entryRequestPending = false; m_createButton->setEnabled(true); m_joinButton->setEnabled(true);
            const auto name = m_servers.value(invite->serverUrl, QUrl(invite->serverUrl).host());
            if (KMessageBox::questionTwoActions(this, i18n("This invite is for a game on %1. Switch to that server and join?", name),
                    i18nc("@title:window", "Join on Another Server?"),
                    KGuiItem(i18nc("@action:button", "Switch and Join"), QStringLiteral("network-connect")),
                    KStandardGuiItem::cancel()) == KMessageBox::PrimaryAction)
                openInvite(invite->code, invite->serverUrl);
            return;
        }
        const auto code = invite ? invite->code : normalizeJoinCode(m_joinCode->text());
        if (code.isEmpty()) {
            m_entryStatus->setText(i18n("That doesn't look like a join code. Codes are 6 to 12 letters and numbers."));
            m_entryRequestPending = false; m_createButton->setEnabled(true); m_joinButton->setEnabled(true);
            return;
        }
        savePreferences(); m_coordinator.joinLobby(code, m_profile->playerName(),
            m_profile->colorMode(), m_profile->customColor());
    });
    connect(rematchButton, &QPushButton::clicked, this, [this] {
        m_rematchRequested = true; m_rematchReadySent = false;
        m_coordinator.returnToLobby(true);
    });
    connect(returnButton, &QPushButton::clicked, this, [this] {
        m_rematchRequested = false; m_coordinator.returnToLobby();
    });
    connect(resultsChangeServer, &QPushButton::clicked, this, &OnlineWidget::changeServer);
    connect(resultsLeave, &QPushButton::clicked, this, [this] {
        Q_EMIT leaveRequested();
    });
    connect(&m_coordinator, &OnlineCoordinator::connected, this, [this] {
        setConnectionState(ConnectionState::Connecting, i18n("Verifying the server…"));
        Q_EMIT statusChanged(i18n("Verifying the server…"));
    });
    connect(&m_coordinator, &OnlineCoordinator::serviceChanged, this, [this](const QJsonObject &hello) {
        m_connectTimeout->stop();
        setConnectionState(ConnectionState::Connected);
        m_createCourse->clear();
        for (const auto &value : hello.value(QStringLiteral("courses")).toArray()) {
            const auto course = value.toObject();
            addCourse(m_createCourse, course);
        }
        const int lastCourse = m_createCourse->findData(KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("Online"))
            .readEntry("lastCourseId", QString()));
        if (lastCourse >= 0) m_createCourse->setCurrentIndex(lastCourse);
        m_serverDisplayName = hello.value(QStringLiteral("serverName")).toString().trimmed().left(60);
        m_restartNotice.clear();
        if (!m_pendingInviteCode.isEmpty()) {
            const auto code = m_pendingInviteCode;
            m_pendingInviteCode.clear();
            QTimer::singleShot(0, this, [this, code] { joinCode(code); });
        }
        showEntry();
        showServerNotice(hello);
        Q_EMIT statusChanged(i18n("Connected to %1.", m_endpoint->text().trimmed()));
    });
    connect(&m_coordinator, &OnlineCoordinator::connectionClosed, this, [this] {
        if (m_matchActive) {
            m_matchActive = false;
            Q_EMIT matchEnded();
        }
        m_connectTimeout->stop();
        m_state = {};
        m_joinCode->clear();
        m_rematchRequested = m_rematchReadySent = false;
        m_entryRequestPending = false;
        m_createButton->setEnabled(true); m_joinButton->setEnabled(true);
        if (m_disconnectRequested) {
            m_disconnectRequested = false;
            setConnectionState(ConnectionState::Disconnected);
            m_pages->setCurrentIndex(0);
            Q_EMIT statusChanged(i18n("Disconnected. Ready to connect."));
            if (m_connectAfterDisconnect) {
                m_connectAfterDisconnect = false;
                if (!m_endpoint->text().trimmed().isEmpty()) beginConnection();
            }
            return;
        }
        if (m_connectionState != ConnectionState::Failed) {
            const auto message = i18n("The connection closed. Retry or choose another server.");
            setConnectionState(ConnectionState::Failed, message);
            Q_EMIT statusChanged(message);
        }
        m_pages->setCurrentIndex(0);
    });
    connect(&m_coordinator, &OnlineCoordinator::serviceNotice, this, [this](const QString &text) {
        m_restartNotice = text;
        const QString existing = m_serverNotice->isVisible() ? m_serverNotice->text() : QString();
        m_serverNotice->setText(QStringLiteral("<b>%1</b>").arg(text.toHtmlEscaped())
            + (existing.isEmpty() ? QString() : QStringLiteral("<br>") + existing));
        m_serverNotice->show();
        m_lobby->setStatus(text);
        Q_EMIT serviceNotice(text);
    });
    connect(&m_coordinator, &OnlineCoordinator::reconnectingChanged, this, [this](bool reconnecting) {
        if (!m_matchActive) return;
        Q_EMIT matchPauseChanged(reconnecting, reconnecting ? i18n("Lost the connection. Reconnecting to the match…") : QString());
        if (!reconnecting) Q_EMIT matchTransportResumed();
    });
    connect(&m_coordinator, &OnlineCoordinator::lobbyChanged, this, [this](const QJsonObject &state) {
        if (m_matchActive) {
            QStringList away;
            for (const auto &value : state.value(QStringLiteral("members")).toArray()) {
                const auto member = value.toObject();
                if (member.contains(QStringLiteral("connected")) && !member.value(QStringLiteral("connected")).toBool())
                    away << member.value(QStringLiteral("displayName")).toString();
            }
            Q_EMIT matchPauseChanged(!away.isEmpty(), away.isEmpty() ? QString()
                : i18n("Waiting for %1 to reconnect…", away.join(QStringLiteral(", "))));
        }
        showLobby(state);
    });
    connect(&m_coordinator, &OnlineCoordinator::lobbyClosed, this, [this](const QString &reason) {
        if (m_matchActive) {
            m_matchActive = false;
            Q_EMIT matchEnded();
        }
        showEntry(); m_entryStatus->setText(reason);
        Q_EMIT statusChanged(reason);
    });
    connect(&m_coordinator, &OnlineCoordinator::matchPrepared, this, [this](const QJsonObject &config) {
        m_matchActive = true;
        Q_EMIT matchRequested(config);
    });
    connect(&m_coordinator, &OnlineCoordinator::statusChanged, this, [this](const QString &status) {
        m_lobby->setStatus(status);
        Q_EMIT statusChanged(status);
    });
    connect(&m_coordinator, &OnlineCoordinator::failed, this, [this](const QString &reason) {
        const bool connectionFailure = m_connectionState == ConnectionState::Connecting
            || m_connectionState == ConnectionState::Failed;
        if (connectionFailure) {
            m_connectTimeout->stop();
            setConnectionState(ConnectionState::Failed, reason);
        } else {
            m_entryStatus->setText(reason);
        }
        m_entryRequestPending = false;
        m_createButton->setEnabled(true); m_joinButton->setEnabled(true);
        m_lobby->setStatus(reason);
        Q_EMIT statusChanged(reason);
    });
}

OnlineWidget::~OnlineWidget()
{
}

void OnlineWidget::reloadServers()
{
    KConfigGroup onlineConfig(KSharedConfig::openConfig(), QStringLiteral("Online"));
    const auto list = ServerList::load(onlineConfig);
    onlineConfig.sync();
    m_servers.clear();
    m_serverOrder.clear();
    for (const auto &server : list.servers()) {
        m_servers.insert(server.url, server.name);
        m_serverOrder.append(server.url);
    }
    m_defaultServer = list.defaultUrl();
}

void OnlineWidget::rebuildServerMenu()
{
    m_serverMenu->clear();
    const QString current = normalizeServerAddress(m_endpoint->text()).url.toString();
    auto *group = new QActionGroup(m_serverMenu);
    for (const auto &url : std::as_const(m_serverOrder)) {
        auto *action = m_serverMenu->addAction(m_servers.value(url));
        action->setObjectName(QStringLiteral("onlineServerChoice"));
        action->setToolTip(url);
        action->setCheckable(true);
        action->setChecked(url == current);
        action->setActionGroup(group);
        connect(action, &QAction::triggered, this, [this, url] { switchToServer(url); });
    }
    m_serverMenu->addSeparator();
    auto *other = m_serverMenu->addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), i18nc("@action:inmenu", "Other Address…"));
    connect(other, &QAction::triggered, this, [this] {
        if (m_connectionState == ConnectionState::Connected || m_connectionState == ConnectionState::Connecting) changeServer();
        m_serverControls->setVisible(true);
        m_endpoint->setFocus();
        m_endpoint->selectAll();
    });
    auto *manage = m_serverMenu->addAction(QIcon::fromTheme(QStringLiteral("configure")), i18nc("@action:inmenu", "Manage Servers…"));
    connect(manage, &QAction::triggered, this, &OnlineWidget::manageServersRequested);
    auto *disconnectAction = m_serverMenu->addAction(QIcon::fromTheme(QStringLiteral("network-disconnect")), i18nc("@action:inmenu", "Disconnect"));
    disconnectAction->setEnabled(m_connectionState == ConnectionState::Connected || m_connectionState == ConnectionState::Connecting);
    connect(disconnectAction, &QAction::triggered, this, &OnlineWidget::changeServer);
}

void OnlineWidget::openInvite(const QString &code, const QString &serverUrl)
{
    const auto current = normalizeServerAddress(m_endpoint->text()).url.toString();
    const QString target = serverUrl.isEmpty() ? (m_connectionState == ConnectionState::Connected ? current : m_defaultServer) : serverUrl;
    m_joinCode->setText(code.left(4) + QLatin1Char('-') + code.mid(4));
    m_pages->setCurrentIndex(0);
    if (m_connectionState == ConnectionState::Connected && target == current) {
        joinCode(code);
        return;
    }
    m_pendingInviteCode = code;
    m_entryStatus->setText(i18n("Connecting to join game %1…", code));
    switchToServer(target);
}

void OnlineWidget::joinCode(const QString &code)
{
    if (m_entryRequestPending || !m_profile->isValid()) return;
    m_entryRequestPending = true;
    m_createButton->setEnabled(false); m_joinButton->setEnabled(false);
    savePreferences();
    m_coordinator.joinLobby(code, m_profile->playerName(), m_profile->colorMode(), m_profile->customColor());
}

void OnlineWidget::switchToServer(const QString &url)
{
    m_sessionServer = url;
    m_serverControls->hide();
    if (m_connectionState == ConnectionState::Connected || m_connectionState == ConnectionState::Connecting) changeServer();
    m_endpoint->setText(url);
    if (m_disconnectRequested) m_connectAfterDisconnect = true;
    else beginConnection();
}

void OnlineWidget::showServerNotice(const QJsonObject &hello)
{
    QStringList parts;
    const QString motd = hello.value(QStringLiteral("motd")).toString().trimmed().left(280);
    if (!motd.isEmpty()) parts << motd.toHtmlEscaped();
    const auto latest = hello.value(QStringLiteral("latestClient")).toObject();
    const QString latestVersion = latest.value(QStringLiteral("version")).toString().left(40);
    const QUrl latestUrl(latest.value(QStringLiteral("url")).toString());
    if (isNewerClientVersion(latestVersion, QCoreApplication::applicationVersion())
        && (latestUrl.scheme() == QLatin1String("https") || latestUrl.scheme() == QLatin1String("http"))) {
        parts << i18n("Kolf %1 is available. <a href=\"%2\">Download the update</a>",
            latestVersion.toHtmlEscaped(), latestUrl.toString(QUrl::FullyEncoded).toHtmlEscaped());
    }
    m_serverNotice->setText(parts.join(QStringLiteral("<br>")));
    m_serverNotice->setVisible(!parts.isEmpty());
}

void OnlineWidget::serversChanged()
{
    reloadServers();
    if (m_connectionState != ConnectionState::Connected && m_connectionState != ConnectionState::Connecting
        && m_sessionServer.isEmpty())
        m_endpoint->setText(m_defaultServer);
    setConnectionState(m_connectionState);
}

void OnlineWidget::enterOnline()
{
    m_pages->setCurrentIndex(0);
    if (m_connectionState != ConnectionState::Connected && m_connectionState != ConnectionState::Connecting) {
        reloadServers();
        m_endpoint->setText(m_sessionServer.isEmpty() ? m_defaultServer : m_sessionServer);
    }
    setConnectionState(m_disconnectRequested ? ConnectionState::Disconnecting : ConnectionState::Disconnected,
        m_endpoint->text().trimmed().isEmpty() ? i18n("Enter a server address to play online.") : QString());
    m_connectAfterDisconnect = m_disconnectRequested;
    if (!m_disconnectRequested && !m_endpoint->text().trimmed().isEmpty())
        beginConnection();
    else
        m_endpoint->setFocus();
}

QString OnlineWidget::normalizeJoinCode(const QString &input)
{
    QString text = input.trimmed();
    const int slash = int(text.lastIndexOf(QLatin1Char('/')));
    if (slash >= 0) text = text.mid(slash + 1);
    const int query = int(text.indexOf(QLatin1Char('?')));
    if (query >= 0) text = text.left(query);
    QString code;
    for (const QChar character : std::as_const(text))
        if (character.isLetterOrNumber()) code.append(character.toUpper());
    static const QRegularExpression valid(QStringLiteral("^[A-HJ-NP-Z2-9]{6,12}$"));
    return valid.match(code).hasMatch() ? code : QString();
}

void OnlineWidget::updateProfileChip()
{
    const QColor color = m_profile->colorMode() == QLatin1String("custom")
        ? colorFromRgba(m_profile->customColor()) : QColor();
    const int size = m_profileChip->fontMetrics().height();
    QPixmap swatch(size, size);
    swatch.fill(Qt::transparent);
    QPainter painter(&swatch);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    painter.setBrush(color.isValid() ? QBrush(color) : QBrush(palette().color(QPalette::Mid), Qt::Dense4Pattern));
    painter.drawEllipse(QRectF(1, 1, size - 2, size - 2));
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    swatch.save(&buffer, "PNG");
    m_profileChip->setText(QStringLiteral("<img src=\"data:image/png;base64,%1\"> <b>%2</b>%3")
        .arg(QString::fromLatin1(png.toBase64()), m_profile->playerName().toHtmlEscaped(),
            color.isValid() ? QString() : i18nc("profile color is assigned automatically", " (color assigned in lobby)")));
}

void OnlineWidget::changeServer()
{
    m_connectAfterDisconnect = false;
    m_connectTimeout->stop();
    if (m_matchActive) {
        m_matchActive = false;
        Q_EMIT matchEnded();
    }
    m_state = {};
    m_joinCode->clear();
    m_createCourse->clear();
    m_entryRequestPending = false;
    m_rematchRequested = m_rematchReadySent = false;
    m_createButton->setEnabled(true);
    m_joinButton->setEnabled(true);
    m_pages->setCurrentIndex(0);
    m_disconnectRequested = true;
    setConnectionState(ConnectionState::Disconnecting);
    Q_EMIT statusChanged(i18n("Disconnecting…"));
    m_coordinator.disconnectFromService();
    if (m_disconnectRequested && m_coordinator.networkClient()->isDisconnected()) {
        m_disconnectRequested = false;
        setConnectionState(ConnectionState::Disconnected);
        Q_EMIT statusChanged(i18n("Disconnected. Ready to connect."));
    }
    m_endpoint->setFocus();
}

void OnlineWidget::setConnectionState(ConnectionState state, const QString &message)
{
    m_connectionState = state;
    const auto address = m_endpoint->text().trimmed();
    const auto normalized = normalizeServerAddress(address);
    const QUrl url = normalized.url;
    const bool plaintextRemote = url.scheme() == QLatin1String("ws")
        && url.host() != QLatin1String("localhost") && url.host() != QLatin1String("127.0.0.1") && url.host() != QLatin1String("::1");
    const QString canonical = url.toString();
    const QString friendlyName = m_servers.value(canonical, url.host().isEmpty() ? address : url.host());
    m_serverButton->setText(address.isEmpty() ? i18n("Choose a Server") : i18n("Server: %1", friendlyName));
    m_serverButton->setToolTip(normalized.isValid() ? i18n("%1 (click to switch or manage servers)", canonical) : address);
    m_serverButton->setAccessibleName(m_serverButton->text());
    const bool busy = state == ConnectionState::Connecting || state == ConnectionState::Disconnecting;
    QString status = !message.isEmpty() ? message
        : state == ConnectionState::Connecting ? i18n("Connecting…")
        : state == ConnectionState::Disconnecting ? i18n("Disconnecting…")
        : state == ConnectionState::Failed ? i18n("Couldn't reach the server. Retry, or choose another server.")
        : state == ConnectionState::Connected ? (m_serverDisplayName.isEmpty()
            ? i18n("● Connected. Host a game or join one with a code.")
            : i18n("● Connected to %1. Host a game or join one with a code.", m_serverDisplayName))
        : address.isEmpty() ? i18n("Enter a server address to play online.") : i18n("Not connected.");
    if (plaintextRemote && (state == ConnectionState::Connected || state == ConnectionState::Connecting))
        status += QLatin1Char(' ') + i18n("This connection is not encrypted; use it only on a network you trust.");
    m_connectStatus->setText(status);
    if (state != ConnectionState::Connected && state != ConnectionState::InLobby && state != ConnectionState::InMatch) {
        m_serverNotice->setText(QStringLiteral("<b>%1</b>").arg(m_restartNotice.toHtmlEscaped()));
        m_serverNotice->setVisible(!m_restartNotice.isEmpty());
        m_serverDisplayName.clear();
    }
    m_connectButton->setEnabled(!busy && !address.isEmpty());
    m_connectButton->setText(state == ConnectionState::Failed ? i18nc("@action:button", "Retry")
        : i18nc("@action:button", "Connect"));
    m_serverButton->setEnabled(state != ConnectionState::Disconnecting);
    m_endpoint->setEnabled(!busy);
    m_serverControls->setVisible(address.isEmpty() || state == ConnectionState::Failed
        || (state == ConnectionState::Disconnected && m_serverControls->isVisible()));
    const bool ready = state == ConnectionState::Connected;
    m_hostCard->setEnabled(ready);
    m_joinCard->setEnabled(ready);
    const QString reason = ready ? QString() : i18n("Connect to a server first.");
    m_hostCard->setToolTip(reason);
    m_joinCard->setToolTip(reason);
}

void OnlineWidget::beginConnection()
{
    if (m_connectionState == ConnectionState::Connecting || m_connectionState == ConnectionState::Disconnecting
        || m_endpoint->text().trimmed().isEmpty()) return;
    m_entryStatus->clear();
    setConnectionState(ConnectionState::Connecting);
    m_connectTimeout->start();
    m_coordinator.connectToService(m_endpoint->text());
}

void OnlineWidget::setEndpointAddress(const QString &endpoint)
{
    m_endpoint->setText(endpoint);
    m_sessionServer = endpoint;
}

void OnlineWidget::setProfile(const QString &name, const QString &colorMode, const QString &customColor)
{
    m_profile->setProfile(name, colorMode, customColor);
}

QString OnlineWidget::profileName() const { return m_profile->playerName(); }
QString OnlineWidget::profileColorMode() const { return m_profile->colorMode(); }
QString OnlineWidget::profileCustomColor() const { return m_profile->customColor(); }
QString OnlineWidget::resultsText() const { return m_result->plainText(); }

void OnlineWidget::showEntry()
{
    setConnectionState(ConnectionState::Connected);
    KConfigGroup onlineConfig(KSharedConfig::openConfig(), QStringLiteral("Online"));
    const auto endpoint = normalizeServerAddress(m_endpoint->text()).url.toString();
    m_sessionServer = endpoint;
    if (!m_servers.contains(endpoint)) {
        auto list = ServerList::load(onlineConfig);
        list.remember(endpoint);
        list.save(onlineConfig);
        onlineConfig.sync();
        reloadServers();
        setConnectionState(ConnectionState::Connected);
    }
    m_entryRequestPending = false;
    m_createButton->setEnabled(true); m_joinButton->setEnabled(true);
    m_entryStatus->clear();
    m_pages->setCurrentIndex(0);
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
        return;
    }
    if (!m_matchActive) m_pages->setCurrentIndex(1);
    m_lobby->setState(state);
    const bool open = state.value(QStringLiteral("phase")) == QLatin1String("Open");
    if (m_rematchRequested && open) {
        for (const auto &value : state.value(QStringLiteral("members")).toArray()) {
            const auto member = value.toObject();
            if (member.value(QStringLiteral("memberId")).toString() != m_coordinator.memberId()) continue;
            if (member.value(QStringLiteral("ready")).toBool()) m_rematchRequested = false;
            else if (!m_rematchReadySent) { m_rematchReadySent = true; m_coordinator.setReady(true); }
        }
    } else if (!open) m_rematchRequested = false;
    if (open && !state.value(QStringLiteral("rematchRequestedMemberIds")).toArray().isEmpty()) announceRematch(state);
}

void OnlineWidget::showResults(const QJsonObject &state)
{
    m_pages->setCurrentIndex(2);
    if (m_matchActive) {
        m_matchActive = false;
        Q_EMIT matchEnded();
    }
    const auto result = state.value(QStringLiteral("latestResult")).toObject();
    const bool completed = result.value(QStringLiteral("status")) == QLatin1String("Completed");
    m_result->setSummary(Kolf::Results::summarizeMatchResult(result));
    if (state.value(QStringLiteral("rematchRequestedMemberIds")).toArray().isEmpty())
        Q_EMIT statusChanged(completed ? i18n("Match complete. Choose Rematch or Return to Lobby.")
            : i18n("Match interrupted. Review the partial Results."));
    else
        announceRematch(state);
}

void OnlineWidget::announceRematch(const QJsonObject &state)
{
    const auto requests = state.value(QStringLiteral("rematchRequestedMemberIds")).toArray();
    if (requests.isEmpty()) return;
    const auto members = state.value(QStringLiteral("members")).toArray();
    if (state.value(QStringLiteral("phase")) == QLatin1String("Open") && requests.size() == members.size()) {
        bool allReady = true;
        for (const auto &value : members) allReady = allReady && value.toObject().value(QStringLiteral("ready")).toBool();
        Q_EMIT statusChanged(allReady
            ? i18n("Rematch accepted (%1/%2). The owner can start.", requests.size(), members.size())
            : i18n("Rematch accepted (%1/%2). Waiting for members to be ready.", requests.size(), members.size()));
        return;
    }
    const auto lastId = requests.last().toString();
    QString name = i18n("A player");
    for (const auto &value : members) {
        const auto member = value.toObject();
        if (member.value(QStringLiteral("memberId")).toString() == lastId) {
            name = member.value(QStringLiteral("displayName")).toString();
            break;
        }
    }
    Q_EMIT statusChanged(i18n("%1 requested a rematch (%2/%3)", name, requests.size(), members.size()));
}

void OnlineWidget::leaveOnline()
{
    m_connectAfterDisconnect = false;
    m_connectTimeout->stop();
    if (m_matchActive) {
        m_matchActive = false;
        Q_EMIT matchEnded();
    }
    if (m_coordinator.inLobby()) m_coordinator.leaveLobby();
    m_disconnectRequested = true;
    m_coordinator.disconnectFromService();
    if (m_coordinator.networkClient()->isDisconnected()) m_disconnectRequested = false;
    m_state = {};
    m_rematchRequested = m_rematchReadySent = false;
    setConnectionState(m_disconnectRequested ? ConnectionState::Disconnecting : ConnectionState::Disconnected);
    m_pages->setCurrentIndex(0);
}

void OnlineWidget::savePreferences()
{
    KConfigGroup onlineConfig(KSharedConfig::openConfig(), QStringLiteral("Online"));
    onlineConfig.writeEntry("displayName", m_profile->playerName());
    onlineConfig.writeEntry("colorMode", m_profile->colorMode());
    onlineConfig.writeEntry("customColor", m_profile->customColor());
    onlineConfig.deleteEntry("color");
    onlineConfig.sync();
}
