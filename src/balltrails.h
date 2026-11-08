// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QGraphicsObject>
#include <QHash>
#include <QList>

class Ball;
class QTimer;

namespace Kolf {

class BallTrails : public QGraphicsObject {
public:
    explicit BallTrails(QGraphicsItem *parent);
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    struct Point {
        QPointF position;
        qint64 time;
    };
    struct Trail {
        QPointF last;
        QColor color;
        QList<Point> points;
    };

    void sample();
    void refreshVisibility();
    void clear();

    QTimer *m_timer;
    QElapsedTimer m_clock;
    QHash<const Ball *, Trail> m_trails;
    QRectF m_bounds;
};

}
