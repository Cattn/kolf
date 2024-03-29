/*
* Copyright (c) 2006-2010 Erin Catto http://www.gphysics.com
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

#ifndef B2_ROPE_JOINT_H
#define B2_ROPE_JOINT_H

#include <Box2D/Dynamics/Joints/b2Joint.h>

/// Rope joint definition. This requires two body anchor points and
/// a maximum lengths.
/// Note: by default the connected objects will not collide.
/// see collideConnected in b2JointDef.
struct b2RopeJointDef : public b2JointDef
{
	b2RopeJointDef()
	{
		type = e_ropeJoint;
		localAnchorA.Set(-1.0f, 0.0f);
		localAnchorB.Set(1.0f, 0.0f);
		maxLength = 0.0f;
	}

	/// The local anchor point relative to bodyA's origin.
	b2Vec2 localAnchorA;

	/// The local anchor point relative to bodyB's origin.
	b2Vec2 localAnchorB;

	/// The maximum length of the rope.
	/// Warning: this must be larger than b2_linearSlop or
	/// the joint will have no effect.
	qreal maxLength;
};

/// A rope joint enforces a maximum distance between two points
/// on two bodies. It has no other effect.
/// Warning: if you attempt to change the maximum length during
/// the simulation you will get some non-physical behavior.
/// A model that would allow you to dynamically modify the length
/// would have some sponginess, so I chose not to implement it
/// that way. See b2DistanceJoint if you want to dynamically
/// control length.
class b2RopeJoint : public b2Joint
{
public:
	b2Vec2 GetAnchorA() const override;
	b2Vec2 GetAnchorB() const override;

	b2Vec2 GetReactionForce(qreal inv_dt) const override;
	qreal GetReactionTorque(qreal inv_dt) const override;

	/// Get the maximum length of the rope.
	qreal GetMaxLength() const;

	b2LimitState GetLimitState() const;

protected:

	friend class b2Joint;
	b2RopeJoint(const b2RopeJointDef* data);

	void InitVelocityConstraints(const b2TimeStep& step) override;
	void SolveVelocityConstraints(const b2TimeStep& step) override;
	bool SolvePositionConstraints(qreal baumgarte) override;

	b2Vec2 m_localAnchorA;
	b2Vec2 m_localAnchorB;

	qreal m_maxLength;
	qreal m_length;

	// Jacobian info
	b2Vec2 m_u, m_rA, m_rB;

	// Effective mass
	qreal m_mass;

	// Impulses for accumulation/warm starting.
	qreal m_impulse;

	b2LimitState m_state;
};

#endif
