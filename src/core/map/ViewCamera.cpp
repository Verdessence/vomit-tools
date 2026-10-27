//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ViewCamera.h.
//
//===========================================================================//
#include "ViewCamera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace xv::map
{

namespace
{

constexpr double DEGREES = std::numbers::pi / 180.0;

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Point_t CViewCamera::GetForward() const
{
	const double flYaw = m_flYaw * DEGREES;
	const double flPitch = m_flPitch * DEGREES;
	return { std::cos( flPitch ) * std::cos( flYaw ), std::cos( flPitch ) * std::sin( flYaw ), -std::sin( flPitch ) };
}

//-----------------------------------------------------------------------------
// Purpose: Level with the ground whatever the pitch, as Hammer's camera never rolls.
//-----------------------------------------------------------------------------
Point_t CViewCamera::GetRight() const
{
	const double flYaw = m_flYaw * DEGREES;
	return { std::sin( flYaw ), -std::cos( flYaw ), 0.0 };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Point_t CViewCamera::GetUp() const
{
	return Cross( GetRight(), GetForward() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CViewCamera::LookAt( Point_t eye, Point_t target )
{
	m_Eye = eye;
	const Point_t d = Subtract( target, eye );
	const double flFlat = std::hypot( d[ 0 ], d[ 1 ] );
	if ( flFlat == 0.0 && d[ 2 ] == 0.0 )
	{
		return;
	}
	if ( flFlat > 0.0 )
	{
		m_flYaw = std::atan2( d[ 1 ], d[ 0 ] ) / DEGREES;
	}
	m_flPitch = std::clamp( -std::atan2( d[ 2 ], flFlat ) / DEGREES, -89.0, 89.0 );
}

//-----------------------------------------------------------------------------
// Purpose: The horizontal field of view sets x; y follows the image's shape.
//-----------------------------------------------------------------------------
std::array<double, 2> CViewCamera::GetFocal() const
{
	const double flX = 1.0 / std::tan( std::clamp( m_flFov, 1.0, 179.0 ) * DEGREES / 2.0 );
	return { flX, flX * static_cast<double>( std::max( m_nWidth, 1 ) ) / static_cast<double>( std::max( m_nHeight, 1 ) ) };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::array<float, 16> CViewCamera::GetViewProjection() const
{
	const Point_t f = GetForward();
	const Point_t r = GetRight();
	const Point_t u = GetUp();
	const auto [ flFx, flFy ] = GetFocal();
	const double n = m_flNear;
	const double fa = m_flFar;
	// Eye space: x right, y up, looking down -z.
	const double view[ 3 ][ 4 ] = {
		{ r[ 0 ], r[ 1 ], r[ 2 ], -Dot( r, m_Eye ) },
		{ u[ 0 ], u[ 1 ], u[ 2 ], -Dot( u, m_Eye ) },
		{ -f[ 0 ], -f[ 1 ], -f[ 2 ], Dot( f, m_Eye ) },
	};
	const double flA = -( fa + n ) / ( fa - n );
	const double flB = -2.0 * fa * n / ( fa - n );
	double rows[ 4 ][ 4 ] = {};
	for ( int c = 0; c < 4; c++ )
	{
		rows[ 0 ][ c ] = flFx * view[ 0 ][ c ];
		rows[ 1 ][ c ] = flFy * view[ 1 ][ c ];
		rows[ 2 ][ c ] = flA * view[ 2 ][ c ] + ( c == 3 ? flB : 0.0 );
		rows[ 3 ][ c ] = -view[ 2 ][ c ];
	}
	std::array<float, 16> matrix{};
	for ( int c = 0; c < 4; c++ )
	{
		for ( int r2 = 0; r2 < 4; r2++ )
		{
			matrix[ static_cast<std::size_t>( c * 4 + r2 ) ] = static_cast<float>( rows[ r2 ][ c ] );
		}
	}
	return matrix;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::array<double, 3>> CViewCamera::Project( Point_t p ) const
{
	const Point_t d = Subtract( p, m_Eye );
	const double flDepth = Dot( d, GetForward() );
	if ( flDepth < m_flNear )
	{
		return std::nullopt;
	}
	const auto [ flFx, flFy ] = GetFocal();
	const double flX = flFx * Dot( d, GetRight() ) / flDepth;
	const double flY = flFy * Dot( d, GetUp() ) / flDepth;
	// The same depth the projection gives: the clip z over w, from -1..1 to 0..1.
	const double flZ = ( ( m_flFar + m_flNear ) / ( m_flFar - m_flNear ) - 2.0 * m_flFar * m_flNear / ( ( m_flFar - m_flNear ) * flDepth ) );
	return std::array<double, 3>{ ( flX + 1.0 ) * 0.5 * m_nWidth, ( 1.0 - flY ) * 0.5 * m_nHeight, flZ * 0.5 + 0.5 };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::pair<Point_t, Point_t> CViewCamera::Ray( double x, double y ) const
{
	const auto [ flFx, flFy ] = GetFocal();
	const double flNx = ( x / std::max( m_nWidth, 1 ) ) * 2.0 - 1.0;
	const double flNy = 1.0 - ( y / std::max( m_nHeight, 1 ) ) * 2.0;
	const Point_t vecDir = Add( Add( GetForward(), Scale( GetRight(), flNx / flFx ) ), Scale( GetUp(), flNy / flFy ) );
	return { m_Eye, Normalized( vecDir ) };
}

//-----------------------------------------------------------------------------
// Purpose: Tests the box's corners against the view's planes: out only when all are beyond
//          one of them.
//-----------------------------------------------------------------------------
bool CViewCamera::CanSee( const Box_t &box ) const
{
	if ( box.IsEmpty() )
	{
		return false;
	}
	const Point_t f = GetForward();
	const Point_t r = GetRight();
	const Point_t u = GetUp();
	const auto [ flFx, flFy ] = GetFocal();
	// Inward normals of the side planes, through the eye.
	const std::array<Point_t, 4> sides = {
		Normalized( Add( Scale( f, 1.0 / flFx ), r ) ),
		Normalized( Subtract( Scale( f, 1.0 / flFx ), r ) ),
		Normalized( Add( Scale( f, 1.0 / flFy ), u ) ),
		Normalized( Subtract( Scale( f, 1.0 / flFy ), u ) ),
	};
	std::array<Point_t, 8> corners;
	for ( std::size_t i = 0; i < 8; i++ )
	{
		corners[ i ] = { ( i & 1 ) ? box.m_Maxs[ 0 ] : box.m_Mins[ 0 ], ( i & 2 ) ? box.m_Maxs[ 1 ] : box.m_Mins[ 1 ], ( i & 4 ) ? box.m_Maxs[ 2 ] : box.m_Mins[ 2 ] };
	}
	auto allOutside = [ & ]( auto &&distance ) {
		return std::ranges::all_of( corners, [ & ]( const Point_t &p ) { return distance( Subtract( p, m_Eye ) ) < 0.0; } );
	};
	if ( allOutside( [ & ]( const Point_t &d ) { return Dot( d, f ) - m_flNear; } ) || allOutside( [ & ]( const Point_t &d ) { return m_flFar - Dot( d, f ); } ) )
	{
		return false;
	}
	for ( const Point_t &side : sides )
	{
		if ( allOutside( [ & ]( const Point_t &d ) { return Dot( d, side ); } ) )
		{
			return false;
		}
	}
	return true;
}

} // namespace xv::map
