// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "canvasitem.h"

#include <QGraphicsRectItem>
#include <QMap>

class KConfigGroup;

namespace Kolf
{
class MissingObject : public QGraphicsRectItem, public CanvasItem
{
public:
	MissingObject(const QString &type, const QMap<QString, QString> &entries, QGraphicsItem *parent, b2World *world);

	QString missingType() const { return m_type; }
	void writeEntries(KConfigGroup &group) const;

	QPointF getPosition() const override { return pos(); }
	void moveBy(double dx, double dy) override;
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

protected:
	Kolf::Overlay *createOverlay() override;

private:
	QString m_type;
	QMap<QString, QString> m_entries;
};
}
