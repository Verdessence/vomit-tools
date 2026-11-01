//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Helpers.h. The helpers' arguments are the FGD article's; what Hammer draws for
//          each (a cone's length, an arrow's) is our guess, listed in hammer-check.md.
//
//===========================================================================//
#include "map/Helpers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <string>

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
// Where moving brushes go, and which way trains run along their paths.
constexpr Color_t MOVEMENT_COLOR{ 80, 200, 255 };
constexpr Color_t PATH_COLOR{ 255, 170, 40 };
constexpr std::size_t MAX_PATH_LINKS = 512;

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

//-----------------------------------------------------------------------------
// Purpose: An arrowhead flHead long at tip, pointing along forward (of unit length).
//-----------------------------------------------------------------------------
void Head( std::vector<HelperLine_t> &out, Point_t tip, Point_t forward, double flHead, Color_t color )
{
	const auto [ u, v ] = Across( forward );
	for ( const Point_t &dir : { u, Scale( u, -1.0 ), v, Scale( v, -1.0 ) } )
		out.push_back( { tip, Add( Subtract( tip, Scale( forward, flHead ) ), Scale( dir, flHead / 2.0 ) ), color } );
}

//-----------------------------------------------------------------------------
// Purpose: A line with a head at its end.
//-----------------------------------------------------------------------------
void Arrow( std::vector<HelperLine_t> &out, Point_t from, Point_t to, Color_t color )
{
	const Point_t delta = Subtract( to, from );
	const double flLength = Length( delta );
	out.push_back( { from, to, color } );
	if ( flLength >= 1e-6 )
		Head( out, to, Scale( delta, 1.0 / flLength ), std::min( ARROW_LENGTH / 2.0, flLength / 3.0 ), color );
}

//-----------------------------------------------------------------------------
// Purpose: The way a movedir key points: angles as Hammer writes them, with the game's two
//          special yaws (-1 up, -2 down).
//-----------------------------------------------------------------------------
Point_t MoveDirection( const Entity_t &entity )
{
	const std::array<double, 3> angles = ParseTriple( entity.GetValue( "movedir" ) ).value_or( std::array<double, 3>{} );
	if ( angles[ 0 ] == 0.0 && angles[ 2 ] == 0.0 && angles[ 1 ] == -1.0 )
		return { 0, 0, 1 };
	if ( angles[ 0 ] == 0.0 && angles[ 2 ] == 0.0 && angles[ 1 ] == -2.0 )
		return { 0, 0, -1 };
	return Transform_t::FromAngles( {}, angles ).Rotate( { 1, 0, 0 } );
}

//-----------------------------------------------------------------------------
// Purpose: Where a moving brush entity goes: the way, from where it starts to where it stops,
//          and its box at the far end. Doors and buttons travel their own size along movedir
//          less lip (the game leaves two units of the size out); func_movelinear and
//          func_water_analog travel movedistance, from startposition of the way along it.
//-----------------------------------------------------------------------------
void Movement( const CScene &scene, const Entity_t &entity, std::vector<HelperLine_t> &out )
{
	const std::string_view szClass = entity.GetClassname();
	const bool bDoor = kv1::EqualsIgnoreCase( szClass, "func_door" ) || kv1::EqualsIgnoreCase( szClass, "func_button" ) || kv1::EqualsIgnoreCase( szClass, "momentary_door" );
	const bool bLinear = kv1::EqualsIgnoreCase( szClass, "func_movelinear" ) || kv1::EqualsIgnoreCase( szClass, "func_water_analog" );
	if ( ( !bDoor && !bLinear ) || entity.m_Solids.empty() )
		return;
	Box_t bounds;
	for ( const std::uint32_t nSolid : entity.m_Solids )
		if ( nSolid < scene.m_Solids.size() )
			bounds.Add( scene.m_Solids[ nSolid ].m_Bounds );
	if ( bounds.IsEmpty() )
		return;
	const Point_t direction = MoveDirection( entity );
	Point_t start = bounds.GetCenter();
	double flDistance = 0.0;
	if ( bDoor )
	{
		const Point_t size = Subtract( bounds.m_Maxs, bounds.m_Mins );
		for ( int i = 0; i < 3; i++ )
			flDistance += std::abs( direction[ i ] * ( size[ i ] - 2.0 ) );
		flDistance -= KeyNumber( entity, "lip", 0.0 );
	}
	else
	{
		flDistance = KeyNumber( entity, "movedistance", 100.0 );
		start = Subtract( start, Scale( direction, flDistance * KeyNumber( entity, "startposition", 0.0 ) ) );
	}
	const Point_t offset = Scale( direction, flDistance );
	const Point_t end = Add( start, offset );
	const Point_t shift = Subtract( start, bounds.GetCenter() );
	Arrow( out, start, end, MOVEMENT_COLOR );
	Box( out, Add( Add( bounds.m_Mins, shift ), offset ), Add( Add( bounds.m_Maxs, shift ), offset ), MOVEMENT_COLOR );
	if ( Length( shift ) > 1e-6 )
		Box( out, Add( bounds.m_Mins, shift ), Add( bounds.m_Maxs, shift ), MOVEMENT_COLOR );
}

