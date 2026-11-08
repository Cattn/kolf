// SPDX-License-Identifier: GPL-2.0-or-later
#include "impacteffects.h"

#include "effects.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QtMath>

namespace Kolf {
namespace {
constexpr int FrameMilliseconds = 16;

qint64 minimumInterval(Impact kind)
{
    switch (kind) {
    case Impact::Dust: return 90;
    case Impact::Tap: return 60;
    case Impact::Splash: return 0;
    }
    return 0;
}
}

ImpactEffects::ImpactEffects(QGraphicsItem *parent)
    : QGraphicsObject(parent)
    , m_timer(new QTimer(this))
{
    setAcceptedMouseButtons(Qt::NoButton);
    setZValue(1000);
    m_clock.start();
    m_timer->setInterval(FrameMilliseconds);
    connect(m_timer, &QTimer::timeout, this, &ImpactEffects::tick);
}

void ImpactEffects::spawn(Impact kind, const QPointF &position, double strength)
{
    const auto level = Effects::level();
    if (level == Effects::Level::Off) return;
    const qint64 now = m_clock.elapsed();
    const int key = int(kind);
    if (m_lastSpawn.contains(key) && now - m_lastSpawn.value(key) < minimumInterval(kind)) return;
    m_lastSpawn.insert(key, now);
    const bool reduced = level == Effects::Level::Reduced;
    const auto random = [this] {
        m_noise ^= m_noise << 13;
        m_noise ^= m_noise >> 17;
        m_noise ^= m_noise << 5;
        return double(m_noise % 10000) / 10000.0;
    };
    const auto burst = [&](int count, double speed, double radius, qint64 life, const QColor &color) {
        for (int i = 0; i < count; ++i) {
            const double angle = random() * 2 * M_PI;
            const double velocity = speed * (0.5 + random());
            m_particles.append({position, QPointF(std::cos(angle), std::sin(angle)) * velocity, now,
                life, radius * (0.7 + 0.6 * random()), 0, color, false, {}});
        }
    };
    const double force = qBound(0.2, strength, 4.0);
    switch (kind) {
    case Impact::Dust:
        burst(reduced ? 2 : 4, 6 + 4 * force, 2.8, 500, QColor(214, 184, 120, 235));
        break;
    case Impact::Splash:
        m_particles.append({position, {}, now, 520, 4, 26, QColor(190, 225, 255, 220), true, {}});
        burst(reduced ? 3 : 9, 30, 1.6, 420, QColor(120, 185, 240, 230));
        break;
    case Impact::Tap:
        m_particles.append({position, {}, now, 220, 2, 14 * force / 2, QColor(255, 255, 255, 170), true, {}});
        if (!reduced) burst(3, 18 * force, 1.0, 200, QColor(255, 255, 255, 220));
        break;
    }
    if (!m_timer->isActive()) m_timer->start();
    tick();
}

void ImpactEffects::celebrate(const QPointF &position, const QString &text, bool big)
{
    const auto level = Effects::level();
    if (level == Effects::Level::Off) return;
    const qint64 now = m_clock.elapsed();
    const QColor colors[] = {QColor(246, 116, 0), QColor(61, 174, 233), QColor(39, 174, 96), QColor(253, 188, 75), QColor(218, 68, 83), QColor(255, 255, 255)};
    const int count = level == Effects::Level::Reduced ? (big ? 8 : 4) : (big ? 28 : 12);
    for (int i = 0; i < count; ++i) {
        m_noise ^= m_noise << 13;
        m_noise ^= m_noise >> 17;
        m_noise ^= m_noise << 5;
        const double angle = (i + double(m_noise % 100) / 100.0) * 2 * M_PI / count;
        const double speed = (big ? 70 : 45) * (0.6 + double((m_noise >> 8) % 100) / 200.0);
        m_particles.append({position, QPointF(std::cos(angle), std::sin(angle)) * speed, now, big ? 1000 : 750,
            big ? 2.2 : 1.8, 0, colors[i % 6], false, {}});
    }
    if (!text.isEmpty())
        m_particles.append({position + QPointF(0, -14), QPointF(0, -16), now, 1600, big ? 15.0 : 12.0, 0, QColor(255, 255, 255), false, text});
    if (!m_timer->isActive()) m_timer->start();
    tick();
}

void ImpactEffects::tick()
{
    const qint64 now = m_clock.elapsed();
    prepareGeometryChange();
    QRectF bounds;
    for (int i = m_particles.size() - 1; i >= 0; --i) {
        const auto &particle = m_particles.at(i);
        if (now - particle.born >= particle.life) {
            m_particles.removeAt(i);
            continue;
        }
        const double seconds = (now - particle.born) / 1000.0;
        const QPointF centre = particle.position + particle.velocity * seconds;
        const double radius = particle.text.isEmpty() ? particle.radius + particle.growth * seconds / (particle.life / 1000.0)
            : particle.radius * (particle.text.size() + 2);
        bounds = bounds.united(QRectF(centre - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius)));
    }
    m_bounds = bounds.adjusted(-2, -2, 2, 2);
    if (m_particles.isEmpty()) m_timer->stop();
    update();
}

QRectF ImpactEffects::boundingRect() const
{
    return m_bounds;
}

void ImpactEffects::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    const qint64 now = m_clock.elapsed();
    painter->setRenderHint(QPainter::Antialiasing);
    for (const auto &particle : std::as_const(m_particles)) {
        const double age = double(now - particle.born) / particle.life;
        if (age >= 1) continue;
        const double seconds = (now - particle.born) / 1000.0;
        const QPointF centre = particle.position + particle.velocity * seconds;
        const double radius = particle.radius + particle.growth * age;
        QColor color = particle.color;
        color.setAlphaF(particle.color.alphaF() * (1 - age));
        if (!particle.text.isEmpty()) {
            const double fade = age < 0.7 ? 1.0 : (1 - age) / 0.3;
            QFont font = painter->font();
            font.setBold(true);
            font.setPixelSize(int(particle.radius));
            painter->setFont(font);
            QPainterPath path;
            const QFontMetricsF metrics(font);
            path.addText(centre - QPointF(metrics.horizontalAdvance(particle.text) / 2, 0), font, particle.text);
            painter->setPen(QPen(QColor(0, 0, 0, int(170 * fade)), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(path);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(255, 255, 255, int(255 * fade)));
            painter->drawPath(path);
            continue;
        }
        if (particle.ring) {
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(color, 1.5));
        } else {
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
        }
        painter->drawEllipse(centre, radius, radius);
    }
}

}
