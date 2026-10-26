//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Geometry.h.
//
//===========================================================================//
#include "Geometry.h"

#include <algorithm>

namespace xv::map
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: Adds p unless a point within flEpsilon is already there, keeping the order.
//-----------------------------------------------------------------------------
void AppendUnique( std::vector<Point_t> &points, Point_t p, double flEpsilon )
{
	if ( std::ranges::none_of( points, [ p, flEpsilon ]( Point_t q ) { const Point_t d = Subtract( p, q ); return Dot( d, d ) < flEpsilon * flEpsilon; } ) )
	{
		points.push_back( p );
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void Box_t::Add( Point_t p )
{
	for ( std::size_t i = 0; i < 3; i++ )
	{
		m_Mins[ i ] = std::min( m_Mins[ i ], p[ i ] );
		m_Maxs[ i ] = std::max( m_Maxs[ i ], p[ i ] );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void Box_t::Add( const Box_t &box )
{
	if ( !box.IsEmpty() )
	{
		Add( box.m_Mins );
		Add( box.m_Maxs );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool Box_t::Contains( Point_t p ) const
{
	for ( std::size_t i = 0; i < 3; i++ )
	{
		if ( p[ i ] < m_Mins[ i ] || p[ i ] > m_Maxs[ i ] )
		{
			return false;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool Box_t::Intersects( const Box_t &box ) const
{
	for ( std::size_t i = 0; i < 3; i++ )
	{
		if ( box.m_Maxs[ i ] < m_Mins[ i ] || box.m_Mins[ i ] > m_Maxs[ i ] )
		{
			return false;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<Plane_t> PlaneFromPoints( const std::array<Point_t, 3> &points )
{
	const Point_t vecNormal = Cross( Subtract( points[ 0 ], points[ 1 ] ), Subtract( points[ 2 ], points[ 1 ] ) );
	const double flLength = Length( vecNormal );
	// Relative to the edges, so tiny and huge brushes are judged alike.
	const double flEdges = Length( Subtract( points[ 0 ], points[ 1 ] ) ) * Length( Subtract( points[ 2 ], points[ 1 ] ) );
	if ( !std::isfinite( flLength ) || flLength <= 1e-12 * flEdges || flLength == 0.0 )
	{
		return std::nullopt;
	}
	Plane_t plane;
	plane.m_Normal = Scale( vecNormal, 1.0 / flLength );
	plane.m_flDistance = Dot( plane.m_Normal, points[ 1 ] );
	return plane;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<Point_t> BaseWinding( const Plane_t &plane, double flExtent )
{
	// The world axis least like the normal gives a well-conditioned direction across the plane.
	std::size_t nAxis = 0;
	for ( std::size_t i = 1; i < 3; i++ )
	{
		if ( std::abs( plane.m_Normal[ i ] ) < std::abs( plane.m_Normal[ nAxis ] ) )
		{
			nAxis = i;
		}
	}
	Point_t vecAxis{};
	vecAxis[ nAxis ] = 1.0;
	const Point_t vecU = Normalized( Cross( plane.m_Normal, vecAxis ) );
	const Point_t vecV = Cross( plane.m_Normal, vecU ); // u x v = normal
	const Point_t vecCenter = Scale( plane.m_Normal, plane.m_flDistance );
	const Point_t u = Scale( vecU, flExtent );
	const Point_t v = Scale( vecV, flExtent );
	// Anticlockwise seen from in front would be (-u-v, +u-v, +u+v, -u+v); this is the reverse.
	return { Subtract( Add( vecCenter, v ), u ), Add( Add( vecCenter, u ), v ), Subtract( Add( vecCenter, u ), v ), Subtract( Subtract( vecCenter, u ), v ) };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<Point_t> ClipPolygon( std::span<const Point_t> polygon, const Plane_t &plane, double flEpsilon )
{
	std::vector<Point_t> kept;
	kept.reserve( polygon.size() + 1 );
	for ( std::size_t i = 0; i < polygon.size(); i++ )
	{
		const Point_t a = polygon[ i ];
		const Point_t b = polygon[ ( i + 1 ) % polygon.size() ];
		const double flA = plane.Distance( a );
		const double flB = plane.Distance( b );
		if ( flA <= flEpsilon )
		{
			kept.push_back( a );
		}
		// The edge crosses from one side to the other (not merely touching the plane).
		if ( ( flA < -flEpsilon && flB > flEpsilon ) || ( flA > flEpsilon && flB < -flEpsilon ) )
		{
			kept.push_back( Lerp( a, b, flA / ( flA - flB ) ) );
		}
	}
	return kept;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::vector<Polygon_t>> IntersectHalfSpaces( std::span<const Plane_t> planes, double flBound, double flEpsilon, std::string *pError )
{
	std::vector<Polygon_t> faces;
	for ( int nAxis = 0; nAxis < 3; nAxis++ )
	{
		for ( int nSign : { -1, 1 } )
		{
			Polygon_t face;
			for ( const auto &corner : std::array<std::array<int, 2>, 4>{ { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } } } )
			{
				Point_t p{};
				p[ static_cast<std::size_t>( nAxis ) ] = flBound * nSign;
				p[ static_cast<std::size_t>( ( nAxis + 1 ) % 3 ) ] = flBound * corner[ 0 ];
				p[ static_cast<std::size_t>( ( nAxis + 2 ) % 3 ) ] = flBound * corner[ 1 ];
				face.m_Points.push_back( p );
			}
			faces.push_back( std::move( face ) );
		}
	}
	for ( std::size_t i = 0; i < planes.size(); i++ )
	{
		const Plane_t &plane = planes[ i ];
		std::vector<Polygon_t> clipped;
		std::vector<Point_t> cut;
		for ( const Polygon_t &face : faces )
		{
			Polygon_t next{ face.m_nPlane, {} };
			for ( std::size_t j = 0; j < face.m_Points.size(); j++ )
			{
				const Point_t a = face.m_Points[ j ];
				const Point_t b = face.m_Points[ ( j + 1 ) % face.m_Points.size() ];
				const double flA = Dot( plane.m_Normal, a ) - plane.m_flDistance;
				const double flB = Dot( plane.m_Normal, b ) - plane.m_flDistance;
				if ( flA <= flEpsilon )
				{
					AppendUnique( next.m_Points, a, flEpsilon );
				}
				if ( ( flA < -flEpsilon && flB > flEpsilon ) || ( flB < -flEpsilon && flA > flEpsilon ) )
				{
					const Point_t p = Add( a, Scale( Subtract( b, a ), flA / ( flA - flB ) ) );
					AppendUnique( next.m_Points, p, flEpsilon );
					AppendUnique( cut, p, flEpsilon );
				}
				if ( std::abs( flA ) <= flEpsilon )
				{
					AppendUnique( cut, a, flEpsilon );
				}
			}
			if ( next.m_Points.size() >= 3 )
			{
				clipped.push_back( std::move( next ) );
			}
		}
		if ( cut.size() >= 3 )
		{
			// The cut's points come out of order; sort them around their centre.
			Point_t vecCenter{};
			for ( const Point_t p : cut )
			{
				vecCenter = Add( vecCenter, Scale( p, 1.0 / static_cast<double>( cut.size() ) ) );
			}
			const Point_t vecU = Subtract( cut[ 0 ], vecCenter );
			const Point_t vecV = Cross( plane.m_Normal, vecU );
			std::ranges::sort( cut, [ & ]( Point_t a, Point_t b ) {
				a = Subtract( a, vecCenter );
				b = Subtract( b, vecCenter );
				return std::atan2( Dot( a, vecV ), Dot( a, vecU ) ) < std::atan2( Dot( b, vecV ), Dot( b, vecU ) );
			} );
			clipped.push_back( { static_cast<int>( i ), std::move( cut ) } );
		}
		faces = std::move( clipped );
		if ( faces.empty() )
		{
			if ( pError )
			{
				*pError = "empty intersection";
			}
			return std::nullopt;
		}
	}
	if ( faces.size() < 4 || std::ranges::any_of( faces, []( const Polygon_t &face ) { return face.m_nPlane < 0; } ) )
	{
		if ( pError )
		{
			*pError = "open or degenerate hull";
		}
		return std::nullopt;
	}
	return faces;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::array<double, 2> TextureCoord( Point_t p, const TextureAxis_t &u, const TextureAxis_t &v, int nWidth, int nHeight )
{
	// A zero scale would put the texture at infinity; reading it as 1 keeps the face drawable.
	const double flUScale = u.m_flScale != 0.0 ? u.m_flScale : 1.0;
	const double flVScale = v.m_flScale != 0.0 ? v.m_flScale : 1.0;
	return { ( Dot( p, u.m_Axis ) / flUScale + u.m_flShift ) / std::max( nWidth, 1 ), ( Dot( p, v.m_Axis ) / flVScale + v.m_flShift ) / std::max( nHeight, 1 ) };
}

} // namespace xv::map