//-----------------------------------------------------------------------------
// Purpose: Which way rotating doors and buttons turn and how far, as the game sets them up:
//          about the entity's origin, one of its angles growing by distance degrees (yaw unless
//          a flag picks roll or pitch; another turns it the other way). func_rotating spins
//          round for good, its flags naming the axis. An arc traced by the brushes' middle, its
//          head where they stop, and (for those that stop) their box there.
//-----------------------------------------------------------------------------
void Turning( const CScene &scene, const Entity_t &entity, std::vector<HelperLine_t> &out )
{
	const std::string_view szClass = entity.GetClassname();
	const bool bSpinner = kv1::EqualsIgnoreCase( szClass, "func_rotating" );
	const bool bDoor = kv1::EqualsIgnoreCase( szClass, "func_door_rotating" );
	const bool bButton = kv1::EqualsIgnoreCase( szClass, "func_rot_button" ) || kv1::EqualsIgnoreCase( szClass, "momentary_rot_button" );
	if ( ( !bSpinner && !bDoor && !bButton ) || entity.m_Solids.empty() )
		return;
	Box_t bounds;
	for ( const std::uint32_t nSolid : entity.m_Solids )
		if ( nSolid < scene.m_Solids.size() )
			bounds.Add( scene.m_Solids[ nSolid ].m_Bounds );
	if ( bounds.IsEmpty() )
		return;
	const int nFlags = static_cast<int>( KeyNumber( entity, "spawnflags", 0.0 ) );
	std::array<double, 3> step{ 0, 1, 0 };
	double flDegrees = 360.0;
	if ( bSpinner )
	{
		if ( nFlags & 4 )
			step = { 0, 0, 1 };
		else if ( nFlags & 8 )
			step = { 1, 0, 0 };
	}
	else
	{
		if ( nFlags & 64 )
			step = { 0, 0, 1 };
		else if ( nFlags & 128 )
			step = { 1, 0, 0 };
		flDegrees = KeyNumber( entity, "distance", bDoor ? 90.0 : 0.0 );
	}
	if ( nFlags & 2 )
		flDegrees = -flDegrees;
	if ( std::abs( flDegrees ) < 1e-3 )
		return;
	const Point_t pivot = ParsePoint( entity.GetValue( "origin" ) ).value_or( bounds.GetCenter() );
	const std::array<double, 3> angles = ParseTriple( entity.GetValue( "angles" ) ).value_or( std::array<double, 3>{} );
	const Transform_t start = Transform_t::FromAngles( {}, angles );
	// A point as it is when the angles have grown by flTurn: back to the entity's own frame
	// (the turn's rows transposed), then out again at the new angles.
	const auto turned = [ & ]( Point_t point, double flTurn ) {
		const Point_t offset = Subtract( point, pivot );
		const auto &r = start.m_Rotation;
		const Point_t local{ r[ 0 ] * offset[ 0 ] + r[ 3 ] * offset[ 1 ] + r[ 6 ] * offset[ 2 ], r[ 1 ] * offset[ 0 ] + r[ 4 ] * offset[ 1 ] + r[ 7 ] * offset[ 2 ], r[ 2 ] * offset[ 0 ] + r[ 5 ] * offset[ 1 ] + r[ 8 ] * offset[ 2 ] };
		std::array<double, 3> now = angles;
		for ( int i = 0; i < 3; i++ )
			now[ i ] += step[ i ] * flTurn;
		return Add( pivot, Transform_t::FromAngles( {}, now ).Rotate( local ) );
	};
	// The brushes' middle traces the arc; one on the axis is moved out to the box's half size,
	// across the axis, so the arc can be seen.
	Point_t traced = bounds.GetCenter();
	const Point_t spin = [ & ] {
		// The axis the turn is about: the direction a small turn leaves alone.
		Point_t best{ 1, 0, 0 };
		double flLeast = 1e30;
		for ( const Point_t &candidate : { Point_t{ 1, 0, 0 }, Point_t{ 0, 1, 0 }, Point_t{ 0, 0, 1 }, Normalized( start.Rotate( { 1, 0, 0 } ) ), Normalized( start.Rotate( { 0, 1, 0 } ) ), Normalized( start.Rotate( { 0, 0, 1 } ) ) } )
			if ( const double flMoved = Length( Subtract( turned( Add( pivot, candidate ), 10.0 ), Add( pivot, candidate ) ) ); flMoved < flLeast )
			{
				flLeast = flMoved;
				best = candidate;
			}
		return best;
	}();
	const Point_t fromAxis = Subtract( Subtract( traced, pivot ), Scale( spin, Dot( Subtract( traced, pivot ), spin ) ) );
	const double flHalf = Length( Subtract( bounds.m_Maxs, bounds.m_Mins ) ) / 2.0;
	if ( Length( fromAxis ) < std::max( 8.0, flHalf / 4.0 ) )
		traced = Add( traced, Scale( Across( spin ).first, std::max( 16.0, flHalf ) ) );
	const int nSteps = std::max( 8, static_cast<int>( std::abs( flDegrees ) / 5.0 ) );
	Point_t previous = traced;
	for ( int n = 1; n <= nSteps; n++ )
	{
		const Point_t at = turned( traced, flDegrees * n / nSteps );
		out.push_back( { previous, at, MOVEMENT_COLOR } );
		if ( n == nSteps && Length( Subtract( at, previous ) ) > 1e-6 )
			Head( out, at, Normalized( Subtract( at, previous ) ), ARROW_LENGTH / 2.0, MOVEMENT_COLOR );
		previous = at;
	}
	if ( bSpinner )
		return;
	auto corner = [ & ]( int n ) { return turned( Point_t{ ( n & 1 ) ? bounds.m_Maxs[ 0 ] : bounds.m_Mins[ 0 ], ( n & 2 ) ? bounds.m_Maxs[ 1 ] : bounds.m_Mins[ 1 ], ( n & 4 ) ? bounds.m_Maxs[ 2 ] : bounds.m_Mins[ 2 ] }, flDegrees ); };
	for ( int n = 0; n < 8; n++ )
		for ( int nBit = 1; nBit < 8; nBit <<= 1 )
			if ( !( n & nBit ) )
				out.push_back( { corner( n ), corner( n | nBit ), MOVEMENT_COLOR } );
}

