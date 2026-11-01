//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Generator.h. The arch follows the VDC's Arch Properties article (the box's
//          ellipse, wall width, sides, arc, start angle, add height); the other templates are
//          our own.
//
//===========================================================================//
#include "map/Generator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace xv::map
{
namespace
{
// Below this a brush would be a sliver vbsp throws away.
constexpr double SMALLEST = 0.5;

struct Builder_t
{
	const CMap &m_Map;
	const FaceDefaults_t &m_Faces;
	std::vector<Solid_t> m_Solids;

	//-----------------------------------------------------------------------------
	// Purpose: A box between two corners, when it has some size each way.
	//-----------------------------------------------------------------------------
	void Block( Point_t mins, Point_t maxs )
	{
		for ( std::size_t a = 0; a < 3; a++ )
		{
			if ( mins[ a ] > maxs[ a ] )
				std::swap( mins[ a ], maxs[ a ] );
			if ( maxs[ a ] - mins[ a ] < SMALLEST )
				return;
		}
		Box_t box;
		box.m_Mins = mins;
		box.m_Maxs = maxs;
		m_Solids.push_back( MakeBlock( m_Map, box, m_Faces ) );
	}

	//-----------------------------------------------------------------------------
	// Purpose: A convex outline in the top view (x, y) stood up from flBottom to flTop.
	//-----------------------------------------------------------------------------
	void Prism( const std::vector<std::array<double, 2>> &outline, double flBottom, double flTop )
	{
		if ( outline.size() < 3 || flTop - flBottom < SMALLEST )
			return;
		std::vector<std::vector<Point_t>> polygons( 2 );
		for ( const auto &p : outline )
		{
			polygons[ 0 ].push_back( { p[ 0 ], p[ 1 ], flBottom } );
			polygons[ 1 ].push_back( { p[ 0 ], p[ 1 ], flTop } );
		}
		for ( std::size_t n = 0; n < outline.size(); n++ )
		{
			const auto &a = outline[ n ];
			const auto &b = outline[ ( n + 1 ) % outline.size() ];
			if ( std::hypot( b[ 0 ] - a[ 0 ], b[ 1 ] - a[ 1 ] ) < 1e-6 )
				continue;
			polygons.push_back( { { a[ 0 ], a[ 1 ], flBottom }, { b[ 0 ], b[ 1 ], flBottom }, { b[ 0 ], b[ 1 ], flTop }, { a[ 0 ], a[ 1 ], flTop } } );
		}
		m_Solids.push_back( SolidFromFaces( m_Map, polygons, m_Faces ) );
	}

	//-----------------------------------------------------------------------------
	// Purpose: A piece of a ring between two ellipses about centre, from one angle to another
	//          (degrees, counter-clockwise from +x); a wedge when the inner one has no size.
	//-----------------------------------------------------------------------------
	void RingPiece( std::array<double, 2> centre, std::array<double, 2> outer, std::array<double, 2> inner, double flFrom, double flTo, double flBottom, double flTop )
	{
		auto at = [ & ]( std::array<double, 2> radii, double flDegrees ) {
			const double flRadians = flDegrees * std::numbers::pi / 180.0;
			// Kept to a thousandth, so the pieces' shared corners are written the same.
			return std::array<double, 2>{ std::round( ( centre[ 0 ] + radii[ 0 ] * std::cos( flRadians ) ) * 1000.0 ) / 1000.0,
				std::round( ( centre[ 1 ] + radii[ 1 ] * std::sin( flRadians ) ) * 1000.0 ) / 1000.0 };
		};
		std::vector<std::array<double, 2>> outline{ at( outer, flFrom ), at( outer, flTo ) };
		if ( inner[ 0 ] > SMALLEST && inner[ 1 ] > SMALLEST )
		{
			outline.push_back( at( inner, flTo ) );
			outline.push_back( at( inner, flFrom ) );
		}
		else
			outline.push_back( centre );
		Prism( outline, flBottom, flTop );
	}
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool Fail( std::string *pError, std::string szWhy )
{
	if ( pError )
		*pError = std::move( szWhy );
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Each step a slice along the climb, up to its height (or one step thick).
//-----------------------------------------------------------------------------
bool Stairs( Builder_t &builder, const Box_t &box, const BuildingParams_t &params, std::string *pError )
{
	if ( params.m_nSteps < 1 || params.m_nSteps > 256 )
		return Fail( pError, "Stairs need 1 to 256 steps." );
	const bool bY = params.m_nDirection == BuildingDirection_t::BUILDING_DIRECTION_POSITIVE_Y || params.m_nDirection == BuildingDirection_t::BUILDING_DIRECTION_NEGATIVE_Y;
	const bool bNegative = params.m_nDirection == BuildingDirection_t::BUILDING_DIRECTION_NEGATIVE_X || params.m_nDirection == BuildingDirection_t::BUILDING_DIRECTION_NEGATIVE_Y;
	const std::size_t nAxis = bY ? 1 : 0;
	const double flRun = ( box.m_Maxs[ nAxis ] - box.m_Mins[ nAxis ] ) / params.m_nSteps;
	const double flRise = ( box.m_Maxs[ 2 ] - box.m_Mins[ 2 ] ) / params.m_nSteps;
	for ( int i = 0; i < params.m_nSteps; i++ )
	{
		Point_t mins = box.m_Mins;
		Point_t maxs = box.m_Maxs;
		mins[ nAxis ] = bNegative ? box.m_Maxs[ nAxis ] - ( i + 1 ) * flRun : box.m_Mins[ nAxis ] + i * flRun;
		maxs[ nAxis ] = mins[ nAxis ] + flRun;
		mins[ 2 ] = params.m_bSolidUnder ? box.m_Mins[ 2 ] : box.m_Mins[ 2 ] + i * flRise;
		maxs[ 2 ] = box.m_Mins[ 2 ] + ( i + 1 ) * flRise;
		builder.Block( mins, maxs );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Steps round the box's middle, between the inner radius and the box's ellipse,
//          climbing the box's height.
//-----------------------------------------------------------------------------
bool SpiralStairs( Builder_t &builder, const Box_t &box, const BuildingParams_t &params, std::string *pError )
{
	if ( params.m_nSteps < 1 || params.m_nSteps > 512 )
		return Fail( pError, "Spiral stairs need 1 to 512 steps." );
	if ( params.m_flDegreesPerStep <= 0.0 || params.m_flDegreesPerStep >= 180.0 )
		return Fail( pError, "Each step turns more than 0 and less than 180 degrees." );
	const std::array<double, 2> centre{ ( box.m_Mins[ 0 ] + box.m_Maxs[ 0 ] ) / 2.0, ( box.m_Mins[ 1 ] + box.m_Maxs[ 1 ] ) / 2.0 };
	const std::array<double, 2> outer{ ( box.m_Maxs[ 0 ] - box.m_Mins[ 0 ] ) / 2.0, ( box.m_Maxs[ 1 ] - box.m_Mins[ 1 ] ) / 2.0 };
	if ( params.m_flInnerRadius >= std::min( outer[ 0 ], outer[ 1 ] ) - SMALLEST )
		return Fail( pError, "The inner radius must be less than the box's half width." );
	const double flInner = std::max( 0.0, params.m_flInnerRadius );
	const double flRise = ( box.m_Maxs[ 2 ] - box.m_Mins[ 2 ] ) / params.m_nSteps;
	for ( int i = 0; i < params.m_nSteps; i++ )
	{
		const double flFrom = params.m_flStartAngle + i * params.m_flDegreesPerStep;
		builder.RingPiece( centre, outer, { flInner, flInner }, flFrom, flFrom + params.m_flDegreesPerStep, params.m_bSolidUnder ? box.m_Mins[ 2 ] : box.m_Mins[ 2 ] + i * flRise,
			box.m_Mins[ 2 ] + ( i + 1 ) * flRise );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's arch: the box's ellipse in the top view, the wall width in from it (out
//          from it when negative), in sides pieces over the arc.
//-----------------------------------------------------------------------------
bool Arch( Builder_t &builder, const Box_t &box, const BuildingParams_t &params, std::string *pError )
{
	if ( params.m_nSides < 3 || params.m_nSides > 100 )
		return Fail( pError, "An arch has 3 to 100 sides." );
	if ( params.m_flArc < 8.0 || params.m_flArc > 360.0 )
		return Fail( pError, "An arch spans 8 to 360 degrees." );
	const std::array<double, 2> centre{ ( box.m_Mins[ 0 ] + box.m_Maxs[ 0 ] ) / 2.0, ( box.m_Mins[ 1 ] + box.m_Maxs[ 1 ] ) / 2.0 };
	const std::array<double, 2> radii{ ( box.m_Maxs[ 0 ] - box.m_Mins[ 0 ] ) / 2.0, ( box.m_Maxs[ 1 ] - box.m_Mins[ 1 ] ) / 2.0 };
	const double flWidth = params.m_flWallWidth;
	std::array<double, 2> outer = radii;
	std::array<double, 2> inner{ radii[ 0 ] - flWidth, radii[ 1 ] - flWidth };
	if ( flWidth < 0.0 )
	{
		outer = inner;
		inner = radii;
	}
	if ( std::abs( flWidth ) < SMALLEST )
		return Fail( pError, "The wall width can't be 0." );
	const double flStep = params.m_flArc / params.m_nSides;
	for ( int i = 0; i < params.m_nSides; i++ )
	{
		const double flFrom = params.m_flStartAngle + i * flStep;
		builder.RingPiece( centre, outer, { std::max( 0.0, inner[ 0 ] ), std::max( 0.0, inner[ 1 ] ) }, flFrom, flFrom + flStep, box.m_Mins[ 2 ] + i * params.m_flAddHeight,
			box.m_Maxs[ 2 ] + i * params.m_flAddHeight );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A wall along the long axis from flFrom to flTo, between the two across, with a doorway
//          in its middle if asked.
//-----------------------------------------------------------------------------
void Wall( Builder_t &builder, std::size_t nAlong, double flFrom, double flTo, std::array<double, 2> across, double flBottom, double flTop, bool bDoor, const BuildingParams_t &params )
{
	const std::size_t nAcross = nAlong == 0 ? 1 : 0;
	auto block = [ & ]( double flA, double flB, double flLow, double flHigh ) {
		Point_t mins{};
		Point_t maxs{};
		mins[ nAlong ] = flA;
		maxs[ nAlong ] = flB;
		mins[ nAcross ] = across[ 0 ];
		maxs[ nAcross ] = across[ 1 ];
		mins[ 2 ] = flLow;
		maxs[ 2 ] = flHigh;
		builder.Block( mins, maxs );
	};
	if ( !bDoor )
	{
		block( flFrom, flTo, flBottom, flTop );
		return;
	}
	const double flMiddle = ( flFrom + flTo ) / 2.0;
	block( flFrom, flMiddle - params.m_flDoorWidth / 2.0, flBottom, flTop );
	block( flMiddle + params.m_flDoorWidth / 2.0, flTo, flBottom, flTop );
	block( flMiddle - params.m_flDoorWidth / 2.0, flMiddle + params.m_flDoorWidth / 2.0, flBottom + params.m_flDoorHeight, flTop );
}

//-----------------------------------------------------------------------------
// Purpose: A floor, a ceiling and four walls inside the box, none overlapping: the ±X walls run
//          the box's depth, the ±Y walls fit between them.
//-----------------------------------------------------------------------------
bool Room( Builder_t &builder, const Box_t &box, const BuildingParams_t &params, std::string *pError )
{
	const double t = params.m_flThickness;
	const Point_t size = box.GetSize();
	if ( t < SMALLEST || 2.0 * t + SMALLEST > std::min( { size[ 0 ], size[ 1 ], size[ 2 ] } ) )
		return Fail( pError, "The walls are too thick for the box." );
	const double flBottom = box.m_Mins[ 2 ] + t;
	const double flTop = box.m_Maxs[ 2 ] - t;
	if ( std::ranges::any_of( params.m_Doors, []( bool b ) { return b; } ) )
	{
		if ( params.m_flDoorHeight < SMALLEST || params.m_flDoorHeight > flTop - flBottom )
			return Fail( pError, "The doorway is taller than the room." );
		const double flShortest = std::min( size[ 0 ], size[ 1 ] ) - 2.0 * t;
		if ( params.m_flDoorWidth < SMALLEST || params.m_flDoorWidth > flShortest - 2.0 * SMALLEST )
			return Fail( pError, "The doorway is wider than a wall." );
	}
	builder.Block( box.m_Mins, { box.m_Maxs[ 0 ], box.m_Maxs[ 1 ], flBottom } );
	builder.Block( { box.m_Mins[ 0 ], box.m_Mins[ 1 ], flTop }, box.m_Maxs );
	Wall( builder, 1, box.m_Mins[ 1 ], box.m_Maxs[ 1 ], { box.m_Maxs[ 0 ] - t, box.m_Maxs[ 0 ] }, flBottom, flTop, params.m_Doors[ 0 ], params );
	Wall( builder, 1, box.m_Mins[ 1 ], box.m_Maxs[ 1 ], { box.m_Mins[ 0 ], box.m_Mins[ 0 ] + t }, flBottom, flTop, params.m_Doors[ 1 ], params );
	Wall( builder, 0, box.m_Mins[ 0 ] + t, box.m_Maxs[ 0 ] - t, { box.m_Maxs[ 1 ] - t, box.m_Maxs[ 1 ] }, flBottom, flTop, params.m_Doors[ 2 ], params );
	Wall( builder, 0, box.m_Mins[ 0 ] + t, box.m_Maxs[ 0 ] - t, { box.m_Mins[ 1 ], box.m_Mins[ 1 ] + t }, flBottom, flTop, params.m_Doors[ 3 ], params );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The box as a wall along its longer side: a band below the sill, then each row of
//          windows (piers between them) with a band above it; the windows spaced evenly.
//-----------------------------------------------------------------------------
bool WindowWall( Builder_t &builder, const Box_t &box, const BuildingParams_t &params, std::string *pError )
{
	const Point_t size = box.GetSize();
	const std::size_t nAlong = size[ 0 ] >= size[ 1 ] ? 0 : 1;
	const std::size_t nAcross = 1 - nAlong;
	const int nColumns = params.m_nColumns;
	const int nRows = params.m_nRows;
	if ( nColumns < 1 || nRows < 1 || nColumns > 64 || nRows > 64 )
		return Fail( pError, "A wall has 1 to 64 columns and rows of windows." );
	const double flLength = size[ nAlong ];
	const double flPier = ( flLength - nColumns * params.m_flWindowWidth ) / ( nColumns + 1 );
	if ( params.m_flWindowWidth < SMALLEST || flPier < SMALLEST )
		return Fail( pError, "The windows don't fit along the wall." );
	const double flGap = ( size[ 2 ] - params.m_flSill - nRows * params.m_flWindowHeight ) / nRows;
	if ( params.m_flWindowHeight < SMALLEST || params.m_flSill < 0.0 || flGap < 0.0 )
		return Fail( pError, "The windows don't fit up the wall." );
	auto block = [ & ]( double flA, double flB, double flLow, double flHigh ) {
		Point_t mins{};
		Point_t maxs{};
		mins[ nAlong ] = box.m_Mins[ nAlong ] + flA;
		maxs[ nAlong ] = box.m_Mins[ nAlong ] + flB;
		mins[ nAcross ] = box.m_Mins[ nAcross ];
		maxs[ nAcross ] = box.m_Maxs[ nAcross ];
		mins[ 2 ] = box.m_Mins[ 2 ] + flLow;
		maxs[ 2 ] = box.m_Mins[ 2 ] + flHigh;
		builder.Block( mins, maxs );
	};
	block( 0.0, flLength, 0.0, params.m_flSill );
	for ( int nRow = 0; nRow < nRows; nRow++ )
	{
		const double flLow = params.m_flSill + nRow * ( params.m_flWindowHeight + flGap );
		const double flHigh = flLow + params.m_flWindowHeight;
		for ( int nColumn = 0; nColumn <= nColumns; nColumn++ )
		{
			const double flA = nColumn * ( flPier + params.m_flWindowWidth );
			block( flA, flA + flPier, flLow, flHigh );
		}
		block( 0.0, flLength, flHigh, flHigh + flGap );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Posts at each end and evenly between, about the spacing apart, under a rail along
//          the box's longer side.
//-----------------------------------------------------------------------------
bool Railing( Builder_t &builder, const Box_t &box, const BuildingParams_t &params, std::string *pError )
{
	const Point_t size = box.GetSize();
	const std::size_t nAlong = size[ 0 ] >= size[ 1 ] ? 0 : 1;
	const double flPost = params.m_flPostSize;
	if ( flPost < SMALLEST || flPost * 2.0 > size[ nAlong ] || params.m_flPostSpacing < flPost )
		return Fail( pError, "The posts don't fit along the railing." );
	if ( params.m_flRailHeight < SMALLEST || params.m_flRailHeight > size[ 2 ] - SMALLEST )
		return Fail( pError, "The rail is taller than the railing." );
	const int nPosts = std::max( 2, static_cast<int>( std::lround( ( size[ nAlong ] - flPost ) / params.m_flPostSpacing ) ) + 1 );
	const double flStep = ( size[ nAlong ] - flPost ) / ( nPosts - 1 );
	const double flRail = box.m_Maxs[ 2 ] - params.m_flRailHeight;
	for ( int n = 0; n < nPosts; n++ )
	{
		Point_t mins = box.m_Mins;
		Point_t maxs = box.m_Maxs;
		mins[ nAlong ] = box.m_Mins[ nAlong ] + n * flStep;
		maxs[ nAlong ] = mins[ nAlong ] + flPost;
		maxs[ 2 ] = flRail;
		builder.Block( mins, maxs );
	}
	builder.Block( { box.m_Mins[ 0 ], box.m_Mins[ 1 ], flRail }, box.m_Maxs );
	return true;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<Solid_t> GenerateBuilding( const CMap &map, BuildingKind_t nKind, const Box_t &box, const BuildingParams_t &params, const FaceDefaults_t &faces, std::string *pError )
{
	Builder_t builder{ map, faces, {} };
	const Point_t size = box.GetSize();
	if ( box.IsEmpty() || size[ 0 ] < SMALLEST || size[ 1 ] < SMALLEST || size[ 2 ] < SMALLEST )
	{
		Fail( pError, "The box needs some size each way." );
		return {};
	}
	bool bMade = false;
	switch ( nKind )
	{
		case BuildingKind_t::BUILDING_KIND_STAIRS:
			bMade = Stairs( builder, box, params, pError );
			break;
		case BuildingKind_t::BUILDING_KIND_SPIRAL_STAIRS:
			bMade = SpiralStairs( builder, box, params, pError );
			break;
		case BuildingKind_t::BUILDING_KIND_ARCH:
			bMade = Arch( builder, box, params, pError );
			break;
		case BuildingKind_t::BUILDING_KIND_ROOM:
			bMade = Room( builder, box, params, pError );
			break;
		case BuildingKind_t::BUILDING_KIND_WINDOW_WALL:
			bMade = WindowWall( builder, box, params, pError );
			break;
		case BuildingKind_t::BUILDING_KIND_RAILING:
			bMade = Railing( builder, box, params, pError );
			break;
	}
	return bMade ? std::move( builder.m_Solids ) : std::vector<Solid_t>{};
}

} // namespace xv::map
