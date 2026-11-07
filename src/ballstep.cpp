// SPDX-License-Identifier: GPL-2.0-or-later
#include "ballstep.h"

#include "ball.h"

bool Kolf::BallStepContext::claimContact()
{
	if (m_covered || m_contactClaimed)
		return false;
	m_contactClaimed = true;
	return true;
}

void Kolf::BallStepContext::surface(const QString &kind, double frictionMultiplier, int priority)
{
	if (m_covered || m_surfaces.contains(kind))
		return;
	m_surfaces.insert(kind, {frictionMultiplier, priority});
}

void Kolf::BallStepContext::force(const QString &kind, const Vector &delta)
{
	if (m_covered || m_forces.contains(kind))
		return;
	m_forces.insert(kind, delta);
	m_force += delta;
	m_hasForce = true;
}

void Kolf::BallStepContext::impulse(const Vector &delta)
{
	if (m_covered)
		return;
	m_force += delta;
	m_hasForce = true;
}

double Kolf::BallStepContext::frictionMultiplier() const
{
	int top = 0;
	bool any = false;
	for (const auto &surface : m_surfaces) {
		if (!any || surface.priority > top)
			top = surface.priority;
		any = true;
	}
	double result = 1;
	for (const auto &surface : m_surfaces)
		if (surface.priority == top)
			result *= surface.frictionMultiplier;
	return result;
}

void Kolf::BallStepContext::apply()
{
	if (!m_ball->isVisible() || m_ball->curState() == Holed)
		return;
	if (!m_surfaces.isEmpty())
		m_ball->setFrictionMultiplier(frictionMultiplier());
	if (!m_hasForce || m_forcesBlocked)
		return;
	Vector velocity = m_ball->velocity();
	velocity += m_force;
	m_ball->setVelocity(velocity);
	m_ball->setState(velocity.isNull() ? Stopped : Rolling);
}
