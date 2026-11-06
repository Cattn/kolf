// SPDX-License-Identifier: GPL-2.0-or-later
#include "newgameview.h"

#include "game.h"
#include "itemfactory.h"

#include <KColorButton>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <KSharedConfig>

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QPainterPath>
#include <QSignalBlocker>
#include <algorithm>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QEventLoop>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Kolf::Ui;

namespace {
const QList<QColor> &defaultColors()
{
    static const QList<QColor> colors{QColor(0x00, 0x72, 0xb2), QColor(0xe6, 0x9f, 0x00), QColor(0xcc, 0x79, 0xa7),
        QColor(0xd5, 0x5e, 0x00), QColor(0x56, 0xb4, 0xe9), QColor(0x33, 0x22, 0x88), QColor(0xaa, 0x33, 0x77),
        QColor(0x33, 0x33, 0x33), QColor(0xf0, 0xe4, 0x42), QColor(0xff, 0xff, 0xff)};
    return colors;
}
constexpr int MaximumPlayers = 10;
const QSize ThumbnailSize(260, 260);

KConfigGroup modeGroup()
{
    return KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("New Game Dialog Mode"));
}
}

NewGameView::NewGameView(const Kolf::ItemFactory &factory, QWidget *parent)
    : QWidget(parent)
    , m_factory(factory)
{
    setObjectName(QStringLiteral("newGameView"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(20, 14, 20, 14);
    auto *title = new QLabel(i18nc("@title", "New game"), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.6);
    titleFont.setBold(true);
    title->setFont(titleFont);
    outer->addWidget(title);

    auto *columns = new QHBoxLayout;
    auto *playersBox = new QGroupBox(i18n("Players"), this);
    auto *playersLayout = new QVBoxLayout(playersBox);
    m_playerRows = new QVBoxLayout;
    playersLayout->addLayout(m_playerRows);
    m_addPlayer = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add-user")), i18nc("@action:button", "Add Player"), playersBox);
    m_addPlayer->setObjectName(QStringLiteral("setupAddPlayer"));
    auto *addRow = new QHBoxLayout;
    addRow->addWidget(m_addPlayer);
    addRow->addStretch();
    playersLayout->addLayout(addRow);
    playersLayout->addStretch();
    columns->addWidget(playersBox, 1);

    auto *courseBox = new QGroupBox(i18n("Course"), this);
    auto *courseLayout = new QVBoxLayout(courseBox);
    auto *picker = new QHBoxLayout;
    m_previous = new QToolButton(courseBox);
    m_previous->setIcon(QIcon::fromTheme(QStringLiteral("go-previous")));
    m_previous->setAccessibleName(i18n("Previous course"));
    m_course = new QComboBox(courseBox);
    m_course->setObjectName(QStringLiteral("setupCourse"));
    m_course->setAccessibleName(i18n("Course"));
    m_next = new QToolButton(courseBox);
    m_next->setObjectName(QStringLiteral("setupNextCourse"));
    m_next->setIcon(QIcon::fromTheme(QStringLiteral("go-next")));
    m_next->setAccessibleName(i18n("Next course"));
    picker->addWidget(m_previous);
    picker->addWidget(m_course, 1);
    picker->addWidget(m_next);
    courseLayout->addLayout(picker);
    auto *details = new QHBoxLayout;
    m_thumbnail = new QLabel(courseBox);
    m_thumbnail->setFixedSize(ThumbnailSize);
    m_thumbnail->setAlignment(Qt::AlignCenter);
    details->addWidget(m_thumbnail);
    auto *text = new QVBoxLayout;
    m_courseName = new QLabel(courseBox);
    QFont nameFont = m_courseName->font();
    nameFont.setPointSizeF(nameFont.pointSizeF() * 1.3);
    nameFont.setBold(true);
    m_courseName->setFont(nameFont);
    m_courseName->setWordWrap(true);
    text->addWidget(m_courseName);
    m_courseDetails = new QLabel(courseBox);
    m_courseDetails->setWordWrap(true);
    text->addWidget(m_courseDetails);
    m_highScores = new QPushButton(QIcon::fromTheme(QStringLiteral("games-highscores")), i18nc("@action:button", "High Scores"), courseBox);
    text->addWidget(m_highScores, 0, Qt::AlignLeft);
    text->addStretch();
    m_openCourse = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")), i18nc("@action:button", "Open Course File…"), courseBox);
    m_removeCourse = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), i18nc("@action:button", "Remove From List"), courseBox);
    text->addWidget(m_openCourse, 0, Qt::AlignLeft);
    text->addWidget(m_removeCourse, 0, Qt::AlignLeft);
    details->addLayout(text, 1);
    courseLayout->addLayout(details);
    courseLayout->addStretch();
    columns->addWidget(courseBox, 1);
    outer->addLayout(columns, 1);

    m_strict = new QCheckBox(i18n("Strict mode: no undo, editing, or switching holes; scores count for high scores"), this);
    m_strict->setObjectName(QStringLiteral("setupStrict"));
    outer->addWidget(m_strict);
    auto *buttons = new QHBoxLayout;
    auto *back = new QPushButton(QIcon::fromTheme(QStringLiteral("go-previous")), i18nc("@action:button", "Back"), this);
    back->setObjectName(QStringLiteral("setupBack"));
    m_start = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), i18nc("@action:button", "Start Game"), this);
    m_start->setObjectName(QStringLiteral("setupStart"));
    m_start->setDefault(true);
    buttons->addWidget(back);
    buttons->addStretch();
    buttons->addWidget(m_start);
    outer->addLayout(buttons);

    connect(m_addPlayer, &QPushButton::clicked, this, [this] {
        addPlayerRow(i18n("Player %1", m_playerRows->count() + 1), nextFreeColor());
    });
    connect(m_course, &QComboBox::currentIndexChanged, this, &NewGameView::updateCourse);
    connect(m_previous, &QToolButton::clicked, this, [this] { step(-1); });
    connect(m_next, &QToolButton::clicked, this, [this] { step(1); });
    connect(m_openCourse, &QPushButton::clicked, this, &NewGameView::openCourseFile);
    connect(m_removeCourse, &QPushButton::clicked, this, &NewGameView::removeCourse);
    connect(m_highScores, &QPushButton::clicked, this, [this] { Q_EMIT highScoresRequested(m_course->currentData().toString()); });
    connect(back, &QPushButton::clicked, this, &NewGameView::backRequested);
    connect(m_start, &QPushButton::clicked, this, &NewGameView::start);
}

