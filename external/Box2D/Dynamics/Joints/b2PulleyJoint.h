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

#ifndef B2_PULLEY_JOINT_H
#define B2_PULLEY_JOINT_H

#include <Box2D/Dynamics/Joints/b2Joint.h>

const qreal b2_minPulleyLength = 2.0f;

/// Pulley joint definition. This requires two ground anchors,
/// two dynamic body anchor points, max lengths for each side,
/// and a pulley ratio.
struct b2PulleyJointDef : public b2JointDef
{
	b2PulleyJointDef()
	{
		type = e_pulleyJoint;
		groundAnchorA.Set(-1.0f, 1.0f);
		groundAnchorB.Set(1.0f, 1.0f);
		localAnchorA.Set(-1.0f, 0.0f);
		localAnchorB.Set(1.0f, 0.0f);
		lengthA = 0.0f;
		maxLengthA = 0.0f;
		lengthB = 0.0f;
		maxLengthB = 0.0f;
		ratio = 1.0f;
		collideConnected = true;
	}

	/// Initialize the bodies, anchors, lengths, max lengths, and ratio using the world anchors.
	void Initialize(b2Body* bodyA, b2Body* bodyB,
					const b2Vec2& groundAnchorA, const b2Vec2& groundAnchorB,
					const b2Vec2& anchorA, const b2Vec2& anchorB,
					qreal ratio);

	/// The first ground anchor in world coordinates. This point never moves.
	b2Vec2 groundAnchorA;

	/// The second ground anchor in world coordinates. This point never moves.
	b2Vec2 groundAnchorB;

	/// The local anchor point relative to bodyA's origin.
	b2Vec2 localAnchorA;

	/// The local anchor point relative to bodyB's origin.
	b2Vec2 localAnchorB;

	/// The a reference length for the segment attached to bodyA.
	qreal lengthA;

	/// The maximum length of the segment attached to bodyA.
	qreal maxLengthA;

	/// The a reference length for the segment attached to bodyB.
	qreal lengthB;

	/// The maximum length of the segment attached to bodyB.
	qreal maxLengthB;

	/// The pulley ratio, used to simulate a block-and-tackle.
	qreal ratio;
};

/// The pulley joint is connected to two bodies and two fixed ground points.
/// The pulley supports a ratio such that:
/// length1 + ratio * length2 <= constant
/// Yes, the force transmitted is scaled by the ratio.
/// The pulley also enforces a maximum length limit on both sides. This is
/// useful to prevent one side of the pulley hitting the top.
class b2PulleyJoint : public b2Joint
{
public:
	b2Vec2 GetAnchorA() const override;
	b2Vec2 GetAnchorB() const override;

	b2Vec2 GetReactionForce(qreal inv_dt) const override;
	qreal GetReactionTorque(qreal inv_dt) const override;

	/// Get the first ground anchor.
	b2Vec2 GetGroundAnchorA() const;

	/// Get the second ground anchor.
	b2Vec2 GetGroundAnchorB() const;

	/// Get the current length of the segment attached to body1.
	qreal GetLength1() const;

	/// Get the current length of the segment attached to body2.
	qreal GetLength2() const;

	/// Get the pulley ratio.
	qreal GetRatio() const;

protected:

	friend class b2Joint;
	b2PulleyJoint(const b2PulleyJointDef* data);

	void InitVelocityConstraints(const b2TimeStep& step) override;
	void SolveVelocityConstraints(const b2TimeStep& step) override;
	bool SolvePositionConstraints(qreal baumgarte) override;

	b2Vec2 m_groundAnchor1;
	b2Vec2 m_groundAnchor2;
	b2Vec2 m_localAnchor1;
	b2Vec2 m_localAnchor2;

	b2Vec2 m_u1;
	b2Vec2 m_u2;
	
	qreal m_constant;
	qreal m_ratio;
	
	qreal m_maxLength1;
	qreal m_maxLength2;

	// Effective masses
	qreal m_pulleyMass;
	qreal m_limitMass1;
	qreal m_limitMass2;

	// Impulses for accumulation/warm starting.
	qreal m_impulse;
	qreal m_limitImpulse1;
	qreal m_limitImpulse2;

	b2LimitState m_state;
	b2LimitState m_limitState1;
	b2LimitState m_limitState2;
};

#endif
