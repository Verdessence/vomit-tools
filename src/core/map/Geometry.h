//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Double-precision geometry for brushes: points, planes, boxes, convex polygons cut by
//          planes, and Hammer's texture axes. Shared by the map reader and the model-to-VMF
//          exporter.
//
//===========================================================================//
#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "util/Vec3.h"

namespace xv::map
{

using Point_t = std::array<double, 3>;

[[nodiscard]] inline Point_t Add( Point_t a, Point_t b )
{
	return { a[ 0 ] + b[ 0 ], a[ 1 ] + b[ 1 ], a[ 2 ] + b[ 2 ] };
}

[[nodiscard]] inline Point_t Subtract( Point_t a, Point_t b )
{
	return { a[ 0 ] - b[ 0 ], a[ 1 ] - b[ 1 ], a[ 2 ] - b[ 2 ] };
}

[[nodiscard]] inline Point_t Scale( Point_t a, double flScale )
{
	return { a[ 0 ] * flScale, a[ 1 ] * flScale, a[ 2 ] * flScale };
}

[[nodiscard]] inline double Dot( Point_t a, Point_t b )
{
	return a[ 0 ] * b[ 0 ] + a[ 1 ] * b[ 1 ] + a[ 2 ] * b[ 2 ];
}

[[nodiscard]] inline Point_t Cross( Point_t a, Point_t b )
{
	return { a[ 1 ] * b[ 2 ] - a[ 2 ] * b[ 1 ], a[ 2 ] * b[ 0 ] - a[ 0 ] * b[ 2 ], a[ 0 ] * b[ 1 ] - a[ 1 ] * b[ 0 ] };
}

[[nodiscard]] inline double Length( Point_t a )
{
	return std::sqrt( Dot( a, a ) );
}

// a scaled to length 1; a itself when it has no length.
[[nodiscard]] inline Point_t Normalized( Point_t a )
{
	const double flLength = Length( a );
	return flLength > 0.0 ? Scale( a, 1.0 / flLength ) : a;
}

[[nodiscard]] inline Point_t Lerp( Point_t a, Point_t b, double t )
{
	return Add( a, Scale( Subtract( b, a ), t ) );
}

[[nodiscard]] inline Point_t ToPoint( CVec3 vec )
{
	return { vec.m_flX, vec.m_flY, vec.m_flZ };
}

[[nodiscard]] inline CVec3 ToVec3( Point_t p )
{
	return { static_cast<float>( p[ 0 ] ), static_cast<float>( p[ 1 ] ), static_cast<float>( p[ 2 ] ) };
}

// The points p with dot(normal, p) == distance. Solids lie behind their planes.
struct Plane_t
{
	Point_t m_Normal{};
	double m_flDistance = 0.0;

	// Above zero in front of the plane (outside a solid), below zero behind it.
	[[nodiscard]] double Distance( Point_t p ) const { return Dot( m_Normal, p ) - m_flDistance; }
};

struct Box_t
{
	Point_t m_Mins{ std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity() };
	Point_t m_Maxs{ -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() };

	void Add( Point_t p );
	void Add( const Box_t &box );
	[[nodiscard]] bool IsEmpty() const { return m_Mins[ 0 ] > m_Maxs[ 0 ]; }
	[[nodiscard]] Point_t GetCenter() const { return Scale( map::Add( m_Mins, m_Maxs ), 0.5 ); }
	[[nodiscard]] Point_t GetSize() const { return Subtract( m_Maxs, m_Mins ); }
	[[nodiscard]] bool Contains( Point_t p ) const;
	[[nodiscard]] bool Intersects( const Box_t &box ) const;

	friend bool operator==( const Box_t &, const Box_t & ) = default;
};

// A convex polygon on one of a set of planes (m_nPlane indexes them; -1 for none).
struct Polygon_t
{
	int m_nPlane = -1;
	std::vector<Point_t> m_Points;
};

// The plane through a VMF side's three points, facing out of the solid: Hammer writes them so
// that (p0 - p1) x (p2 - p1) points outwards. nullopt when the points don't span a plane.
[[nodiscard]] std::optional<Plane_t> PlaneFromPoints( const std::array<Point_t, 3> &points );

// A square of half-width flExtent on plane, centred on the point nearest the origin, wound
// clockwise seen from in front (the order brush faces are drawn in).
[[nodiscard]] std::vector<Point_t> BaseWinding( const Plane_t &plane, double flExtent );
// The same into out, reusing its room.
void BaseWinding( const Plane_t &plane, double flExtent, std::vector<Point_t> &out );

// The part of a convex polygon behind plane (or within flEpsilon of it), keeping the winding.
[[nodiscard]] std::vector<Point_t> ClipPolygon( std::span<const Point_t> polygon, const Plane_t &plane, double flEpsilon );
// The same into out (not polygon itself), reusing its room: cutting many faces allocates nothing
// once it has grown.
void ClipPolygon( std::span<const Point_t> polygon, const Plane_t &plane, double flEpsilon, std::vector<Point_t> &out );

// The convex solid behind every plane, as one polygon per plane that touches it (wound
// anticlockwise seen from outside), found by cutting a box of half-width flBound. nullopt when
// the planes leave nothing, or leave the solid open (it reaches the box).
[[nodiscard]] std::optional<std::vector<Polygon_t>> IntersectHalfSpaces( std::span<const Plane_t> planes, double flBound, double flEpsilon, std::string *pError = nullptr );

// One of a side's texture axes: "[x y z shift] scale".
struct TextureAxis_t
{
	Point_t m_Axis{};
	double m_flShift = 0.0;
	double m_flScale = 0.25;
};

// Where p falls on a face's texture in texels, before dividing by the texture's size.
[[nodiscard]] std::array<double, 2> TexelCoord( Point_t p, const TextureAxis_t &u, const TextureAxis_t &v );

// Where p falls on a face's texture, in texture widths (Hammer's maths: the world position
// along the axis, divided by the scale, plus the shift, over the texture's size in pixels).
[[nodiscard]] std::array<double, 2> TextureCoord( Point_t p, const TextureAxis_t &u, const TextureAxis_t &v, int nWidth, int nHeight );

} // namespace xv::map