void NewGameView::prepare(const QString &lockedCourse)
{
    while (auto *item = m_playerRows->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const KConfigGroup roster(KSharedConfig::openConfig(), QStringLiteral("Local Players"));
    const QStringList names = roster.readEntry("names", QStringList());
    const QStringList colors = roster.readEntry("colors", QStringList());
    for (int index = 0; index < names.size() && index < MaximumPlayers; ++index)
        addPlayerRow(names.at(index), QColor(colors.value(index)).isValid() ? QColor(colors.value(index)) : nextFreeColor());
    if (m_playerRows->count() == 0) {
        addPlayerRow(i18n("Player %1", 1), defaultColors().at(0));
        addPlayerRow(i18n("Player %1", 2), defaultColors().at(1));
    }
    m_strict->setChecked(modeGroup().readEntry("competition", false));
    m_locked = !lockedCourse.isEmpty();
    loadCourses();
    if (m_locked) {
        int index = m_course->findData(lockedCourse);
        if (index < 0) {
            CourseInfo info;
            KolfGame::courseInfo(info, lockedCourse);
            m_course->insertItem(0, info.name.isEmpty() ? QFileInfo(lockedCourse).fileName() : info.name, lockedCourse);
            index = 0;
        }
        m_course->setCurrentIndex(index);
    }
    for (auto *widget : std::initializer_list<QWidget *>{m_course, m_previous, m_next, m_openCourse, m_removeCourse})
        widget->setEnabled(!m_locked);
    updateCourse();
}

void NewGameView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_start->setFocus();
}

