// SPDX-License-Identifier: GPL-2.0-or-later
#include "snapshot.h"
#include "canvasitem.h"
#include <QJsonArray>
#include <QPen>
#include <cmath>

namespace Kolf::Replication {
QString visualKind(QGraphicsItem *item) {
    if (dynamic_cast<QGraphicsLineItem *>(item)) return QStringLiteral("line");
    if (dynamic_cast<Tagaro::SpriteObjectItem *>(item)) return QStringLiteral("sprite");
    return QStringLiteral("unsupported");
}
QJsonObject captureVisual(const QString &id, QGraphicsItem *item) {
    QJsonObject v{{QStringLiteral("id"), id}, {QStringLiteral("kind"), visualKind(item)},
        {QStringLiteral("x"), item->x()}, {QStringLiteral("y"), item->y()},
        {QStringLiteral("z"), item->zValue()}, {QStringLiteral("visible"), item->isVisible()},
        {QStringLiteral("rotation"), item->rotation()}, {QStringLiteral("opacity"), item->opacity()}};
    if (auto *line = dynamic_cast<QGraphicsLineItem *>(item)) {
        const auto l = line->line();
        v[QStringLiteral("line")] = QJsonArray{l.x1(), l.y1(), l.x2(), l.y2()};
        v[QStringLiteral("color")] = line->pen().color().name(QColor::HexArgb);
    }
    if (auto *sprite = dynamic_cast<Tagaro::SpriteObjectItem *>(item)) {
        v[QStringLiteral("sprite")] = sprite->spriteKey();
        v[QStringLiteral("frame")] = sprite->frame();
    }
    if (auto *ellipse = dynamic_cast<EllipticalCanvasItem *>(item); ellipse && ellipse->ellipseItem())
        v[QStringLiteral("color")] = ellipse->ellipseItem()->brush().color().name(QColor::HexArgb);
    return v;
}
bool validateVisual(const QJsonObject &v) {
    if (v[QStringLiteral("id")].toString().isEmpty() || v[QStringLiteral("id")].toString().size() > 512
        || !v[QStringLiteral("visible")].isBool()) return false;
    for (const auto &k : {"x", "y", "z", "rotation", "opacity"}) {
        auto n = v[QLatin1String(k)];
        if (!n.isDouble() || !std::isfinite(n.toDouble()) || std::abs(n.toDouble()) > 1000000) return false;
    }
    if (v[QStringLiteral("opacity")].toDouble() < 0 || v[QStringLiteral("opacity")].toDouble() > 1) return false;
    const auto kind = v[QStringLiteral("kind")].toString();
    if (kind == QLatin1String("line")) {
        const auto line = v[QStringLiteral("line")].toArray();
        if (line.size() != 4) return false;
        for (const auto n : line) if (!n.isDouble() || !std::isfinite(n.toDouble()) || std::abs(n.toDouble()) > 1000000) return false;
    } else if (kind == QLatin1String("sprite")) {
        if (v[QStringLiteral("sprite")].toString().isEmpty() || v[QStringLiteral("sprite")].toString().size() > 128
            || !v[QStringLiteral("frame")].isDouble() || v[QStringLiteral("frame")].toInt(-2) < -1 || v[QStringLiteral("frame")].toInt() > 10000) return false;
    } else return false;
    return !v.contains(QStringLiteral("color")) || QColor(v[QStringLiteral("color")].toString()).isValid();
}
void applyVisual(const QJsonObject &v, QGraphicsItem *item) {
    // Deliberately use QGraphicsItem, never CanvasItem::setPosition/moveBy or
    // Ball/Wall::setVisible. No physics, strut passenger movement or callbacks.
    item->setPos(v[QStringLiteral("x")].toDouble(), v[QStringLiteral("y")].toDouble());
    item->setZValue(v[QStringLiteral("z")].toDouble());
    item->setVisible(v[QStringLiteral("visible")].toBool());
    item->setRotation(v[QStringLiteral("rotation")].toDouble());
    item->setOpacity(v[QStringLiteral("opacity")].toDouble());
    if (auto *line = dynamic_cast<QGraphicsLineItem *>(item)) {
        auto l = v[QStringLiteral("line")].toArray();
        line->QGraphicsLineItem::setLine(l[0].toDouble(), l[1].toDouble(), l[2].toDouble(), l[3].toDouble());
        auto pen = line->pen(); pen.setColor(QColor(v[QStringLiteral("color")].toString())); line->setPen(pen);
    }
    if (auto *sprite = dynamic_cast<Tagaro::SpriteObjectItem *>(item)) {
        sprite->setSpriteKey(v[QStringLiteral("sprite")].toString());
        sprite->setFrame(v[QStringLiteral("frame")].toInt());
    }
    if (auto *ellipse = dynamic_cast<EllipticalCanvasItem *>(item); ellipse && ellipse->ellipseItem())
        ellipse->ellipseItem()->setBrush(QColor(v[QStringLiteral("color")].toString()));
}
}
