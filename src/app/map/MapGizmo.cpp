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

#include <QPolygonF>

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
// Purpose: Decode which transform belongs to a part of the combined gizmo.
//-----------------------------------------------------------------------------
MapGizmoMode_t CMapGizmo::GetPartMode( int nPart ) const
{
	if ( m_nMode != MapGizmoMode_t::MAP_GIZMO_MODE_ALL )
		return m_nMode;
	return nPart >= 3 && nPart < 6 ? MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE : nPart >= 6 && nPart < 9 ? MapGizmoMode_t::MAP_GIZMO_MODE_SCALE
																									 : MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE;
}

//-----------------------------------------------------------------------------
// Purpose: Decode an axis colour; free movement uses the selection colour.
//-----------------------------------------------------------------------------
int CMapGizmo::GetPartAxis( int nPart ) const
{
	return m_nMode == MapGizmoMode_t::MAP_GIZMO_MODE_ALL ? ( nPart == 12 ? -1 : nPart % 3 ) : nPart;
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
	if ( m_nMode == MapGizmoMode_t::MAP_GIZMO_MODE_ALL )
	{
		CMapGizmo part = *this;
		for ( int i = 0; i < 3; ++i )
		{
			part.SetMode( i == 0 ? MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE : i == 1 ? MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE
																					 : MapGizmoMode_t::MAP_GIZMO_MODE_SCALE );
			for ( auto line : part.GetLines( camera, flPixels * ( i == 0 ? 1.0 : i == 1 ? 0.8
																						: 0.55 ) ) )
			{
				line.m_nPart += i * 3;
				lines.push_back( line );
			}
		}
		const double flLength = GetLength( camera, flPixels );
		for ( int a = 0; a < 3; ++a )
		{
			const Point_t &u = m_Axes[ ( a + 1 ) % 3 ];
			const Point_t &v = m_Axes[ ( a + 2 ) % 3 ];
			auto corner = [ & ]( int n ) { return xv::map::Add( m_Pivot, xv::map::Add( xv::map::Scale( u, flLength * ( n & 1 ? 0.38 : 0.23 ) ), xv::map::Scale( v, flLength * ( n & 2 ? 0.38 : 0.23 ) ) ) ); };
			for ( const auto &edge : std::array<std::array<int, 2>, 4>{ { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 } } } )
				lines.push_back( { corner( edge[ 0 ] ), corner( edge[ 1 ] ), 9 + a } );
		}
		for ( int i = 0; i < RING_SEGMENTS; ++i )
		{
			auto circle = [ & ]( int n ) {
				const double flAngle = 2.0 * std::numbers::pi * n / RING_SEGMENTS;
				return xv::map::Add( m_Pivot, xv::map::Add( xv::map::Scale( camera.GetRight(), flLength * 0.1 * std::cos( flAngle ) ), xv::map::Scale( camera.GetUp(), flLength * 0.1 * std::sin( flAngle ) ) ) );
			};
			lines.push_back( { circle( i ), circle( i + 1 ), 12 } );
		}
		return lines;
	}
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
	}
	return lines;
}