void NewGameView::loadCourses()
{
    const QSignalBlocker blocker(m_course);
    m_course->clear();
    m_extraCourses = modeGroup().readEntry("extra", QStringList());
    QStringList files;
    const QStringList directories = QStandardPaths::locateAll(QStandardPaths::AppDataLocation, QStringLiteral("courses"), QStandardPaths::LocateDirectory);
    for (const QString &directory : directories) {
        const QStringList manifest = [&directory] {
            QFile file(QDir(directory).filePath(QStringLiteral("manifest.txt")));
            QStringList order;
            if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                for (const auto &line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n')))
                    if (line.contains(QLatin1Char('|'))) order.append(line.section(QLatin1Char('|'), 1).trimmed());
            return order;
        }();
        QStringList entries = QDir(directory).entryList(QDir::Files, QDir::Name);
        entries.removeAll(QStringLiteral("manifest.txt"));
        std::stable_sort(entries.begin(), entries.end(), [&manifest](const QString &a, const QString &b) {
            const auto rank = [&manifest](const QString &name) { const auto index = manifest.indexOf(name); return index < 0 ? 1000 : int(index); };
            return rank(a) < rank(b);
        });
        for (const auto &entry : entries) files.append(QDir(directory).filePath(entry));
    }
    for (const QString &file : m_extraCourses + files) {
        if (!QFileInfo::exists(file) || m_course->findData(file) >= 0) continue;
        CourseInfo info;
        KolfGame::courseInfo(info, file);
        m_course->addItem(info.name.isEmpty() ? QFileInfo(file).fileName() : info.name, file);
    }
    const int last = m_course->findData(modeGroup().readEntry("course", QString()));
    m_course->setCurrentIndex(last >= 0 ? last : 0);
}

void NewGameView::addPlayerRow(const QString &name, const QColor &color)
{
    if (m_playerRows->count() >= MaximumPlayers) return;
    auto *row = new QWidget(this);
    row->setObjectName(QStringLiteral("setupPlayerRow"));
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *colorButton = new KColorButton(color, row);
    colorButton->setObjectName(QStringLiteral("setupPlayerColor"));
    colorButton->setAccessibleName(i18n("Ball color"));
    colorButton->setFixedWidth(48);
    auto *edit = new QLineEdit(name, row);
    edit->setObjectName(QStringLiteral("setupPlayerName"));
    edit->setAccessibleName(i18n("Player name"));
    edit->setMaxLength(32);
    auto *remove = new QToolButton(row);
    remove->setIcon(QIcon::fromTheme(QStringLiteral("list-remove-user")));
    remove->setToolTip(i18n("Remove this player"));
    remove->setAccessibleName(i18n("Remove player"));
    layout->addWidget(colorButton);
    layout->addWidget(edit, 1);
    layout->addWidget(remove);
    m_playerRows->addWidget(row);
    connect(remove, &QToolButton::clicked, this, [this, row] {
        if (m_playerRows->count() <= 1) return;
        m_playerRows->removeWidget(row);
        row->deleteLater();
        QMetaObject::invokeMethod(this, &NewGameView::renumberPlayers, Qt::QueuedConnection);
    });
    renumberPlayers();
}

void NewGameView::renumberPlayers()
{
    const int count = m_playerRows->count();
    m_addPlayer->setEnabled(count < MaximumPlayers);
    for (int index = 0; index < count; ++index) {
        auto *row = m_playerRows->itemAt(index)->widget();
        if (auto *remove = row ? row->findChild<QToolButton *>() : nullptr) remove->setEnabled(count > 1);
    }
}

QColor NewGameView::nextFreeColor() const
{
    QList<QColor> used;
    for (int index = 0; index < m_playerRows->count(); ++index)
        if (auto *button = m_playerRows->itemAt(index)->widget()->findChild<KColorButton *>()) used.append(button->color());
    for (const auto &color : defaultColors())
        if (!used.contains(color)) return color;
    return defaultColors().at(m_playerRows->count() % defaultColors().size());
}

void NewGameView::step(int delta)
{
    if (m_course->count() == 0) return;
    m_course->setCurrentIndex((m_course->currentIndex() + delta + m_course->count()) % m_course->count());
}

void NewGameView::updateCourse()
{
    const QString file = m_course->currentData().toString();
    m_removeCourse->setVisible(!m_locked && m_extraCourses.contains(file));
    m_start->setEnabled(!file.isEmpty());
    if (file.isEmpty()) {
        m_courseName->setText(i18n("No course selected"));
        m_courseDetails->clear();
        m_thumbnail->clear();
        return;
    }
    CourseInfo info;
    KolfGame::courseInfo(info, file);
    m_courseName->setText(info.name);
    QStringList lines{i18ncp("@info", "1 hole", "%1 holes", info.holes) + QStringLiteral(" · ") + i18nc("@info", "par %1", info.par)};
    if (!info.author.isEmpty()) lines << i18nc("@info author attribution", "by %1", info.author);
    m_courseDetails->setText(lines.join(QLatin1Char('\n')));
    m_thumbnail->setPixmap(thumbnail(m_factory, file));
}

QPixmap NewGameView::thumbnail(const Kolf::ItemFactory &factory, const QString &course)
{
    QFile file(course);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) return {};
    const QString key = QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
    static QHash<QString, QPixmap> thumbnails;
    if (thumbnails.contains(key)) return thumbnails.value(key);
    PlayerList players;
    players.append(Player());
    players.last().setId(1);
    players.last().setName(QString());
    players.last().ball()->setColor(defaultColors().at(9));
    QPixmap pixmap(ThumbnailSize);
    pixmap.fill(Qt::transparent);
    {
        KolfGame preview(factory, &players, course, nullptr);
        preview.setAttribute(Qt::WA_DontShowOnScreen);
        preview.setSound(false);
        preview.ignoreEvents(true);
        preview.setShowInfo(false);
        preview.resize(ThumbnailSize);
        preview.show();
        preview.startFirstHole(1);
        preview.hidePutter();
        QEventLoop settle;
        QTimer::singleShot(400, &settle, &QEventLoop::quit);
        settle.exec();
        preview.pause();
        pixmap = preview.grab();
    }
    QPixmap framed(ThumbnailSize);
    framed.fill(Qt::transparent);
    QPainter painter(&framed);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(QPointF(), QSizeF(ThumbnailSize)), 8, 8);
    painter.setClipPath(clip);
    painter.drawPixmap(0, 0, pixmap.scaled(ThumbnailSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    thumbnails.insert(key, framed);
    return framed;
}

void NewGameView::openCourseFile()
{
    const QString file = QFileDialog::getOpenFileName(this, i18nc("@title:window", "Choose a Kolf Course"), QString(),
        i18n("Kolf courses (*.kolf *.course);;All files (*)"));
    if (file.isEmpty()) return;
    if (!m_extraCourses.contains(file)) m_extraCourses.prepend(file);
    auto group = modeGroup();
    group.writeEntry("extra", m_extraCourses);
    group.writeEntry("course", file);
    group.sync();
    loadCourses();
    updateCourse();
}

void NewGameView::removeCourse()
{
    const QString file = m_course->currentData().toString();
    m_extraCourses.removeAll(file);
    auto group = modeGroup();
    group.writeEntry("extra", m_extraCourses);
    group.sync();
    loadCourses();
    updateCourse();
}

void NewGameView::start()
{
    QList<LocalPlayer> players;
    QStringList names, colors;
    for (int index = 0; index < m_playerRows->count(); ++index) {
        auto *row = m_playerRows->itemAt(index)->widget();
        const QString name = row->findChild<QLineEdit *>()->text().trimmed();
        const QColor color = row->findChild<KColorButton *>()->color();
        players.append({name.isEmpty() ? i18n("Player %1", index + 1) : name, color});
        names << players.last().name;
        colors << color.name();
    }
    KConfigGroup roster(KSharedConfig::openConfig(), QStringLiteral("Local Players"));
    roster.writeEntry("names", names);
    roster.writeEntry("colors", colors);
    auto group = modeGroup();
    group.writeEntry("competition", m_strict->isChecked());
    if (!m_locked) group.writeEntry("course", m_course->currentData().toString());
    group.writeEntry("extra", m_extraCourses);
    group.sync();
    Q_EMIT startRequested(players, m_course->currentData().toString(), m_strict->isChecked());
}
