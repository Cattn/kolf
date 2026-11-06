#include "lobbyview.h"
#include "onlinecoordinator.h"
#include "playerprofileeditor.h"
#include "color.h"
#include "coursefile.h"
#include "ui/newgameview.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <KSharedConfig>
#include <KStandardGuiItem>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Kolf::Online;

LobbyView::LobbyView(const Kolf::ItemFactory &factory, OnlineCoordinator &coordinator, QWidget *parent)
    : QWidget(parent), m_factory(factory), m_coordinator(coordinator)
{
    setObjectName(QStringLiteral("onlineLobby"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 14, 20, 14);
    auto *header = new QHBoxLayout;
    m_heading = new QLabel(this);
    m_heading->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto font = m_heading->font();
    font.setPointSizeF(font.pointSizeF() * 1.4);
    font.setBold(true);
    m_heading->setFont(font);
    header->addWidget(m_heading, 1);
    auto *copy = new QPushButton(i18n("Copy code"), this);
    copy->setObjectName(QStringLiteral("lobbyCopyCode"));
    header->addWidget(copy);
    auto *leave = new QPushButton(i18n("Leave"), this);
    leave->setObjectName(QStringLiteral("lobbyLeave"));
    header->addWidget(leave);
    layout->addLayout(header);
    auto *columns = new QHBoxLayout;
    auto *roster = new QVBoxLayout;
    m_peopleHeading = new QLabel(this);
    roster->addWidget(m_peopleHeading);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *people = new QWidget(scroll);
    m_people = new QVBoxLayout(people);
    m_people->setContentsMargins(0, 0, 8, 0);
    m_people->setAlignment(Qt::AlignTop);
    scroll->setWidget(people);
    roster->addWidget(scroll, 1);
    columns->addLayout(roster, 1);
    auto *setup = new QVBoxLayout;
    auto *course = new QGroupBox(i18n("Course"), this);
    auto *courseLayout = new QVBoxLayout(course);
    m_course = new QComboBox(course);
    m_course->setObjectName(QStringLiteral("lobbyCourse"));
    m_course->setAccessibleName(i18n("Course"));
    m_course->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    courseLayout->addWidget(m_course);
    m_thumbnail = new QLabel(course);
    m_thumbnail->setObjectName(QStringLiteral("lobbyThumbnail"));
    m_thumbnail->setAlignment(Qt::AlignCenter);
    m_thumbnail->setMinimumSize(180, 180);
    courseLayout->addWidget(m_thumbnail, 0, Qt::AlignHCenter);
    m_courseDetails = new QLabel(course);
    m_courseDetails->setWordWrap(true);
    m_courseDetails->setTextFormat(Qt::PlainText);
    courseLayout->addWidget(m_courseDetails);
    m_custom = new QPushButton(i18n("Custom Course…"), course);
    m_custom->setObjectName(QStringLiteral("lobbyCustomCourse"));
    courseLayout->addWidget(m_custom, 0, Qt::AlignLeft);
    m_courseStatus = new QLabel(course);
    m_courseStatus->setObjectName(QStringLiteral("lobbyCourseStatus"));
    m_courseStatus->setWordWrap(true);
    courseLayout->addWidget(m_courseStatus);
    setup->addWidget(course);
    auto *rules = new QGroupBox(i18n("Rules"), this);
    auto *rulesLayout = new QVBoxLayout(rules);
    m_hostControls = new QCheckBox(i18n("Host controls (undo, reset, skip)"), rules);
    m_hostControls->setObjectName(QStringLiteral("lobbyHostControls"));
    m_allowVoting = new QCheckBox(i18n("Players can request actions"), rules);
    m_allowVoting->setObjectName(QStringLiteral("lobbyAllowVoting"));
    rulesLayout->addWidget(m_hostControls);
    rulesLayout->addWidget(m_allowVoting);
    setup->addWidget(rules);
    setup->addStretch();
    columns->addLayout(setup, 1);
    layout->addLayout(columns, 1);
    auto *footer = new QHBoxLayout;
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("lobbyStatus"));
    m_status->setWordWrap(true);
    footer->addWidget(m_status, 1);
    m_primary = new QPushButton(this);
    m_primary->setObjectName(QStringLiteral("lobbyPrimary"));
    m_primary->setDefault(true);
    m_primary->setMinimumHeight(38);
    footer->addWidget(m_primary);
    layout->addLayout(footer);
    connect(copy, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_state.value(QStringLiteral("joinCode")).toString());
    });
    connect(leave, &QPushButton::clicked, this, [this] {
        const bool host = m_state.value(QStringLiteral("ownerMemberId")).toString() == m_coordinator.memberId();
        if (host && KMessageBox::warningTwoActions(this, i18n("Leaving closes this lobby for everyone."),
            i18n("Leave Lobby?"), KGuiItem(i18n("Leave")), KStandardGuiItem::cancel()) != KMessageBox::PrimaryAction) return;
        m_coordinator.leaveLobby();
        Q_EMIT leaveRequested();
    });
    connect(m_primary, &QPushButton::clicked, this, [this] {
        if (m_state.value(QStringLiteral("ownerMemberId")).toString() == m_coordinator.memberId()) {
            m_coordinator.setReady(true);
            m_coordinator.startMatch();
        } else {
            for (const auto &value : m_state.value(QStringLiteral("members")).toArray()) {
                const auto member = value.toObject();
                if (member.value(QStringLiteral("memberId")).toString() == m_coordinator.memberId())
                    m_coordinator.setReady(!member.value(QStringLiteral("ready")).toBool());
            }
        }
    });
    connect(m_course, &QComboBox::activated, this, [this](int index) {
        m_coordinator.setCourse(m_course->itemData(index).toString());
        KConfigGroup config(KSharedConfig::openConfig(), QStringLiteral("Online"));
        config.writeEntry("lastCourseId", m_course->itemData(index).toString());
        config.sync();
    });
    connect(m_custom, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, i18n("Choose a Kolf course"), {}, i18n("Kolf courses (*.kolf);;All files (*)"));
        if (!path.isEmpty()) m_coordinator.uploadCourse(path);
    });
    const auto sendRules = [this] { m_coordinator.setLobbyRules(m_hostControls->isChecked(), m_allowVoting->isChecked()); };
    connect(m_hostControls, &QCheckBox::clicked, this, sendRules);
    connect(m_allowVoting, &QCheckBox::clicked, this, sendRules);
    connect(&m_coordinator, &OnlineCoordinator::courseUploadProgress, this, [this](int percent) {
        m_courseStatus->setText(percent < 100 ? i18n("Uploading course… %1%", percent) : i18n("Course uploaded."));
    });
    connect(&m_coordinator, &OnlineCoordinator::failed, m_courseStatus, &QLabel::setText);
}