//-----------------------------------------------------------------------------
// Purpose: Solid arrowheads and scale handles in the gizmo's own axes.
//-----------------------------------------------------------------------------
std::vector<MapGizmoTriangle_t> CMapGizmo::GetTriangles( const xv::map::CViewCamera &camera, double flPixels ) const
{
	std::vector<MapGizmoTriangle_t> triangles;
	if ( m_nMode == MapGizmoMode_t::MAP_GIZMO_MODE_ALL )
	{
		CMapGizmo part = *this;
		for ( int i : { 0, 2 } )
		{
			part.SetMode( i == 0 ? MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE : MapGizmoMode_t::MAP_GIZMO_MODE_SCALE );
			for ( auto triangle : part.GetTriangles( camera, flPixels * ( i == 0 ? 1.0 : 0.55 ) ) )
			{
				triangle.m_nPart += i * 3;
				triangles.push_back( triangle );
			}
		}
		const double flLength = GetLength( camera, flPixels );
		for ( int a = 0; a < 3; ++a )
		{
			const Point_t &u = m_Axes[ ( a + 1 ) % 3 ];
			const Point_t &v = m_Axes[ ( a + 2 ) % 3 ];
			auto corner = [ & ]( int n ) { return xv::map::Add( m_Pivot, xv::map::Add( xv::map::Scale( u, flLength * ( n & 1 ? 0.38 : 0.23 ) ), xv::map::Scale( v, flLength * ( n & 2 ? 0.38 : 0.23 ) ) ) ); };
			triangles.push_back( { { corner( 0 ), corner( 1 ), corner( 3 ) }, m_Axes[ a ], 9 + a } );
			triangles.push_back( { { corner( 0 ), corner( 3 ), corner( 2 ) }, m_Axes[ a ], 9 + a } );
		}
		return triangles;
	}
	const double flLength = GetLength( camera, flPixels );
	if ( flLength <= 0.0 || m_nMode == MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE )
		return triangles;
	auto add = [ & ]( Point_t a, Point_t b, Point_t c, int nAxis ) {
		const Point_t normal = xv::map::Normalized( xv::map::Cross( xv::map::Subtract( b, a ), xv::map::Subtract( c, a ) ) );
		triangles.push_back( { { a, b, c }, normal, nAxis } );
	};
	for ( int a = 0; a < 3; ++a )
	{
		const Point_t &axis = m_Axes[ a ];
		const Point_t &u = m_Axes[ ( a + 1 ) % 3 ];
		const Point_t &v = m_Axes[ ( a + 2 ) % 3 ];
		const Point_t tip = xv::map::Add( m_Pivot, xv::map::Scale( axis, flLength ) );
		const double flHead = flLength * 0.22;
		if ( m_nMode == MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE )
		{
			const Point_t base = xv::map::Subtract( tip, xv::map::Scale( axis, flHead ) );
			auto rim = [ & ]( int i ) {
				const double flAngle = 2.0 * std::numbers::pi * i / 12;
				return xv::map::Add( base, xv::map::Add( xv::map::Scale( u, flHead * 0.45 * std::cos( flAngle ) ), xv::map::Scale( v, flHead * 0.45 * std::sin( flAngle ) ) ) );
			};
			for ( int i = 0; i < 12; ++i )
			{
				add( tip, rim( i ), rim( i + 1 ), a );
				add( base, rim( i + 1 ), rim( i ), a );
			}
		}
		else
		{
			const double h = flHead * 0.35;
			const Point_t centre = xv::map::Subtract( tip, xv::map::Scale( axis, h ) );
			auto corner = [ & ]( int n ) { return xv::map::Add( centre, xv::map::Add( xv::map::Scale( axis, ( n & 1 ? h : -h ) * ( m_nMode == MapGizmoMode_t::MAP_GIZMO_MODE_SHEAR ? 0.15 : 1.0 ) ), xv::map::Add( xv::map::Scale( u, n & 2 ? h : -h ), xv::map::Scale( v, n & 4 ? h : -h ) ) ) ); };
			for ( const auto &face : std::array<std::array<int, 4>, 6>{ { { 0, 4, 6, 2 }, { 1, 3, 7, 5 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 2, 3, 1 }, { 4, 5, 7, 6 } } } )
			{
				add( corner( face[ 0 ] ), corner( face[ 1 ] ), corner( face[ 2 ] ), a );
				add( corner( face[ 0 ] ), corner( face[ 2 ] ), corner( face[ 3 ] ), a );
			}
		}
	}
	return triangles;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CMapGizmo::HitAt( const xv::map::CViewCamera &camera, double flPixels, QPointF at, double flSlack ) const
{
	int nBest = -1;
	double flBest = flSlack;
	int nBestPriority = 99;
	auto consider = [ & ]( int nPart, double flDistance, int nPriority ) {
		if ( flDistance < flBest - 1e-6 || ( std::abs( flDistance - flBest ) <= 1e-6 && nPriority < nBestPriority ) )
		{
			nBest = nPart;
			flBest = flDistance;
			nBestPriority = nPriority;
		}
	};
	for ( const auto &triangle : GetTriangles( camera, flPixels ) )
	{
		QPolygonF polygon;
		for ( const auto &point : camera.ProjectPolygon( triangle.m_Points ) )
			polygon << QPointF{ point[ 0 ], point[ 1 ] };
		if ( polygon.size() >= 3 && polygon.containsPoint( at, Qt::OddEvenFill ) )
			consider( triangle.m_nPart, 0.0, triangle.m_nPart >= 9 ? 1 : 0 );
	}
	if ( m_nMode == MapGizmoMode_t::MAP_GIZMO_MODE_ALL )
	{
		if ( const auto centre = camera.Project( m_Pivot ); centre && std::hypot( at.x() - ( *centre )[ 0 ], at.y() - ( *centre )[ 1 ] ) <= flPixels * 0.1 )
			consider( 12, 0.0, 1 );
	}
	for ( const MapGizmoLine_t &line : GetLines( camera, flPixels ) )
	{
		const auto projected = camera.ProjectLine( line.m_From, line.m_To );
		if ( !projected )
			continue;
		const auto &[ a, b ] = *projected;
		const double flDistance = DistanceToSegment( at, QPointF{ a[ 0 ], a[ 1 ] }, QPointF{ b[ 0 ], b[ 1 ] } );
		const int nPriority = GetPartMode( line.m_nPart ) == MapGizmoMode_t::MAP_GIZMO_MODE_ROTATE ? 3 : 2;
		consider( line.m_nPart, flDistance, nPriority );
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
	if ( m_nMode == MapGizmoMode_t::MAP_GIZMO_MODE_ALL )
	{
		if ( nAxis < 0 || nAxis > 12 )
			return {};
		CMapGizmo part = *this;
		if ( nAxis < 9 )
		{
			part.SetMode( GetPartMode( nAxis ) );
			return part.Drag( camera, nAxis % 3, from, to, flGrid, flDegreesStep );
		}
		const Point_t normal = nAxis == 12 ? camera.GetForward() : m_Axes[ nAxis - 9 ];
		part.m_Axes[ 0 ] = normal;
		const auto pFrom = part.OnPlane( camera, 0, from );
		const auto pTo = part.OnPlane( camera, 0, to );
		if ( !pFrom || !pTo )
			return {};
		const Point_t delta = xv::map::Subtract( *pTo, *pFrom );
		Point_t snapped{};
		for ( int i = 0; i < 3; ++i )
		{
			if ( nAxis != 12 && i == nAxis - 9 )
				continue;
			double flAmount = xv::map::Dot( delta, m_Axes[ i ] );
			if ( flGrid > 0.0 )
				flAmount = std::round( flAmount / flGrid ) * flGrid;
			snapped = xv::map::Add( snapped, xv::map::Scale( m_Axes[ i ], flAmount ) );
		}
		return Transform_t::Translation( snapped );
	}
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
		case MapGizmoMode_t::MAP_GIZMO_MODE_SHEAR:
		{
			const auto flFrom = AlongAxis( camera, nAxis, from );
			const auto flTo = AlongAxis( camera, nAxis, to );
			if ( !flFrom || !flTo )
				return {};
			int nAcross = ( nAxis + 1 ) % 3;
			for ( int i = 0; i < 3; ++i )
				if ( i != nAxis && std::abs( xv::map::Dot( m_Axes[ i ], camera.GetUp() ) ) > std::abs( xv::map::Dot( m_Axes[ nAcross ], camera.GetUp() ) ) )
					nAcross = i;
			const Point_t &across = m_Axes[ nAcross ];
			double flExtent = m_Bounds.IsEmpty() ? GetLength( camera, 90.0 ) : 0.0;
			if ( !m_Bounds.IsEmpty() )
				for ( int i = 0; i < 3; ++i )
					flExtent += std::abs( across[ i ] ) * ( m_Bounds.m_Maxs[ i ] - m_Bounds.m_Mins[ i ] );
			if ( flExtent < 1e-6 )
				return {};
			double flDelta = *flTo - *flFrom;
			if ( flGrid > 0.0 )
				flDelta = std::round( flDelta / flGrid ) * flGrid;
			const Point_t pivot = xv::map::Subtract( m_Pivot, xv::map::Scale( across, flExtent * 0.5 ) );
			Transform_t shear;
			for ( int r = 0; r < 3; ++r )
				for ( int c = 0; c < 3; ++c )
					shear.m_Rotation[ r * 3 + c ] += axis[ r ] * across[ c ] * flDelta / flExtent;
			shear.m_Pivot = pivot;
			shear.m_Offset = xv::map::Subtract( pivot, shear.Rotate( pivot ) );
			return shear;
		}
		case MapGizmoMode_t::MAP_GIZMO_MODE_ALL: return {};
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
