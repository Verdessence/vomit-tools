//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Edit.h.
//
//===========================================================================//
#include "Edit.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "kv1/Kv1.h"
#include "map/Brushes.h"
#include "map/Entities.h"
#include "map/Mesh.h"
#include "map/Skybox3D.h"
#include "map/Vmf.h"
#include "map/VmfValues.h"
#include "map/Visgroups.h"

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
// Purpose: A texture axis after transform. Locked, the texel under every point stays under it as
//          the point moves: the axis turns with the faces and the shift takes up the move.
//-----------------------------------------------------------------------------
TextureAxis_t TransformAxis( const TextureAxis_t &axis, const Transform_t &transform, bool bLock )
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
	// Kept as long as it was (a scale stretches the texture with the face, Hammer's scale lock).
	out.m_Axis = Scale( turned, flAxisLength / flLength );
	out.m_flScale = axis.m_flScale * flLength / flAxisLength;
	out.m_flShift = axis.m_flShift - Dot( transform.m_Offset, out.m_Axis ) / out.m_flScale;
	// Shifts wrap with the texture; keeping them small keeps the file tidy.
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
			// Its owner lists it (a new entity may already).
			Entity_t &owner = solid.m_nEntity < 0 ? map.m_World : map.m_Entities[ static_cast<std::size_t>( solid.m_nEntity ) ];
			if ( std::ranges::find( owner.m_Solids, nSolid ) == owner.m_Solids.end() )
			{
				owner.m_Solids.push_back( nSolid );
			}
			map.m_Solids.push_back( std::move( solid ) );
		}
		else
		{
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
Solid_t TransformSolid( const Solid_t &solid, const Transform_t &transform, bool bTextureLock )
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
		side.m_UAxis = TransformAxis( side.m_UAxis, transform, bTextureLock );
		side.m_VAxis = TransformAxis( side.m_VAxis, transform, bTextureLock );
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