void LobbyView::setStatus(const QString &status) { m_status->setText(status); }

void LobbyView::editPlayer(const QJsonObject &player)
{
    QDialog dialog(this);
    dialog.setWindowTitle(player.isEmpty() ? i18n("Add local player") : i18n("Edit player"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *editor = new PlayerProfileEditor(&dialog);
    editor->setProfile(player.value(QStringLiteral("displayName")).toString(i18n("Player %1", m_state.value(QStringLiteral("players")).toArray().size() + 1)),
        player.value(QStringLiteral("colorMode")).toString(QStringLiteral("auto")), player.value(QStringLiteral("customColor")).toString());
    layout->addWidget(editor);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] { if (editor->isValid()) dialog.accept(); });
    if (dialog.exec() != QDialog::Accepted) return;
    if (player.isEmpty()) m_coordinator.addPlayer(editor->playerName(), editor->colorMode(), editor->customColor());
    else m_coordinator.updatePlayer(player.value(QStringLiteral("playerId")).toString(), editor->playerName(), editor->colorMode(), editor->customColor());
}

void LobbyView::setState(const QJsonObject &state)
{
    const auto oldMembers = m_state.value(QStringLiteral("members"));
    const auto oldPlayers = m_state.value(QStringLiteral("players"));
    const auto oldPhase = m_state.value(QStringLiteral("phase"));
    m_state = state;
    const auto members = state.value(QStringLiteral("members")).toArray();
    const auto players = state.value(QStringLiteral("players")).toArray();
    const QString hostId = state.value(QStringLiteral("ownerMemberId")).toString();
    const bool host = hostId == m_coordinator.memberId();
    const bool open = state.value(QStringLiteral("phase")) == QLatin1String("Open");
    m_heading->setText(i18n("Lobby · Code %1", state.value(QStringLiteral("joinCode")).toString()));
    m_peopleHeading->setText(i18n("People and players · %1 · %2/8 players", i18np("1 person", "%1 people", members.size()), players.size()));
    int waiting = 0;
    bool localReady = false;
    QStringList waitingNames;
    for (const auto &value : members) {
        const auto member = value.toObject();
        const auto id = member.value(QStringLiteral("memberId")).toString();
        const bool ready = member.value(QStringLiteral("ready")).toBool();
        if (id == m_coordinator.memberId()) localReady = ready;
        if (id != hostId && !ready) { ++waiting; waitingNames << member.value(QStringLiteral("displayName")).toString(); }
    }
    if (oldMembers != state.value(QStringLiteral("members")) || oldPlayers != state.value(QStringLiteral("players")) || oldPhase != state.value(QStringLiteral("phase"))) {
        while (auto *item = m_people->takeAt(0)) { item->widget()->deleteLater(); delete item; }
        QHash<QString, int> colorCounts;
        for (const auto &value : players) ++colorCounts[value.toObject().value(QStringLiteral("resolvedColor")).toString().toLower()];
        for (const auto &value : members) {
            const auto member = value.toObject();
            const auto id = member.value(QStringLiteral("memberId")).toString();
            const bool local = id == m_coordinator.memberId();
            const bool owner = id == hostId;
            auto *card = new QGroupBox(this);
            card->setObjectName(QStringLiteral("lobbyPerson"));
            auto *cardLayout = new QVBoxLayout(card);
            const QString name = member.value(QStringLiteral("displayName")).toString();
            auto *heading = new QLabel(i18n("%1%2%3 · %4", owner ? i18n("Host · ") : QString(), name,
                local ? i18n(" (you)") : QString(), owner ? i18n("Starts the match") : member.value(QStringLiteral("ready")).toBool() ? i18n("✓ Ready") : i18n("… Not ready")), card);
            heading->setTextFormat(Qt::PlainText);
            heading->setWordWrap(true);
            auto font = heading->font(); font.setBold(true); heading->setFont(font);
            cardLayout->addWidget(heading);
            int owned = 0;
            for (const auto &playerValue : players) if (playerValue.toObject().value(QStringLiteral("ownerMemberId")) == id) ++owned;
            for (const auto &playerValue : players) {
                const auto player = playerValue.toObject();
                if (player.value(QStringLiteral("ownerMemberId")) != id) continue;
                auto *row = new QHBoxLayout;
                const auto color = player.value(QStringLiteral("resolvedColor")).toString();
                auto *swatch = new QLabel(card);
                QPixmap ball(20, 20); ball.fill(Qt::transparent);
                QPainter painter(&ball); painter.setRenderHint(QPainter::Antialiasing);
                painter.setPen(palette().color(QPalette::Mid)); painter.setBrush(colorFromRgba(color));
                painter.drawEllipse(QRectF(1, 1, 18, 18)); painter.end();
                swatch->setPixmap(ball); row->addWidget(swatch);
                auto *label = new QLabel(player.value(QStringLiteral("displayName")).toString(), card);
                label->setTextFormat(Qt::PlainText);
                label->setWordWrap(true); row->addWidget(label, 1);
                if (colorCounts.value(color.toLower()) > 1) {
                    auto *warning = new QLabel(QStringLiteral("⚠"), card);
                    warning->setToolTip(i18n("Shared ball color. Player names identify each ball."));
                    warning->setAccessibleName(warning->toolTip()); row->addWidget(warning);
                }
                if (local) {
                    auto *edit = new QToolButton(card);
                    edit->setIcon(QIcon::fromTheme(QStringLiteral("document-edit")));
                    edit->setAccessibleName(i18n("Edit %1", label->text())); edit->setToolTip(edit->accessibleName());
                    edit->setEnabled(open); row->addWidget(edit);
                    connect(edit, &QToolButton::clicked, this, [this, player] { editPlayer(player); });
                    auto *remove = new QToolButton(card);
                    remove->setIcon(QIcon::fromTheme(QStringLiteral("list-remove-user")));
                    remove->setAccessibleName(i18n("Remove %1", label->text())); remove->setToolTip(remove->accessibleName());
                    remove->setEnabled(open && owned > 1); row->addWidget(remove);
                    connect(remove, &QToolButton::clicked, this, [this, player] { m_coordinator.removePlayer(player.value(QStringLiteral("playerId")).toString()); });
                }
                cardLayout->addLayout(row);
            }
            if (local) {
                auto *add = new QPushButton(i18n("Add local player"), card);
                add->setObjectName(QStringLiteral("lobbyAddPlayer"));
                add->setEnabled(open && players.size() < 8);
                cardLayout->addWidget(add, 0, Qt::AlignLeft);
                connect(add, &QPushButton::clicked, this, [this] { editPlayer(); });
            }
            m_people->addWidget(card);
        }
    }
    m_primary->setText(!open ? i18n("Preparing match…") : host ? (waiting > 0 ? i18np("Waiting for 1 person…", "Waiting for %1 people…", waiting) : i18n("Start match")) : localReady ? i18n("Not ready") : i18n("I'm ready"));
    const QString reason = !open ? i18n("Preparing the course. You can leave while waiting.") : members.size() < 2 ? i18n("Invite another person using the code above.")
        : waiting > 0 ? i18n("Waiting for %1 to get ready.", waitingNames.join(QStringLiteral(", "))) : i18n("Everyone is ready. The host can start.");
    m_primary->setEnabled(open && (!host || (members.size() >= 2 && waiting == 0)));
    m_primary->setToolTip(m_primary->isEnabled() ? QString() : reason);
    setStatus(reason);
    const QSignalBlocker blockCourse(m_course), blockHost(m_hostControls), blockVoting(m_allowVoting);
    m_course->clear();
    QJsonObject selected;
    for (const auto &value : state.value(QStringLiteral("courses")).toArray()) {
        const auto course = value.toObject();
        m_course->addItem(course.value(QStringLiteral("displayName")).toString(), course.value(QStringLiteral("courseId")));
        if (course.value(QStringLiteral("courseId")) == state.value(QStringLiteral("selectedCourseId"))) selected = course;
    }
    m_course->setCurrentIndex(m_course->findData(state.value(QStringLiteral("selectedCourseId"))));
    m_course->setVisible(host);
    m_course->setEnabled(open);
    m_custom->setVisible(host); m_custom->setEnabled(open);
    m_hostControls->setChecked(state.value(QStringLiteral("hostControls")).toBool());
    m_allowVoting->setChecked(state.value(QStringLiteral("allowVoting")).toBool(true));
    m_hostControls->setEnabled(open && host); m_allowVoting->setEnabled(open && host);
    m_hostControls->setToolTip(host ? QString() : i18n("Only the host can change lobby rules."));
    m_allowVoting->setToolTip(m_hostControls->toolTip());
    m_courseDetails->setText(i18n("%1\n%2 holes · par %3\nby %4", selected.value(QStringLiteral("displayName")).toString(), selected.value(QStringLiteral("holes")).toInt(), selected.value(QStringLiteral("totalPar")).toInt(), selected.value(QStringLiteral("author")).toString()));
    updateThumbnail(selected);
}

void LobbyView::updateThumbnail(const QJsonObject &course)
{
    const auto key = course.value(QStringLiteral("courseId")).toString();
    if (m_thumbnailKey == key) return;
    m_thumbnailKey = key;
    m_thumbnail->clear();
    QString path;
    if (course.value(QStringLiteral("source")) == QLatin1String("uploaded")) path = courseCachePath(course.value(QStringLiteral("sha256")).toString());
    else {
        const auto resource = course.value(QStringLiteral("resourceName")).toString();
        static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_.-]+$"));
        if (resource != QLatin1String("..") && valid.match(resource).hasMatch())
            path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("kolf/courses/%1").arg(resource));
        if (key == QLatin1String("test")) path = qEnvironmentVariable("KOLF_ONLINE_TEST_COURSE", path);
    }
    if (!QFileInfo::exists(path)) { m_thumbnail->setText(i18n("Preview available after the course is downloaded.")); return; }
    QTimer::singleShot(0, this, [this, path, key] {
        if (m_thumbnailKey != key) return;
        const auto pixmap = Kolf::Ui::NewGameView::thumbnail(m_factory, path);
        if (m_thumbnailKey == key) m_thumbnail->setPixmap(pixmap.scaled(180, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });
}
