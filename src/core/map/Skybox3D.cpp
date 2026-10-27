//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Finding the 3D skybox's room and what is in it.
//
//===========================================================================//
#include "map/Skybox3D.h"

#include <cmath>
#include <span>

#include "kv1/Kv1.h"
#include "map/Scene.h"
#include "util/Numbers.h"

namespace xv::map
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: How far along dir from the ray hits the triangle, if it does (Moller-Trumbore).
//-----------------------------------------------------------------------------
std::optional<double> HitTriangle( Point_t from, Point_t dir, Point_t a, Point_t b, Point_t c )
{
	const Point_t ab = Subtract( b, a );
	const Point_t ac = Subtract( c, a );
	const Point_t p = Cross( dir, ac );
	const double flDet = Dot( ab, p );
	if ( std::abs( flDet ) < 1e-12 )
	{
		return std::nullopt;
	}
	const Point_t s = Subtract( from, a );
	const double u = Dot( s, p ) / flDet;
	if ( u < 0.0 || u > 1.0 )
	{
		return std::nullopt;
	}
	const Point_t q = Cross( s, ab );
	const double v = Dot( dir, q ) / flDet;
	if ( v < 0.0 || u + v > 1.0 )
	{
		return std::nullopt;
	}
	const double t = Dot( ac, q ) / flDet;
	return t > 0.0 ? std::optional<double>{ t } : std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: The object the ray meets first in the meshes, and how far along: the first face of
//          sky (what a skybox room is walled with), else the first of anything.
//-----------------------------------------------------------------------------
std::optional<std::pair<std::uint32_t, double>> Nearest( const CScene &scene, std::span<const CMapMesh *const> meshes, Point_t from, Point_t dir )
{
	std::optional<std::pair<std::uint32_t, double>> sky;
	std::optional<std::pair<std::uint32_t, double>> any;
	for ( const CMapMesh *pMesh : meshes )
	{
		for ( const MeshBatch_t &batch : pMesh->m_Batches )
		{
			const bool bSky = batch.m_nMaterial < scene.m_Materials.size() && scene.m_Materials[ batch.m_nMaterial ].starts_with( "tools/toolsskybox" );
			for ( std::uint32_t i = batch.m_nFirstIndex; i + 2 < batch.m_nFirstIndex + batch.m_nIndexCount; i += 3 )
			{
				const MeshVertex_t &a = pMesh->m_Vertices[ pMesh->m_Indices[ i ] ];
				const std::optional<double> t = HitTriangle( from, dir, ToPoint( a.m_vecPosition ), ToPoint( pMesh->m_Vertices[ pMesh->m_Indices[ i + 1 ] ].m_vecPosition ), ToPoint( pMesh->m_Vertices[ pMesh->m_Indices[ i + 2 ] ].m_vecPosition ) );
				if ( !t )
				{
					continue;
				}
				std::optional<std::pair<std::uint32_t, double>> &best = bSky ? sky : any;
				if ( !best || *t < best->second )
				{
					best = std::pair{ a.m_nObject, *t };
				}
			}
		}
	}
	return sky ? sky : any;
}

//-----------------------------------------------------------------------------
// Purpose: Each object's box: a solid's, a point entity's, a brush entity's solids', and a
//          func_instance's contents'.
//-----------------------------------------------------------------------------
std::vector<Box_t> ObjectBounds( const CScene &scene )
{
	std::vector<Box_t> bounds( scene.GetObjectCount() );
	for ( std::size_t s = 0; s < scene.m_Solids.size(); s++ )
	{
		bounds[ scene.SolidObject( s ) ] = scene.m_Solids[ s ].m_Bounds;
	}
	for ( std::size_t e = 0; e < scene.m_Entities.size(); e++ )
	{
		Box_t &box = bounds[ scene.EntityObject( e ) ];
		if ( scene.m_Entities[ e ].m_bPoint )
		{
			box = scene.GetEntityBox( e );
		}
		for ( const std::uint32_t s : scene.m_Map.m_Entities[ e ].m_Solids )
		{
			box.Add( scene.m_Solids[ s ].m_Bounds );
		}
	}
	for ( const PlacedInstance_t &placed : scene.m_Instances )
	{
		bounds[ placed.m_nObject ] = {};
	}
	for ( const MeshVertex_t &vertex : scene.GetInstanceMesh().m_Vertices )
	{
		if ( vertex.m_nObject < bounds.size() )
		{
			bounds[ vertex.m_nObject ].Add( ToPoint( vertex.m_vecPosition ) );
		}
	}
	return bounds;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<Skybox3D_t> FindSkybox3D( const CScene &scene )
{
	std::optional<Skybox3D_t> sky;
	ForEachInstance( scene, [ & ]( const CScene &inner, const Transform_t &toScene, std::optional<std::uint32_t> ) {
		for ( std::size_t e = 0; e < inner.m_Entities.size() && !sky; e++ )
		{
			const Entity_t &entity = inner.m_Map.m_Entities[ e ];
			if ( kv1::EqualsIgnoreCase( entity.GetClassname(), "sky_camera" ) )
			{
				sky.emplace();
				sky->m_Camera = toScene.Apply( inner.m_Entities[ e ].m_Origin );
				const double flScale = ParseNumber<double>( entity.GetValue( "scale" ) ).value_or( 16.0 );
				sky->m_flScale = flScale > 0.0 ? flScale : 16.0;
			}
		}
	} );
	if ( !sky )
	{
		return sky;
	}
	// The room: from the camera to whatever it meets first along each axis, and through that
	// wall's thickness. Only along that axis: a wall can be one side of a shell around the
	// whole map.
	const std::vector<Box_t> bounds = ObjectBounds( scene );
	const std::array<const CMapMesh *, 2> meshes{ &scene.GetMesh(), &scene.GetInstanceMesh() };
	sky->m_Room.Add( sky->m_Camera );
	for ( std::size_t a = 0; a < 3; a++ )
	{
		for ( const double flSign : { 1.0, -1.0 } )
		{
			Point_t dir{};
			dir[ a ] = flSign;
			const auto hit = Nearest( scene, meshes, sky->m_Camera, dir );
			if ( !hit )
			{
				continue;
			}
			double flFar = sky->m_Camera[ a ] + flSign * hit->second;
			if ( hit->first < bounds.size() && !bounds[ hit->first ].IsEmpty() )
			{
				flFar = flSign > 0.0 ? std::max( flFar, bounds[ hit->first ].m_Maxs[ a ] ) : std::min( flFar, bounds[ hit->first ].m_Mins[ a ] );
			}
			Point_t at = sky->m_Camera;
			at[ a ] = flFar;
			sky->m_Room.Add( at );
		}
	}
	// Everything wholly inside it (within a unit).
	Box_t reach = sky->m_Room;
	reach.m_Mins = Subtract( reach.m_Mins, { 1, 1, 1 } );
	reach.m_Maxs = Add( reach.m_Maxs, { 1, 1, 1 } );
	for ( std::uint32_t n = 0; n < bounds.size(); n++ )
	{
		if ( !bounds[ n ].IsEmpty() && reach.Contains( bounds[ n ].m_Mins ) && reach.Contains( bounds[ n ].m_Maxs ) )
		{
			sky->m_Objects.push_back( n );
		}
	}
	return sky;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CViewCamera SkyboxCamera( const CViewCamera &camera, const Skybox3D_t &sky )
{
	CViewCamera out = camera;
	out.m_Eye = Add( sky.m_Camera, Scale( camera.m_Eye, 1.0 / sky.m_flScale ) );
	// The room is the scale times smaller, so is the nearest it can show.
	out.m_flNear = camera.m_flNear / sky.m_flScale;
	return out;
}

} // namespace xv::map
