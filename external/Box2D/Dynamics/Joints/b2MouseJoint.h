/*
* Copyright (c) 2006-2007 Erin Catto http://www.gphysics.com
*
* This software is provided 'as-is', without any express or implied
* warranty.  In no event will the authors be held liable for any damages
* arising from the use of this software.
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
* 1. The origin of this software must not be misrepresented; you must not
* claim that you wrote the original software. If you use this software
* in a product, an acknowledgment in the product documentation would be
* appreciated but is not required.
* 2. Altered source versions must be plainly marked as such, and must not be
* misrepresented as being the original software.
* 3. This notice may not be removed or altered from any source distribution.
*/

#ifndef B2_MOUSE_JOINT_H
#define B2_MOUSE_JOINT_H

#include <Box2D/Dynamics/Joints/b2Joint.h>

/// Mouse joint definition. This requires a world target point,
/// tuning parameters, and the time step.
struct b2MouseJointDef : public b2JointDef
{
	b2MouseJointDef()
	{
		type = e_mouseJoint;
		target.Set(0.0f, 0.0f);
		maxForce = 0.0f;
		frequencyHz = 5.0f;
		dampingRatio = 0.7f;
	}

	/// The initial world target point. This is assumed
	/// to coincide with the body anchor initially.
	b2Vec2 target;

	/// The maximum constraint force that can be exerted
	/// to move the candidate body. Usually you will express
	/// as some multiple of the weight (multiplier * mass * gravity).
	qreal maxForce;

	/// The response speed.
	qreal frequencyHz;

	/// The damping ratio. 0 = no damping, 1 = critical damping.
	qreal dampingRatio;
};

/// A mouse joint is used to make a point on a body track a
/// specified world point. This a soft constraint with a maximum
/// force. This allows the constraint to stretch and without
/// applying huge forces.
/// NOTE: this joint is not documented in the manual because it was
/// developed to be used in the testbed. If you want to learn how to
/// use the mouse joint, look at the testbed.
class b2MouseJoint : public b2Joint
{
public:

	/// Implements b2Joint.
	b2Vec2 GetAnchorA() const override;

	/// Implements b2Joint.
	b2Vec2 GetAnchorB() const override;

	/// Implements b2Joint.
	b2Vec2 GetReactionForce(qreal inv_dt) const override;

	/// Implements b2Joint.
	qreal GetReactionTorque(qreal inv_dt) const override;

	/// Use this to update the target point.
	void SetTarget(const b2Vec2& target);
	const b2Vec2& GetTarget() const;

	/// Set/get the maximum force in Newtons.
	void SetMaxForce(qreal force);
	qreal GetMaxForce() const;

	/// Set/get the frequency in Hertz.
	void SetFrequency(qreal hz);
	qreal GetFrequency() const;

	/// Set/get the damping ratio (dimensionless).
	void SetDampingRatio(qreal ratio);
	qreal GetDampingRatio() const;

protected:
	friend class b2Joint;

	b2MouseJoint(const b2MouseJointDef* def);

	void InitVelocityConstraints(const b2TimeStep& step) override;
	void SolveVelocityConstraints(const b2TimeStep& step) override;
	bool SolvePositionConstraints(qreal baumgarte) override { B2_NOT_USED(baumgarte); return true; }

	b2Vec2 m_localAnchor;
	b2Vec2 m_target;
	b2Vec2 m_impulse;

	b2Mat22 m_mass;		// effective mass for point-to-point constraint.
	b2Vec2 m_C;				// position error
	qreal m_maxForce;
	qreal m_frequencyHz;
	qreal m_dampingRatio;
	qreal m_beta;
	qreal m_gamma;
};

#endif
