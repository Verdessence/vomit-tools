//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapCompiled.h.
//
//===========================================================================//
#include "MapCompiled.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <utility>

#include "map/Raster.h"
#include "map/Scene.h"

namespace
{

using xv::map::Point_t;

// The entity whose brushes carry the compiled faces' materials into the scene's table, and
// whose object the faces are drawn as (a brush entity's own object draws nothing).
constexpr std::string_view COMPILED_NAME = "vomit_compiled_faces";
// The parts' keys for what is drawn as compiled, past any object number.
constexpr std::uint32_t FIRST_PART = 0x80000000u;
// The world as seen from this many sets of clusters is kept (a set a copy of its vertices).
constexpr std::size_t VISIBLE_WORLDS_KEPT = 4;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Lower( std::string_view szText )
{
	std::string szOut( szText );
	std::ranges::transform( szOut, szOut.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	return szOut;
}

// How a mesh is lit from the compile's light.
enum class CubeLight_t
{
	CUBE_LIGHT_MODEL,   // one model: from its middle
	CUBE_LIGHT_OVERLAY, // laid on faces: as each vertex's face is lit
	CUBE_LIGHT_MODELS,  // many models in one mesh: each vertex as a model there
};

//-----------------------------------------------------------------------------
// Purpose: A cube near a point: at it, else a little above (a point on a floor is often just
//          inside the solid leaf under it, which has none).
//-----------------------------------------------------------------------------
std::optional<xv::bsp::CAmbientLighting::Cube_t> CubeNear( const xv::bsp::CAmbientLighting &ambient, Point_t point )
{
	for ( const double flUp : { 0.0, 16.0, 48.0 } )
	{
		if ( auto cube = ambient.CubeAt( { point[ 0 ], point[ 1 ], point[ 2 ] + flUp } ) )
		{
			return cube;
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: The light a model whose drawing fills bounds gets: the cube at its middle, and the
//          light baked on the surface under it. One set in a wall (a window, a balcony) has
//          only solid under it: the ground in front of it, around it, is what lights it.
//-----------------------------------------------------------------------------
CMapCompiled::ModelLight_t ModelLightNear( const xv::bsp::CAmbientLighting &ambient, const xv::bsp::CSurfaceLight *pSurface, const xv::map::Box_t &bounds )
{
	CMapCompiled::ModelLight_t light;
	const Point_t middle = xv::map::Scale( xv::map::Add( bounds.m_Mins, bounds.m_Maxs ), 0.5 );
	light.m_Cube = ambient.IsEmpty() ? std::nullopt : CubeNear( ambient, middle );
	light.m_vecBelow = pSurface ? pSurface->Below( middle ) : std::nullopt;
	const double flHalf = std::max( bounds.m_Maxs[ 0 ] - bounds.m_Mins[ 0 ], bounds.m_Maxs[ 1 ] - bounds.m_Mins[ 1 ] ) * 0.5;
	for ( const double flOut : { 32.0, 128.0, 384.0 } )
	{
		for ( int nWay = 0; !light.m_vecBelow && pSurface && nWay < 8; nWay++ )
		{
			const double flAngle = nWay * std::numbers::pi / 4.0;
			light.m_vecBelow = pSurface->Below( { middle[ 0 ] + std::cos( flAngle ) * ( flHalf + flOut ), middle[ 1 ] + std::sin( flAngle ) * ( flHalf + flOut ), middle[ 2 ] } );
		}
	}
	return light;
}

//-----------------------------------------------------------------------------
// Purpose: A mesh lit from the compile's light. A model (CUBE_LIGHT_MODEL): the ambient cube
//          at the middle of what it draws, which holds the light bounced to it, and, for the
//          direct light the cubes leave out, the light baked on the surface under it, full on
//          what faces up and less on what faces down (a likeness of the engine's lighting, which
//          adds the map's lights to models as it draws them). An overlay (CUBE_LIGHT_OVERLAY): each
//          vertex as the face it lies on is lit, which is how the game lights one, else by the
//          cube just off it. Models in one mesh (CUBE_LIGHT_MODELS): each vertex as a model
//          there would be. Null when neither is to be had.
//-----------------------------------------------------------------------------
std::shared_ptr<const std::vector<xv::map::LightVertex_t>> LightFromCubes( const xv::map::CMapMesh &mesh, const xv::bsp::CAmbientLighting &ambient, const xv::bsp::CSurfaceLight *pSurface, CubeLight_t nWay )
{
	if ( mesh.m_Vertices.empty() )
	{
		return nullptr;
	}
	auto pLights = std::make_shared<std::vector<xv::map::LightVertex_t>>( mesh.m_Vertices.size(), xv::map::LIGHT_VERTEX_NONE );
	const bool bPerVertex = nWay == CubeLight_t::CUBE_LIGHT_OVERLAY;
	std::optional<xv::bsp::CAmbientLighting::Cube_t> whole;
	std::optional<xv::CVec3> below;
	if ( nWay == CubeLight_t::CUBE_LIGHT_MODEL )
	{
		xv::map::Box_t bounds;
		for ( const xv::map::MeshVertex_t &vertex : mesh.m_Vertices )
		{
			bounds.Add( xv::map::ToPoint( vertex.m_vecPosition ) );
		}
		const CMapCompiled::ModelLight_t light = ModelLightNear( ambient, pSurface, bounds );
		whole = light.m_Cube;
		below = light.m_vecBelow;
		if ( !whole && !below )
		{
			return nullptr;
		}
	}
	bool bAny = false;
	// An overlay's corners past the face's edge (on another face, or off it) take the light its
	// others found.
	std::vector<std::optional<xv::CVec3>> onFace( bPerVertex ? mesh.m_Vertices.size() : 0 );
	xv::CVec3 vecFound;
	int nFound = 0;
	for ( std::size_t i = 0; i < onFace.size() && pSurface; i++ )
	{
		onFace[ i ] = pSurface->On( xv::map::ToPoint( mesh.m_Vertices[ i ].m_vecPosition ), xv::map::ToPoint( mesh.m_Vertices[ i ].m_vecNormal ) );
		if ( onFace[ i ] )
		{
			vecFound += *onFace[ i ];
			nFound++;
		}
	}
	for ( std::size_t i = 0; i < mesh.m_Vertices.size(); i++ )
	{
		const xv::map::MeshVertex_t &vertex = mesh.m_Vertices[ i ];
		std::optional<xv::CVec3> light;
		if ( bPerVertex )
		{
			light = onFace[ i ];
			if ( !light && nFound > 0 )
			{
				light = vecFound * ( 1.0f / static_cast<float>( nFound ) );
			}
			if ( !light && !ambient.IsEmpty() )
			{
				if ( const auto cube = CubeNear( ambient, xv::map::ToPoint( vertex.m_vecPosition + vertex.m_vecNormal * 2.0f ) ) )
				{
					light = xv::bsp::CAmbientLighting::LightFor( *cube, vertex.m_vecNormal );
				}
			}
		}
		else
		{
			if ( nWay == CubeLight_t::CUBE_LIGHT_MODELS )
			{
				// A little above the vertex: one on a model's base lies on the floor.
				const Point_t at = xv::map::ToPoint( vertex.m_vecPosition + xv::CVec3{ 0.0f, 0.0f, 8.0f } );
				whole = ambient.IsEmpty() ? std::nullopt : CubeNear( ambient, at );
				below = pSurface ? pSurface->Below( at ) : std::nullopt;
			}
			xv::CVec3 vecLight = whole ? xv::bsp::CAmbientLighting::LightFor( *whole, vertex.m_vecNormal ) : xv::CVec3{};
			if ( below )
			{
				vecLight += *below * std::clamp( 0.5f + 0.5f * vertex.m_vecNormal.m_flZ, 0.25f, 1.0f );
			}
			light = vecLight;
		}
		if ( light )
		{
			( *pLights )[ i ] = { light->m_flX, light->m_flY, light->m_flZ, xv::map::LIGHT_KIND_VERTEX };
			bAny = true;
		}
	}
	return bAny ? pLights : nullptr;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapCompiled::CMapCompiled( std::shared_ptr<const xv::bsp::CompiledMap_t> pCompiled )
	: m_pCompiled( std::move( pCompiled ) )
{
}

CMapCompiled::~CMapCompiled() = default;

//-----------------------------------------------------------------------------
// Purpose: Hidden from the start (visgroupshown 0), so neither it nor its boxes show in the
//          2D views; the boxes are 16 units at the origin.
//-----------------------------------------------------------------------------
std::string CMapCompiled::MapText( const xv::bsp::CompiledMap_t &compiled, int nFirstId )
{
	int nId = nFirstId;
	const std::string szHidden = "editor\n{\n\"color\" \"0 180 0\"\n\"visgroupshown\" \"0\"\n\"visgroupautoshown\" \"1\"\n}\n";
	std::string szOut = "entity\n{\n\"id\" \"" + std::to_string( nId++ ) + "\"\n\"classname\" \"func_brush\"\n\"targetname\" \"" + std::string( COMPILED_NAME ) + "\"\n";
	// A 16 unit box's planes, Hammer's way round (+z, -z, -x, +x, +y, -y), and their axes.
	static constexpr std::array<std::string_view, 6> PLANES = {
		"(0 16 16) (16 16 16) (16 0 16)",
		"(0 0 0) (16 0 0) (16 16 0)",
		"(0 16 16) (0 0 16) (0 0 0)",
		"(16 16 0) (16 0 0) (16 0 16)",
		"(16 16 16) (0 16 16) (0 16 0)",
		"(16 0 0) (0 0 0) (0 0 16)",
	};
	static constexpr std::array<std::string_view, 6> AXES = {
		"\"uaxis\" \"[1 0 0 0] 0.25\"\n\"vaxis\" \"[0 -1 0 0] 0.25\"\n",
		"\"uaxis\" \"[1 0 0 0] 0.25\"\n\"vaxis\" \"[0 -1 0 0] 0.25\"\n",
		"\"uaxis\" \"[0 1 0 0] 0.25\"\n\"vaxis\" \"[0 0 -1 0] 0.25\"\n",
		"\"uaxis\" \"[0 1 0 0] 0.25\"\n\"vaxis\" \"[0 0 -1 0] 0.25\"\n",
		"\"uaxis\" \"[1 0 0 0] 0.25\"\n\"vaxis\" \"[0 0 -1 0] 0.25\"\n",
		"\"uaxis\" \"[1 0 0 0] 0.25\"\n\"vaxis\" \"[0 0 -1 0] 0.25\"\n",
	};
	const std::vector<std::string> &materials = compiled.m_Materials;
	for ( std::size_t nFirst = 0; nFirst < std::max<std::size_t>( materials.size(), 1 ); nFirst += PLANES.size() )
	{
		szOut += "solid\n{\n\"id\" \"" + std::to_string( nId++ ) + "\"\n";
		for ( std::size_t i = 0; i < PLANES.size(); i++ )
		{
			const std::string szMaterial = materials.empty() ? std::string( "TOOLS/TOOLSNODRAW" ) : materials[ std::min( nFirst + i, materials.size() - 1 ) ];
			szOut += "side\n{\n\"id\" \"" + std::to_string( nId++ ) + "\"\n\"plane\" \"" + std::string( PLANES[ i ] ) + "\"\n\"material\" \"" + szMaterial + "\"\n" + std::string( AXES[ i ] ) +
				"\"rotation\" \"0\"\n\"lightmapscale\" \"16\"\n\"smoothing_groups\" \"0\"\n}\n";
		}
		szOut += szHidden + "}\n";
	}
	szOut += szHidden + "}\n";
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapCompiled::Build( const xv::map::CScene &scene )
{
	m_Meshes.clear();
	m_Hidden.assign( scene.m_SolidObjects.begin(), scene.m_SolidObjects.end() );
	m_CubeLit.clear();
	m_LumpPlacements.clear();
	m_bLumpPlaced = false;
	m_bBuilt = false;
	bool bFound = false;
	for ( std::size_t nEntity = 0; nEntity < scene.m_Map.m_Entities.size(); nEntity++ )
	{
		if ( scene.m_Map.m_Entities[ nEntity ].GetValue( "targetname" ) == COMPILED_NAME )
		{
			m_nObject = scene.EntityObject( nEntity );
			bFound = true;
			break;
		}
	}
	if ( !bFound )
	{
		return;
	}
	std::ranges::sort( m_Hidden );
	std::unordered_map<std::string, std::uint32_t> byName;
	for ( std::size_t i = 0; i < scene.m_Materials.size(); i++ )
	{
		byName.try_emplace( Lower( scene.m_Materials[ i ] ), static_cast<std::uint32_t>( i ) );
	}
	std::vector<std::uint32_t> materials( m_pCompiled->m_Materials.size(), 0 );
	for ( std::size_t i = 0; i < materials.size(); i++ )
	{
		if ( const auto found = byName.find( Lower( m_pCompiled->m_Materials[ i ] ) ); found != byName.end() )
		{
			materials[ i ] = found->second;
		}
	}
	for ( const xv::bsp::CompiledModel_t &model : m_pCompiled->m_Models )
	{
		if ( !model.m_pMesh || model.m_pMesh->m_Vertices.empty() )
		{
			m_Meshes.push_back( nullptr );
			continue;
		}
		auto pOwn = std::make_shared<xv::map::CMapMesh>( *model.m_pMesh );
		for ( xv::map::MeshBatch_t &batch : pOwn->m_Batches )
		{
			batch.m_nMaterial = batch.m_nMaterial < materials.size() ? materials[ batch.m_nMaterial ] : 0;
		}
		for ( xv::map::MeshFace_t &face : pOwn->m_Faces )
		{
			face.m_nMaterial = face.m_nMaterial < materials.size() ? materials[ face.m_nMaterial ] : 0;
		}
		for ( xv::map::MeshVertex_t &vertex : pOwn->m_Vertices )
		{
			vertex.m_nObject = m_nObject;
		}
		m_Meshes.push_back( std::move( pOwn ) );
	}
	if ( !m_pCompiled->m_Models.empty() )
	{
		const xv::bsp::CompiledModel_t &world = m_pCompiled->m_Models.front();
		m_pSurfaceLight = std::make_unique<xv::bsp::CSurfaceLight>( world.m_pMesh, world.m_pLights, m_pCompiled->m_pLighting );
	}
	m_bBuilt = true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const std::shared_ptr<const xv::map::CMapMesh> &CMapCompiled::GetModelMesh( std::size_t nModel ) const
{
	static const std::shared_ptr<const xv::map::CMapMesh> NONE;
	return nModel < m_Meshes.size() ? m_Meshes[ nModel ] : NONE;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const std::vector<CMapCompiled::Drawn_t> &CMapCompiled::GetLumpPlacements()
{
	if ( m_bLumpPlaced || !m_bBuilt )
	{
		return m_LumpPlacements;
	}
	m_bLumpPlaced = true;
	for ( const xv::bsp::ModelPlacement_t &placement : m_pCompiled->m_Placements )
	{
		const auto nModel = static_cast<std::size_t>( placement.m_nModel );
		if ( nModel == 0 || !GetModelMesh( nModel ) )
		{
			continue;
		}
		auto pPlaced = std::make_shared<xv::map::CMapMesh>();
		xv::map::AppendPlaced( *pPlaced, *m_Meshes[ nModel ], xv::map::Transform_t::FromAngles( placement.m_Origin, placement.m_Angles ), m_nObject );
		m_LumpPlacements.push_back( { std::move( pPlaced ), m_pCompiled->m_Models[ nModel ].m_pLights } );
	}
	return m_LumpPlacements;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::shared_ptr<const std::vector<xv::map::LightVertex_t>> CMapCompiled::LightPlaced( const xv::map::CMapMesh &mesh ) const
{
	return m_pCompiled->m_pAmbient ? LightFromCubes( mesh, *m_pCompiled->m_pAmbient, m_pSurfaceLight.get(), CubeLight_t::CUBE_LIGHT_MODEL ) : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::shared_ptr<const std::vector<xv::map::LightVertex_t>> CMapCompiled::CubeLights( const std::shared_ptr<const xv::map::CMapMesh> &pMesh, int nWay )
{
	if ( const auto found = m_CubeLit.find( pMesh.get() ); found != m_CubeLit.end() && found->second.m_pMesh == pMesh )
	{
		return found->second.m_pLights;
	}
	auto pLights = m_pCompiled->m_pAmbient ? LightFromCubes( *pMesh, *m_pCompiled->m_pAmbient, m_pSurfaceLight.get(), static_cast<CubeLight_t>( nWay ) ) : nullptr;
	m_CubeLit[ pMesh.get() ] = { pMesh, pLights };
	return pLights;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapCompiled::SetClusters( std::vector<int> clusters )
{
	std::ranges::sort( clusters );
	clusters.erase( std::unique( clusters.begin(), clusters.end() ), clusters.end() );
	// Outside the map (in solid, in the void) everything can be seen.
	if ( std::ranges::find( clusters, -1 ) != clusters.end() )
		clusters.clear();
	m_Clusters = std::move( clusters );
}

//-----------------------------------------------------------------------------
// Purpose: The world's triangles whose faces the clusters may see, batch by batch (the
//          vertices and so the light stay as they are); displacements always, as the
//          compiler puts them in no leaf. Kept for the last few sets of clusters.
//-----------------------------------------------------------------------------
std::shared_ptr<const xv::map::CMapMesh> CMapCompiled::VisibleWorld()
{
	const std::shared_ptr<const xv::map::CMapMesh> &pWorld = m_Meshes.front();
	const xv::bsp::CVisibility *pVisibility = m_pCompiled->m_pVisibility.get();
	const std::vector<std::int32_t> &numbers = m_pCompiled->m_Models.front().m_FaceNumbers;
	if ( m_Clusters.empty() || !pVisibility || numbers.size() != pWorld->m_Faces.size() )
		return pWorld;
	for ( const auto &[ clusters, pKept ] : m_VisibleWorlds )
	{
		if ( clusters == m_Clusters )
			return pKept;
	}
	std::vector<bool> visible;
	for ( const int nCluster : m_Clusters )
	{
		const std::vector<bool> seen = pVisibility->VisibleFaces( nCluster );
		if ( visible.empty() )
			visible = seen;
		else
			for ( std::size_t i = 0; i < visible.size() && i < seen.size(); i++ )
				visible[ i ] = visible[ i ] || seen[ i ];
	}
	auto pMesh = std::make_shared<xv::map::CMapMesh>( *pWorld );
	pMesh->m_Indices.clear();
	pMesh->m_TriangleFaces.clear();
	pMesh->m_Batches.clear();
	for ( const xv::map::MeshBatch_t &batch : pWorld->m_Batches )
	{
		xv::map::MeshBatch_t kept = batch;
		kept.m_nFirstIndex = static_cast<std::uint32_t>( pMesh->m_Indices.size() );
		for ( std::uint32_t i = batch.m_nFirstIndex; i + 2 < batch.m_nFirstIndex + batch.m_nIndexCount; i += 3 )
		{
			const std::uint32_t nFace = pWorld->m_TriangleFaces[ i / 3 ];
			const std::int32_t nNumber = nFace < numbers.size() ? numbers[ nFace ] : -1;
			const bool bShown = nFace >= pWorld->m_Faces.size() || pWorld->m_Faces[ nFace ].m_bDisplacement || nNumber < 0 || static_cast<std::size_t>( nNumber ) >= visible.size() || visible[ static_cast<std::size_t>( nNumber ) ];
			if ( !bShown )
				continue;
			pMesh->m_Indices.insert( pMesh->m_Indices.end(), { pWorld->m_Indices[ i ], pWorld->m_Indices[ i + 1 ], pWorld->m_Indices[ i + 2 ] } );
			pMesh->m_TriangleFaces.push_back( nFace );
		}
		kept.m_nIndexCount = static_cast<std::uint32_t>( pMesh->m_Indices.size() ) - kept.m_nFirstIndex;
		if ( kept.m_nIndexCount > 0 )
			pMesh->m_Batches.push_back( kept );
	}
	if ( m_VisibleWorlds.size() >= VISIBLE_WORLDS_KEPT )
		m_VisibleWorlds.erase( m_VisibleWorlds.begin() );
	m_VisibleWorlds.emplace_back( m_Clusters, pMesh );
	return pMesh;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapCompiled::Apply( MapFrame_t &frame, const std::vector<Drawn_t> &placed, const std::vector<std::uint32_t> &hidden )
{
	if ( !m_bBuilt || !frame.m_pScene )
	{
		return;
	}
	auto pLights = std::make_shared<MapMeshLights_t>();
	auto pParts = std::make_shared<xv::map::MeshParts_t>( frame.m_pEntityParts ? *frame.m_pEntityParts : xv::map::MeshParts_t{} );
	std::uint32_t nPart = FIRST_PART;
	const auto add = [ & ]( const std::shared_ptr<const xv::map::CMapMesh> &pMesh, const std::shared_ptr<const std::vector<xv::map::LightVertex_t>> &pMeshLights ) {
		( *pParts )[ nPart++ ] = pMesh;
		if ( pMeshLights )
		{
			( *pLights )[ pMesh.get() ] = pMeshLights;
		}
	};
	if ( !m_Meshes.empty() && m_Meshes.front() )
	{
		const std::shared_ptr<const xv::map::CMapMesh> pWorld = VisibleWorld();
		m_nWorldTriangles = pWorld->m_Indices.size() / 3;
		add( pWorld, m_pCompiled->m_Models.front().m_pLights );
	}
	for ( const Drawn_t &drawn : placed )
	{
		if ( drawn.m_pMesh )
		{
			add( drawn.m_pMesh, drawn.m_pLights );
		}
	}
	frame.m_pEntityParts = std::move( pParts );
	frame.m_bEntityPreview = false;

	// The scene's solids aren't drawn; the compiled faces, on their object, are.
	auto pFlags = std::make_shared<std::vector<std::uint8_t>>( frame.m_pFlags ? *frame.m_pFlags : std::vector<std::uint8_t>{} );
	pFlags->resize( std::max<std::size_t>( { pFlags->size(), frame.m_pScene->GetObjectCount(), m_nObject + std::size_t{ 1 } } ), static_cast<std::uint8_t>( xv::map::RASTER_OBJECT_SHOWN ) );
	( *pFlags )[ m_nObject ] = static_cast<std::uint8_t>( xv::map::RASTER_OBJECT_SHOWN );
	for ( const std::vector<std::uint32_t> *pList : { &std::as_const( m_Hidden ), &hidden } )
	{
		for ( const std::uint32_t nObject : *pList )
		{
			if ( nObject < pFlags->size() && nObject != m_nObject )
			{
				( *pFlags )[ nObject ] &= static_cast<std::uint8_t>( ~xv::map::RASTER_OBJECT_SHOWN );
			}
		}
	}
	frame.m_pFlags = std::move( pFlags );

	// The scene's props and overlays, lit from the cubes.
	const auto lightParts = [ & ]( const std::shared_ptr<const xv::map::MeshParts_t> &pParts, const std::shared_ptr<const xv::map::CMapMesh> &pCombined, bool bOverlay ) {
		const CubeLight_t nPart = bOverlay ? CubeLight_t::CUBE_LIGHT_OVERLAY : CubeLight_t::CUBE_LIGHT_MODEL;
		const CubeLight_t nCombined = bOverlay ? CubeLight_t::CUBE_LIGHT_OVERLAY : CubeLight_t::CUBE_LIGHT_MODELS;
		if ( pParts )
		{
			for ( const auto &[ nObject, pPart ] : *pParts )
			{
				if ( pPart && !std::ranges::binary_search( hidden, nObject ) )
				{
					if ( auto pPartLights = CubeLights( pPart, static_cast<int>( nPart ) ) )
					{
						( *pLights )[ pPart.get() ] = std::move( pPartLights );
					}
				}
			}
		}
		else if ( pCombined )
		{
			if ( auto pCombinedLights = CubeLights( pCombined, static_cast<int>( nCombined ) ) )
			{
				( *pLights )[ pCombined.get() ] = std::move( pCombinedLights );
			}
		}
	};
	lightParts( frame.m_pPropParts, frame.m_pPropMesh, false );
	lightParts( frame.m_pOverlayParts, frame.m_pOverlayMesh, true );
	frame.m_pMeshLights = std::move( pLights );

	xv::map::ShadeOptions_t &shade = frame.m_Options.m_Shade;
	if ( shade.m_nMode == xv::map::RasterMode_t::RASTER_MODE_TEXTURED || shade.m_nMode == xv::map::RasterMode_t::RASTER_MODE_TEXTURED_SHADED || shade.m_nMode == xv::map::RasterMode_t::RASTER_MODE_LIGHTING )
	{
		frame.m_pLighting = m_pCompiled->m_pLighting;
		shade.m_nMode = xv::map::RasterMode_t::RASTER_MODE_LIGHTING;
		shade.m_pLighting = m_pCompiled->m_pLighting.get();
		shade.m_bLightHdr = m_pCompiled->m_bHdr;
		shade.m_bLightOnly = false;
	}
	else
	{
		frame.m_pLighting = nullptr;
		shade.m_pLighting = nullptr;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapCompiled::ModelLight_t CMapCompiled::ModelLightAt( const xv::map::Box_t &bounds ) const
{
	static const xv::bsp::CAmbientLighting NONE;
	return ModelLightNear( m_pCompiled->m_pAmbient ? *m_pCompiled->m_pAmbient : NONE, m_pSurfaceLight.get(), bounds );
}

//-----------------------------------------------------------------------------
// Purpose: Each vertex from the cube by its normal, and the light under the model, full on
//          what faces up and less on what faces down.
//-----------------------------------------------------------------------------
std::shared_ptr<const std::vector<xv::map::LightVertex_t>> CMapCompiled::LightModel( const xv::map::CMapMesh &mesh, const ModelLight_t &light )
{
	if ( mesh.m_Vertices.empty() || ( !light.m_Cube && !light.m_vecBelow ) )
	{
		return nullptr;
	}
	auto pLights = std::make_shared<std::vector<xv::map::LightVertex_t>>( mesh.m_Vertices.size() );
	for ( std::size_t i = 0; i < mesh.m_Vertices.size(); i++ )
	{
		const xv::CVec3 &vecNormal = mesh.m_Vertices[ i ].m_vecNormal;
		xv::CVec3 vecLight = light.m_Cube ? xv::bsp::CAmbientLighting::LightFor( *light.m_Cube, vecNormal ) : xv::CVec3{};
		if ( light.m_vecBelow )
		{
			vecLight += *light.m_vecBelow * std::clamp( 0.5f + 0.5f * vecNormal.m_flZ, 0.25f, 1.0f );
		}
		( *pLights )[ i ] = { vecLight.m_flX, vecLight.m_flY, vecLight.m_flZ, xv::map::LIGHT_KIND_VERTEX };
	}
	return pLights;
}
