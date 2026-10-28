//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapGizmo.h.
//
//===========================================================================//
#include "MapGizmo.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using xv::map::Point_t;
using xv::map::Transform_t;

namespace
{

constexpr int RING_SEGMENTS = 32;

//-----------------------------------------------------------------------------
// Purpose: A pixel's distance from the segment between two projected points.
//-----------------------------------------------------------------------------
double DistanceToSegment( QPointF p, QPointF a, QPointF b )
{
	const QPointF ab = b - a;
	const double flLength = QPointF::dotProduct( ab, ab );
	const double t = flLength > 0.0 ? std::clamp( QPointF::dotProduct( p - a, ab ) / flLength, 0.0, 1.0 ) : 0.0;
	const QPointF nearest = a + ab * t;
	return std::hypot( p.x() - nearest.x(), p.y() - nearest.y() );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapGizmo::Place( Point_t pivot, const std::array<Point_t, 3> &axes )
{
	m_Pivot = pivot;
	for ( std::size_t a = 0; a < 3; a++ )
	{
		m_Axes[ a ] = xv::map::Normalized( axes[ a ] );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The same size on screen wherever it stands: a unit step sideways at the pivot's
//          depth tells how many pixels a unit is there.
//-----------------------------------------------------------------------------
double CMapGizmo::GetLength( const xv::map::CViewCamera &camera, double flPixels ) const
{
	const auto centre = camera.Project( m_Pivot );
	const auto beside = camera.Project( xv::map::Add( m_Pivot, camera.GetRight() ) );
	if ( !centre || !beside )
	{
		return 0.0;
	}
	const double flPixelsPerUnit = std::hypot( ( *beside )[ 0 ] - ( *centre )[ 0 ], ( *beside )[ 1 ] - ( *centre )[ 1 ] );
	return flPixelsPerUnit > 1e-9 ? flPixels / flPixelsPerUnit : 0.0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<MapGizmoLine_t> CMapGizmo::GetLines( const xv::map::CViewCamera &camera, double flPixels ) const
{
	std::vector<MapGizmoLine_t> lines;
	const double flLength = GetLength( camera, flPixels );
	if ( flLength <= 0.0 )
	{
		return lines;
	}
	for ( int a = 0; a < 3; a++ )
	{
		const Point_t &axis = m_Axes[ static_cast<std::size_t>( a ) ];
		const Point_t &u = m_Axes[ static_cast<std::size_t>( ( a + 1 ) % 3 ) ];
		const Point_t &v = m_Axes[ static_cast<std::size_t>( ( a + 2 ) % 3 ) ];
		const Point_t tip = xv::map::Add( m_Pivot, xv::map::Scale( axis, flLength ) );
		if ( m_nMode == MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE )
		{
			// A ring in the plane the axis is normal to.
			for ( int i = 0; i < RING_SEGMENTS; i++ )
			{
				const double flA = 2.0 * std::numbers::pi * i / RING_SEGMENTS;
				const double flB = 2.0 * std::numbers::pi * ( i + 1 ) / RING_SEGMENTS;
				const Point_t pa = xv::map::Add( m_Pivot, xv::map::Add( xv::map::Scale( u, flLength * std::cos( flA ) ), xv::map::Scale( v, flLength * std::sin( flA ) ) ) );
				const Point_t pb = xv::map::Add( m_Pivot, xv::map::Add( xv::map::Scale( u, flLength * std::cos( flB ) ), xv::map::Scale( v, flLength * std::sin( flB ) ) ) );
				lines.push_back( { pa, pb, a } );
			}
			continue;
		}
		lines.push_back( { m_Pivot, tip, a } );
		const double flHead = flLength * 0.18;
		if ( m_nMode == MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE )
		{
			// An arrowhead: four lines back from the tip.
			const Point_t back = xv::map::Subtract( tip, xv::map::Scale( axis, flHead ) );
			for ( const Point_t &side : { u, v } )
			{
				lines.push_back( { tip, xv::map::Add( back, xv::map::Scale( side, flHead * 0.4 ) ), a } );
				lines.push_back( { tip, xv::map::Subtract( back, xv::map::Scale( side, flHead * 0.4 ) ), a } );
			}
		}
		else
		{
			// A cube at the tip.
			const double h = flHead * 0.35;
			const Point_t c = xv::map::Subtract( tip, xv::map::Scale( axis, h ) );
			auto corner = [ & ]( int n ) {
				return xv::map::Add( c, xv::map::Add( xv::map::Scale( axis, ( n & 1 ) ? h : -h ), xv::map::Add( xv::map::Scale( u, ( n & 2 ) ? h : -h ), xv::map::Scale( v, ( n & 4 ) ? h : -h ) ) ) );
			};
			for ( int n = 0; n < 8; n++ )
			{
				for ( const int nBit : { 1, 2, 4 } )
				{
					if ( ( n & nBit ) == 0 )
					{
						lines.push_back( { corner( n ), corner( n | nBit ), a } );
					}
				}
			}
		}
	}
	return lines;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CMapGizmo::HitAt( const xv::map::CViewCamera &camera, double flPixels, QPointF at, double flSlack ) const
{
	int nBest = -1;
	double flBest = flSlack;
	for ( const MapGizmoLine_t &line : GetLines( camera, flPixels ) )
	{
		const auto a = camera.Project( line.m_From );
		const auto b = camera.Project( line.m_To );
		if ( !a || !b )
		{
			continue;
		}
		const double flDistance = DistanceToSegment( at, QPointF{ ( *a )[ 0 ], ( *a )[ 1 ] }, QPointF{ ( *b )[ 0 ], ( *b )[ 1 ] } );
		if ( flDistance <= flBest )
		{
			flBest = flDistance;
			nBest = line.m_nAxis;
		}
	}
	return nBest;
}

//-----------------------------------------------------------------------------
// Purpose: The closest points of the axis' line and the pixel's ray; the axis' one, as a
//          distance from the pivot. None when the two are parallel.
//-----------------------------------------------------------------------------
std::optional<double> CMapGizmo::AlongAxis( const xv::map::CViewCamera &camera, int nAxis, QPointF at ) const
{
	const Point_t &a = m_Axes[ static_cast<std::size_t>( nAxis ) ];
	const auto [ from, d ] = camera.Ray( at.x(), at.y() );
	const Point_t w = xv::map::Subtract( m_Pivot, from );
	const double b = xv::map::Dot( a, d );
	const double flDenominator = 1.0 - b * b;
	if ( flDenominator < 1e-6 )
	{
		return std::nullopt;
	}
	return ( b * xv::map::Dot( w, d ) - xv::map::Dot( w, a ) ) / flDenominator;
}

//-----------------------------------------------------------------------------
// Purpose: Where the pixel's ray meets the plane through the pivot that the axis is normal to.
//-----------------------------------------------------------------------------
std::optional<Point_t> CMapGizmo::OnPlane( const xv::map::CViewCamera &camera, int nAxis, QPointF at ) const
{
	const Point_t &n = m_Axes[ static_cast<std::size_t>( nAxis ) ];
	const auto [ from, d ] = camera.Ray( at.x(), at.y() );
	const double flFacing = xv::map::Dot( d, n );
	if ( std::abs( flFacing ) < 1e-6 )
	{
		return std::nullopt;
	}
	const double t = xv::map::Dot( xv::map::Subtract( m_Pivot, from ), n ) / flFacing;
	if ( t <= 0.0 )
	{
		return std::nullopt;
	}
	return xv::map::Add( from, xv::map::Scale( d, t ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Transform_t CMapGizmo::Drag( const xv::map::CViewCamera &camera, int nAxis, QPointF from, QPointF to, double flGrid, double flDegreesStep ) const
{
	if ( nAxis < 0 || nAxis > 2 )
	{
		return {};
	}
	const Point_t &axis = m_Axes[ static_cast<std::size_t>( nAxis ) ];
	switch ( m_nMode )
	{
		case MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE:
		{
			const auto flFrom = AlongAxis( camera, nAxis, from );
			const auto flTo = AlongAxis( camera, nAxis, to );
			if ( !flFrom || !flTo )
			{
				return {};
			}
			double flDelta = *flTo - *flFrom;
			if ( flGrid > 0.0 )
			{
				flDelta = std::round( flDelta / flGrid ) * flGrid;
			}
			return Transform_t::Translation( xv::map::Scale( axis, flDelta ) );
		}
		case MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE:
		{
			const auto pFrom = OnPlane( camera, nAxis, from );
			const auto pTo = OnPlane( camera, nAxis, to );
			if ( !pFrom || !pTo )
			{
				return {};
			}
			const Point_t u = xv::map::Subtract( *pFrom, m_Pivot );
			const Point_t v = xv::map::Subtract( *pTo, m_Pivot );
			if ( xv::map::Length( u ) < 1e-9 || xv::map::Length( v ) < 1e-9 )
			{
				return {};
			}
			// The angle from u to v, the right-hand way about the axis.
			double flDegrees = std::atan2( xv::map::Dot( xv::map::Cross( u, v ), axis ), xv::map::Dot( u, v ) ) * 180.0 / std::numbers::pi;
			if ( flDegreesStep > 0.0 )
			{
				flDegrees = std::round( flDegrees / flDegreesStep ) * flDegreesStep;
			}
			return Transform_t::RotationAbout( m_Pivot, axis, flDegrees );
		}
		case MapGizmoMode_t::MAP_GIZMO_MODE_SCALE:
		{
			const auto flFrom = AlongAxis( camera, nAxis, from );
			const auto flTo = AlongAxis( camera, nAxis, to );
			if ( !flFrom || !flTo || std::abs( *flFrom ) < 1e-6 )
			{
				return {};
			}
			return Transform_t::ScalingAlong( m_Pivot, axis, *flTo / *flFrom );
		}
	}
	return {};
}
