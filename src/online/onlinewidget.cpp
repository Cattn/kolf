// SPDX-License-Identifier: GPL-2.0-or-later
#include "onlinewidget.h"
#include "color.h"
#include "playerprofileeditor.h"
#include "results/summary.h"
#include "ui/resultsview.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QComboBox>
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

OnlineWidget::OnlineWidget(QWidget *parent)
    : QWidget(parent)
    , m_coordinator(this)
{
    auto *root = new QVBoxLayout(this);
    m_pages = new QStackedWidget(this);
    root->addWidget(m_pages);

    KConfigGroup onlineConfig(KSharedConfig::openConfig(), QStringLiteral("Online"));
    const auto configuredEndpoint = qEnvironmentVariable("KOLF_ONLINE_DEFAULT_ENDPOINT").trimmed();
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
    m_serverLabel = new QLabel(hub);
    m_serverLabel->setObjectName(QStringLiteral("onlineServer"));
    header->addWidget(m_serverLabel);
    m_changeServerButton = new QPushButton(QIcon::fromTheme(QStringLiteral("network-server")), i18nc("@action:button", "Change Server…"), hub);
    m_changeServerButton->setObjectName(QStringLiteral("onlineChangeServer"));
    header->addWidget(m_changeServerButton);
    auto *cancelButton = new QPushButton(QIcon::fromTheme(QStringLiteral("go-previous")), i18nc("@action:button", "Back"), hub);
    cancelButton->setObjectName(QStringLiteral("onlineBack"));
    header->addWidget(cancelButton);
    hubLayout->addLayout(header);
    m_connectStatus = new QLabel(hub);
    m_connectStatus->setObjectName(QStringLiteral("onlineStatus"));
    m_connectStatus->setWordWrap(true);
    hubLayout->addWidget(m_connectStatus);

    m_serverControls = new QGroupBox(i18n("Server"), hub);
    m_serverControls->setObjectName(QStringLiteral("onlineServerControls"));
    auto *serverLayout = new QFormLayout(m_serverControls);
    m_recentServers = new QComboBox(m_serverControls);
    m_recentServers->setAccessibleName(i18n("Recent servers"));
    for (const auto &server : onlineConfig.readEntry("recentServers", QStringList()))
        m_recentServers->addItem(server);
    serverLayout->addRow(i18nc("@label", "Recent:"), m_recentServers);
    m_endpoint = new QLineEdit(m_serverControls);
    m_endpoint->setObjectName(QStringLiteral("onlineAddress"));
    m_endpoint->setPlaceholderText(i18nc("@info:placeholder", "server.example.org:3011"));
    m_endpoint->setText(onlineConfig.readEntry("lastSuccessfulEndpoint",
        onlineConfig.readEntry("endpoint", configuredEndpoint)));
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
    m_customCourse = new QPushButton(i18nc("@action:button", "Custom Course…"), lobbyPage);
    auto *courseChoices = new QVBoxLayout;
    courseChoices->addWidget(m_lobbyCourse);
    courseChoices->addWidget(m_customCourse);
    courseRow->addRow(i18nc("@label", "Course:"), courseChoices); setupColumn->addLayout(courseRow);
    m_lobbyStatus = new QLabel(lobbyPage); m_lobbyStatus->setWordWrap(true); setupColumn->addWidget(m_lobbyStatus);
    m_colorWarning = new QLabel(lobbyPage); m_colorWarning->setWordWrap(true); setupColumn->addWidget(m_colorWarning);
    setupColumn->addStretch();
    columns->addLayout(setupColumn, 1);
    lobbyLayout->addLayout(columns, 1);
    auto *lobbyButtons = new QHBoxLayout;
    auto *lobbyChangeServer = new QPushButton(i18nc("@action:button", "Change Server"), lobbyPage);
    auto *lobbyLeave = new QPushButton(i18nc("@action:button", "Leave Online"), lobbyPage);
    m_ready = new QPushButton(lobbyPage); m_start = new QPushButton(i18nc("@action:button", "Start"), lobbyPage);
    lobbyButtons->addWidget(lobbyChangeServer); lobbyButtons->addWidget(lobbyLeave);
    lobbyButtons->addStretch(); lobbyButtons->addWidget(m_ready); lobbyButtons->addWidget(m_start);
    lobbyLayout->addLayout(lobbyButtons);
    m_pages->addWidget(lobbyPage);

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
    connect(copyInvite, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_state.value(QStringLiteral("joinCode")).toString());
    });
    connect(m_changeServerButton, &QPushButton::clicked, this, [this] {
        if (m_connectionState == ConnectionState::Connected) changeServer();
        m_serverControls->setVisible(true);
        m_endpoint->setFocus();
        m_endpoint->selectAll();
    });
    connect(m_recentServers, &QComboBox::activated, this, [this](int index) {
        if (index >= 0) m_endpoint->setText(m_recentServers->itemText(index));
    });
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
        const auto code = normalizeJoinCode(m_joinCode->text());
        if (code.isEmpty()) {
            m_entryStatus->setText(i18n("That doesn't look like a join code. Codes are 6 to 12 letters and numbers."));
            m_entryRequestPending = false; m_createButton->setEnabled(true); m_joinButton->setEnabled(true);
            return;
        }
        savePreferences(); m_coordinator.joinLobby(code, m_profile->playerName(),
            m_profile->colorMode(), m_profile->customColor());
    });
    connect(lobbyChangeServer, &QPushButton::clicked, this, &OnlineWidget::changeServer);
    connect(lobbyLeave, &QPushButton::clicked, this, &OnlineWidget::leaveRequested);
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
        QString name = i18n("Player %1", m_state.value(QStringLiteral("players")).toArray().size() + 1);
        QString mode = QStringLiteral("auto"), customColor;
        if (editProfile(this, i18nc("@title:window", "Add local player"), name, mode, customColor))
            m_coordinator.addPlayer(name, mode, customColor);
    });
    connect(m_editPlayer, &QPushButton::clicked, this, [this] {
        const auto *item = m_players->currentItem();
        if (!item) return;
        QString name = item->data(Qt::UserRole + 1).toString();
        QString mode = item->data(Qt::UserRole + 2).toString();
        QString customColor = item->data(Qt::UserRole + 5).toString();
        if (editProfile(this, i18nc("@title:window", "Edit player"), name, mode, customColor))
            m_coordinator.updatePlayer(item->data(Qt::UserRole).toString(), name, mode, customColor);
    });
    connect(m_removePlayer, &QPushButton::clicked, this, [this] {
        if (const auto *item = m_players->currentItem()) m_coordinator.removePlayer(item->data(Qt::UserRole).toString());
    });
    connect(m_players, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        const bool local = item && item->data(Qt::UserRole + 3).toString() == m_coordinator.memberId();
        m_editPlayer->setEnabled(local); m_removePlayer->setEnabled(local && item->data(Qt::UserRole + 4).toInt() > 1);
    });
    connect(m_lobbyCourse, &QComboBox::activated, this, [this](int index) {
        if (!m_state.isEmpty() && index >= 0) {
            m_coordinator.setCourse(m_lobbyCourse->itemData(index).toString());
            KConfigGroup config(KSharedConfig::openConfig(), QStringLiteral("Online"));
            config.writeEntry("lastCourseId", m_lobbyCourse->itemData(index).toString());
            config.sync();
        }
    });
    connect(m_customCourse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, i18nc("@title:window", "Choose a Kolf course"),
            QString(), i18n("Kolf courses (*.kolf);;All files (*)"));
        if (!path.isEmpty()) m_coordinator.uploadCourse(path);
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
        m_createCourse->clear(); m_lobbyCourse->clear();
        for (const auto &value : hello.value(QStringLiteral("courses")).toArray()) {
            const auto course = value.toObject();
            addCourse(m_createCourse, course);
            addCourse(m_lobbyCourse, course);
        }
        const int lastCourse = m_createCourse->findData(KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("Online"))
            .readEntry("lastCourseId", QString()));
        if (lastCourse >= 0) m_createCourse->setCurrentIndex(lastCourse);
        showEntry();
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
            return;
        }
        if (m_connectionState != ConnectionState::Failed) {
            const auto message = i18n("The connection closed. Retry or choose another server.");
            setConnectionState(ConnectionState::Failed, message);
            Q_EMIT statusChanged(message);
        }
        m_pages->setCurrentIndex(0);
    });
    connect(&m_coordinator, &OnlineCoordinator::lobbyChanged, this, &OnlineWidget::showLobby);
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
        Q_EMIT statusChanged(reason);
    });
}

