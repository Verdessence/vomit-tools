//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Physics.h.
//
//===========================================================================//
#include "Physics.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <regex>
#include <string>

#include <box3d/box3d.h>

#include "map/Brushes.h"
#include "map/Displacement.h"
#include "map/VmfValues.h"
#include "model/Phy.h"
#include "util/Numbers.h"

namespace xv::map
{

namespace
{

// Source's unit is an inch; Box3D is tuned for metres.
constexpr double METRES_PER_UNIT = 0.0254;
// The fixed step and its sub-steps (Box3D's own suggestion for games).
constexpr double STEP = 1.0 / 60.0;
constexpr int SUB_STEPS = 4;
// At most this many steps a call, so a long stall doesn't run away with the frame.
constexpr int MAX_STEPS_A_CALL = 8;
// Hulls kept small: Box3D's are at most 255 vertices, and fewer collide faster.
constexpr int MAX_HULL_VERTICES = 64;
// The densities when a mass isn't given: about a wooden crate's, and a brush's (kg/m³).
constexpr float PROP_DENSITY = 500.0f;
constexpr float BRUSH_DENSITY = 500.0f;
// Air density 1 as damping per second (a guess at Hammer++'s; hammer-check).
constexpr double LINEAR_DAMPING = 0.02;
constexpr double ANGULAR_DAMPING = 0.05;
// Bullet and explosion impulses from the panel's magnitudes, and the explosion's reach (units).
constexpr double IMPULSE_PER_MAGNITUDE = 10.0; // kg·units/s a point of magnitude
constexpr double EXPLOSION_RADIUS = 256.0;
// The world's shapes and the moving ones, for the rays that only want the moving.
constexpr std::uint64_t CATEGORY_WORLD = 1;
constexpr std::uint64_t CATEGORY_MOVING = 2;
// Moves smaller than these are Box3D's float rounding, not motion (units, rotation entries).
constexpr double NEGLIGIBLE_MOVE = 1e-3;
constexpr double NEGLIGIBLE_TURN = 1e-5;

//-----------------------------------------------------------------------------
// Purpose: Source's units to Box3D's metres, and back.
//-----------------------------------------------------------------------------
b3Vec3 ToBox( const Point_t &p )
{
	return b3Vec3{ static_cast<float>( p[ 0 ] * METRES_PER_UNIT ), static_cast<float>( p[ 1 ] * METRES_PER_UNIT ), static_cast<float>( p[ 2 ] * METRES_PER_UNIT ) };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Point_t FromBox( const b3Vec3 &v )
{
	return Point_t{ v.x / METRES_PER_UNIT, v.y / METRES_PER_UNIT, v.z / METRES_PER_UNIT };
}

//-----------------------------------------------------------------------------
// Purpose: A rotation as three rows, as Transform_t keeps it.
//-----------------------------------------------------------------------------
std::array<double, 9> RowsOf( const b3Quat &q )
{
	const double x = q.v.x, y = q.v.y, z = q.v.z, w = q.s;
	return { 1 - 2 * ( y * y + z * z ), 2 * ( x * y - z * w ), 2 * ( x * z + y * w ),
		2 * ( x * y + z * w ), 1 - 2 * ( x * x + z * z ), 2 * ( y * z - x * w ),
		2 * ( x * z - y * w ), 2 * ( y * z + x * w ), 1 - 2 * ( x * x + y * y ) };
}

//-----------------------------------------------------------------------------
// Purpose: A point turned by three rows.
//-----------------------------------------------------------------------------
Point_t Rotated( const std::array<double, 9> &m, const Point_t &p )
{
	return { m[ 0 ] * p[ 0 ] + m[ 1 ] * p[ 1 ] + m[ 2 ] * p[ 2 ], m[ 3 ] * p[ 0 ] + m[ 4 ] * p[ 1 ] + m[ 5 ] * p[ 2 ], m[ 6 ] * p[ 0 ] + m[ 7 ] * p[ 1 ] + m[ 8 ] * p[ 2 ] };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Upper( std::string_view sz )
{
	std::string out{ sz };
	std::ranges::transform( out, out.begin(), []( unsigned char ch ) { return static_cast<char>( std::toupper( ch ) ); } );
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Tool textures physics passes through (a brush made only of these doesn't block it);
//          clip and nodraw do block.
//-----------------------------------------------------------------------------
bool LetsPhysicsThrough( std::string_view szMaterial )
{
	static constexpr std::string_view THROUGH[] = { "TOOLS/TOOLSTRIGGER", "TOOLS/TOOLSHINT", "TOOLS/TOOLSSKIP", "TOOLS/TOOLSAREAPORTAL", "TOOLS/TOOLSOCCLUDER", "TOOLS/TOOLSFOG",
		"TOOLS/TOOLSPLAYERCLIP", "TOOLS/TOOLSNPCCLIP", "TOOLS/TOOLSINVISIBLELADDER", "TOOLS/TOOLSBLOCKLIGHT", "TOOLS/TOOLSBLOCK_LOS", "TOOLS/TOOLSDOTTED", "TOOLS/TOOLSLIGHT", "TOOLS/TOOLSBLOCKBULLETS" };
	const std::string szUpper = Upper( szMaterial );
	return std::ranges::find( THROUGH, std::string_view{ szUpper } ) != std::end( THROUGH );
}

//-----------------------------------------------------------------------------
// Purpose: Brush entities whose brushes aren't solid to physics in the game.
//-----------------------------------------------------------------------------
bool IsPassableEntity( std::string_view szClass )
{
	static constexpr std::string_view PASSABLE[] = { "func_illusionary", "func_areaportal", "func_areaportalwindow", "func_viscluster", "func_occluder", "func_ladder", "func_buyzone",
		"func_bomb_target", "func_hostage_rescue", "func_nobuild", "func_precipitation", "func_dustmotes", "func_dustcloud", "func_smokevolume", "func_vehicleclip", "func_water_analog",
		"func_brush_illusionary" };
	return szClass.starts_with( "trigger_" ) || szClass.starts_with( "env_" ) || std::ranges::find( PASSABLE, szClass ) != std::end( PASSABLE );
}

//-----------------------------------------------------------------------------
// Purpose: A point entity drawn with a model: a prop (or any entity with a .mdl).
//-----------------------------------------------------------------------------
bool IsModelEntity( const Entity_t &entity )
{
	const std::string szModel = Upper( entity.GetValue( "model" ) );
	return szModel.ends_with( ".MDL" );
}

//-----------------------------------------------------------------------------
// Purpose: Where a prop is and how it is turned (and scaled).
//-----------------------------------------------------------------------------
Transform_t PlacementOf( const Entity_t &entity )
{
	const Point_t origin = ParsePoint( entity.GetValue( "origin" ) ).value_or( Point_t{} );
	const Point_t angles = ParsePoint( entity.GetValue( "angles" ) ).value_or( Point_t{} );
	double flScale = 1.0;
	if ( const auto scale = ParseNumber<double>( entity.GetValue( "modelscale" ) ); scale && *scale > 0.0 )
	{
		flScale = *scale;
	}
	return Transform_t::FromAngles( origin, { angles[ 0 ], angles[ 1 ], angles[ 2 ] }, flScale );
}

//-----------------------------------------------------------------------------
// Purpose: Whether a solid blocks physics: not deleted, not of a passable entity, and not made
//          only of tool textures that let it through.
//-----------------------------------------------------------------------------
bool IsSolidToPhysics( const CMap &map, const Solid_t &solid )
{
	if ( solid.m_bDeleted )
	{
		return false;
	}
	if ( solid.m_nEntity >= 0 && static_cast<std::size_t>( solid.m_nEntity ) < map.m_Entities.size() && IsPassableEntity( map.m_Entities[ static_cast<std::size_t>( solid.m_nEntity ) ].GetClassname() ) )
	{
		return false;
	}
	return std::ranges::any_of( solid.m_Sides, []( const Side_t &side ) { return !LetsPhysicsThrough( side.m_szMaterial ); } );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Each solid's "mass" summed, else the model's "totalmass".
//-----------------------------------------------------------------------------
double PhysicsTextMass( std::string_view szText )
{
	const std::string szCopy{ szText };
	const auto sum = [ &szCopy ]( const char *pszKey ) {
		const std::regex pattern( std::string{ "\"" } + pszKey + "\"\\s*\"([-+0-9.eE]+)\"", std::regex::icase );
		double flTotal = 0.0;
		for ( auto it = std::sregex_iterator( szCopy.begin(), szCopy.end(), pattern ); it != std::sregex_iterator(); ++it )
		{
			flTotal += ParseNumber<double>( ( *it )[ 1 ].str() ).value_or( 0.0 );
		}
		return flTotal;
	};
	const double flMass = sum( "mass" );
	return flMass > 0.0 ? flMass : sum( "totalmass" );
}

//-----------------------------------------------------------------------------
// Purpose: The .phy beside the model, checked against it, its pieces placed in model space by
//          their bones' rest pose (a piece of bone -1 is in model space already).
//-----------------------------------------------------------------------------
std::shared_ptr<const PropCollision_t> ReadPropCollision( std::string_view szModel, const model::Reader_t &pfnRead, std::string *pError )
{
	const auto fail = [ pError ]( std::string szWhy ) -> std::shared_ptr<const PropCollision_t> {
		if ( pError )
		{
			*pError = std::move( szWhy );
		}
		return nullptr;
	};
	if ( !pfnRead || szModel.size() < 4 )
	{
		return fail( "no model" );
	}
	std::string szError;
	const auto studio = model::ReadStudio( szModel, pfnRead, &szError );
	if ( !studio )
	{
		return fail( szError );
	}
	const auto bytes = pfnRead( std::string{ szModel.substr( 0, szModel.size() - 4 ) } + ".phy" );
	if ( !bytes )
	{
		return fail( "it has no collision model (.phy)" );
	}
	const auto physics = model::ReadPhysics( *bytes, &szError );
	if ( !physics )
	{
		return fail( szError );
	}
	if ( physics->m_nChecksum != studio->m_nChecksum )
	{
		return fail( "its .phy doesn't match the model" );
	}
	std::vector<std::optional<model::CTransform>> toModel;
	for ( const auto &bone : studio->m_Bones )
	{
		toModel.push_back( bone.m_PoseToBone.Inverse() );
	}
	auto pCollision = std::make_shared<PropCollision_t>();
	for ( const model::Convex_t &convex : physics->m_Convexes )
	{
		std::vector<bool> used( convex.m_Vertices.size(), false );
		for ( const std::uint32_t n : convex.m_Indices )
		{
			if ( n < used.size() )
			{
				used[ n ] = true;
			}
		}
		std::vector<Point_t> piece;
		for ( std::size_t i = 0; i < convex.m_Vertices.size(); i++ )
		{
			if ( !used[ i ] && !convex.m_Indices.empty() )
			{
				continue;
			}
			CVec3 p = convex.m_Vertices[ i ];
			if ( convex.m_nBone >= 0 && static_cast<std::size_t>( convex.m_nBone ) < toModel.size() && toModel[ static_cast<std::size_t>( convex.m_nBone ) ] )
			{
				p = toModel[ static_cast<std::size_t>( convex.m_nBone ) ]->Apply( p );
			}
			piece.push_back( { p.m_flX, p.m_flY, p.m_flZ } );
		}
		if ( piece.size() >= 4 )
		{
			pCollision->m_Pieces.push_back( std::move( piece ) );
		}
	}
	if ( pCollision->m_Pieces.empty() )
	{
		return fail( "its collision model is empty" );
	}
	pCollision->m_flMass = PhysicsTextMass( physics->m_szText );
	return pCollision;
}

//-----------------------------------------------------------------------------
// The world, the bodies, and the hulls and meshes they keep (Box3D reads them in place).
//-----------------------------------------------------------------------------
struct CPhysicsScene::Impl_t
{
	b3WorldId m_World = b3_nullWorldId;
	PhysicsSettings_t m_Settings;
	struct Body_t
	{
		b3BodyId m_Id = b3_nullBodyId;
		Point_t m_Start{}; // its origin at the start: a prop's origin, a brush's middle
		std::vector<const b3HullData *> m_Hulls;
	};
	std::vector<Body_t> m_Bodies;
	std::vector<std::uint32_t> m_Objects;
	std::vector<b3HullData *> m_OwnedHulls;
	std::vector<b3MeshData *> m_OwnedMeshes;
	std::vector<std::pair<Point_t, Point_t>> m_StaticEdges;
	std::vector<std::string> m_Problems;
	double m_flPending = 0.0;
	// Dragging: the body, the point held (in its own frame, metres) and where it is pulled.
	std::optional<std::size_t> m_nGrabbed;
	b3Vec3 m_GrabLocal{};
	Point_t m_GrabTarget{};
	double m_flGrabMagnitude = 0.0;

	~Impl_t()
	{
		if ( b3World_IsValid( m_World ) )
		{
			b3DestroyWorld( m_World );
		}
		for ( b3HullData *pHull : m_OwnedHulls )
		{
			b3DestroyHull( pHull );
		}
		for ( b3MeshData *pMesh : m_OwnedMeshes )
		{
			b3DestroyMesh( pMesh );
		}
	}

	//-----------------------------------------------------------------------------
	// A hull of points given in metres, kept until the world goes; null when they make none.
	//-----------------------------------------------------------------------------
	const b3HullData *MakeHull( const std::vector<b3Vec3> &points )
	{
		if ( points.size() < 4 )
		{
			return nullptr;
		}
		b3HullData *pHull = b3CreateHull( points.data(), static_cast<int>( points.size() ), MAX_HULL_VERTICES );
		if ( pHull )
		{
			m_OwnedHulls.push_back( pHull );
		}
		return pHull;
	}

	//-----------------------------------------------------------------------------
	// A hull's edges, placed by a transform from its frame into the world (metres).
	//-----------------------------------------------------------------------------
	static void AddEdges( const b3HullData &hull, const b3WorldTransform &frame, std::vector<std::pair<Point_t, Point_t>> &out )
	{
		const b3Vec3 *pPoints = b3GetHullPoints( &hull );
		const b3HullHalfEdge *pEdges = b3GetHullEdges( &hull );
		if ( !pPoints || !pEdges )
		{
			return;
		}
		const auto place = [ &frame ]( b3Vec3 v ) {
			const b3Vec3 turned = b3RotateVector( frame.q, v );
			return FromBox( b3Vec3{ turned.x + static_cast<float>( frame.p.x ), turned.y + static_cast<float>( frame.p.y ), turned.z + static_cast<float>( frame.p.z ) } );
		};
		for ( int e = 0; e < hull.edgeCount; e++ )
		{
			const b3HullHalfEdge &edge = pEdges[ e ];
			if ( e < edge.twin )
			{
				out.emplace_back( place( pPoints[ edge.origin ] ), place( pPoints[ pEdges[ edge.twin ].origin ] ) );
			}
		}
	}

	//-----------------------------------------------------------------------------
	// A static hull of points in Source units (the world's frame).
	//-----------------------------------------------------------------------------
	void AddStaticHull( b3BodyId ground, const std::vector<Point_t> &points )
	{
		std::vector<b3Vec3> metres;
		metres.reserve( points.size() );
		for ( const Point_t &p : points )
		{
			metres.push_back( ToBox( p ) );
		}
		const b3HullData *pHull = MakeHull( metres );
		if ( !pHull )
		{
			return;
		}
		b3ShapeDef def = b3DefaultShapeDef();
		def.filter.categoryBits = CATEGORY_WORLD;
		b3CreateHullShape( ground, &def, pHull );
		AddEdges( *pHull, b3WorldTransform{ { 0, 0, 0 }, b3Quat_identity }, m_StaticEdges );
	}

	//-----------------------------------------------------------------------------
	// A moving body of convex pieces in Source units, its origin at start (unturned).
	//-----------------------------------------------------------------------------
	bool AddBody( std::uint32_t nObject, const Point_t &start, const std::vector<std::vector<Point_t>> &pieces, float flDensity, double flMass )
	{
		b3BodyDef def = b3DefaultBodyDef();
		def.type = b3_dynamicBody;
		def.position = ToBox( start );
		def.isAwake = !m_Settings.m_bSleepUntilTouched;
		def.linearDamping = static_cast<float>( LINEAR_DAMPING * m_Settings.m_flAirDensity );
		def.angularDamping = static_cast<float>( ANGULAR_DAMPING * m_Settings.m_flAirDensity );
		Body_t body;
		body.m_Start = start;
		std::vector<std::pair<const b3HullData *, b3ShapeDef>> shapes;
		for ( const auto &piece : pieces )
		{
			std::vector<b3Vec3> metres;
			for ( const Point_t &p : piece )
			{
				metres.push_back( ToBox( Subtract( p, start ) ) );
			}
			if ( const b3HullData *pHull = MakeHull( metres ) )
			{
				body.m_Hulls.push_back( pHull );
			}
		}
		if ( body.m_Hulls.empty() )
		{
			return false;
		}
		body.m_Id = b3CreateBody( m_World, &def );
		for ( const b3HullData *pHull : body.m_Hulls )
		{
			b3ShapeDef shape = b3DefaultShapeDef();
			shape.density = flDensity;
			shape.filter.categoryBits = CATEGORY_MOVING;
			b3CreateHullShape( body.m_Id, &shape, pHull );
		}
		if ( flMass > 0.0 )
		{
			// The given mass, the shape spread as the hulls have it.
			b3MassData mass = b3Body_GetMassData( body.m_Id );
			if ( mass.mass > 0.0f )
			{
				const float flScale = static_cast<float>( flMass ) / mass.mass;
				mass.mass = static_cast<float>( flMass );
				mass.inertia.cx = b3MulSV( flScale, mass.inertia.cx );
				mass.inertia.cy = b3MulSV( flScale, mass.inertia.cy );
				mass.inertia.cz = b3MulSV( flScale, mass.inertia.cz );
				b3Body_SetMassData( body.m_Id, mass );
			}
		}
		m_Bodies.push_back( std::move( body ) );
		m_Objects.push_back( nObject );
		return true;
	}

	//-----------------------------------------------------------------------------
	// Gravity and damping as the settings say.
	//-----------------------------------------------------------------------------
	void ApplySettings()
	{
		b3World_SetGravity( m_World, ToBox( m_Settings.m_vecGravity ) );
		for ( const Body_t &body : m_Bodies )
		{
			b3Body_SetLinearDamping( body.m_Id, static_cast<float>( LINEAR_DAMPING * m_Settings.m_flAirDensity ) );
			b3Body_SetAngularDamping( body.m_Id, static_cast<float>( ANGULAR_DAMPING * m_Settings.m_flAirDensity ) );
		}
	}

	//-----------------------------------------------------------------------------
	// The held point pulled towards the target: a damped spring, as stiff as the magnitude.
	//-----------------------------------------------------------------------------
	void PullGrabbed()
	{
		if ( !m_nGrabbed || *m_nGrabbed >= m_Bodies.size() )
		{
			return;
		}
		const b3BodyId id = m_Bodies[ *m_nGrabbed ].m_Id;
		const b3WorldTransform frame = b3Body_GetTransform( id );
		const b3Vec3 turned = b3RotateVector( frame.q, m_GrabLocal );
		const b3Vec3 held{ turned.x + static_cast<float>( frame.p.x ), turned.y + static_cast<float>( frame.p.y ), turned.z + static_cast<float>( frame.p.z ) };
		const b3Vec3 target = ToBox( m_GrabTarget );
		const float flStiffness = static_cast<float>( 4.0 * m_flGrabMagnitude * m_flGrabMagnitude );
		const float flDamping = static_cast<float>( 2.0 * std::sqrt( static_cast<double>( flStiffness ) ) );
		const b3Vec3 velocity = b3Body_GetLinearVelocity( id );
		const float flMass = b3Body_GetMass( id );
		const b3Vec3 force{ flMass * ( flStiffness * ( target.x - held.x ) - flDamping * velocity.x ), flMass * ( flStiffness * ( target.y - held.y ) - flDamping * velocity.y ),
			flMass * ( flStiffness * ( target.z - held.z ) - flDamping * velocity.z ) };
		// Gravity held up too, so a held object hangs where it is pulled.
		const b3Vec3 gravity = ToBox( m_Settings.m_vecGravity );
		const b3Vec3 total{ force.x - flMass * gravity.x, force.y - flMass * gravity.y, force.z - flMass * gravity.z };
		b3Body_ApplyForce( id, total, b3Pos{ held.x, held.y, held.z }, true );
	}
};

//-----------------------------------------------------------------------------
// Purpose: The world as static shapes, the moving objects as bodies.
//-----------------------------------------------------------------------------
CPhysicsScene::CPhysicsScene( const CScene &scene, const std::vector<std::uint32_t> &moving, const PropCollisionLookup_t &pfnCollision, const PhysicsSettings_t &settings )
	: m_pImpl( std::make_unique<Impl_t>() )
{
	Impl_t &impl = *m_pImpl;
	impl.m_Settings = settings;
	b3WorldDef worldDef = b3DefaultWorldDef();
	worldDef.gravity = ToBox( settings.m_vecGravity );
	impl.m_World = b3CreateWorld( &worldDef );
	const CMap &map = scene.m_Map;

	// What moves: each object of the selection, a prop or a brush (an entity's solids together).
	std::vector<bool> movingSolid( map.m_Solids.size(), false );
	std::vector<bool> movingEntity( map.m_Entities.size(), false );
	for ( const std::uint32_t nObject : moving )
	{
		const ObjectSet_t set = ObjectsOf( scene, { nObject } );
		if ( set.m_Solids.empty() && set.m_Entities.size() == 1 )
		{
			const std::uint32_t nEntity = set.m_Entities.front();
			const Entity_t &entity = map.m_Entities[ nEntity ];
			if ( movingEntity[ nEntity ] || !IsModelEntity( entity ) )
			{
				continue;
			}
			const std::string szModel{ entity.GetValue( "model" ) };
			const auto pCollision = pfnCollision ? pfnCollision( szModel ) : nullptr;
			if ( !pCollision )
			{
				impl.m_Problems.push_back( szModel + " has no collision model, so it stays" );
				continue;
			}
			const Transform_t placement = PlacementOf( entity );
			std::vector<std::vector<Point_t>> pieces;
			for ( const auto &piece : pCollision->m_Pieces )
			{
				std::vector<Point_t> placed;
				for ( const Point_t &p : piece )
				{
					placed.push_back( placement.Apply( p ) );
				}
				pieces.push_back( std::move( placed ) );
			}
			const double flMass = settings.m_bAutoMass ? pCollision->m_flMass : 0.0;
			if ( impl.AddBody( nObject, placement.m_Offset, pieces, PROP_DENSITY, flMass ) )
			{
				movingEntity[ nEntity ] = true;
			}
			continue;
		}
		if ( settings.m_bPropsOnly || set.m_Solids.empty() )
		{
			continue;
		}
		std::vector<std::vector<Point_t>> pieces;
		Box_t bounds;
		for ( const std::uint32_t nSolid : set.m_Solids )
		{
			if ( movingSolid[ nSolid ] || map.m_Solids[ nSolid ].m_bDeleted )
			{
				continue;
			}
			std::vector<Point_t> corners;
			for ( const FaceGeometry_t &face : scene.m_Solids[ nSolid ].m_Faces )
			{
				for ( const Point_t &p : face.m_Points )
				{
					corners.push_back( p );
					bounds.Add( p );
				}
			}
			pieces.push_back( std::move( corners ) );
		}
		if ( pieces.empty() || bounds.IsEmpty() )
		{
			continue;
		}
		if ( impl.AddBody( nObject, bounds.GetCenter(), pieces, BRUSH_DENSITY, 0.0 ) )
		{
			for ( const std::uint32_t nSolid : set.m_Solids )
			{
				movingSolid[ nSolid ] = true;
			}
			for ( const std::uint32_t nEntity : set.m_Entities )
			{
				movingEntity[ nEntity ] = true;
			}
		}
	}

	// The world: solid brushes as hulls, displacements as their triangles, props as their
	// collision models.
	b3BodyDef groundDef = b3DefaultBodyDef();
	const b3BodyId ground = b3CreateBody( impl.m_World, &groundDef );
	std::vector<b3Vec3> meshVertices;
	std::vector<std::int32_t> meshIndices;
	for ( std::size_t i = 0; i < map.m_Solids.size() && i < scene.m_Solids.size(); i++ )
	{
		const Solid_t &solid = map.m_Solids[ i ];
		if ( movingSolid[ i ] || !IsSolidToPhysics( map, solid ) )
		{
			continue;
		}
		const SolidGeometry_t &geometry = scene.m_Solids[ i ];
		const bool bDisplaced = std::ranges::any_of( solid.m_Sides, []( const Side_t &side ) { return static_cast<bool>( side.m_pDisp ); } );
		if ( bDisplaced )
		{
			// A brush with a displacement collides only as its displacements' surfaces.
			for ( const FaceGeometry_t &face : geometry.m_Faces )
			{
				const Side_t &side = solid.m_Sides[ face.m_nSide ];
				if ( !side.m_pDisp )
				{
					continue;
				}
				const auto mesh = BuildDisplacement( *side.m_pDisp, face );
				if ( !mesh )
				{
					continue;
				}
				const auto nFirst = static_cast<std::int32_t>( meshVertices.size() );
				for ( const Point_t &p : mesh->m_Points )
				{
					meshVertices.push_back( ToBox( p ) );
				}
				for ( std::size_t k = 0; k + 2 < mesh->m_Indices.size(); k += 3 )
				{
					// Source winds its faces clockwise seen from outside; Box3D's triangles face the
					// side they turn anticlockwise to.
					for ( const std::size_t c : { 0, 2, 1 } )
					{
						meshIndices.push_back( nFirst + static_cast<std::int32_t>( mesh->m_Indices[ k + c ] ) );
					}
					const Point_t a = mesh->m_Points[ mesh->m_Indices[ k ] ];
					const Point_t b = mesh->m_Points[ mesh->m_Indices[ k + 1 ] ];
					const Point_t c = mesh->m_Points[ mesh->m_Indices[ k + 2 ] ];
					impl.m_StaticEdges.emplace_back( a, b );
					impl.m_StaticEdges.emplace_back( b, c );
					impl.m_StaticEdges.emplace_back( c, a );
				}
			}
			continue;
		}
		std::vector<Point_t> corners;
		for ( const FaceGeometry_t &face : geometry.m_Faces )
		{
			corners.insert( corners.end(), face.m_Points.begin(), face.m_Points.end() );
		}
		impl.AddStaticHull( ground, corners );
	}
	if ( meshIndices.size() >= 3 )
	{
		b3MeshDef def{};
		def.vertices = meshVertices.data();
		def.indices = meshIndices.data();
		def.vertexCount = static_cast<int>( meshVertices.size() );
		def.triangleCount = static_cast<int>( meshIndices.size() / 3 );
		def.weldVertices = true;
		def.weldTolerance = 0.001f;
		def.identifyEdges = true;
		if ( b3MeshData *pMesh = b3CreateMesh( &def, nullptr, 0 ) )
		{
			impl.m_OwnedMeshes.push_back( pMesh );
			b3ShapeDef shape = b3DefaultShapeDef();
			shape.filter.categoryBits = CATEGORY_WORLD;
			b3CreateMeshShape( ground, &shape, pMesh, b3Vec3{ 1, 1, 1 } );
		}
	}
	for ( std::size_t i = 0; i < map.m_Entities.size(); i++ )
	{
		const Entity_t &entity = map.m_Entities[ i ];
		if ( movingEntity[ i ] || !IsModelEntity( entity ) || !pfnCollision )
		{
			continue;
		}
		const auto pCollision = pfnCollision( entity.GetValue( "model" ) );
		if ( !pCollision )
		{
			continue;
		}
		const Transform_t placement = PlacementOf( entity );
		for ( const auto &piece : pCollision->m_Pieces )
		{
			std::vector<Point_t> placed;
			for ( const Point_t &p : piece )
			{
				placed.push_back( placement.Apply( p ) );
			}
			impl.AddStaticHull( ground, placed );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPhysicsScene::~CPhysicsScene() = default;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPhysicsScene::SetSettings( const PhysicsSettings_t &settings )
{
	m_pImpl->m_Settings = settings;
	m_pImpl->ApplySettings();
}

//-----------------------------------------------------------------------------
// Purpose: Fixed steps for the time that passed, scaled.
//-----------------------------------------------------------------------------
void CPhysicsScene::Step( double flSeconds )
{
	Impl_t &impl = *m_pImpl;
	impl.m_flPending += std::max( 0.0, flSeconds ) * std::max( 0.0, impl.m_Settings.m_flTimeScale );
	int nSteps = 0;
	while ( impl.m_flPending >= STEP && nSteps < MAX_STEPS_A_CALL )
	{
		impl.PullGrabbed();
		b3World_Step( impl.m_World, static_cast<float>( STEP ), SUB_STEPS );
		impl.m_flPending -= STEP;
		nSteps++;
	}
	if ( nSteps == MAX_STEPS_A_CALL )
	{
		impl.m_flPending = 0.0;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const std::vector<std::uint32_t> &CPhysicsScene::GetObjects() const
{
	return m_pImpl->m_Objects;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const std::vector<std::string> &CPhysicsScene::GetProblems() const
{
	return m_pImpl->m_Problems;
}

//-----------------------------------------------------------------------------
// Purpose: A body's pose against its start: turned by its rotation, then moved so its origin
//          is where it is now.
//-----------------------------------------------------------------------------
std::vector<std::pair<std::uint32_t, Transform_t>> CPhysicsScene::GetMoves() const
{
	const Impl_t &impl = *m_pImpl;
	std::vector<std::pair<std::uint32_t, Transform_t>> moves;
	for ( std::size_t i = 0; i < impl.m_Bodies.size(); i++ )
	{
		const Impl_t::Body_t &body = impl.m_Bodies[ i ];
		const b3WorldTransform frame = b3Body_GetTransform( body.m_Id );
		Transform_t move;
		move.m_Rotation = RowsOf( frame.q );
		const Point_t now = FromBox( b3Vec3{ static_cast<float>( frame.p.x ), static_cast<float>( frame.p.y ), static_cast<float>( frame.p.z ) } );
		move.m_Offset = Subtract( now, Rotated( move.m_Rotation, body.m_Start ) );
		move.m_Pivot = body.m_Start;
		moves.emplace_back( impl.m_Objects[ i ], move );
	}
	return moves;
}

//-----------------------------------------------------------------------------
// Purpose: Each moving object's own transform, put together into one edit.
//-----------------------------------------------------------------------------
MapEdit_t CPhysicsScene::MakeEdit( const CScene &scene ) const
{
	MapEdit_t edit;
	for ( const auto &[ nObject, move ] : GetMoves() )
	{
		// What hasn't moved past Box3D's rounding (asleep, or never woken) is left as written.
		const std::array<double, 9> identity{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
		double flTurn = 0.0;
		for ( std::size_t i = 0; i < 9; i++ )
		{
			flTurn = std::max( flTurn, std::abs( move.m_Rotation[ i ] - identity[ i ] ) );
		}
		if ( Length( move.m_Offset ) < NEGLIGIBLE_MOVE && flTurn < NEGLIGIBLE_TURN )
		{
			continue;
		}
		MapEdit_t one = TransformObjects( scene, { nObject }, move, true );
		std::ranges::move( one.m_Solids, std::back_inserter( edit.m_Solids ) );
		std::ranges::move( one.m_Entities, std::back_inserter( edit.m_Entities ) );
		std::ranges::move( one.m_SolidTransforms, std::back_inserter( edit.m_SolidTransforms ) );
	}
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CPhysicsScene::IsSettled() const
{
	return std::ranges::none_of( m_pImpl->m_Bodies, []( const Impl_t::Body_t &body ) { return b3Body_IsAwake( body.m_Id ); } );
}

//-----------------------------------------------------------------------------
// Purpose: Only the moving bodies' shapes are asked.
//-----------------------------------------------------------------------------
std::optional<PhysicsHit_t> CPhysicsScene::CastRay( Point_t from, Point_t dir, double flMax ) const
{
	const Impl_t &impl = *m_pImpl;
	b3QueryFilter filter = b3DefaultQueryFilter();
	filter.categoryBits = CATEGORY_WORLD | CATEGORY_MOVING;
	filter.maskBits = CATEGORY_MOVING;
	const b3RayResult result = b3World_CastRayClosest( impl.m_World, ToBox( from ), ToBox( Scale( dir, flMax ) ), filter );
	if ( !b3Shape_IsValid( result.shapeId ) )
	{
		return std::nullopt;
	}
	const b3BodyId body = b3Shape_GetBody( result.shapeId );
	for ( std::size_t i = 0; i < impl.m_Bodies.size(); i++ )
	{
		if ( B3_ID_EQUALS( impl.m_Bodies[ i ].m_Id, body ) )
		{
			return PhysicsHit_t{ i, FromBox( b3Vec3{ static_cast<float>( result.point.x ), static_cast<float>( result.point.y ), static_cast<float>( result.point.z ) } ), result.fraction * flMax };
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CPhysicsScene::Shoot( Point_t from, Point_t dir, double flMagnitude )
{
	const auto hit = CastRay( from, dir, 65536.0 );
	if ( !hit )
	{
		return false;
	}
	const b3Vec3 impulse = ToBox( Scale( Normalized( dir ), flMagnitude * IMPULSE_PER_MAGNITUDE ) );
	const b3Vec3 at = ToBox( hit->m_Point );
	b3Body_ApplyLinearImpulse( m_pImpl->m_Bodies[ hit->m_nBody ].m_Id, impulse, b3Pos{ at.x, at.y, at.z }, true );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Away from the point, falling off to nothing at the radius.
//-----------------------------------------------------------------------------
void CPhysicsScene::Explode( Point_t at, double flMagnitude )
{
	for ( const Impl_t::Body_t &body : m_pImpl->m_Bodies )
	{
		const b3Pos centre = b3Body_GetWorldCenterOfMass( body.m_Id );
		const Point_t c = FromBox( b3Vec3{ static_cast<float>( centre.x ), static_cast<float>( centre.y ), static_cast<float>( centre.z ) } );
		const Point_t away = Subtract( c, at );
		const double flDistance = Length( away );
		if ( flDistance >= EXPLOSION_RADIUS )
		{
			continue;
		}
		const Point_t direction = flDistance > 1e-3 ? Scale( away, 1.0 / flDistance ) : Point_t{ 0, 0, 1 };
		const double flStrength = flMagnitude * IMPULSE_PER_MAGNITUDE * ( 1.0 - flDistance / EXPLOSION_RADIUS );
		b3Body_ApplyLinearImpulseToCenter( body.m_Id, ToBox( Scale( direction, flStrength ) ), true );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPhysicsScene::Grab( std::size_t nBody, Point_t at )
{
	Impl_t &impl = *m_pImpl;
	if ( nBody >= impl.m_Bodies.size() )
	{
		return;
	}
	const b3WorldTransform frame = b3Body_GetTransform( impl.m_Bodies[ nBody ].m_Id );
	const b3Vec3 world = ToBox( at );
	const b3Vec3 offset{ world.x - static_cast<float>( frame.p.x ), world.y - static_cast<float>( frame.p.y ), world.z - static_cast<float>( frame.p.z ) };
	impl.m_GrabLocal = b3InvRotateVector( frame.q, offset );
	impl.m_nGrabbed = nBody;
	impl.m_GrabTarget = at;
	b3Body_SetAwake( impl.m_Bodies[ nBody ].m_Id, true );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPhysicsScene::DragTo( Point_t target, double flMagnitude )
{
	m_pImpl->m_GrabTarget = target;
	m_pImpl->m_flGrabMagnitude = flMagnitude;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPhysicsScene::Release()
{
	m_pImpl->m_nGrabbed.reset();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CPhysicsScene::IsGrabbing() const
{
	return m_pImpl->m_nGrabbed.has_value();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::pair<Point_t, Point_t>> CPhysicsScene::GetEdges( bool bMoving ) const
{
	const Impl_t &impl = *m_pImpl;
	if ( !bMoving )
	{
		return impl.m_StaticEdges;
	}
	std::vector<std::pair<Point_t, Point_t>> edges;
	for ( const Impl_t::Body_t &body : impl.m_Bodies )
	{
		const b3WorldTransform frame = b3Body_GetTransform( body.m_Id );
		for ( const b3HullData *pHull : body.m_Hulls )
		{
			Impl_t::AddEdges( *pHull, frame, edges );
		}
	}
	return edges;
}

} // namespace xv::map
