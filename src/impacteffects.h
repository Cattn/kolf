// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QGraphicsObject>
#include <QHash>
#include <QList>

class QTimer;

namespace Kolf {

enum class Impact { Dust, Splash, Tap };

class ImpactEffects : public QGraphicsObject {
public:
    explicit ImpactEffects(QGraphicsItem *parent);
    void spawn(Impact kind, const QPointF &position, double strength);
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    struct Particle {
        QPointF position;
        QPointF velocity;
        qint64 born;
        qint64 life;
        double radius;
        double growth;
        QColor color;
        bool ring;
    };

    void tick();

    QTimer *m_timer;
    QElapsedTimer m_clock;
    QList<Particle> m_particles;
    QHash<int, qint64> m_lastSpawn;
    QRectF m_bounds;
    quint32 m_noise = 0x9e3779b9u;
};

}
