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

#ifndef B2_LINE_JOINT_H
#define B2_LINE_JOINT_H

#include <Box2D/Dynamics/Joints/b2Joint.h>

/// Line joint definition. This requires defining a line of
/// motion using an axis and an anchor point. The definition uses local
/// anchor points and a local axis so that the initial configuration
/// can violate the constraint slightly. The joint translation is zero
/// when the local anchor points coincide in world space. Using local
/// anchors and a local axis helps when saving and loading a game.
struct b2LineJointDef : public b2JointDef
{
	b2LineJointDef()
	{
		type = e_lineJoint;
		localAnchorA.SetZero();
		localAnchorB.SetZero();
		localAxisA.Set(1.0f, 0.0f);
		enableLimit = false;
		lowerTranslation = 0.0f;
		upperTranslation = 0.0f;
		enableMotor = false;
		maxMotorForce = 0.0f;
		motorSpeed = 0.0f;
	}

	/// Initialize the bodies, anchors, axis, and reference angle using the world
	/// anchor and world axis.
	void Initialize(b2Body* bodyA, b2Body* bodyB, const b2Vec2& anchor, const b2Vec2& axis);

	/// The local anchor point relative to body1's origin.
	b2Vec2 localAnchorA;

	/// The local anchor point relative to body2's origin.
	b2Vec2 localAnchorB;

	/// The local translation axis in body1.
	b2Vec2 localAxisA;

	/// Enable/disable the joint limit.
	bool enableLimit;

	/// The lower translation limit, usually in meters.
	qreal lowerTranslation;

	/// The upper translation limit, usually in meters.
	qreal upperTranslation;

	/// Enable/disable the joint motor.
	bool enableMotor;

	/// The maximum motor torque, usually in N-m.
	qreal maxMotorForce;

	/// The desired motor speed in radians per second.
	qreal motorSpeed;
};

/// A line joint. This joint provides two degrees of freedom: translation
/// along an axis fixed in body1 and rotation in the plane. You can use a
/// joint limit to restrict the range of motion and a joint motor to drive
/// the motion or to model joint friction.
class b2LineJoint : public b2Joint
{
public:
	b2Vec2 GetAnchorA() const override;
	b2Vec2 GetAnchorB() const override;

	b2Vec2 GetReactionForce(qreal inv_dt) const override;
	qreal GetReactionTorque(qreal inv_dt) const override;

	/// Get the current joint translation, usually in meters.
	qreal GetJointTranslation() const;

	/// Get the current joint translation speed, usually in meters per second.
	qreal GetJointSpeed() const;

	/// Is the joint limit enabled?
	bool IsLimitEnabled() const;

	/// Enable/disable the joint limit.
	void EnableLimit(bool flag);

	/// Get the lower joint limit, usually in meters.
	qreal GetLowerLimit() const;

	/// Get the upper joint limit, usually in meters.
	qreal GetUpperLimit() const;

	/// Set the joint limits, usually in meters.
	void SetLimits(qreal lower, qreal upper);

	/// Is the joint motor enabled?
	bool IsMotorEnabled() const;

	/// Enable/disable the joint motor.
	void EnableMotor(bool flag);

	/// Set the motor speed, usually in meters per second.
	void SetMotorSpeed(qreal speed);

	/// Get the motor speed, usually in meters per second.
	qreal GetMotorSpeed() const;

	/// Set/Get the maximum motor force, usually in N.
	void SetMaxMotorForce(qreal force);
	qreal GetMaxMotorForce() const;

	/// Get the current motor force given the inverse time step, usually in N.
	qreal GetMotorForce(qreal inv_dt) const;

protected:

	friend class b2Joint;
	b2LineJoint(const b2LineJointDef* def);

	void InitVelocityConstraints(const b2TimeStep& step) override;
	void SolveVelocityConstraints(const b2TimeStep& step) override;
	bool SolvePositionConstraints(qreal baumgarte) override;

	b2Vec2 m_localAnchor1;
	b2Vec2 m_localAnchor2;
	b2Vec2 m_localXAxis1;
	b2Vec2 m_localYAxis1;

	b2Vec2 m_axis, m_perp;
	qreal m_s1, m_s2;
	qreal m_a1, m_a2;

	b2Mat22 m_K;
	b2Vec2 m_impulse;

	qreal m_motorMass;			// effective mass for motor/limit translational constraint.
	qreal m_motorImpulse;

	qreal m_lowerTranslation;
	qreal m_upperTranslation;
	qreal m_maxMotorForce;
	qreal m_motorSpeed;

	bool m_enableLimit;
	bool m_enableMotor;
	b2LimitState m_limitState;
};

inline qreal b2LineJoint::GetMotorSpeed() const
{
	return m_motorSpeed;
}

inline qreal b2LineJoint::GetMaxMotorForce() const
{
	return m_maxMotorForce;
}

#endif
