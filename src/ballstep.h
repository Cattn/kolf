// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "vector.h"

#include <QMap>
#include <QString>

class Ball;

namespace Kolf
{
class BallStepContext
{
public:
	explicit BallStepContext(Ball *ball) : m_ball(ball) {}

	Ball *ball() const { return m_ball; }

	void cover() { m_covered = true; }
	bool covered() const { return m_covered; }

	bool claimContact();
	bool contactClaimed() const { return m_contactClaimed; }

	void blockForces() { m_forcesBlocked = true; }
	bool forcesBlocked() const { return m_forcesBlocked; }

	void surface(const QString &kind, double frictionMultiplier, int priority);
	void force(const QString &kind, const Vector &delta);

	double frictionMultiplier() const;
	bool hasForce() const { return m_hasForce; }
	Vector totalForce() const { return m_force; }

	void apply();

private:
	struct Surface {
		double frictionMultiplier = 1;
		int priority = 0;
	};

	Ball *m_ball;
	bool m_covered = false;
	bool m_contactClaimed = false;
	bool m_forcesBlocked = false;
	bool m_hasForce = false;
	Vector m_force;
	QMap<QString, Surface> m_surfaces;
	QMap<QString, Vector> m_forces;
};
}
