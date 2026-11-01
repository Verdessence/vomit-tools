//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Helpers.h. The helpers' arguments are the FGD article's; what Hammer draws for
//          each (a cone's length, an arrow's) is our guess, listed in hammer-check.md.
//
//===========================================================================//
#include "map/Helpers.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "kv1/Kv1.h"
#include "map/Entities.h"
#include "map/Fgd.h"
#include "map/Instance.h"
#include "map/Lighting.h"
#include "map/Scene.h"
#include "map/VmfValues.h"
#include "util/Numbers.h"

namespace xv::map
{
namespace
{
// How long direction()'s arrow is, and lightcone()'s cones when the light names no distance.
constexpr double ARROW_LENGTH = 32.0;
constexpr double CONE_LENGTH = 128.0;
constexpr int RING_SEGMENTS = 48;

//-----------------------------------------------------------------------------
// Purpose: An entity name as the I/O system matches it: without case, a trailing * matching
//          any rest. Names the game fills in (!self, !activator) name nobody here.
//-----------------------------------------------------------------------------
bool NameMatches( std::string_view szPattern, std::string_view szName )
{
	if ( szPattern.empty() || szPattern.starts_with( '!' ) )
		return false;
	if ( szPattern.ends_with( '*' ) )
	{
		szPattern.remove_suffix( 1 );
		return szName.size() >= szPattern.size() && kv1::EqualsIgnoreCase( szName.substr( 0, szPattern.size() ), szPattern );
	}
	return kv1::EqualsIgnoreCase( szPattern, szName );
}

//-----------------------------------------------------------------------------
// Purpose: The entities whose szKey matches szValue, by their origins.
//-----------------------------------------------------------------------------
std::vector<Point_t> EntitiesWhere( const CScene &scene, std::string_view szKey, std::string_view szValue )
{
	std::vector<Point_t> out;
	if ( szValue.empty() )
		return out;
	const auto &entities = scene.m_Map.m_Entities;
	for ( std::size_t e = 0; e < entities.size(); e++ )
		if ( !entities[ e ].m_bDeleted && NameMatches( szValue, entities[ e ].GetValue( szKey ) ) )
			out.push_back( scene.m_Entities[ e ].m_Origin );
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: A helper's colour argument ("255 255 255"), else the entity's own.
//-----------------------------------------------------------------------------
Color_t ColorArgument( const FgdHelper_t &helper, std::size_t nArgument, Color_t fallback )
{
	if ( nArgument < helper.m_Arguments.size() )
		if ( const auto color = ParseColor( helper.m_Arguments[ nArgument ] ) )
			return *color;
	return fallback;
}

//-----------------------------------------------------------------------------
// Purpose: A key's number, or flDefault when it has none.
//-----------------------------------------------------------------------------
double KeyNumber( const Entity_t &entity, std::string_view szKey, double flDefault )
{
	return ParseNumber<double>( entity.GetValue( szKey ) ).value_or( flDefault );
}

//-----------------------------------------------------------------------------
// Purpose: Two directions across axis, at right angles to it and to each other.
//-----------------------------------------------------------------------------
std::pair<Point_t, Point_t> Across( Point_t axis )
{
	const Point_t side = std::abs( axis[ 2 ] ) < 0.9 ? Point_t{ 0, 0, 1 } : Point_t{ 1, 0, 0 };
	const Point_t u = Normalized( Cross( axis, side ) );
	return { u, Cross( axis, u ) };
}

//-----------------------------------------------------------------------------
// Purpose: A circle of radius about centre, in the plane of u and v.
//-----------------------------------------------------------------------------
void Ring( std::vector<HelperLine_t> &out, Point_t centre, Point_t u, Point_t v, double flRadius, Color_t color )
{
	auto at = [ & ]( int n ) {
		const double flAngle = 2.0 * std::numbers::pi * n / RING_SEGMENTS;
		return Add( centre, Add( Scale( u, std::cos( flAngle ) * flRadius ), Scale( v, std::sin( flAngle ) * flRadius ) ) );
	};
	for ( int n = 0; n < RING_SEGMENTS; n++ )
		out.push_back( { at( n ), at( n + 1 ), color } );
}

//-----------------------------------------------------------------------------
// Purpose: A cone of half angle flDegrees from apex along axis, as far as flLength: its rim
//          and four lines from the apex to it.
//-----------------------------------------------------------------------------
void Cone( std::vector<HelperLine_t> &out, Point_t apex, Point_t axis, double flDegrees, double flLength, Color_t color )
{
	flDegrees = std::clamp( flDegrees, 0.0, 89.0 );
	const double flRadius = std::tan( flDegrees * std::numbers::pi / 180.0 ) * flLength;
	const auto [ u, v ] = Across( axis );
	const Point_t centre = Add( apex, Scale( axis, flLength ) );
	Ring( out, centre, u, v, flRadius, color );
	for ( const Point_t &dir : { u, v, Scale( u, -1.0 ), Scale( v, -1.0 ) } )
		out.push_back( { apex, Add( centre, Scale( dir, flRadius ) ), color } );
}

//-----------------------------------------------------------------------------
// Purpose: The twelve edges of a box.
//-----------------------------------------------------------------------------
void Box( std::vector<HelperLine_t> &out, Point_t mins, Point_t maxs, Color_t color )
{
	auto corner = [ & ]( int n ) { return Point_t{ ( n & 1 ) ? maxs[ 0 ] : mins[ 0 ], ( n & 2 ) ? maxs[ 1 ] : mins[ 1 ], ( n & 4 ) ? maxs[ 2 ] : mins[ 2 ] }; };
	for ( int n = 0; n < 8; n++ )
		for ( int nBit = 1; nBit < 8; nBit <<= 1 )
			if ( !( n & nBit ) )
				out.push_back( { corner( n ), corner( n | nBit ), color } );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
EntityHelpers_t DescribeHelpers( const CScene &scene, std::uint32_t nEntity )
{
	EntityHelpers_t helpers;
	if ( nEntity >= scene.m_Map.m_Entities.size() )
		return helpers;
	const Entity_t &entity = scene.m_Map.m_Entities[ nEntity ];
	const EntityVisual_t &visual = scene.m_Entities[ nEntity ];
	const Color_t color = visual.m_Color;
	const Point_t origin = visual.m_Origin;
	for ( const double flRadius : visual.m_Spheres )
	{
		Ring( helpers.m_Rings, origin, { 1, 0, 0 }, { 0, 1, 0 }, flRadius, color );
		Ring( helpers.m_Rings, origin, { 1, 0, 0 }, { 0, 0, 1 }, flRadius, color );
		Ring( helpers.m_Rings, origin, { 0, 1, 0 }, { 0, 0, 1 }, flRadius, color );
	}
	if ( !visual.m_pClass )
		return helpers;
	for ( const FgdHelper_t &helper : visual.m_pClass->m_Helpers )
	{
		const auto &args = helper.m_Arguments;
		if ( helper.m_szName == "line" && args.size() >= 3 )
		{
			// line(colour, their key, our key): from us to each entity whose key holds our key's
			// value; with two more, between the entities the two pairs name.
			const Color_t lineColor = ColorArgument( helper, 0, color );
			const std::vector<Point_t> first = EntitiesWhere( scene, args[ 1 ], entity.GetValue( args[ 2 ] ) );
			const bool bPair = args.size() >= 5;
			const std::vector<Point_t> starts = bPair ? first : std::vector<Point_t>{ origin };
			const std::vector<Point_t> ends = bPair ? EntitiesWhere( scene, args[ 3 ], entity.GetValue( args[ 4 ] ) ) : first;
			for ( const Point_t &from : starts )
				for ( const Point_t &to : ends )
					helpers.m_Lines.push_back( { from, to, lineColor } );
		}
		else if ( helper.m_szName == "lightcone" )
		{
			// The inner cone and the outer, as far as the light's own distance when it has one.
			const Point_t axis = LightDirection( entity );
			double flLength = KeyNumber( entity, "_zero_percent_distance", 0.0 );
			if ( flLength <= 0.0 )
				flLength = KeyNumber( entity, "_distance", 0.0 );
			if ( flLength <= 0.0 )
				flLength = CONE_LENGTH;
			const Color_t lightColor = ParseColor( entity.GetValue( "_light" ) ).value_or( color );
			Cone( helpers.m_Lines, origin, axis, KeyNumber( entity, "_inner_cone", 30.0 ), flLength, lightColor );
			Cone( helpers.m_Lines, origin, axis, KeyNumber( entity, "_cone", 45.0 ), flLength, lightColor );
		}
		else if ( helper.m_szName == "wirebox" && args.size() >= 2 )
		{
			const auto mins = ParsePoint( entity.GetValue( args[ 0 ] ) );
			const auto maxs = ParsePoint( entity.GetValue( args[ 1 ] ) );
			if ( mins && maxs )
				Box( helpers.m_Lines, Add( origin, *mins ), Add( origin, *maxs ), color );
		}
		else if ( helper.m_szName == "direction" )
		{
			// An arrow along the key's angles (angles by default); invert_pitch flips its pitch.
			const std::string_view szKey = args.empty() ? std::string_view{ "angles" } : std::string_view{ args[ 0 ] };
			std::array<double, 3> angles = ParseTriple( entity.GetValue( szKey ) ).value_or( visual.m_Angles );
			if ( args.size() >= 2 && kv1::EqualsIgnoreCase( args[ 1 ], "invert_pitch" ) )
				angles[ 0 ] = -angles[ 0 ];
			const Point_t forward = Transform_t::FromAngles( {}, angles ).Rotate( { 1, 0, 0 } );
			const Point_t tip = Add( origin, Scale( forward, ARROW_LENGTH ) );
			const auto [ u, v ] = Across( forward );
			helpers.m_Lines.push_back( { origin, tip, color } );
			for ( const Point_t &dir : { u, Scale( u, -1.0 ), v, Scale( v, -1.0 ) } )
				helpers.m_Lines.push_back( { tip, Add( Subtract( tip, Scale( forward, ARROW_LENGTH / 4.0 ) ), Scale( dir, ARROW_LENGTH / 8.0 ) ), color } );
		}
		else if ( helper.m_szName == "frustum" )
		{
			// frustum(fov, near, far, colour, pitch scale): the projection's pyramid between its
			// near and far planes.
			auto key = [ & ]( std::size_t n, double flDefault ) { return n < args.size() ? KeyNumber( entity, args[ n ], flDefault ) : flDefault; };
			const double flFov = std::clamp( key( 0, 90.0 ), 1.0, 179.0 );
			const double flNear = key( 1, 4.0 );
			const double flFar = key( 2, 750.0 );
			const Color_t frustumColor = args.size() >= 4 ? ParseColor( entity.GetValue( args[ 3 ] ) ).value_or( color ) : color;
			std::array<double, 3> angles = visual.m_Angles;
			if ( args.size() >= 5 )
				angles[ 0 ] *= ParseNumber<double>( args[ 4 ] ).value_or( 1.0 );
			const Transform_t turn = Transform_t::FromAngles( {}, angles );
			const Point_t forward = turn.Rotate( { 1, 0, 0 } );
			const Point_t left = turn.Rotate( { 0, 1, 0 } );
			const Point_t up = turn.Rotate( { 0, 0, 1 } );
			const double flSlope = std::tan( flFov * std::numbers::pi / 360.0 );
			auto corners = [ & ]( double flDistance ) {
				const Point_t centre = Add( origin, Scale( forward, flDistance ) );
				const double flHalf = flSlope * flDistance;
				std::array<Point_t, 4> out{};
				for ( int n = 0; n < 4; n++ )
					out[ n ] = Add( centre, Add( Scale( left, ( n == 0 || n == 3 ) ? flHalf : -flHalf ), Scale( up, n < 2 ? flHalf : -flHalf ) ) );
				return out;
			};
			const auto nearCorners = corners( flNear );
			const auto farCorners = corners( flFar );
			for ( int n = 0; n < 4; n++ )
			{
				helpers.m_Lines.push_back( { nearCorners[ n ], nearCorners[ ( n + 1 ) % 4 ], frustumColor } );
				helpers.m_Lines.push_back( { farCorners[ n ], farCorners[ ( n + 1 ) % 4 ], frustumColor } );
				helpers.m_Lines.push_back( { nearCorners[ n ], farCorners[ n ], frustumColor } );
			}
		}
		else if ( helper.m_szName == "sidelist" )
		{
			const std::string_view szKey = args.empty() ? std::string_view{ "sides" } : std::string_view{ args[ 0 ] };
			std::string_view szValue = entity.GetValue( szKey );
			while ( !szValue.empty() )
			{
				const std::size_t nSpace = szValue.find( ' ' );
				if ( const auto nSide = ParseNumber<int>( szValue.substr( 0, nSpace ) ) )
					helpers.m_Sides.push_back( *nSide );
				szValue = nSpace == std::string_view::npos ? std::string_view{} : szValue.substr( nSpace + 1 );
			}
		}
	}
	return helpers;
}

} // namespace xv::map
