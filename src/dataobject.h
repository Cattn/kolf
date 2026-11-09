// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "canvasitem.h"
#include "objectregistry.h"

#include <QGraphicsObject>
#include <QJsonObject>
#include <QSizeF>
#include <QStringList>
#include <memory>

class QSvgRenderer;
class Ball;

namespace Kolf { class RectShape; }

namespace Kolf
{
struct DataComponent
{
	QString type;
	QJsonObject parameters;
};

struct DataObjectDefinition
{
	QString id;
	QString name;
	QString help;
	QString file;
	QString shape;
	QSizeF size;
	bool resizable = false;
	bool rotatable = false;
	QString layer;
	QList<DataComponent> components;
	std::shared_ptr<QSvgRenderer> sprite;
};

QString sanitizeSvg(const QByteArray &svg);

class DataObjectLibrary
{
public:
	static std::shared_ptr<const DataObjectDefinition> parse(const QByteArray &json, const QString &file, const QByteArray &sprite, QString &error);
	static bool loadFile(const QString &path, QString &error);
	static QStringList loadDirectory(const QString &directory);
	static QStringList loadInstalled();
};

class DataObject : public QGraphicsObject, public CanvasItem
{
	Q_OBJECT
public:
	DataObject(std::shared_ptr<const DataObjectDefinition> definition, QGraphicsItem *parent, b2World *world);

	QRectF boundingRect() const override;
	QPainterPath shape() const override;
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

	QPointF getPosition() const override { return pos(); }
	void moveBy(double dx, double dy) override;
	void setSize(const QSizeF &size) override;

	QVariantMap properties() const override;
	void applyProperties(const QVariantMap &values) override;
	QVariantMap runtimeState() const override;
	bool gusting() const;
	bool swirling() const;
	double gustFactor() const;
	void setRuntimeState(const QVariantMap &state) override;
	void onBall(BallStepContext &context) override;
	bool isHazardAt(const QPointF &point) const override;
	void setGame(KolfGame *game) override;
	void receiveChannel(const QString &channel, bool level, bool pulse);
	bool hasComponent(const QString &type) const;
	bool isResizable() const { return m_definition->resizable; }
	bool isRotatable() const { return m_definition->rotatable; }
	QStringList channels(const QString &componentType) const;

protected:
	Kolf::Overlay *createOverlay() override;

private:
	QVariant parameter(const DataComponent &component, const QString &name, const QVariant &fallback) const;
	bool containsPoint(const QPointF &point) const;
	void playSound(const QString &event);
	void updateGeometry();
	void step();
	void setOpen(bool open);
	void perform(const QString &action);
	void updatePulse();
	void stepOccupants();
	QPointF edgeOutside(const QPointF &from) const;

	std::shared_ptr<const DataObjectDefinition> m_definition;
	QVariantMap m_values;
	QSizeF m_size;
	qint64 m_lastBoost = -1000000;
	qint64 m_phase = 0;
	int m_swirl = 0;
	Kolf::RectShape *m_solidShape = nullptr;
	bool m_open = false;
	bool m_running = true;
	double m_t = 0;
	int m_direction = 1;
	int m_occupied = 0;
	bool m_resyncInside = true;
	QPointF m_basePosition;
	bool m_baseSet = false;
	QList<Ball *> m_insideBalls;
	QList<Ball *> m_stoppedBalls;
	QList<Ball *> m_occupants;
	bool m_resyncOccupants = true;
	bool m_crumbled = false;
	bool m_spent = false;
};
}
