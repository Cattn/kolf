// SPDX-License-Identifier: GPL-2.0-or-later
#include "balltrails.h"

#include "ball.h"
#include "effects.h"

#include <QPainter>
#include <QTimer>

namespace Kolf {
namespace {
constexpr int SampleMilliseconds = 20;
constexpr qint64 FullTailMilliseconds = 450;
constexpr qint64 ReducedTailMilliseconds = 200;
constexpr double MinimumStep = 1.0;
constexpr double JumpDistance = 40;

qint64 tailMilliseconds()
{
    return Effects::level() == Effects::Level::Reduced ? ReducedTailMilliseconds : FullTailMilliseconds;
}
}

BallTrails::BallTrails(QGraphicsItem *parent)
    : QGraphicsObject(parent)
    , m_timer(new QTimer(this))
{
    setAcceptedMouseButtons(Qt::NoButton);
    m_clock.start();
    m_timer->setInterval(SampleMilliseconds);
    connect(m_timer, &QTimer::timeout, this, &BallTrails::sample);
    connect(Effects::notifier(), &Effects::Notifier::changed, this, &BallTrails::refreshVisibility);
    refreshVisibility();
}

void BallTrails::refreshVisibility()
{
    const bool enabled = Effects::ballTrails() && Effects::level() != Effects::Level::Off;
    setVisible(enabled);
    if (enabled) {
        m_timer->start();
    } else {
        m_timer->stop();
        clear();
    }
}

void BallTrails::clear()
{
    prepareGeometryChange();
    m_trails.clear();
    m_bounds = QRectF();
}

void BallTrails::sample()
{
    const qint64 now = m_clock.elapsed();
    const qint64 cutoff = now - tailMilliseconds();
    QHash<const Ball *, Trail> next;
    qreal z = 0;
    if (parentItem()) {
        for (auto *item : parentItem()->childItems()) {
            const auto *ball = dynamic_cast<const Ball *>(item);
            if (!ball || !ball->isVisible()) continue;
            z = qMax(z, ball->zValue());
            Trail trail = m_trails.value(ball, Trail{ball->pos(), ball->color(), {}});
            const QPointF position = ball->pos();
            const double step = QLineF(trail.last, position).length();
            if (step > JumpDistance) {
                trail.points.clear();
                trail.last = position;
            } else if (step > MinimumStep) {
                if (trail.points.isEmpty()) trail.points.append({trail.last, now});
                trail.points.append({position, now});
                trail.last = position;
            }
            trail.color = ball->color();
            while (!trail.points.isEmpty() && trail.points.constFirst().time < cutoff) trail.points.removeFirst();
            next.insert(ball, trail);
        }
    }
    prepareGeometryChange();
    m_trails = next;
    m_bounds = QRectF();
    for (const auto &trail : std::as_const(m_trails))
        for (const auto &point : trail.points)
            m_bounds = m_bounds.united(QRectF(point.position, QSizeF(0.01, 0.01)));
    if (!m_bounds.isNull()) m_bounds.adjust(-4, -4, 4, 4);
    setZValue(z - 0.5);
    update();
}

QRectF BallTrails::boundingRect() const
{
    return m_bounds;
}

void BallTrails::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    const qint64 now = m_clock.elapsed();
    const double tail = double(tailMilliseconds());
    painter->setRenderHint(QPainter::Antialiasing);
    for (const auto &trail : std::as_const(m_trails)) {
        QColor color = trail.color.lighter(130);
        for (int i = 1; i < trail.points.size(); ++i) {
            const auto &from = trail.points.at(i - 1);
            const auto &to = trail.points.at(i);
            const double life = qBound(0.0, 1.0 - double(now - from.time) / tail, 1.0);
            if (life <= 0) continue;
            color.setAlphaF(0.75 * life);
            painter->setPen(QPen(color, 1.5 + 2.0 * life, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->drawLine(from.position, to.position);
        }
    }
}

}
