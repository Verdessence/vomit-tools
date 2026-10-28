//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Edit.h.
//
//===========================================================================//
#include "Edit.h"

#include <algorithm>
#include <string>
#include <set>
#include <cctype>
#include <cmath>
#include <numbers>
#include <utility>

#include "kv1/Kv1.h"
#include "util/Numbers.h"
#include "map/Brushes.h"
#include "map/Entities.h"
#include "map/Mesh.h"
#include "map/Skybox3D.h"
#include "map/Vmf.h"
#include "map/VmfValues.h"
#include "map/Visgroups.h"
#include "map/Write.h"

namespace xv::map
{

namespace
{

constexpr double DEGREES = std::numbers::pi / 180.0;

//-----------------------------------------------------------------------------
// Purpose: The determinant of the transform's 3x3 part: below zero, it mirrors.
//-----------------------------------------------------------------------------
double Determinant( const Transform_t &transform )
{
	const auto &m = transform.m_Rotation;
	return m[ 0 ] * ( m[ 4 ] * m[ 8 ] - m[ 5 ] * m[ 7 ] ) - m[ 1 ] * ( m[ 3 ] * m[ 8 ] - m[ 5 ] * m[ 6 ] ) + m[ 2 ] * ( m[ 3 ] * m[ 7 ] - m[ 4 ] * m[ 6 ] );
}

//-----------------------------------------------------------------------------
// Purpose: A texture axis after transform. Locked, the axis turns with the faces and the shift
//          takes up the move, so the texel under every point stays under it. A stretch then
//          stretches the texture too only with the scale lock (Hammer's Texture Scale Lock);
//          without it the texture keeps its size, anchored at the pivot.
//-----------------------------------------------------------------------------
TextureAxis_t TransformAxis( const TextureAxis_t &axis, const Transform_t &transform, bool bLock, bool bScaleLock )
{
	if ( !bLock )
	{
		return axis;
	}
	TextureAxis_t out = axis;
	const Point_t turned = transform.Rotate( axis.m_Axis );
	const double flLength = Length( turned );
	const double flAxisLength = Length( axis.m_Axis );
	if ( flLength <= 0.0 || flAxisLength <= 0.0 )
	{
		return axis;
	}
	out.m_Axis = Scale( turned, flAxisLength / flLength );
	if ( bScaleLock )
	{
		out.m_flScale = axis.m_flScale * flLength / flAxisLength;
		out.m_flShift = axis.m_flShift - Dot( transform.m_Offset, out.m_Axis ) / out.m_flScale;
	}
	else
	{
		const double flScale = axis.m_flScale != 0.0 ? axis.m_flScale : 1.0;
		out.m_flShift = axis.m_flShift + ( Dot( transform.m_Pivot, axis.m_Axis ) - Dot( transform.Apply( transform.m_Pivot ), out.m_Axis ) ) / flScale;
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: A material by its spelling: the scene's index for it, added if it is new.
//-----------------------------------------------------------------------------
std::uint32_t ResolveMaterial( CScene &scene, std::string_view szSpelling )
{
	const std::string szName = NormalizeMaterial( szSpelling );
	const auto it = std::ranges::find( scene.m_Materials, szName );
	if ( it != scene.m_Materials.end() )
	{
		return static_cast<std::uint32_t>( it - scene.m_Materials.begin() );
	}
	scene.m_Materials.push_back( szName );
	// The map's own list (a prefix of the scene's) follows, so indices mean the same in both.
	while ( scene.m_Map.m_Materials.size() < scene.m_Materials.size() )
	{
		scene.m_Map.m_Materials.push_back( scene.m_Materials[ scene.m_Map.m_Materials.size() ] );
	}
	return static_cast<std::uint32_t>( scene.m_Materials.size() - 1 );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::shared_ptr<CScene> ApplyEdit( const CScene &scene, const MapEdit_t &edit )
{
	auto pScene = std::make_shared<CScene>( scene );
	CScene &out = *pScene;
	out.m_nRevision = scene.m_nRevision + 1;
	out.m_Changed.clear();
	CMap &map = out.m_Map;

	if ( edit.m_World )
	{
		// The world's solids are its own list, not the edit's to give.
		std::vector<std::uint32_t> solids = std::move( map.m_World.m_Solids );
		map.m_World = *edit.m_World;
		map.m_World.m_Solids = std::move( solids );
		map.m_World.m_bDeleted = false;
	}
	if ( edit.m_Cordons )
	{
		map.m_Cordons = edit.m_Cordons->first;
		map.m_bCordonsActive = edit.m_Cordons->second;
		map.m_bCordonsChanged = true;
	}
	// Entities first: new solids may belong to new entities.
	bool bSkybox = false;
	for ( const auto &[ nIndex, entity ] : edit.m_Entities )
	{
		if ( nIndex >= map.m_Entities.size() )
		{
			map.m_Entities.push_back( entity );
			out.m_Entities.push_back( DescribeEntity( entity, out.m_pFgd.get() ) );
			out.m_EntityObjects.push_back( static_cast<std::uint32_t>( out.m_Objects.size() ) );
			out.m_Objects.push_back( { false, static_cast<std::uint32_t>( map.m_Entities.size() - 1 ) } );
		}
		else
		{
			bSkybox = bSkybox || kv1::EqualsIgnoreCase( std::as_const( map.m_Entities )[ nIndex ].GetClassname(), "sky_camera" );
			map.m_Entities[ nIndex ] = entity;
			out.m_Entities[ nIndex ] = DescribeEntity( entity, out.m_pFgd.get() );
		}
		bSkybox = bSkybox || kv1::EqualsIgnoreCase( entity.GetClassname(), "sky_camera" );
		out.m_nNextObjectId = std::max( out.m_nNextObjectId, entity.m_nId + 1 );
		out.m_Changed.push_back( out.EntityObject( std::min<std::size_t>( nIndex, map.m_Entities.size() - 1 ) ) );
	}

	for ( const auto &[ nIndex, given ] : edit.m_Solids )
	{
		Solid_t solid = given;
		for ( Side_t &side : solid.m_Sides )
		{
			if ( side.m_nMaterial == NEW_MATERIAL )
			{
				side.m_nMaterial = ResolveMaterial( out, side.m_szMaterial );
			}
			out.m_nNextSideId = std::max( out.m_nNextSideId, side.m_nId + 1 );
		}
		out.m_nNextObjectId = std::max( out.m_nNextObjectId, solid.m_nId + 1 );
		std::uint32_t nSolid = nIndex;
		if ( nIndex >= map.m_Solids.size() )
		{
			nSolid = static_cast<std::uint32_t>( map.m_Solids.size() );
			out.m_Solids.push_back( BuildSolid( solid ) );
			out.m_SolidObjects.push_back( static_cast<std::uint32_t>( out.m_Objects.size() ) );
			out.m_Objects.push_back( { true, nSolid } );
			// Its owner lists it (a new entity may already); where it is written is where it is.
			solid.m_nReadEntity = solid.m_nEntity;
			Entity_t &owner = solid.m_nEntity < 0 ? map.m_World : map.m_Entities[ static_cast<std::size_t>( solid.m_nEntity ) ];
			if ( std::ranges::find( owner.m_Solids, nSolid ) == owner.m_Solids.end() )
			{
				owner.m_Solids.push_back( nSolid );
			}
			map.m_Solids.push_back( std::move( solid ) );
		}
		else
		{
			// Another owner: off the old one's list, onto the new one's.
			const int nWas = std::as_const( map.m_Solids )[ nSolid ].m_nEntity;
			if ( nWas != solid.m_nEntity )
			{
				Entity_t &was = nWas < 0 ? map.m_World : map.m_Entities[ static_cast<std::size_t>( nWas ) ];
				std::erase( was.m_Solids, nSolid );
				Entity_t &now = solid.m_nEntity < 0 ? map.m_World : map.m_Entities[ static_cast<std::size_t>( solid.m_nEntity ) ];
				if ( std::ranges::find( now.m_Solids, nSolid ) == now.m_Solids.end() )
				{
					now.m_Solids.push_back( nSolid );
				}
			}
			out.m_Solids[ nSolid ] = BuildSolid( solid );
			map.m_Solids[ nSolid ] = std::move( solid );
		}
		const auto it = std::ranges::lower_bound( out.m_EditedSolids, nSolid );
		if ( it == out.m_EditedSolids.end() || *it != nSolid )
		{
			out.m_EditedSolids.insert( it, nSolid );
		}
		out.m_Changed.push_back( out.SolidObject( nSolid ) );
	}

	if ( !edit.m_RemovedGroups.empty() )
	{
		std::erase_if( map.m_Groups, [ & ]( const Group_t &group ) { return group.m_Span.m_nLength == 0 && std::ranges::find( edit.m_RemovedGroups, group.m_nId ) != edit.m_RemovedGroups.end(); } );
	}
	for ( const Group_t &group : edit.m_Groups )
	{
		map.m_Groups.push_back( group );
		map.m_Groups.back().m_Span = {};
		out.m_nNextObjectId = std::max( out.m_nNextObjectId, group.m_nId + 1 );
	}

	// The edit mesh: every solid edits have touched, as it is now.
	std::vector<MeshSolid_t> edited;
	for ( const std::uint32_t nSolid : out.m_EditedSolids )
	{
		if ( !std::as_const( map.m_Solids )[ nSolid ].m_bDeleted )
		{
			edited.push_back( { nSolid, out.SolidObject( nSolid ), &std::as_const( out.m_Solids )[ nSolid ] } );
		}
	}
	out.m_pEditMesh = std::make_shared<const CMapMesh>( BuildMesh( std::as_const( map ), std::span<const MeshSolid_t>{ edited } ) );

	// What spans the whole map.
	out.m_Bounds = {};
	for ( std::uint32_t s = 0; s < map.m_Solids.size(); s++ )
	{
		if ( !std::as_const( map.m_Solids )[ s ].m_bDeleted )
		{
			out.m_Bounds.Add( std::as_const( out.m_Solids )[ s ].m_Bounds );
		}
	}
	for ( std::size_t e = 0; e < out.m_Entities.size(); e++ )
	{
		if ( std::as_const( out.m_Entities )[ e ].m_bPoint && !std::as_const( map.m_Entities )[ e ].m_bDeleted )
		{
			out.m_Bounds.Add( out.GetEntityBox( e ) );
		}
	}
	for ( const MeshChunk_t &chunk : out.GetInstanceMesh().m_Chunks )
	{
		out.m_Bounds.Add( chunk.m_Bounds );
	}
	out.m_AutoVisgroups = BuildAutoVisgroups( out );
	if ( bSkybox )
	{
		out.m_Skybox3D = FindSkybox3D( out );
	}
	std::ranges::sort( out.m_Changed );
	out.m_Changed.erase( std::unique( out.m_Changed.begin(), out.m_Changed.end() ), out.m_Changed.end() );
	return pScene;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t InverseOf( const CScene &scene, const MapEdit_t &edit )
{
	const CMap &map = scene.m_Map;
	MapEdit_t inverse;
	if ( edit.m_World )
	{
		inverse.m_World = map.m_World;
	}
	if ( edit.m_Cordons )
	{
		inverse.m_Cordons = std::make_pair( map.m_Cordons, map.m_bCordonsActive );
	}
	for ( const Group_t &group : edit.m_Groups )
	{
		inverse.m_RemovedGroups.push_back( group.m_nId );
	}
	for ( const auto &[ nIndex, entity ] : edit.m_Entities )
	{
		if ( nIndex < map.m_Entities.size() )
		{
			inverse.m_Entities.emplace_back( nIndex, map.m_Entities[ nIndex ] );
		}
		else
		{
			Entity_t gone = entity;
			gone.m_bDeleted = true;
			inverse.m_Entities.emplace_back( nIndex, std::move( gone ) );
		}
	}
	for ( const auto &[ nIndex, solid ] : edit.m_Solids )
	{
		if ( nIndex < map.m_Solids.size() )
		{
			inverse.m_Solids.emplace_back( nIndex, map.m_Solids[ nIndex ] );
		}
		else
		{
			Solid_t gone = solid;
			gone.m_bDeleted = true;
			inverse.m_Solids.emplace_back( nIndex, std::move( gone ) );
		}
	}
	return inverse;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Solid_t TransformSolid( const Solid_t &solid, const Transform_t &transform, bool bTextureLock, bool bScaleLock )
{
	Solid_t out = solid;
	const bool bMirror = Determinant( transform ) < 0.0;
	for ( Side_t &side : out.m_Sides )
	{
		for ( Point_t &p : side.m_Plane )
		{
			p = transform.Apply( p );
		}
		for ( Point_t &p : side.m_PlusVertices )
		{
			p = transform.Apply( p );
		}
		if ( bMirror )
		{
			std::swap( side.m_Plane[ 0 ], side.m_Plane[ 2 ] );
			std::ranges::reverse( side.m_PlusVertices );
		}
		side.m_UAxis = TransformAxis( side.m_UAxis, transform, bTextureLock, bScaleLock );
		side.m_VAxis = TransformAxis( side.m_VAxis, transform, bTextureLock, bScaleLock );
		if ( side.m_pDisp )
		{
			DispInfo_t &disp = *side.m_pDisp;
			disp.m_Start = transform.Apply( disp.m_Start );
			// The grid's directions turn with the face; their lengths stay.
			auto turn = [ & ]( std::vector<CVec3> &vectors ) {
				for ( CVec3 &v : vectors )
				{
					const Point_t turned = transform.Rotate( ToPoint( v ) );
					const double flBefore = Length( ToPoint( v ) );
					const double flAfter = Length( turned );
					v = ToVec3( flAfter > 0.0 ? Scale( turned, flBefore / flAfter ) : turned );
				}
			};
			turn( disp.m_Normals );
			turn( disp.m_Offsets );
			turn( disp.m_OffsetNormals );
		}
	}
	out.m_bChanged = true;
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Yaw from the forward direction's heading, pitch from how far it dips, roll from how
//          far up tips over (zero where forward is straight up or down).
//-----------------------------------------------------------------------------
std::array<double, 3> AnglesOf( const Transform_t &transform )
{
	const auto &m = transform.m_Rotation;
	// The rows' scale, if there is one, doesn't change the angles.
	const double flForward = std::hypot( m[ 0 ], m[ 3 ] );
	const double flPitch = std::atan2( -m[ 6 ], flForward ) / DEGREES;
	double flYaw = 0.0;
	double flRoll = 0.0;
	if ( flForward > 1e-9 )
	{
		flYaw = std::atan2( m[ 3 ], m[ 0 ] ) / DEGREES;
		flRoll = std::atan2( m[ 7 ], m[ 8 ] ) / DEGREES;
	}
	else
	{
		flYaw = std::atan2( -m[ 1 ], m[ 4 ] ) / DEGREES;
	}
	auto tidy = []( double fl ) {
		const double flRounded = std::round( fl * 1e4 ) / 1e4;
		return flRounded == 0.0 ? 0.0 : flRounded;
	};
	return { tidy( flPitch ), tidy( flYaw ), tidy( flRoll ) };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
ObjectSet_t ObjectsOf( const CScene &scene, const std::vector<std::uint32_t> &objects )
{
	ObjectSet_t set;
	for ( const std::uint32_t n : objects )
	{
		if ( n >= scene.GetObjectCount() || scene.IsDeleted( n ) )
		{
			continue;
		}
		const std::uint32_t nIndex = scene.GetIndex( n );
		if ( scene.IsSolidObject( n ) )
		{
			set.m_Solids.push_back( nIndex );
			continue;
		}
		set.m_Entities.push_back( nIndex );
		for ( const std::uint32_t nSolid : scene.m_Map.m_Entities[ nIndex ].m_Solids )
		{
			if ( !scene.m_Map.m_Solids[ nSolid ].m_bDeleted )
			{
				set.m_Solids.push_back( nSolid );
			}
		}
	}
	for ( std::vector<std::uint32_t> *pList : { &set.m_Solids, &set.m_Entities } )
	{
		std::ranges::sort( *pList );
		pList->erase( std::unique( pList->begin(), pList->end() ), pList->end() );
	}
	return set;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t DeleteObjects( const CScene &scene, const std::vector<std::uint32_t> &objects )
{
	const ObjectSet_t set = ObjectsOf( scene, objects );
	MapEdit_t edit;
	for ( const std::uint32_t nSolid : set.m_Solids )
	{
		Solid_t solid = scene.m_Map.m_Solids[ nSolid ];
		solid.m_bDeleted = true;
		edit.m_Solids.emplace_back( nSolid, std::move( solid ) );
	}
	for ( const std::uint32_t nEntity : set.m_Entities )
	{
		Entity_t entity = scene.m_Map.m_Entities[ nEntity ];
		entity.m_bDeleted = true;
		edit.m_Entities.emplace_back( nEntity, std::move( entity ) );
	}
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t TransformObjects( const CScene &scene, const std::vector<std::uint32_t> &objects, const Transform_t &transform, bool bTextureLock, bool bScaleLock )
{
	const ObjectSet_t set = ObjectsOf( scene, objects );
	MapEdit_t edit;
	for ( const std::uint32_t nSolid : set.m_Solids )
	{
		edit.m_Solids.emplace_back( nSolid, TransformSolid( scene.m_Map.m_Solids[ nSolid ], transform, bTextureLock, bScaleLock ) );
	}
	for ( const std::uint32_t nEntity : set.m_Entities )
	{
		const Entity_t &entity = scene.m_Map.m_Entities[ nEntity ];
		if ( entity.HasKey( "origin" ) || entity.HasKey( "angles" ) )
		{
			edit.m_Entities.emplace_back( nEntity, TransformEntity( scene.m_Map, entity, transform ) );
		}
	}
	return edit;
}

namespace
{

// Objects coming into a map: from its own solids and entities, or another map's (the clipboard).
struct Incoming_t
{
	std::vector<Solid_t> m_Solids;    // m_nEntity: an index into m_Entities, or -1 (the world, or kept)
	std::vector<Entity_t> m_Entities;
	std::vector<int> m_OwnersKept;    // per solid: an entity of the map it stays in, or -1
};

//-----------------------------------------------------------------------------
// Purpose: Strings into map's store, and spans cleared: the object is new to the map.
//-----------------------------------------------------------------------------
void Adopt( const CMap &map, Solid_t &solid, bool bForeign )
{
	solid.m_Span = {};
	solid.m_nReadEntity = -2; // set when the edit is made: a new solid has no old place
	solid.m_bChanged = false;
	solid.m_bDeleted = false;
	solid.m_bInHidden = false;
	solid.m_Editor.m_Span = {};
	for ( Side_t &side : solid.m_Sides )
	{
		side.m_Span = {};
		if ( side.m_pDisp )
		{
			side.m_pDisp->m_Span = {};
		}
		if ( bForeign )
		{
			side.m_szMaterial = map.Store( std::string{ side.m_szMaterial } );
			side.m_nMaterial = NEW_MATERIAL;
		}
	}
	if ( bForeign && !solid.m_Editor.m_szComments.empty() )
	{
		solid.m_Editor.m_szComments = map.Store( std::string{ solid.m_Editor.m_szComments } );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void Adopt( const CMap &map, Entity_t &entity, bool bForeign )
{
	entity.m_Span = {};
	entity.m_ConnectionsSpan = {};
	entity.m_bConnectionsChanged = false;
	entity.m_bDeleted = false;
	entity.m_bInHidden = false;
	entity.m_Editor.m_Span = {};
	entity.m_Solids.clear();
	std::erase_if( entity.m_Keys, []( const EntityKey_t &key ) { return key.m_bDeleted; } );
	for ( EntityKey_t &key : entity.m_Keys )
	{
		key.m_Span = {};
		key.m_bChanged = false;
		if ( bForeign )
		{
			key.m_szKey = map.Store( std::string{ key.m_szKey } );
			key.m_szValue = map.Store( std::string{ key.m_szValue } );
		}
	}
	for ( Connection_t &connection : entity.m_Connections )
	{
		connection.m_Span = {};
		if ( bForeign )
		{
			for ( std::string_view *pText : { &connection.m_szOutput, &connection.m_szTarget, &connection.m_szInput, &connection.m_szParameter } )
			{
				*pText = map.Store( std::string{ *pText } );
			}
		}
	}
	if ( bForeign && !entity.m_Editor.m_szComments.empty() )
	{
		entity.m_Editor.m_szComments = map.Store( std::string{ entity.m_Editor.m_szComments } );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The incoming objects added after the map's own, moved by transform, with ids of their
//          own; their object numbers (as ApplyEdit gives them: entities, then solids) into pAdded.
//-----------------------------------------------------------------------------
MapEdit_t AddIncoming( const CScene &scene, Incoming_t incoming, const Transform_t &transform, bool bTextureLock, std::vector<std::uint32_t> *pAdded )
{
	const CMap &map = scene.m_Map;
	MapEdit_t edit;
	int nNextId = scene.m_nNextObjectId;
	int nNextSide = scene.m_nNextSideId;
	const auto nFirstEntity = static_cast<std::uint32_t>( map.m_Entities.size() );
	const auto nFirstSolid = static_cast<std::uint32_t>( map.m_Solids.size() );
	std::uint32_t nObject = scene.GetObjectCount();
	std::vector<Entity_t> entities = std::move( incoming.m_Entities );
	for ( Entity_t &entity : entities )
	{
		entity = TransformEntity( map, entity, transform );
		entity.m_nId = nNextId++;
		if ( pAdded )
		{
			pAdded->push_back( nObject++ );
		}
	}
	for ( std::size_t i = 0; i < incoming.m_Solids.size(); i++ )
	{
		Solid_t solid = TransformSolid( incoming.m_Solids[ i ], transform, bTextureLock );
		solid.m_bChanged = false;
		solid.m_nId = nNextId++;
		for ( Side_t &side : solid.m_Sides )
		{
			side.m_nId = nNextSide++;
		}
		const std::uint32_t nIndex = nFirstSolid + static_cast<std::uint32_t>( i );
		if ( solid.m_nEntity >= 0 )
		{
			entities[ static_cast<std::size_t>( solid.m_nEntity ) ].m_Solids.push_back( nIndex );
			solid.m_nEntity = static_cast<int>( nFirstEntity ) + solid.m_nEntity;
		}
		else
		{
			solid.m_nEntity = incoming.m_OwnersKept.empty() ? -1 : incoming.m_OwnersKept[ i ];
		}
		edit.m_Solids.emplace_back( nIndex, std::move( solid ) );
	}
	for ( std::size_t i = 0; i < entities.size(); i++ )
	{
		edit.m_Entities.emplace_back( nFirstEntity + static_cast<std::uint32_t>( i ), std::move( entities[ i ] ) );
	}
	for ( std::size_t i = 0; pAdded && i < incoming.m_Solids.size(); i++ )
	{
		pAdded->push_back( nObject++ );
	}
	return edit;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: A solid of an entity that isn't copied with it stays with that entity (a brush picked
//          out of a brush entity).
//-----------------------------------------------------------------------------
MapEdit_t CopyObjects( const CScene &scene, const std::vector<std::uint32_t> &objects, const Transform_t &transform, bool bTextureLock, std::vector<std::uint32_t> *pAdded )
{
	const ObjectSet_t set = ObjectsOf( scene, objects );
	const CMap &map = scene.m_Map;
	Incoming_t incoming;
	for ( const std::uint32_t nEntity : set.m_Entities )
	{
		Entity_t entity = map.m_Entities[ nEntity ];
		Adopt( map, entity, false );
		incoming.m_Entities.push_back( std::move( entity ) );
	}
	for ( const std::uint32_t nSolid : set.m_Solids )
	{
		Solid_t solid = map.m_Solids[ nSolid ];
		Adopt( map, solid, false );
		const auto it = std::ranges::find( set.m_Entities, static_cast<std::uint32_t>( solid.m_nEntity ) );
		const bool bWithEntity = solid.m_nEntity >= 0 && it != set.m_Entities.end();
		incoming.m_OwnersKept.push_back( bWithEntity ? -1 : solid.m_nEntity );
		solid.m_nEntity = bWithEntity ? static_cast<int>( it - set.m_Entities.begin() ) : -1;
		incoming.m_Solids.push_back( std::move( solid ) );
	}
	return AddIncoming( scene, std::move( incoming ), transform, bTextureLock, pAdded );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t AddObjects( const CScene &scene, std::vector<Solid_t> solids, std::vector<Entity_t> entities, std::vector<std::uint32_t> *pAdded )
{
	Incoming_t incoming;
	incoming.m_Solids = std::move( solids );
	incoming.m_Entities = std::move( entities );
	return AddIncoming( scene, std::move( incoming ), {}, false, pAdded );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string ObjectsToText( const CScene &scene, const std::vector<std::uint32_t> &objects )
{
	const ObjectSet_t set = ObjectsOf( scene, objects );
	const CMap &map = scene.m_Map;
	const std::string szNewline{ map.GetNewline() };
	std::string szText;
	std::string szWorld;
	for ( const std::uint32_t nSolid : set.m_Solids )
	{
		const Solid_t &solid = map.m_Solids[ nSolid ];
		if ( solid.m_nEntity < 0 || std::ranges::find( set.m_Entities, static_cast<std::uint32_t>( solid.m_nEntity ) ) == set.m_Entities.end() )
		{
			szWorld += '\t' + WriteSolid( map, solid, "\t" ) + szNewline;
		}
	}
	if ( !szWorld.empty() )
	{
		szText += "world" + szNewline + "{" + szNewline + "\t\"id\" \"1\"" + szNewline + "\t\"classname\" \"worldspawn\"" + szNewline + szWorld + "}" + szNewline;
	}
	for ( const std::uint32_t nEntity : set.m_Entities )
	{
		szText += WriteEntity( map, map.m_Entities[ nEntity ], {} ) + szNewline;
	}
	return szText;
}

//-----------------------------------------------------------------------------
// Purpose: Everything the text holds but its world's keys: the world's solids and the entities.
//-----------------------------------------------------------------------------
MapEdit_t ObjectsFromText( const CScene &scene, std::string_view szText, const Transform_t &transform, std::vector<std::uint32_t> *pAdded )
{
	const std::optional<CMap> clip = ReadVmf( std::vector<char>( szText.begin(), szText.end() ) );
	if ( !clip )
	{
		return {};
	}
	const CMap &map = scene.m_Map;
	Incoming_t incoming;
	for ( const Entity_t &source : clip->m_Entities )
	{
		Entity_t entity = source;
		Adopt( map, entity, true );
		incoming.m_Entities.push_back( std::move( entity ) );
	}
	for ( const Solid_t &source : clip->m_Solids )
	{
		Solid_t solid = source;
		Adopt( map, solid, true );
		incoming.m_Solids.push_back( std::move( solid ) );
	}
	if ( incoming.m_Solids.empty() && incoming.m_Entities.empty() )
	{
		return {};
	}
	// Anything the clipboard map's names point at must outlive it.
	return AddIncoming( scene, std::move( incoming ), transform, true, pAdded );
}

//-----------------------------------------------------------------------------
// Purpose: A name the map hasn't got: the trailing number raised (name2 -> name3), or 1 appended,
//          until it is free; the name taken afterwards.
//-----------------------------------------------------------------------------
static std::string FreeName( std::string_view szName, std::set<std::string, std::less<>> &taken )
{
	std::size_t nDigits = 0;
	while ( nDigits < szName.size() && std::isdigit( static_cast<unsigned char>( szName[ szName.size() - 1 - nDigits ] ) ) )
	{
		nDigits++;
	}
	const std::string_view szBase = szName.substr( 0, szName.size() - nDigits );
	long long nNumber = 0;
	if ( nDigits > 0 && nDigits < 18 )
	{
		nNumber = std::stoll( std::string{ szName.substr( szName.size() - nDigits ) } );
	}
	std::string szFree;
	do
	{
		szFree = std::string{ szBase } + std::to_string( ++nNumber );
	}
	while ( taken.contains( szFree ) );
	taken.insert( szFree );
	return szFree;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
static Box_t BoundsOfMap( const CMap &clip )
{
	Box_t bounds;
	for ( const Solid_t &solid : clip.m_Solids )
	{
		const SolidGeometry_t shape = BuildSolid( solid );
		if ( shape.IsValid() )
		{
			bounds.Add( shape.m_Bounds );
		}
	}
	for ( const Entity_t &entity : clip.m_Entities )
	{
		if ( entity.m_Solids.empty() )
		{
			if ( const std::optional<Point_t> origin = ParsePoint( entity.GetValue( "origin" ) ) )
			{
				bounds.Add( *origin );
			}
		}
	}
	return bounds;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Box_t BoundsOfText( std::string_view szText )
{
	const std::optional<CMap> clip = ReadVmf( std::vector<char>( szText.begin(), szText.end() ) );
	return clip ? BoundsOfMap( *clip ) : Box_t{};
}

//-----------------------------------------------------------------------------
// Purpose: Every copy's objects go in as one batch, each copy transformed before, so the ids and
//          object numbers come out as AddIncoming gives them; the groups take the ids after.
//-----------------------------------------------------------------------------
MapEdit_t PasteSpecial( const CScene &scene, std::string_view szText, const PasteSpecial_t &how, std::vector<std::uint32_t> *pAdded )
{
	const std::optional<CMap> clip = ReadVmf( std::vector<char>( szText.begin(), szText.end() ) );
	if ( !clip || how.m_nCopies < 1 || ( clip->m_Solids.empty() && clip->m_Entities.empty() ) )
	{
		return {};
	}
	const CMap &map = scene.m_Map;
	// The set's centre, which m_Start places and the turns are about (unless there is a pivot).
	const Box_t bounds = BoundsOfMap( *clip );
	const Point_t centre = bounds.IsEmpty() ? Point_t{} : bounds.GetCenter();
	const Point_t about = how.m_Pivot.value_or( centre );
	std::set<std::string, std::less<>> taken;
	if ( how.m_bUniqueNames )
	{
		for ( const Entity_t &entity : map.m_Entities )
		{
			if ( const std::string_view szName = entity.GetValue( "targetname" ); !szName.empty() && !entity.m_bDeleted )
			{
				taken.insert( std::string{ szName } );
			}
		}
	}
	Incoming_t incoming;
	const int nFirstGroup = scene.m_nNextObjectId + static_cast<int>( clip->m_Solids.size() + clip->m_Entities.size() ) * how.m_nCopies;
	MapEdit_t groups;
	for ( int nCopy = 1; nCopy <= how.m_nCopies; nCopy++ )
	{
		const int nStep = how.m_Start ? nCopy - 1 : nCopy;
		Transform_t transform = Transform_t::Rotation( about, 2, how.m_Angles[ 2 ] * nStep ) * Transform_t::Rotation( about, 1, how.m_Angles[ 1 ] * nStep ) * Transform_t::Rotation( about, 0, how.m_Angles[ 0 ] * nStep );
		transform = Transform_t::Translation( Add( how.m_Start ? Subtract( *how.m_Start, centre ) : Point_t{}, Scale( how.m_Offset, nStep ) ) ) * transform;
		transform.m_Pivot = about;
		const int nGroup = how.m_bGroup ? nFirstGroup + nCopy - 1 : 0;
		if ( how.m_bGroup )
		{
			Group_t group;
			group.m_nId = nGroup;
			group.m_Editor.m_Color = { 220, 220, 220 };
			group.m_Editor.m_bHasColor = true;
			groups.m_Groups.push_back( group );
		}
		const int nFirstEntity = static_cast<int>( incoming.m_Entities.size() );
		std::vector<std::pair<std::string, std::string_view>> renames; // old name, new (stored)
		for ( const Entity_t &source : clip->m_Entities )
		{
			Entity_t entity = source;
			Adopt( map, entity, true );
			entity = TransformEntity( map, entity, transform );
			if ( how.m_bUniqueNames )
			{
				if ( const std::string_view szName = entity.GetValue( "targetname" ); !szName.empty() )
				{
					if ( taken.contains( szName ) )
					{
						const std::string_view szNew = map.Store( FreeName( szName, taken ) );
						renames.emplace_back( std::string{ szName }, szNew );
						SetKeyValue( map, entity, "targetname", szNew );
					}
					else
					{
						taken.insert( std::string{ szName } );
					}
				}
			}
			if ( how.m_bGroup )
			{
				entity.m_Editor.m_nGroup = nGroup;
			}
			incoming.m_Entities.push_back( std::move( entity ) );
		}
		for ( std::size_t i = static_cast<std::size_t>( nFirstEntity ); i < incoming.m_Entities.size() && !renames.empty(); i++ )
		{
			for ( Connection_t &connection : incoming.m_Entities[ i ].m_Connections )
			{
				for ( const auto &[ szOld, szNew ] : renames )
				{
					if ( connection.m_szTarget == szOld )
					{
						connection.m_szTarget = szNew;
						incoming.m_Entities[ i ].m_bConnectionsChanged = true;
					}
				}
			}
		}
		for ( const Solid_t &source : clip->m_Solids )
		{
			Solid_t solid = source;
			Adopt( map, solid, true );
			solid = TransformSolid( solid, transform, true );
			solid.m_bChanged = false;
			if ( solid.m_nEntity >= 0 )
			{
				solid.m_nEntity += nFirstEntity;
			}
			if ( how.m_bGroup )
			{
				solid.m_Editor.m_nGroup = nGroup;
			}
			incoming.m_Solids.push_back( std::move( solid ) );
		}
	}
	MapEdit_t edit = AddIncoming( scene, std::move( incoming ), {}, false, pAdded );
	edit.m_Groups = std::move( groups.m_Groups );
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetKeyValue( const CMap &map, Entity_t &entity, std::string_view szKey, std::string_view szValue )
{
	for ( auto it = entity.m_Keys.rbegin(); it != entity.m_Keys.rend(); ++it )
	{
		if ( !it->m_bDeleted && kv1::EqualsIgnoreCase( it->m_szKey, szKey ) )
		{
			if ( it->m_szValue != szValue )
			{
				it->m_szValue = map.Store( std::string{ szValue } );
				it->m_bChanged = true;
			}
			return;
		}
	}
	entity.m_Keys.push_back( { map.Store( std::string{ szKey } ), map.Store( std::string{ szValue } ), {} } );
}

//-----------------------------------------------------------------------------
// Purpose: The world solids picked, and selected entities' solids with their entities emptied
//          (deleted): all of them in one new entity, which the edit adds last.
//-----------------------------------------------------------------------------
MapEdit_t TieToEntity( const CScene &scene, const std::vector<std::uint32_t> &objects, std::string_view szClass, std::vector<std::uint32_t> *pAdded )
{
	MapEdit_t edit;
	const CMap &map = scene.m_Map;
	const ObjectSet_t set = ObjectsOf( scene, objects );
	if ( set.m_Solids.empty() )
	{
		return edit;
	}
	const auto nNewEntity = static_cast<std::uint32_t>( map.m_Entities.size() );
	Entity_t entity;
	entity.m_nId = scene.m_nNextObjectId;
	entity.m_Editor.m_Color = ENTITY_COLOR;
	entity.m_Editor.m_bHasColor = true;
	const FgdClass_t *pClass = scene.m_pFgd ? scene.m_pFgd->Find( szClass ) : nullptr;
	entity.m_Keys.push_back( { "classname", map.Store( std::string{ pClass ? std::string_view{ pClass->m_szName } : szClass } ), {} } );
	for ( const FgdKey_t &key : pClass ? pClass->m_Keys : std::vector<FgdKey_t>{} )
	{
		if ( !key.m_szDefault.empty() && key.m_szType != "flags" && !kv1::EqualsIgnoreCase( key.m_szName, "classname" ) )
		{
			entity.m_Keys.push_back( { map.Store( key.m_szName ), map.Store( key.m_szDefault ), {} } );
		}
	}
	if ( const FgdHelper_t *pColor = pClass ? pClass->FindHelper( "color" ) : nullptr; pColor && !pColor->m_Arguments.empty() )
	{
		entity.m_Editor.m_Color = ParseColor( pColor->m_Arguments[ 0 ] ).value_or( ENTITY_COLOR );
	}
	for ( const std::uint32_t nSolid : set.m_Solids )
	{
		Solid_t solid = map.m_Solids[ nSolid ];
		solid.m_nEntity = static_cast<int>( nNewEntity );
		solid.m_bChanged = true;
		entity.m_Solids.push_back( nSolid );
		edit.m_Solids.emplace_back( nSolid, std::move( solid ) );
	}
	for ( const std::uint32_t nEntity : set.m_Entities )
	{
		Entity_t emptied = map.m_Entities[ nEntity ];
		if ( !emptied.m_Solids.empty() )
		{
			emptied.m_bDeleted = true;
			edit.m_Entities.emplace_back( nEntity, std::move( emptied ) );
		}
	}
	edit.m_Entities.emplace_back( nNewEntity, std::move( entity ) );
	if ( pAdded )
	{
		pAdded->push_back( scene.GetObjectCount() );
	}
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t MoveToWorld( const CScene &scene, const std::vector<std::uint32_t> &objects )
{
	MapEdit_t edit;
	const CMap &map = scene.m_Map;
	const ObjectSet_t set = ObjectsOf( scene, objects );
	for ( const std::uint32_t nSolid : set.m_Solids )
	{
		if ( map.m_Solids[ nSolid ].m_nEntity < 0 )
		{
			continue;
		}
		Solid_t solid = map.m_Solids[ nSolid ];
		solid.m_nEntity = -1;
		solid.m_bChanged = true;
		edit.m_Solids.emplace_back( nSolid, std::move( solid ) );
	}
	for ( const std::uint32_t nEntity : set.m_Entities )
	{
		Entity_t gone = map.m_Entities[ nEntity ];
		if ( !gone.m_Solids.empty() )
		{
			gone.m_bDeleted = true;
			edit.m_Entities.emplace_back( nEntity, std::move( gone ) );
		}
	}
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose: A new group with Hammer's next id; each object's editor block names it (a brush
//          entity's solids follow their entity).
//-----------------------------------------------------------------------------
MapEdit_t GroupObjects( const CScene &scene, const std::vector<std::uint32_t> &objects, int *pGroupId )
{
	MapEdit_t edit;
	const CMap &map = scene.m_Map;
	const ObjectSet_t set = ObjectsOf( scene, objects );
	if ( set.m_Solids.size() + set.m_Entities.size() < 2 && !( set.m_Entities.size() == 1 && !set.m_Solids.empty() ) )
	{
		return edit;
	}
	Group_t group;
	group.m_nId = scene.m_nNextObjectId;
	group.m_Editor.m_Color = { 220, 220, 220 };
	group.m_Editor.m_bHasColor = true;
	edit.m_Groups.push_back( group );
	if ( pGroupId )
	{
		*pGroupId = group.m_nId;
	}
	for ( const std::uint32_t nSolid : set.m_Solids )
	{
		Solid_t solid = map.m_Solids[ nSolid ];
		solid.m_Editor.m_nGroup = group.m_nId;
		solid.m_Editor.m_bChanged = true;
		solid.m_bChanged = true;
		edit.m_Solids.emplace_back( nSolid, std::move( solid ) );
	}
	for ( const std::uint32_t nEntity : set.m_Entities )
	{
		Entity_t entity = map.m_Entities[ nEntity ];
		entity.m_Editor.m_nGroup = group.m_nId;
		entity.m_Editor.m_bChanged = true;
		edit.m_Entities.emplace_back( nEntity, std::move( entity ) );
	}
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t UngroupObjects( const CScene &scene, const std::vector<std::uint32_t> &objects )
{
	MapEdit_t edit;
	const CMap &map = scene.m_Map;
	const ObjectSet_t set = ObjectsOf( scene, objects );
	for ( const std::uint32_t nSolid : set.m_Solids )
	{
		if ( map.m_Solids[ nSolid ].m_Editor.m_nGroup == 0 )
		{
			continue;
		}
		Solid_t solid = map.m_Solids[ nSolid ];
		solid.m_Editor.m_nGroup = 0;
		solid.m_Editor.m_bChanged = true;
		solid.m_bChanged = true;
		edit.m_Solids.emplace_back( nSolid, std::move( solid ) );
	}
	for ( const std::uint32_t nEntity : set.m_Entities )
	{
		if ( map.m_Entities[ nEntity ].m_Editor.m_nGroup == 0 )
		{
			continue;
		}
		Entity_t entity = map.m_Entities[ nEntity ];
		entity.m_Editor.m_nGroup = 0;
		entity.m_Editor.m_bChanged = true;
		edit.m_Entities.emplace_back( nEntity, std::move( entity ) );
	}
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void RemoveKey( Entity_t &entity, std::string_view szKey )
{
	for ( EntityKey_t &key : entity.m_Keys )
	{
		if ( !key.m_bDeleted && kv1::EqualsIgnoreCase( key.m_szKey, szKey ) )
		{
			key.m_bDeleted = true;
		}
	}
	// A key added since the map was read leaves no hole to keep.
	std::erase_if( entity.m_Keys, []( const EntityKey_t &key ) { return key.m_bDeleted && key.IsNew(); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetSpawnFlag( const CMap &map, Entity_t &entity, std::uint32_t nBit, bool bOn, std::uint32_t nDefault )
{
	const std::string_view szFlags = entity.GetValue( "spawnflags" );
	std::uint32_t nFlags = entity.HasKey( "spawnflags" ) ? static_cast<std::uint32_t>( ParseNumber<long long>( szFlags ).value_or( 0 ) ) : nDefault;
	nFlags = bOn ? ( nFlags | nBit ) : ( nFlags & ~nBit );
	SetKeyValue( map, entity, "spawnflags", std::to_string( nFlags ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetConnections( const CMap &map, Entity_t &entity, std::vector<Connection_t> connections )
{
	for ( Connection_t &connection : connections )
	{
		connection.m_Span = {};
		for ( std::string_view *pText : { &connection.m_szOutput, &connection.m_szTarget, &connection.m_szInput, &connection.m_szParameter } )
		{
			*pText = map.Store( std::string{ *pText } );
		}
	}
	entity.m_Connections = std::move( connections );
	entity.m_bConnectionsChanged = true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetComments( const CMap &map, Entity_t &entity, std::string_view szComments )
{
	if ( entity.m_Editor.m_szComments == szComments )
	{
		return;
	}
	entity.m_Editor.m_szComments = map.Store( std::string{ szComments } );
	entity.m_Editor.m_bChanged = true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t EditEntities( const CScene &scene, const std::vector<std::uint32_t> &objects, const std::function<void( Entity_t & )> &pfnEdit )
{
	MapEdit_t edit;
	std::vector<std::uint32_t> done;
	for ( const std::uint32_t n : objects )
	{
		const int nEntity = scene.GetObjectEntity( n );
		if ( nEntity < 0 || std::ranges::find( done, static_cast<std::uint32_t>( nEntity ) ) != done.end() )
		{
			continue;
		}
		done.push_back( static_cast<std::uint32_t>( nEntity ) );
		Entity_t entity = scene.m_Map.m_Entities[ static_cast<std::size_t>( nEntity ) ];
		pfnEdit( entity );
		edit.m_Entities.emplace_back( static_cast<std::uint32_t>( nEntity ), std::move( entity ) );
	}
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapEdit_t EditWorld( const CScene &scene, const std::function<void( Entity_t & )> &pfnEdit )
{
	MapEdit_t edit;
	Entity_t world = scene.m_Map.m_World;
	pfnEdit( world );
	edit.m_World = std::move( world );
	return edit;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Entity_t TransformEntity( const CMap &map, const Entity_t &entity, const Transform_t &transform )
{
	Entity_t out = entity;
	if ( const auto origin = ParsePoint( entity.GetValue( "origin" ) ) )
	{
		SetKeyValue( map, out, "origin", FormatPoint( transform.Apply( *origin ) ) );
	}
	// Turned only when the transform turns: a move leaves the angles as they were written.
	const auto &m = transform.m_Rotation;
	const bool bTurns = !( m[ 1 ] == 0.0 && m[ 2 ] == 0.0 && m[ 3 ] == 0.0 && m[ 5 ] == 0.0 && m[ 6 ] == 0.0 && m[ 7 ] == 0.0 );
	if ( bTurns && entity.HasKey( "angles" ) )
	{
		const auto angles = ParsePoint( entity.GetValue( "angles" ) ).value_or( Point_t{} );
		Transform_t turn = transform;
		turn.m_Offset = {};
		const std::array<double, 3> turned = AnglesOf( turn * Transform_t::FromAngles( {}, { angles[ 0 ], angles[ 1 ], angles[ 2 ] } ) );
		SetKeyValue( map, out, "angles", FormatPoint( { turned[ 0 ], turned[ 1 ], turned[ 2 ] } ) );
	}
	return out;
}

} // namespace xv::map
