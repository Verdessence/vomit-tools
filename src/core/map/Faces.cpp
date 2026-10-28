//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Faces.h.
//
//===========================================================================//
#include "Faces.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "map/Brushes.h"

namespace xv::map
{

namespace
{

constexpr double DEGREES = std::numbers::pi / 180.0;

//-----------------------------------------------------------------------------
// Purpose: Which of x, y and z the normal is mostly along.
//-----------------------------------------------------------------------------
std::size_t MajorAxis( const Point_t &normal )
{
	std::size_t nMajor = 0;
	for ( std::size_t a = 1; a < 3; a++ )
	{
		nMajor = std::abs( normal[ a ] ) > std::abs( normal[ nMajor ] ) ? a : nMajor;
	}
	return nMajor;
}

//-----------------------------------------------------------------------------
// Purpose: v turned about axis (a unit vector) by the right-hand rule.
//-----------------------------------------------------------------------------
Point_t Turn( const Point_t &v, const Point_t &axis, double flDegrees )
{
	const double c = std::cos( flDegrees * DEGREES ), s = std::sin( flDegrees * DEGREES );
	return Add( Add( Scale( v, c ), Scale( Cross( axis, v ), s ) ), Scale( axis, Dot( axis, v ) * ( 1.0 - c ) ) );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::pair<Point_t, Point_t> WorldAxes( const Point_t &normal )
{
	switch ( MajorAxis( normal ) )
	{
		case 0: return { { 0, 1, 0 }, { 0, 0, -1 } };
		case 1: return { { 1, 0, 0 }, { 0, 0, -1 } };
		default: return { { 1, 0, 0 }, { 0, -1, 0 } };
	}
}

//-----------------------------------------------------------------------------
// Purpose: Down the face is the world's down projected onto it, or -y where the face is level;
//          across it is what is at right angles to both.
//-----------------------------------------------------------------------------
std::pair<Point_t, Point_t> FaceAxes( const Point_t &normal )
{
	const Point_t n = Normalized( normal );
	const Point_t up = std::abs( n[ 2 ] ) > 0.999 ? Point_t{ 0, 1, 0 } : Point_t{ 0, 0, 1 };
	const Point_t u = Normalized( Cross( up, n ) );
	const Point_t v = Normalized( Cross( u, n ) );
	return { u, v };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsWorldAligned( const Side_t &side, const Point_t &normal )
{
	const auto [ u, v ] = WorldAxes( normal );
	auto same = []( const Point_t &a, const Point_t &b ) { return Length( Subtract( Normalized( a ), b ) ) < 1e-4; };
	return same( side.m_UAxis.m_Axis, u ) && same( side.m_VAxis.m_Axis, v );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
TexelExtent_t ExtentOf( const Side_t &side, const std::vector<Point_t> &points )
{
	TexelExtent_t extent{ std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest() };
	TextureAxis_t u = side.m_UAxis;
	TextureAxis_t v = side.m_VAxis;
	u.m_flShift = 0.0;
	v.m_flShift = 0.0;
	for ( const Point_t &p : points )
	{
		const auto texel = TexelCoord( p, u, v );
		extent.m_flMinU = std::min( extent.m_flMinU, texel[ 0 ] );
		extent.m_flMaxU = std::max( extent.m_flMaxU, texel[ 0 ] );
		extent.m_flMinV = std::min( extent.m_flMinV, texel[ 1 ] );
		extent.m_flMaxV = std::max( extent.m_flMaxV, texel[ 1 ] );
	}
	if ( points.empty() )
	{
		extent = {};
	}
	return extent;
}

//-----------------------------------------------------------------------------
// Purpose: A shift moves the texture's origin to the extent's edge (the texture's own width
//          or height along, for the far edges). Fit first scales each axis so the extent is one
//          texture wide, then justifies to the top left.
//-----------------------------------------------------------------------------
void Justify( Side_t &side, Justify_t nHow, const TexelExtent_t &extent, int nWidth, int nHeight )
{
	TexelExtent_t at = extent;
	if ( nHow == Justify_t::JUSTIFY_FIT )
	{
		// The extent in texels is the world span over the scale: the scale that makes it the
		// texture's size is the old one times extent / size.
		const double flSpanU = extent.m_flMaxU - extent.m_flMinU;
		const double flSpanV = extent.m_flMaxV - extent.m_flMinV;
		if ( flSpanU > 1e-9 && nWidth > 0 )
		{
			side.m_UAxis.m_flScale *= flSpanU / nWidth;
			at.m_flMinU = extent.m_flMinU * nWidth / flSpanU;
			at.m_flMaxU = at.m_flMinU + nWidth;
		}
		if ( flSpanV > 1e-9 && nHeight > 0 )
		{
			side.m_VAxis.m_flScale *= flSpanV / nHeight;
			at.m_flMinV = extent.m_flMinV * nHeight / flSpanV;
			at.m_flMaxV = at.m_flMinV + nHeight;
		}
		side.m_UAxis.m_flShift = -at.m_flMinU;
		side.m_VAxis.m_flShift = -at.m_flMinV;
		return;
	}
	switch ( nHow )
	{
		case Justify_t::JUSTIFY_LEFT: side.m_UAxis.m_flShift = -at.m_flMinU; break;
		case Justify_t::JUSTIFY_RIGHT: side.m_UAxis.m_flShift = nWidth - at.m_flMaxU; break;
		case Justify_t::JUSTIFY_TOP: side.m_VAxis.m_flShift = -at.m_flMinV; break;
		case Justify_t::JUSTIFY_BOTTOM: side.m_VAxis.m_flShift = nHeight - at.m_flMaxV; break;
		case Justify_t::JUSTIFY_CENTER:
			side.m_UAxis.m_flShift = nWidth / 2.0 - ( at.m_flMinU + at.m_flMaxU ) / 2.0;
			side.m_VAxis.m_flShift = nHeight / 2.0 - ( at.m_flMinV + at.m_flMaxV ) / 2.0;
			break;
		case Justify_t::JUSTIFY_FIT: break;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetRotation( Side_t &side, const Point_t &normal, double flDegrees )
{
	const Point_t n = Normalized( normal );
	const double flTurn = flDegrees - side.m_flRotation;
	side.m_UAxis.m_Axis = Turn( side.m_UAxis.m_Axis, n, flTurn );
	side.m_VAxis.m_Axis = Turn( side.m_VAxis.m_Axis, n, flTurn );
	side.m_flRotation = flDegrees;
}

//-----------------------------------------------------------------------------
// Purpose: The alignment's axes, turned by the rotation the side already has.
//-----------------------------------------------------------------------------
void SetAlignment( Side_t &side, const Point_t &normal, bool bWorld )
{
	const auto [ u, v ] = bWorld ? WorldAxes( normal ) : FaceAxes( normal );
	side.m_UAxis.m_Axis = u;
	side.m_VAxis.m_Axis = v;
	if ( side.m_flRotation != 0.0 )
	{
		const Point_t n = Normalized( normal );
		side.m_UAxis.m_Axis = Turn( u, n, side.m_flRotation );
		side.m_VAxis.m_Axis = Turn( v, n, side.m_flRotation );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetMaterial( const CMap &map, Side_t &side, std::string_view szMaterial )
{
	side.m_szMaterial = map.Store( std::string{ szMaterial } );
	side.m_nMaterial = NEW_MATERIAL;
}

//-----------------------------------------------------------------------------
// Purpose: The faces' solids copied once each, every face of theirs on the list changed.
//-----------------------------------------------------------------------------
MapEdit_t EditFaces( const CScene &scene, const std::vector<FaceRef_t> &faces, const std::function<void( Side_t &, const std::vector<Point_t> & )> &pfnEdit )
{
	MapEdit_t edit;
	std::vector<FaceRef_t> sorted = faces;
	std::ranges::sort( sorted );
	sorted.erase( std::ranges::unique( sorted ).begin(), sorted.end() );
	for ( std::size_t i = 0; i < sorted.size(); )
	{
		const std::uint32_t nSolid = sorted[ i ].m_nSolid;
		if ( nSolid >= scene.m_Map.m_Solids.size() )
		{
			i++;
			continue;
		}
		Solid_t solid = scene.m_Map.m_Solids[ nSolid ];
		const SolidGeometry_t &shape = scene.m_Solids[ nSolid ];
		for ( ; i < sorted.size() && sorted[ i ].m_nSolid == nSolid; i++ )
		{
			const std::uint32_t nSide = sorted[ i ].m_nSide;
			if ( nSide >= solid.m_Sides.size() )
			{
				continue;
			}
			const FaceGeometry_t *pFace = shape.FindFace( nSide );
			pfnEdit( solid.m_Sides[ nSide ], pFace ? pFace->m_Points : std::vector<Point_t>{} );
		}
		solid.m_bChanged = true;
		edit.m_Solids.emplace_back( nSolid, std::move( solid ) );
	}
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<FaceRef_t> FacesOf( const CScene &scene, const std::vector<std::uint32_t> &objects )
{
	std::vector<FaceRef_t> faces;
	const ObjectSet_t set = ObjectsOf( scene, objects );
	for ( const std::uint32_t nSolid : set.m_Solids )
	{
		for ( std::uint32_t nSide = 0; nSide < scene.m_Map.m_Solids[ nSolid ].m_Sides.size(); nSide++ )
		{
			faces.push_back( { nSolid, nSide } );
		}
	}
	return faces;
}

} // namespace xv::map