OnlineWidget::~OnlineWidget()
{
}

void OnlineWidget::enterOnline()
{
    m_pages->setCurrentIndex(0);
    setConnectionState(m_disconnectRequested ? ConnectionState::Disconnecting : ConnectionState::Disconnected,
        m_endpoint->text().trimmed().isEmpty() ? i18n("Enter a server address to play online.") : QString());
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
    m_connectTimeout->stop();
    if (m_matchActive) {
        m_matchActive = false;
        Q_EMIT matchEnded();
    }
    m_state = {};
    m_joinCode->clear();
    m_createCourse->clear();
    m_lobbyCourse->clear();
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
    const QUrl url(address.contains(QStringLiteral("://")) ? address : QStringLiteral("ws://") + address);
    const bool plaintextRemote = url.scheme() == QLatin1String("ws")
        && !url.host().isEmpty() && url.host() != QLatin1String("localhost")
        && url.host() != QLatin1String("127.0.0.1") && url.host() != QLatin1String("::1");
    const auto configuredAddress = qEnvironmentVariable("KOLF_ONLINE_DEFAULT_ENDPOINT").trimmed();
    const auto friendlyName = address == configuredAddress
        ? qEnvironmentVariable("KOLF_ONLINE_DEFAULT_SERVER_NAME").trimmed() : QString();
    m_serverLabel->setText(address.isEmpty() ? i18n("No server chosen")
        : i18n("Server: %1", friendlyName.isEmpty() ? (url.host().isEmpty() ? address : url.host()) : friendlyName));
    m_serverLabel->setToolTip(address);
    const bool busy = state == ConnectionState::Connecting || state == ConnectionState::Disconnecting;
    QString status = !message.isEmpty() ? message
        : state == ConnectionState::Connecting ? i18n("Connecting…")
        : state == ConnectionState::Disconnecting ? i18n("Disconnecting…")
        : state == ConnectionState::Failed ? i18n("Couldn't reach the server. Retry, or choose another server.")
        : state == ConnectionState::Connected ? i18n("● Connected. Host a game or join one with a code.")
        : address.isEmpty() ? i18n("Enter a server address to play online.") : i18n("Not connected.");
    if (plaintextRemote && (state == ConnectionState::Connected || state == ConnectionState::Connecting))
        status += QLatin1Char(' ') + i18n("This connection is not encrypted; use it only on a network you trust.");
    m_connectStatus->setText(status);
    m_connectButton->setEnabled(!busy && !address.isEmpty());
    m_connectButton->setText(state == ConnectionState::Failed ? i18nc("@action:button", "Retry")
        : i18nc("@action:button", "Connect"));
    m_changeServerButton->setEnabled(!busy);
    m_endpoint->setEnabled(!busy);
    m_recentServers->setEnabled(!busy && m_recentServers->count() > 0);
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
    setConnectionState(ConnectionState::Connecting);
    m_connectTimeout->start();
    m_coordinator.connectToService(m_endpoint->text());
}

