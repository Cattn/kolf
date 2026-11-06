// SPDX-License-Identifier: GPL-2.0-or-later
#include "hazardprompt.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QCheckBox>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

using namespace Kolf::Ui;

namespace {
const char *const RememberKey = "hazardOptions";

KConfigGroup notificationMessages()
{
    return KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("Notification Messages"));
}

bool rememberedChoice(HazardPrompt::Choice &choice)
{
    const QString value = notificationMessages().readEntry(RememberKey, QString());
    if (value.isEmpty()) return false;
    if (value == QLatin1String("rehit") || value == i18nc("@item harzard action", "Rehit From Last Location")) {
        choice = HazardPrompt::Rehit;
        return true;
    }
    if (value == QLatin1String("drop") || value == i18nc("@item harzard action", "Drop Outside of Hazard")) {
        choice = HazardPrompt::Drop;
        return true;
    }
    return false;
}
}

HazardPrompt::HazardPrompt(const QString &playerName, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("hazardPrompt"));
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(i18nc("@title:window", "%1 in a Hazard", playerName));
    setAccessibleName(windowTitle());
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    auto *title = new QLabel(i18n("<b>%1 is in a hazard.</b> One penalty stroke. Where should the next shot be played from?",
        playerName.toHtmlEscaped()), this);
    title->setWordWrap(true);
    title->setMinimumWidth(260);
    layout->addWidget(title);
    auto *drop = new QPushButton(i18nc("@action:button", "Drop Outside (D)"), this);
    drop->setObjectName(QStringLiteral("hazardDrop"));
    drop->setToolTip(i18n("Place the ball just outside the hazard, back along the line it came in on."));
    auto *rehit = new QPushButton(i18nc("@action:button", "Rehit From Last Spot (R)"), this);
    rehit->setObjectName(QStringLiteral("hazardRehit"));
    rehit->setToolTip(i18n("Return the ball to where this shot was played from."));
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(drop);
    buttons->addWidget(rehit);
    layout->addLayout(buttons);
    m_remember = new QCheckBox(i18nc("@option:check", "Always do this (change in Configure Kolf)"), this);
    m_remember->setObjectName(QStringLiteral("hazardRemember"));
    layout->addWidget(m_remember);
    drop->setDefault(true);
    drop->setFocus();
    connect(drop, &QPushButton::clicked, this, [this] { m_choice = Drop; accept(); });
    connect(rehit, &QPushButton::clicked, this, [this] { m_choice = Rehit; accept(); });
}

void HazardPrompt::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_D) { m_choice = Drop; accept(); return; }
    if (event->key() == Qt::Key_R) { m_choice = Rehit; accept(); return; }
    if (event->key() == Qt::Key_Escape) { m_choice = Drop; accept(); return; }
    QDialog::keyPressEvent(event);
}

void HazardPrompt::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 10, 10);
    QColor background = palette().color(QPalette::Window);
    background.setAlpha(245);
    painter.fillPath(path, background);
    painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
    painter.drawPath(path);
}

HazardPrompt::Choice HazardPrompt::ask(QGraphicsView *view, const QPointF &ballScenePos, const QString &playerName)
{
    Choice choice = Drop;
    if (rememberedChoice(choice)) return choice;

    QPointer<HazardPrompt> prompt = new HazardPrompt(playerName, view);
    prompt->setWindowModality(Qt::WindowModal);
    prompt->adjustSize();
    const QPoint ball = view->viewport()->mapToGlobal(view->mapFromScene(ballScenePos));
    const QRect area(view->viewport()->mapToGlobal(QPoint(0, 0)), view->viewport()->size());
    QRect target(ball + QPoint(18, 18), prompt->size());
    if (target.right() > area.right()) target.moveRight(ball.x() - 18);
    if (target.bottom() > area.bottom()) target.moveBottom(ball.y() - 18);
    target.moveLeft(qBound(area.left(), target.left(), qMax(area.left(), area.right() - target.width())));
    target.moveTop(qBound(area.top(), target.top(), qMax(area.top(), area.bottom() - target.height())));
    prompt->move(target.topLeft());
    prompt->exec();
    if (!prompt) return choice;
    choice = prompt->m_choice;
    if (prompt->m_remember->isChecked()) {
        auto group = notificationMessages();
        group.writeEntry(RememberKey, choice == Rehit ? QStringLiteral("rehit") : QStringLiteral("drop"));
        group.sync();
    }
    delete prompt;
    return choice;
}