//-----------------------------------------------------------------------------
// Purpose: A trigger_teleport's arrow to where it sends things (its target, an
//          info_teleport_destination most often).
//-----------------------------------------------------------------------------
void Teleport( const CScene &scene, const Entity_t &entity, Point_t origin, std::vector<HelperLine_t> &out )
{
	if ( !kv1::EqualsIgnoreCase( entity.GetClassname(), "trigger_teleport" ) )
		return;
	for ( const Point_t &to : EntitiesWhere( scene, "targetname", entity.GetValue( "target" ) ) )
		Arrow( out, origin, to, MOVEMENT_COLOR );
}

//-----------------------------------------------------------------------------
// Purpose: A train's path, from its first corner (its target), or a path's from itself: each
//          link along target to the next, its head where it goes, until the path ends or comes
//          round again.
//-----------------------------------------------------------------------------
void TrainPath( const CScene &scene, std::uint32_t nEntity, std::vector<HelperLine_t> &out )
{
	const auto &entities = scene.m_Map.m_Entities;
	const Entity_t &entity = entities[ nEntity ];
	const std::string_view szClass = entity.GetClassname();
	const bool bTrain = kv1::EqualsIgnoreCase( szClass, "func_tracktrain" ) || kv1::EqualsIgnoreCase( szClass, "func_train" );
	const bool bPath = kv1::EqualsIgnoreCase( szClass, "path_track" ) || kv1::EqualsIgnoreCase( szClass, "path_corner" );
	if ( !bTrain && !bPath )
		return;
	const auto find = [ & ]( std::string_view szName ) -> std::optional<std::size_t> {
		if ( szName.empty() )
			return std::nullopt;
		for ( std::size_t e = 0; e < entities.size(); e++ )
			if ( !entities[ e ].m_bDeleted && kv1::EqualsIgnoreCase( entities[ e ].GetValue( "targetname" ), szName ) )
				return e;
		return std::nullopt;
	};
	std::optional<std::size_t> nAt = bTrain ? find( entity.GetValue( "target" ) ) : std::optional<std::size_t>{ nEntity };
	std::vector<std::size_t> seen;
	while ( nAt && seen.size() < MAX_PATH_LINKS && std::ranges::find( seen, *nAt ) == seen.end() )
	{
		seen.push_back( *nAt );
		const std::optional<std::size_t> nNext = find( entities[ *nAt ].GetValue( "target" ) );
		if ( nNext )
			Arrow( out, scene.m_Entities[ *nAt ].m_Origin, scene.m_Entities[ *nNext ].m_Origin, PATH_COLOR );
		nAt = nNext;
	}
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
	Movement( scene, entity, helpers.m_Lines );
	Turning( scene, entity, helpers.m_Lines );
	Teleport( scene, entity, origin, helpers.m_Lines );
	TrainPath( scene, nEntity, helpers.m_Lines );
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

//-----------------------------------------------------------------------------
// Purpose: Every path's link to its target. Names are looked up once for the whole map, and a
//          name two entities share links to the first, as TrainPath finds it.
//-----------------------------------------------------------------------------
std::vector<PathLink_t> DescribePathLinks( const CScene &scene )
{
	const auto &entities = scene.m_Map.m_Entities;
	const auto lower = []( std::string_view szText ) {
		std::string szLower{ szText };
		std::ranges::transform( szLower, szLower.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
		return szLower;
	};
	std::map<std::string, std::uint32_t> names;
	for ( std::uint32_t e = 0; e < entities.size(); e++ )
		if ( !entities[ e ].m_bDeleted )
			if ( std::string szName = lower( entities[ e ].GetValue( "targetname" ) ); !szName.empty() )
				names.try_emplace( std::move( szName ), e );
	std::vector<PathLink_t> links;
	for ( std::uint32_t e = 0; e < entities.size(); e++ )
	{
		const Entity_t &entity = entities[ e ];
		if ( entity.m_bDeleted || ( !kv1::EqualsIgnoreCase( entity.GetClassname(), "path_track" ) && !kv1::EqualsIgnoreCase( entity.GetClassname(), "path_corner" ) ) )
			continue;
		const std::string szTarget = lower( entity.GetValue( "target" ) );
		const auto found = names.find( szTarget );
		if ( szTarget.empty() || found == names.end() || found->second == e )
			continue;
		PathLink_t link;
		link.m_nFrom = e;
		link.m_nTo = found->second;
		Arrow( link.m_Lines, scene.m_Entities[ e ].m_Origin, scene.m_Entities[ found->second ].m_Origin, PATH_COLOR );
		links.push_back( std::move( link ) );
	}
	return links;
}

} // namespace xv::map