void OnlineWidget::setEndpointAddress(const QString &endpoint)
{
    m_endpoint->setText(endpoint);
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
    m_entryStatus->clear();
    m_pages->setCurrentIndex(0);
}

void OnlineWidget::showLobby(const QJsonObject &state)
{
    const auto previousCourseId = m_state.value(QStringLiteral("selectedCourseId")).toString();
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
    m_lobbySummary->setText(i18n("Join code: %1", state.value(QStringLiteral("joinCode")).toString()));
    m_players->clear();
    QSet<QString> manualColors;
    bool duplicateManualColor = false;
    for (const auto &value : state.value(QStringLiteral("players")).toArray()) {
        const auto player = value.toObject();
        if (player.value(QStringLiteral("colorMode")) != QLatin1String("custom")) continue;
        const auto selected = player.value(QStringLiteral("customColor")).toString().toLower();
        if (manualColors.contains(selected)) duplicateManualColor = true;
        manualColors.insert(selected);
    }
    m_colorWarning->setText(duplicateManualColor
        ? i18n("Some players selected the same color. Names will still identify them.") : QString());
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
            const auto resolved = player.value(QStringLiteral("resolvedColor")).toString();
            auto *item = new QListWidgetItem(i18n("    %1 — %2 (%3)", player.value(QStringLiteral("displayName")).toString(),
                player.value(QStringLiteral("colorMode")) == QLatin1String("auto") ? i18n("Auto") : i18n("Custom"), resolved), m_players);
            const QColor color = colorFromRgba(resolved);
            if (color.isValid()) {
                QPixmap swatch(16, 16); swatch.fill(color); item->setIcon(QIcon(swatch));
            }
            item->setData(Qt::UserRole, player.value(QStringLiteral("playerId")));
            item->setData(Qt::UserRole + 1, player.value(QStringLiteral("displayName")));
            item->setData(Qt::UserRole + 2, player.value(QStringLiteral("colorMode")));
            item->setData(Qt::UserRole + 3, member.value(QStringLiteral("memberId")));
            item->setData(Qt::UserRole + 5, player.value(QStringLiteral("customColor")));
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
    if (m_rematchRequested && open) {
        if (!localReady && !m_rematchReadySent) {
            m_rematchReadySent = true; m_coordinator.setReady(true);
        } else if (localReady) m_rematchRequested = false;
    } else if (!open && state.value(QStringLiteral("phase")) != QLatin1String("Results")) {
        m_rematchRequested = false;
    }
    m_addPlayer->setEnabled(open && players.size() < 8);
    m_editPlayer->setEnabled(false); m_removePlayer->setEnabled(false);
    const auto courseId = state.value(QStringLiteral("selectedCourseId")).toString();
    const auto courses = state.value(QStringLiteral("courses")).toArray();
    bool catalogChanged = m_lobbyCourse->count() != courses.size();
    for (int i = 0; !catalogChanged && i < courses.size(); ++i) {
        if (m_lobbyCourse->itemData(i).toString() != courses.at(i).toObject().value(QStringLiteral("courseId")).toString())
            catalogChanged = true;
    }
    if (catalogChanged) {
        const QSignalBlocker blocker(m_lobbyCourse);
        m_lobbyCourse->clear();
        for (const auto &value : courses) addCourse(m_lobbyCourse, value.toObject());
    }
    const auto index = m_lobbyCourse->findData(courseId);
    if (index >= 0 && index != m_lobbyCourse->currentIndex()) {
        const QSignalBlocker blocker(m_lobbyCourse);
        m_lobbyCourse->setCurrentIndex(index);
    }
    m_lobbyCourse->setEnabled(open && owner);
    m_customCourse->setEnabled(open && owner);
    if (open) m_lobbyStatus->setText(players.size() < 2 || members.size() < 2
        ? i18n("At least two players and two members are required. Add a player or invite another member.")
        : allReady ? i18n("All members are ready. The owner can start.")
        : i18n("Ready every member on this revision, then the owner can start."));
    if (open) {
        if (!state.value(QStringLiteral("rematchRequestedMemberIds")).toArray().isEmpty())
            announceRematch(state);
        else if (index >= 0 && !previousCourseId.isEmpty() && courseId != previousCourseId)
            Q_EMIT statusChanged(i18n("Course selected: %1", m_lobbyCourse->itemText(index)));
        else if (members.size() < 2 || players.size() < 2)
            Q_EMIT statusChanged(i18n("Lobby ready. Invite another member or add a player."));
        else if (allReady)
            Q_EMIT statusChanged(i18n("All members are ready. The owner can start."));
        else
            Q_EMIT statusChanged(i18n("Lobby ready. Waiting for members to be ready."));
    }
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
    m_connectTimeout->stop();
    if (m_matchActive) {
        m_matchActive = false;
        Q_EMIT matchEnded();
    }
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
