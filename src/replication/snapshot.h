// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QJsonObject>
class QGraphicsItem;
namespace Kolf::Replication {
// All positions are in the item's parent board coordinates. Generated walls are
// board siblings, not children of their platforms, and are applied separately.
QJsonObject captureVisual(const QString &id, QGraphicsItem *item);
bool validateVisual(const QJsonObject &value);
void applyVisual(const QJsonObject &value, QGraphicsItem *item);
QString visualKind(QGraphicsItem *item);
}
