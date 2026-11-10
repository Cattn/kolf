/*
    Copyright (C) 2002-2005, Jason Katz-Brown <jasonkb@mit.edu>
    Copyright 2010 Stefan Majewsky <majewsky@gmx.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "ball.h"
#include <QGraphicsEllipseItem>
#include <QPropertyAnimation>
#include "ballstep.h"
#include "effects.h"
#include "impacteffects.h"
#include "game.h"
#include "overlay.h"
#include "shape.h"

namespace {
constexpr int MaximumAirSteps = 160;
}

#include <QApplication>

Ball::Ball(QGraphicsItem* parent, b2World* world)
	: EllipticalCanvasItem(true, QStringLiteral("ball"), parent, world)
{
	const int diameter = 8;
	setSize(QSizeF(diameter, diameter));
	setZBehavior(CanvasItem::IsRaisedByStrut, 10);

	setData(0, Rtti_NoCollision);
	m_doDetect = true;
	setBeginningOfHole(false);
	m_collisionId = 0;
	m_addStroke = false;
	m_placeOnGround = false;
	m_forceStillGoing = false;
	frictionMultiplier = 1.0;
	maxBumperBounceSpeed = 8;

	QFont font(QApplication::font());
	font.setPixelSize(12);
	label = new QGraphicsSimpleTextItem(QString(), this);
	label->setFont(font);
	label->setBrush(Qt::white);
	label->setPos(5, 5);
	label->setVisible(false);

	// this sets z
	setState(Stopped);
}

void Ball::launch(int steps)
{
	m_airTotal = m_airSteps = qBound(1, steps, MaximumAirSteps);
	updateAirVisual();
}

void Ball::updateAirVisual()
{
	if (m_airSteps <= 0 || m_airTotal <= 0) {
		if (m_airShadow)
			m_airShadow->hide();
		if (!m_shownHoled)
			setScale(1);
		return;
	}
	const double progress = 1.0 - double(m_airSteps) / m_airTotal;
	const double height = std::sin(M_PI * progress) * qMin(1.0, m_airTotal / 60.0);
	if (!m_airShadow) {
		m_airShadow = new QGraphicsEllipseItem(this);
		m_airShadow->setFlag(QGraphicsItem::ItemStacksBehindParent);
		m_airShadow->setPen(Qt::NoPen);
		m_airShadow->setBrush(QColor(0, 0, 0, 90));
	}
	const QRectF body = boundingRect();
	m_airShadow->setRect(body.translated(5 * height, 5 * height));
	m_airShadow->show();
	setTransformOriginPoint(body.center());
	setScale(1 + 0.6 * height);
}

void Ball::showHoled(bool holed)
{
	if (holed == m_shownHoled) return;
	m_shownHoled = holed;
	if (m_dropAnimation) m_dropAnimation->stop();
	if (!holed) {
		setScale(1);
		return;
	}
	if (!m_dropAnimation) {
		m_dropAnimation = new QPropertyAnimation(this, "scale", this);
		m_dropAnimation->setDuration(280);
		m_dropAnimation->setEasingCurve(QEasingCurve::InQuad);
	}
	setTransformOriginPoint(boundingRect().center());
	if (Kolf::Effects::level() == Kolf::Effects::Level::Off) {
		setScale(0.55);
		return;
	}
	m_dropAnimation->setStartValue(1.0);
	m_dropAnimation->setEndValue(0.55);
	m_dropAnimation->start();
}

void Ball::setState(BallState newState)
{
	if (game && game->isGuest()) return;
	state = newState;
	showHoled(state == Holed);
	if (state == Holed || !EllipticalCanvasItem::isVisible())
		setSimulationType(CanvasItem::NoSimulation);
	else
		setSimulationType(CanvasItem::DynamicSimulation);

	if (state != Stopped)
		setBeginningOfHole(false);
}

void Ball::friction()
{
	if (game && !game->maySimulate()) return;
	if (state == Stopped || state == Holed || !isVisible())
	{
		setVelocity(QPointF());
		return;
	}
	const double slick = game && game->ballHasStatus(this, QStringLiteral("slick")) ? 0.25 : 1.0;
	const double subtractAmount = .027 * frictionMultiplier * slick;
	Vector velocity = this->velocity();
	if (velocity.magnitude() <= subtractAmount)
	{
		state = Stopped;
		setVelocity(QPointF());
		game->timeout();
		return;
	}
	velocity.setMagnitude(velocity.magnitude() - subtractAmount);
	setVelocity(velocity);

	frictionMultiplier = 1.0;
}

void Ball::moveBy(double dx, double dy)
{
	if (game && game->isGuest()) return;
	EllipticalCanvasItem::moveBy(dx, dy);

	if (game && !game->isPaused())
		collisionDetect();

	if ((dx || dy) && game && game->curBall() == this)
		game->ballMoved();
}

void Ball::endSimulation()
{
	if (game && !game->maySimulate()) return;
	if (m_airSteps > 0) {
		if (--m_airSteps == 0) {
			setVelocity(QPointF(velocity()) * 0.8);
			if (game)
				game->impact(Kolf::Impact::Dust, pos(), 2);
		}
		updateAirVisual();
	}
	if (state == Stopped && !qFuzzyIsNull(Vector(velocity()).magnitude())) {
		//ball was resting, but received some momentum from collision with other ball
		setState(Rolling);
	}
	CanvasItem::endSimulation();
	if (state == Rolling) {
		if (qFuzzyIsNull(Vector(velocity()).magnitude())) {
			//ball was moving, but stopped moving from collision with other ball
			setState(Stopped);
		}
	}
}

void Ball::collisionDetect()
{
	if (game && !game->maySimulate()) return;
	if (game) game->recordCollision();
	if (!isVisible() || state == Holed || !m_doDetect)
		return;

	if (m_airSteps > 0)
		return;

	// do friction every other time
	m_collisionId = (m_collisionId + 1) % 2;
	if (m_collisionId == 1 && !velocity().isNull())
		friction();

	const double initialVelocity = Vector(velocity()).magnitude();
	const double minSpeed = .06;

	const QList<QGraphicsItem *> items = collidingItems();

	Kolf::BallStepContext context(this);
	if (game && game->ballHasStatus(this, QStringLiteral("steady")))
		context.blockForces();
	for (QGraphicsItem* item : items) {
		if (item->data(0) == Rtti_NoCollision || item->data(0) == Rtti_Putter)
		{
			if (item->data(0) == Rtti_NoCollision)
				game->playSound(Sound::Wall);
			continue;
		}

		if (!isVisible() || state == Holed)
			return;

		CanvasItem *citem = dynamic_cast<CanvasItem *>(item);
		if (!citem)
			continue;
		if (context.covered())
			break;
		citem->onBall(context);
	}
	context.apply();

	const double currentVelocity = Vector(velocity()).magnitude();
	const double velocityChange = qAbs(initialVelocity - currentVelocity);

	if(currentVelocity < minSpeed && velocityChange < minSpeed && currentVelocity)
	{
		//cutoff low velocities
		setVelocity(Vector());
		setState(Stopped);
	}
}

BallState Ball::currentState()
{
	return state;
}

QList<QGraphicsItem*> Ball::infoItems() const
{
	return QList<QGraphicsItem*>() << label;
}

void Ball::setName(const QString &name)
{
	label->setText(name);
}

void Ball::setVisible(bool yes)
{
	EllipticalCanvasItem::setVisible(yes);
	setState(state);
}

Kolf::Overlay* Ball::createOverlay()
{
	return new Kolf::Overlay(this, this);
}

QVariantMap Ball::runtimeState() const
{
	return {{QStringLiteral("maxBumperBounceSpeed"), maxBumperBounceSpeed}, {QStringLiteral("frictionMultiplier"), frictionMultiplier},
		{QStringLiteral("collisionId"), m_collisionId}, {QStringLiteral("beginningOfHole"), m_beginningOfHole},
		{QStringLiteral("hazardReset"), int(m_hazardReset)}, {QStringLiteral("airSteps"), m_airSteps},
		{QStringLiteral("airTotal"), m_airTotal}};
}

void Ball::setRuntimeState(const QVariantMap &runtime)
{
	maxBumperBounceSpeed = runtime.value(QStringLiteral("maxBumperBounceSpeed"), maxBumperBounceSpeed).toDouble();
	frictionMultiplier = runtime.value(QStringLiteral("frictionMultiplier"), frictionMultiplier).toDouble();
	m_collisionId = runtime.value(QStringLiteral("collisionId"), m_collisionId).toInt() % 2;
	m_beginningOfHole = runtime.value(QStringLiteral("beginningOfHole"), m_beginningOfHole).toBool();
	m_hazardReset = HazardReset(qBound(0, runtime.value(QStringLiteral("hazardReset"), int(m_hazardReset)).toInt(), 2));
	m_airTotal = qBound(0, runtime.value(QStringLiteral("airTotal"), m_airTotal).toInt(), MaximumAirSteps);
	m_airSteps = qBound(0, runtime.value(QStringLiteral("airSteps"), m_airSteps).toInt(), m_airTotal);
	updateAirVisual();
}
