// SPDX-License-Identifier: GPL-2.0-or-later
#include "gamehud.h"

#include <KLocalizedString>

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Kolf::Ui;

namespace {
QPixmap swatch(const QColor &color, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(0, 0, 0, 140), 1.5));
    painter.setBrush(color);
    painter.drawEllipse(QRectF(1, 1, size - 2, size - 2));
    return pixmap;
}
}

GameHud::GameHud(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("gameHud"));
    setFrameShape(QFrame::StyledPanel);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 6, 10, 6);
    outer->setSpacing(4);

    auto *row = new QHBoxLayout;
    m_header = row;
    m_hole = new QLabel(this);
    m_hole->setObjectName(QStringLiteral("hudHole"));
    QFont strong = m_hole->font();
    row->addWidget(m_hole);
    row->addStretch();
    m_swatch = new QLabel(this);
    row->addWidget(m_swatch);
    m_turn = new QLabel(this);
    m_turn->setObjectName(QStringLiteral("hudTurn"));
    strong.setBold(true);
    m_turn->setFont(strong);
    m_turn->setWordWrap(true);
    m_turn->setAccessibleName(i18n("Whose turn it is"));
    row->addWidget(m_turn);
    m_stroke = new QLabel(this);
    m_stroke->setObjectName(QStringLiteral("hudStroke"));
    row->addSpacing(12);
    row->addWidget(m_stroke);
    row->addStretch();
    outer->addLayout(row);

    auto *hintCard = new QFrame(this);
    hintCard->setFrameShape(QFrame::StyledPanel);
    hintCard->setAutoFillBackground(true);
    hintCard->setBackgroundRole(QPalette::AlternateBase);
    m_hintRow = hintCard;
    m_hintRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_hintRow->setObjectName(QStringLiteral("hudHint"));
    auto *hintLayout = new QHBoxLayout(m_hintRow);
    hintLayout->setContentsMargins(8, 4, 8, 4);
    auto *hintIcon = new QLabel(m_hintRow);
    hintIcon->setPixmap(QIcon::fromTheme(QStringLiteral("help-hint")).pixmap(16, 16));
    hintLayout->addWidget(hintIcon);
    m_hint = new QLabel(m_hintRow);
    m_hint->setWordWrap(true);
    hintLayout->addWidget(m_hint, 1);
    m_dismissHint = new QToolButton(m_hintRow);
    m_dismissHint->setObjectName(QStringLiteral("hudDismissHint"));
    m_dismissHint->setIcon(QIcon::fromTheme(QStringLiteral("dialog-close")));
    m_dismissHint->setAutoRaise(true);
    m_dismissHint->setToolTip(i18n("Hide this tip"));
    m_dismissHint->setAccessibleName(i18n("Hide this tip"));
    hintLayout->addWidget(m_dismissHint);
    outer->addWidget(m_hintRow);
    m_hintRow->hide();
    connect(m_dismissHint, &QToolButton::clicked, this, [this] {
        clearHint();
        Q_EMIT hintDismissed();
    });
}

void GameHud::setHole(int hole, int holes, int par)
{
    m_hole->setText(holes > 0
        ? i18nc("@info HUD: hole number, hole count, par", "Hole %1 of %2   Par %3", hole, holes, par)
        : i18nc("@info HUD: hole number, par", "Hole %1   Par %2", hole, par));
}

void GameHud::setTurn(const QString &name, const QColor &color, int stroke, bool yourTurn, bool markLocalTurn)
{
    const int size = m_turn->fontMetrics().height();
    m_swatch->setPixmap(swatch(color, size));
    m_swatch->show();
    m_turn->setText(yourTurn && markLocalTurn
        ? i18nc("@info HUD: local player name", "Your turn: %1", name)
        : yourTurn
            ? i18nc("@info HUD: active player name", "%1's turn", name)
            : i18nc("@info HUD: waiting for another player", "Waiting for %1", name));
    m_stroke->setText(i18nc("@info HUD: stroke number", "Stroke %1", stroke));
    m_stroke->show();
}

void GameHud::setMessage(const QString &message)
{
    m_swatch->hide();
    m_stroke->hide();
    m_turn->setText(message);
}

void GameHud::setEditing(int hole, int holes)
{
    m_hole->setText(i18nc("@info HUD in course editor", "Editing hole %1 of %2", hole, qMax(hole, holes)));
    setMessage(i18n("Course editor"));
}

void GameHud::setHint(const QString &hint)
{
    m_hint->setText(hint);
    m_hintRow->show();
}

void GameHud::clearHint()
{
    m_hintRow->hide();
}

void GameHud::addControl(QWidget *widget)
{
    m_header->addWidget(widget);
}
