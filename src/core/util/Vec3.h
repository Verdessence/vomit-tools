//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A point or direction in Hammer's coordinates: +X east, +Y north, +Z up.
//
//===========================================================================//
#pragma once

#include <cmath>

namespace xv
{

struct CVec3
{
	float m_flX = 0.0f;
	float m_flY = 0.0f;
	float m_flZ = 0.0f;

	friend CVec3 operator+( CVec3 a, CVec3 b ) { return { a.m_flX + b.m_flX, a.m_flY + b.m_flY, a.m_flZ + b.m_flZ }; }
	friend CVec3 operator-( CVec3 a, CVec3 b ) { return { a.m_flX - b.m_flX, a.m_flY - b.m_flY, a.m_flZ - b.m_flZ }; }
	friend CVec3 operator-( CVec3 a ) { return { -a.m_flX, -a.m_flY, -a.m_flZ }; }
	friend CVec3 operator*( CVec3 a, float s ) { return { a.m_flX * s, a.m_flY * s, a.m_flZ * s }; }
	friend CVec3 operator*( float s, CVec3 a ) { return a * s; }
	CVec3 &operator+=( CVec3 b ) { return *this = *this + b; }
	CVec3 &operator-=( CVec3 b ) { return *this = *this - b; }
	CVec3 &operator*=( float s ) { return *this = *this * s; }
	friend bool operator==( const CVec3 &, const CVec3 & ) = default;
};

[[nodiscard]] inline float Dot( CVec3 a, CVec3 b )
{
	return a.m_flX * b.m_flX + a.m_flY * b.m_flY + a.m_flZ * b.m_flZ;
}

[[nodiscard]] inline CVec3 Cross( CVec3 a, CVec3 b )
{
	return { a.m_flY * b.m_flZ - a.m_flZ * b.m_flY, a.m_flZ * b.m_flX - a.m_flX * b.m_flZ, a.m_flX * b.m_flY - a.m_flY * b.m_flX };
}

[[nodiscard]] inline float Length( CVec3 v )
{
	return std::sqrt( Dot( v, v ) );
}

[[nodiscard]] inline CVec3 Normalized( CVec3 v )
{
	const float flSize = Length( v );
	return flSize > 0.0f ? v * ( 1.0f / flSize ) : v;
}

} // namespace xv
