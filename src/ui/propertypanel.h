// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "config.h"

#include <QMap>
#include <QVariant>

class CanvasItem;

namespace Kolf
{
class ObjectType;

namespace Ui
{
class PropertyPanel : public Config
{
	Q_OBJECT
public:
	PropertyPanel(CanvasItem *item, const ObjectType &type, QWidget *parent);

private:
	void apply(const QString &key, const QVariant &value);

	CanvasItem *m_item;
};
}
}
