//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Primitives.h.
//
//===========================================================================//
#include "Primitives.h"

#include <algorithm>
#include <array>
#include <vector>
#include <numbers>
#include <cmath>
#include <cctype>
#include <map>
#include <string>

#include "map/Edit.h"
#include "map/Entities.h"
#include "map/VmfValues.h"
#include "util/Numbers.h"

namespace xv::map
{

//-----------------------------------------------------------------------------
// Purpose: Each side's plane points go p0, p1, p2 with (p0 - p1) x (p2 - p1) facing out of the
//          solid, as the VMF wants them: from a corner p1 along two of the face's edges a and b,
//          where a x b is the outward normal.
//-----------------------------------------------------------------------------
Solid_t MakeBlock( const CMap &map, const Box_t &box, const FaceDefaults_t &faces )
{
	const std::string_view szMaterial = map.Store( std::string{ faces.m_szMaterial } );
	const Point_t &n = box.m_Mins;
	const Point_t &x = box.m_Maxs;
	const Point_t size = Subtract( x, n );
	struct Face_t
	{
		Point_t m_Corner;
		Point_t m_A; // edges from the corner
		Point_t m_B;
		Point_t m_U; // texture axes
		Point_t m_V;
	};
	const std::array<Face_t, 6> sides = { {
		{ { n[ 0 ], n[ 1 ], x[ 2 ] }, { size[ 0 ], 0, 0 }, { 0, size[ 1 ], 0 }, { 1, 0, 0 }, { 0, -1, 0 } }, // top
		{ { n[ 0 ], n[ 1 ], n[ 2 ] }, { 0, size[ 1 ], 0 }, { size[ 0 ], 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 } }, // bottom
		{ { n[ 0 ], n[ 1 ], n[ 2 ] }, { 0, 0, size[ 2 ] }, { 0, size[ 1 ], 0 }, { 0, 1, 0 }, { 0, 0, -1 } }, // -x
		{ { x[ 0 ], n[ 1 ], n[ 2 ] }, { 0, size[ 1 ], 0 }, { 0, 0, size[ 2 ] }, { 0, 1, 0 }, { 0, 0, -1 } }, // +x
		{ { n[ 0 ], x[ 1 ], n[ 2 ] }, { 0, 0, size[ 2 ] }, { size[ 0 ], 0, 0 }, { 1, 0, 0 }, { 0, 0, -1 } }, // +y
		{ { n[ 0 ], n[ 1 ], n[ 2 ] }, { size[ 0 ], 0, 0 }, { 0, 0, size[ 2 ] }, { 1, 0, 0 }, { 0, 0, -1 } }, // -y
	} };
	Solid_t solid;
	for ( const Face_t &face : sides )
	{
		Side_t side;
		side.m_Plane = { Add( face.m_Corner, face.m_A ), face.m_Corner, Add( face.m_Corner, face.m_B ) };
		side.m_bPlaneRead = true;
		side.m_szMaterial = szMaterial;
		side.m_nMaterial = NEW_MATERIAL;
		side.m_UAxis = { face.m_U, 0.0, faces.m_flTextureScale };
		side.m_VAxis = { face.m_V, 0.0, faces.m_flTextureScale };
		side.m_nLightmapScale = faces.m_nLightmapScale;
		solid.m_Sides.push_back( std::move( side ) );
	}
	solid.m_Editor.m_Color = faces.m_Color;
	solid.m_Editor.m_bHasColor = true;
	return solid;
}

//-----------------------------------------------------------------------------
// Purpose: Each polygon's plane, its three points put so (p0 - p1) x (p2 - p1) points away
//          from the solid's middle, and the texture aligned to the world for its normal.
//-----------------------------------------------------------------------------
Solid_t SolidFromFaces( const CMap &map, const std::vector<std::vector<Point_t>> &polygons, const FaceDefaults_t &faces )
{
	const std::string_view szMaterial = map.Store( std::string{ faces.m_szMaterial } );
	Point_t centre{};
	std::size_t nPoints = 0;
	for ( const std::vector<Point_t> &polygon : polygons )
	{
		for ( const Point_t &p : polygon )
		{
			centre = Add( centre, p );
			nPoints++;
		}
	}
	centre = Scale( centre, nPoints ? 1.0 / static_cast<double>( nPoints ) : 0.0 );
	Solid_t solid;
	for ( const std::vector<Point_t> &polygon : polygons )
	{
		if ( polygon.size() < 3 )
		{
			continue;
		}
		// Three corners that aren't in a line.
		std::array<Point_t, 3> points{ polygon[ 0 ], polygon[ 1 ], polygon[ 2 ] };
		Point_t normal = Cross( Subtract( points[ 0 ], points[ 1 ] ), Subtract( points[ 2 ], points[ 1 ] ) );
		for ( std::size_t k = 3; k < polygon.size() && Length( normal ) < 1e-6; k++ )
		{
			points[ 2 ] = polygon[ k ];
			normal = Cross( Subtract( points[ 0 ], points[ 1 ] ), Subtract( points[ 2 ], points[ 1 ] ) );
		}
		if ( Length( normal ) < 1e-6 )
		{
			continue;
		}
		if ( Dot( normal, Subtract( points[ 1 ], centre ) ) < 0.0 )
		{
			std::swap( points[ 0 ], points[ 2 ] );
			normal = Scale( normal, -1.0 );
		}
		Side_t side;
		side.m_Plane = points;
		side.m_bPlaneRead = true;
		side.m_szMaterial = szMaterial;
		side.m_nMaterial = NEW_MATERIAL;
		const Point_t n = Normalized( normal );
		std::size_t nMajor = 0;
		for ( std::size_t a = 1; a < 3; a++ )
		{
			nMajor = std::abs( n[ a ] ) > std::abs( n[ nMajor ] ) ? a : nMajor;
		}
		const Point_t u = nMajor == 0 ? Point_t{ 0, 1, 0 } : Point_t{ 1, 0, 0 };
		const Point_t v = nMajor == 2 ? Point_t{ 0, -1, 0 } : Point_t{ 0, 0, -1 };
		side.m_UAxis = { u, 0.0, faces.m_flTextureScale };
		side.m_VAxis = { v, 0.0, faces.m_flTextureScale };
		side.m_nLightmapScale = faces.m_nLightmapScale;
		solid.m_Sides.push_back( std::move( side ) );
	}
	solid.m_Editor.m_Color = faces.m_Color;
	solid.m_Editor.m_bHasColor = true;
	return solid;
}

//-----------------------------------------------------------------------------
// Purpose: The shapes as Hammer makes them in the dragged box. The round ones start a corner at
//          the top of the top view (+y), going round clockwise seen from above.
//-----------------------------------------------------------------------------
Solid_t MakePrimitive( const CMap &map, Primitive_t nKind, const Box_t &box, const FaceDefaults_t &faces, int nSides )
{
	const Point_t &n = box.m_Mins;
	const Point_t &x = box.m_Maxs;
	const Point_t centre = box.GetCenter();
	const double flRx = ( x[ 0 ] - n[ 0 ] ) / 2.0;
	const double flRy = ( x[ 1 ] - n[ 1 ] ) / 2.0;
	const double flRz = ( x[ 2 ] - n[ 2 ] ) / 2.0;
	nSides = std::clamp( nSides, 3, 64 );
	auto ring = [ & ]( double z, double flScale = 1.0 ) {
		std::vector<Point_t> points;
		for ( int i = 0; i < nSides; i++ )
		{
			const double flAngle = std::numbers::pi / 2.0 - 2.0 * std::numbers::pi * i / nSides;
			points.push_back( { centre[ 0 ] + flRx * flScale * std::cos( flAngle ), centre[ 1 ] + flRy * flScale * std::sin( flAngle ), z } );
		}
		return points;
	};
	std::vector<std::vector<Point_t>> polygons;
	switch ( nKind )
	{
		case Primitive_t::PRIMITIVE_BLOCK:
			return MakeBlock( map, box, faces );
		case Primitive_t::PRIMITIVE_WEDGE:
		{
			// A ramp: the full height at -x, down to nothing at +x.
			polygons.push_back( { { n[ 0 ], n[ 1 ], n[ 2 ] }, { x[ 0 ], n[ 1 ], n[ 2 ] }, { x[ 0 ], x[ 1 ], n[ 2 ] }, { n[ 0 ], x[ 1 ], n[ 2 ] } } ); // bottom
			polygons.push_back( { { n[ 0 ], n[ 1 ], n[ 2 ] }, { n[ 0 ], x[ 1 ], n[ 2 ] }, { n[ 0 ], x[ 1 ], x[ 2 ] }, { n[ 0 ], n[ 1 ], x[ 2 ] } } ); // back
			polygons.push_back( { { n[ 0 ], n[ 1 ], x[ 2 ] }, { n[ 0 ], x[ 1 ], x[ 2 ] }, { x[ 0 ], x[ 1 ], n[ 2 ] }, { x[ 0 ], n[ 1 ], n[ 2 ] } } ); // slope
			polygons.push_back( { { n[ 0 ], n[ 1 ], n[ 2 ] }, { n[ 0 ], n[ 1 ], x[ 2 ] }, { x[ 0 ], n[ 1 ], n[ 2 ] } } );                             // -y end
			polygons.push_back( { { n[ 0 ], x[ 1 ], n[ 2 ] }, { x[ 0 ], x[ 1 ], n[ 2 ] }, { n[ 0 ], x[ 1 ], x[ 2 ] } } );                             // +y end
			break;
		}
		case Primitive_t::PRIMITIVE_CYLINDER:
		{
			const std::vector<Point_t> bottom = ring( n[ 2 ] );
			const std::vector<Point_t> top = ring( x[ 2 ] );
			polygons.push_back( bottom );
			polygons.push_back( top );
			for ( int i = 0; i < nSides; i++ )
			{
				const std::size_t k = static_cast<std::size_t>( ( i + 1 ) % nSides );
				polygons.push_back( { bottom[ static_cast<std::size_t>( i ) ], bottom[ k ], top[ k ], top[ static_cast<std::size_t>( i ) ] } );
			}
			break;
		}
		case Primitive_t::PRIMITIVE_SPIKE:
		{
			const std::vector<Point_t> bottom = ring( n[ 2 ] );
			const Point_t apex{ centre[ 0 ], centre[ 1 ], x[ 2 ] };
			polygons.push_back( bottom );
			for ( int i = 0; i < nSides; i++ )
			{
				const std::size_t k = static_cast<std::size_t>( ( i + 1 ) % nSides );
				polygons.push_back( { bottom[ static_cast<std::size_t>( i ) ], bottom[ k ], apex } );
			}
			break;
		}
		case Primitive_t::PRIMITIVE_SPHERE:
		{
			// Bands of quads between rings, triangles to the poles; a quad between two rings at
			// the same two slices is flat.
			const int nBands = std::max( 2, nSides / 2 );
			std::vector<std::vector<Point_t>> rings;
			for ( int b = 1; b < nBands; b++ )
			{
				const double flLatitude = std::numbers::pi * b / nBands;
				rings.push_back( ring( centre[ 2 ] + flRz * std::cos( flLatitude ), std::sin( flLatitude ) ) );
			}
			const Point_t north{ centre[ 0 ], centre[ 1 ], x[ 2 ] };
			const Point_t south{ centre[ 0 ], centre[ 1 ], n[ 2 ] };
			for ( int i = 0; i < nSides; i++ )
			{
				const auto a = static_cast<std::size_t>( i );
				const auto k = static_cast<std::size_t>( ( i + 1 ) % nSides );
				polygons.push_back( { north, rings.front()[ a ], rings.front()[ k ] } );
				for ( std::size_t r = 0; r + 1 < rings.size(); r++ )
				{
					polygons.push_back( { rings[ r ][ a ], rings[ r ][ k ], rings[ r + 1 ][ k ], rings[ r + 1 ][ a ] } );
				}
				polygons.push_back( { rings.back()[ a ], rings.back()[ k ], south } );
			}
			break;
		}
	}
	return SolidFromFaces( map, polygons, faces );
}

//-----------------------------------------------------------------------------
// Purpose: Hammer writes a new entity's keys sorted by name, whatever order the FGD gives them
//          in, with the origin after them all (as the maps it saved show).
//-----------------------------------------------------------------------------
Entity_t MakePointEntity( const CMap &map, const CFgd *pFgd, std::string_view szClass, const Point_t &origin )
{
	Entity_t entity;
	entity.m_Editor.m_Color = ENTITY_COLOR;
	entity.m_Editor.m_bHasColor = true;
	const FgdClass_t *pClass = pFgd ? pFgd->Find( szClass ) : nullptr;
	entity.m_Keys.push_back( { "classname", map.Store( std::string{ pClass ? std::string_view{ pClass->m_szName } : szClass } ), {} } );
	if ( pClass )
	{
		// By lower-case name: a class that gives a base's key again gives its own default.
		std::map<std::string, std::pair<std::string_view, std::string>> defaults;
		for ( const FgdKey_t &key : pClass->m_Keys )
		{
			std::string szValue = key.m_szDefault;
			if ( key.m_szType == "flags" )
			{
				long long nFlags = 0;
				for ( const FgdChoice_t &choice : key.m_Choices )
				{
					nFlags += choice.m_bDefault ? ParseNumber<long long>( choice.m_szValue ).value_or( 0 ) : 0;
				}
				szValue = std::to_string( nFlags );
			}
			std::string szLower = key.m_szName;
			std::ranges::transform( szLower, szLower.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
			if ( szValue.empty() || szLower == "classname" || szLower == "origin" )
			{
				continue;
			}
			defaults[ std::move( szLower ) ] = { key.m_szName, std::move( szValue ) };
		}
		for ( auto &[ szLower, value ] : defaults )
		{
			entity.m_Keys.push_back( { map.Store( std::string{ value.first } ), map.Store( std::move( value.second ) ), {} } );
		}
		if ( const FgdHelper_t *pColor = pClass->FindHelper( "color" ); pColor && !pColor->m_Arguments.empty() )
		{
			entity.m_Editor.m_Color = ParseColor( pColor->m_Arguments[ 0 ] ).value_or( ENTITY_COLOR );
		}
	}
	entity.m_Keys.push_back( { "origin", map.Store( FormatPoint( origin ) ), {} } );
	return entity;
}

} // namespace xv::map
