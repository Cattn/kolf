// SPDX-License-Identifier: GPL-2.0-or-later
#include "homeview.h"

#include "game.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QCommandLinkButton>
#include <QEvent>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

using namespace Kolf::Ui;

namespace {
QCommandLinkButton *homeButton(QWidget *parent, const QString &objectName, const QString &icon,
    const QString &title, const QString &description)
{
    auto *button = new QCommandLinkButton(title, description, parent);
    button->setObjectName(objectName);
    button->setIcon(QIcon::fromTheme(icon));
    button->setIconSize(QSize(32, 32));
    return button;
}
}

HomeView::HomeView(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("homeView"));
    setAccessibleName(i18n("Main menu"));
    setFixedWidth(380);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(4);

    auto *title = new QLabel(i18nc("@title application name on the main menu", "Kolf"), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 2.4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    auto *subtitle = new QLabel(i18n("Miniature golf"), this);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(subtitle);
    layout->addSpacing(12);

    m_continue = homeButton(this, QStringLiteral("homeContinue"), QStringLiteral("media-playback-start"),
        i18nc("@action:button", "Continue"), QString());
    m_play = homeButton(this, QStringLiteral("homePlay"), QStringLiteral("games-config-board"),
        i18nc("@action:button", "Play"), i18n("A local game for 1 to 10 players on one computer"));
    auto *online = homeButton(this, QStringLiteral("homeOnline"), QStringLiteral("network-connect"),
        i18nc("@action:button", "Play Online"), i18n("Host or join a game with friends"));
    auto *editor = homeButton(this, QStringLiteral("homeEditor"), QStringLiteral("document-edit"),
        i18nc("@action:button", "Course Editor"), i18n("Build a new course of your own"));
    auto *tutorial = homeButton(this, QStringLiteral("homeTutorial"), QStringLiteral("help-contents"),
        i18nc("@action:button", "How to Play"), i18n("A short guided course that teaches the controls"));
    for (auto *button : {m_continue, m_play, online, editor, tutorial})
        layout->addWidget(button);

    connect(m_continue, &QCommandLinkButton::clicked, this, [this] { Q_EMIT continueRequested(m_savedGame); });
    connect(m_play, &QCommandLinkButton::clicked, this, &HomeView::playRequested);
    connect(online, &QCommandLinkButton::clicked, this, &HomeView::onlineRequested);
    connect(editor, &QCommandLinkButton::clicked, this, &HomeView::editorRequested);
    connect(tutorial, &QCommandLinkButton::clicked, this, &HomeView::tutorialRequested);
    refresh();
}

QString HomeView::lastSavedGame()
{
    const KConfigGroup settings(KSharedConfig::openConfig(), QStringLiteral("Settings"));
    return settings.readEntry("lastSavedGame", QString());
}

void HomeView::rememberSavedGame(const QString &path)
{
    if (path.isEmpty()) return;
    KConfigGroup settings(KSharedConfig::openConfig(), QStringLiteral("Settings"));
    settings.writeEntry("lastSavedGame", QFileInfo(path).absoluteFilePath());
    settings.sync();
}

void HomeView::refresh()
{
    m_savedGame.clear();
    const QString path = lastSavedGame();
    const QFileInfo file(path);
    if (!path.isEmpty() && file.isFile()) {
        KConfig saved(path, KConfig::SimpleConfig);
        const KConfigGroup group(saved.group(QStringLiteral("0 Saved Game")));
        const QString course = group.readEntry("Course", QString());
        if (!course.isEmpty() && QFileInfo(course).isFile()) {
            CourseInfo info;
            KolfGame::courseInfo(info, course);
            m_savedGame = path;
            m_continue->setDescription(i18nc("@info continue a saved game: course, hole, save time",
                "%1 · hole %2 · saved %3", info.name, group.readEntry("Current Hole", 1),
                QLocale().toString(file.lastModified(), QLocale::ShortFormat)));
        }
    }
    m_continue->setVisible(!m_savedGame.isEmpty());
    adjustSize();
    reposition();
}

void HomeView::setAnchor(QWidget *anchor)
{
    if (m_anchor) m_anchor->removeEventFilter(this);
    m_anchor = anchor;
    if (m_anchor) m_anchor->installEventFilter(this);
    if (parentWidget()) parentWidget()->installEventFilter(this);
    reposition();
}

bool HomeView::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize || event->type() == QEvent::Move || event->type() == QEvent::Show)
        reposition();
    return QFrame::eventFilter(watched, event);
}

void HomeView::reposition()
{
    auto *container = parentWidget();
    if (!container) return;
    const QRect area = m_anchor && m_anchor->isVisible() ? m_anchor->geometry() : container->rect();
    const int naturalHeight = hasHeightForWidth() ? heightForWidth(width()) : sizeHint().height();
    const QSize size = QSize(width(), naturalHeight).boundedTo(container->size());
    QRect target(QPoint(), size);
    target.moveCenter(area.center());
    target.moveLeft(qBound(0, target.left(), qMax(0, container->width() - size.width())));
    target.moveTop(qBound(0, target.top(), qMax(0, container->height() - size.height())));
    setGeometry(target);
}

void HomeView::showEvent(QShowEvent *event)
{
    QFrame::showEvent(event);
    reposition();
    (m_savedGame.isEmpty() ? m_play : m_continue)->setFocus();
}

void HomeView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor background = palette().color(QPalette::Window);
    background.setAlpha(238);
    QColor border = palette().color(QPalette::Mid);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    painter.fillPath(path, background);
    painter.setPen(QPen(border, 1));
    painter.drawPath(path);
}
