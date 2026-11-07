// SPDX-License-Identifier: GPL-2.0-or-later
#include "missingobject.h"

#include "overlay.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <QPainter>
#include <QPen>

Kolf::MissingObject::MissingObject(const QString &type, const QMap<QString, QString> &entries, QGraphicsItem *parent, b2World *world)
	: QGraphicsRectItem(-10, -10, 20, 20, parent)
	, CanvasItem(world)
	, m_type(type)
	, m_entries(entries)
{
	setZBehavior(CanvasItem::FixedZValue, 9000);
	setSimulationType(CanvasItem::NoSimulation);
	setToolTip(i18nc("@info:tooltip", "Object type \"%1\" is not installed", type));
}

void Kolf::MissingObject::writeEntries(KConfigGroup &group) const
{
	if (m_entries.isEmpty()) {
		group.writeEntry("dummykey", true);
		return;
	}
	for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
		group.writeEntry(it.key(), it.value());
}

void Kolf::MissingObject::moveBy(double dx, double dy)
{
	QGraphicsRectItem::moveBy(dx, dy);
	CanvasItem::moveBy(dx, dy);
}

void Kolf::MissingObject::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
	painter->setRenderHint(QPainter::Antialiasing);
	painter->setBrush(QColor(255, 255, 255, 140));
	painter->setPen(QPen(QColor(120, 40, 40), 1.5, Qt::DashLine));
	painter->drawRoundedRect(rect(), 3, 3);
	painter->setPen(QColor(120, 40, 40));
	QFont font = painter->font();
	font.setPixelSize(13);
	font.setBold(true);
	painter->setFont(font);
	painter->drawText(rect(), Qt::AlignCenter, QStringLiteral("?"));
}

Kolf::Overlay *Kolf::MissingObject::createOverlay()
{
	return new Kolf::Overlay(this, this, true);
}
