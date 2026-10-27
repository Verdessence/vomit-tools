//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapRenderData.h.
//
//===========================================================================//
#include "MapRenderData.h"

#include <algorithm>

#include "map/MapConstants.h"
#include "map/MapDocument.h"

namespace
{

//-----------------------------------------------------------------------------
// Purpose: A box's six faces (wound clockwise seen from outside, as brush faces are) and twelve
//          edges, added to mesh for one object.
//-----------------------------------------------------------------------------
void AddBox( xv::map::CMapMesh &mesh, const xv::map::Box_t &box, std::uint32_t nObject )
{
	using xv::map::Point_t;
	const Point_t &n = box.m_Mins;
	const Point_t &x = box.m_Maxs;
	// Each face: its outward normal and its corners, clockwise seen from outside.
	const std::array<std::pair<Point_t, std::array<Point_t, 4>>, 6> faces = { {
		{ { 0, 0, 1 }, { { { n[ 0 ], x[ 1 ], x[ 2 ] }, { x[ 0 ], x[ 1 ], x[ 2 ] }, { x[ 0 ], n[ 1 ], x[ 2 ] }, { n[ 0 ], n[ 1 ], x[ 2 ] } } } },
		{ { 0, 0, -1 }, { { { n[ 0 ], n[ 1 ], n[ 2 ] }, { x[ 0 ], n[ 1 ], n[ 2 ] }, { x[ 0 ], x[ 1 ], n[ 2 ] }, { n[ 0 ], x[ 1 ], n[ 2 ] } } } },
		{ { -1, 0, 0 }, { { { n[ 0 ], x[ 1 ], x[ 2 ] }, { n[ 0 ], n[ 1 ], x[ 2 ] }, { n[ 0 ], n[ 1 ], n[ 2 ] }, { n[ 0 ], x[ 1 ], n[ 2 ] } } } },
		{ { 1, 0, 0 }, { { { x[ 0 ], n[ 1 ], x[ 2 ] }, { x[ 0 ], x[ 1 ], x[ 2 ] }, { x[ 0 ], x[ 1 ], n[ 2 ] }, { x[ 0 ], n[ 1 ], n[ 2 ] } } } },
		{ { 0, 1, 0 }, { { { x[ 0 ], x[ 1 ], x[ 2 ] }, { n[ 0 ], x[ 1 ], x[ 2 ] }, { n[ 0 ], x[ 1 ], n[ 2 ] }, { x[ 0 ], x[ 1 ], n[ 2 ] } } } },
		{ { 0, -1, 0 }, { { { n[ 0 ], n[ 1 ], x[ 2 ] }, { x[ 0 ], n[ 1 ], x[ 2 ] }, { x[ 0 ], n[ 1 ], n[ 2 ] }, { n[ 0 ], n[ 1 ], n[ 2 ] } } } },
	} };
	for ( const auto &[ normal, corners ] : faces )
	{
		const auto nBase = static_cast<std::uint32_t>( mesh.m_Vertices.size() );
		for ( const Point_t &p : corners )
		{
			xv::map::MeshVertex_t vertex;
			vertex.m_vecPosition = xv::map::ToVec3( p );
			vertex.m_vecNormal = xv::map::ToVec3( normal );
			vertex.m_nObject = nObject;
			mesh.m_Vertices.push_back( vertex );
		}
		mesh.m_Indices.insert( mesh.m_Indices.end(), { nBase, nBase + 1, nBase + 2, nBase, nBase + 2, nBase + 3 } );
		mesh.m_Lines.insert( mesh.m_Lines.end(), { nBase, nBase + 1, nBase + 1, nBase + 2, nBase + 2, nBase + 3, nBase + 3, nBase } );
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::RasterScene_t MapFrame_t::MakeScene() const
{
	xv::map::RasterScene_t scene;
	if ( m_pScene )
	{
		scene.m_Meshes.push_back( &m_pScene->m_Mesh );
	}
	if ( m_pEntityMesh )
	{
		scene.m_Meshes.push_back( m_pEntityMesh.get() );
	}
	if ( m_pPropMesh )
	{
		scene.m_Meshes.push_back( m_pPropMesh.get() );
	}
	if ( m_pOverlayMesh )
	{
		scene.m_Meshes.push_back( m_pOverlayMesh.get() );
	}
	if ( m_pSpriteMesh )
	{
		scene.m_Meshes.push_back( m_pSpriteMesh.get() );
	}
	if ( m_pMaterials )
	{
		scene.m_Materials = *m_pMaterials;
	}
	if ( m_pFlags )
	{
		scene.m_ObjectFlags = *m_pFlags;
	}
	if ( m_pFlatColors )
	{
		scene.m_ObjectColors = *m_pFlatColors;
	}
	if ( m_pLineColors )
	{
		scene.m_LineColors = *m_pLineColors;
	}
	scene.m_Lines = m_Lines;
	return scene;
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's precedence for line colours (the selection's red is the caller's).
//-----------------------------------------------------------------------------
QColor MapLineColor( const xv::map::CScene &scene, std::uint32_t nObject, const MapViewSettings_t &settings, bool bInvalid )
{
	if ( bInvalid )
	{
		return mapstyle::INVALID;
	}
	const int nEntity = scene.GetObjectEntity( nObject );
	const xv::map::EditorInfo_t &editor = nEntity >= 0 ? scene.m_Map.m_Entities[ static_cast<std::size_t>( nEntity ) ].m_Editor : scene.GetObjectEditor( nObject );
	if ( settings.m_bUseGroupColors && !editor.m_Visgroups.empty() )
	{
		for ( const xv::map::Visgroup_t &visgroup : scene.m_Map.m_Visgroups )
		{
			if ( visgroup.m_nId == editor.m_Visgroups.front() )
			{
				return QColor{ visgroup.m_Color.m_nR, visgroup.m_Color.m_nG, visgroup.m_Color.m_nB };
			}
		}
	}
	if ( nEntity >= 0 )
	{
		const xv::map::Color_t color = scene.m_Entities[ static_cast<std::size_t>( nEntity ) ].m_Color;
		return QColor{ color.m_nR, color.m_nG, color.m_nB };
	}
	return mapstyle::BRUSH;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapRenderData::CMapRenderData( const CMapDocument &document )
	: m_Document( document )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint32_t CMapRenderData::GetEntityMaterial() const
{
	const xv::map::CScene *pScene = m_Document.GetScene();
	return pScene ? static_cast<std::uint32_t>( pScene->m_Map.m_Materials.size() ) : 0;
}

//-----------------------------------------------------------------------------
// Purpose: What depends only on the scene: the entity boxes, the flat colours, and plain
//          materials until textures come.
//-----------------------------------------------------------------------------
void CMapRenderData::Rebuild()
{
	const xv::map::CScene *pScene = m_Document.GetScene();
	m_pBuiltFor = pScene;
	m_nStateFor = ~0ull;
	m_nSettingsFor = ~0ull;
	m_nGeneration++;
	std::shared_ptr<const xv::sky::CCube> pSky = std::move( m_Frame.m_pSky );
	m_Frame = {};
	m_Frame.m_pSky = std::move( pSky );
	if ( !pScene )
	{
		return;
	}
	m_Frame.m_pScene = m_Document.GetSharedScene();
	m_PropObjects.clear();
	m_pSprites.reset();
	m_SpriteObjects.clear();
	BuildEntityBoxes();

	auto pMaterials = std::make_shared<std::vector<xv::map::RasterMaterial_t>>( pScene->m_Map.m_Materials.size() + 1 );
	pMaterials->back().m_bObjectColor = true;
	m_Frame.m_pMaterials = pMaterials;

	auto pFlat = std::make_shared<std::vector<std::array<std::uint8_t, 3>>>( pScene->GetObjectCount() );
	for ( std::uint32_t n = 0; n < pScene->GetObjectCount(); n++ )
	{
		// Hammer's flat colour: the solid's own editor colour, or the entity's.
		xv::map::Color_t color = pScene->GetObjectEditor( n ).m_Color;
		if ( !pScene->IsSolidObject( n ) )
		{
			color = pScene->m_Entities[ n - pScene->m_Solids.size() ].m_Color;
		}
		( *pFlat )[ n ] = { color.m_nR, color.m_nG, color.m_nB };
	}
	m_Frame.m_pFlatColors = pFlat;
}

//-----------------------------------------------------------------------------
// Purpose: The boxes of point entities, but for those drawn as their model.
//-----------------------------------------------------------------------------
void CMapRenderData::BuildEntityBoxes()
{
	const xv::map::CScene *pScene = m_Document.GetScene();
	auto pEntities = std::make_shared<xv::map::CMapMesh>();
	for ( std::size_t e = 0; pScene && e < pScene->m_Entities.size(); e++ )
	{
		const std::uint32_t nObject = pScene->EntityObject( e );
		if ( pScene->m_Entities[ e ].m_bPoint && !std::ranges::binary_search( m_PropObjects, nObject ) && !std::ranges::binary_search( m_SpriteObjects, nObject ) )
		{
			AddBox( *pEntities, pScene->GetEntityBox( e ), nObject );
		}
	}
	xv::map::MeshBatch_t batch;
	batch.m_nMaterial = GetEntityMaterial();
	batch.m_nChunk = ~0u; // no area: always considered
	batch.m_nIndexCount = static_cast<std::uint32_t>( pEntities->m_Indices.size() );
	if ( batch.m_nIndexCount > 0 )
	{
		pEntities->m_Batches.push_back( batch );
	}
	m_Frame.m_pEntityMesh = pEntities;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapRenderData::SetMaterials( std::shared_ptr<const std::vector<xv::map::RasterMaterial_t>> pMaterials, std::shared_ptr<const void> pTextures )
{
	if ( m_Document.GetScene() != m_pBuiltFor )
	{
		Rebuild();
	}
	// The map's materials, then the one entity boxes are drawn with, then the props'.
	auto pAll = std::make_shared<std::vector<xv::map::RasterMaterial_t>>( pMaterials ? *pMaterials : std::vector<xv::map::RasterMaterial_t>{} );
	const std::uint32_t nEntity = GetEntityMaterial();
	if ( pAll->size() <= nEntity )
	{
		pAll->resize( nEntity + 1 );
	}
	xv::map::RasterMaterial_t entity;
	entity.m_bObjectColor = true;
	( *pAll )[ nEntity ] = entity;
	m_Frame.m_pMaterials = std::move( pAll );
	m_Frame.m_pTextures = std::move( pTextures );
	m_nGeneration++;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapRenderData::SetProps( std::shared_ptr<const xv::map::CMapMesh> pProps, std::vector<std::uint32_t> objects )
{
	if ( m_Document.GetScene() != m_pBuiltFor )
	{
		Rebuild();
	}
	if ( pProps == m_Frame.m_pPropMesh )
	{
		return;
	}
	m_Frame.m_pPropMesh = std::move( pProps );
	m_PropObjects = std::move( objects );
	std::ranges::sort( m_PropObjects );
	BuildEntityBoxes();
	m_nGeneration++;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapRenderData::SetOverlays( std::shared_ptr<const xv::map::CMapMesh> pOverlays )
{
	if ( m_Document.GetScene() != m_pBuiltFor )
	{
		Rebuild();
	}
	if ( pOverlays != m_Frame.m_pOverlayMesh )
	{
		m_Frame.m_pOverlayMesh = std::move( pOverlays );
		m_nGeneration++;
	}
}

//-----------------------------------------------------------------------------
// Purpose: The sprites' tints go in as their entities' flat colours, which sprites are
//          multiplied by.
//-----------------------------------------------------------------------------
void CMapRenderData::SetSprites( std::shared_ptr<const std::vector<xv::map::SpritePlacement_t>> pSprites )
{
	if ( m_Document.GetScene() != m_pBuiltFor )
	{
		Rebuild();
	}
	if ( pSprites == m_pSprites )
	{
		return;
	}
	m_pSprites = std::move( pSprites );
	m_SpriteObjects.clear();
	if ( m_pSprites && m_Frame.m_pFlatColors )
	{
		auto pFlat = std::make_shared<std::vector<std::array<std::uint8_t, 3>>>( *m_Frame.m_pFlatColors );
		for ( const xv::map::SpritePlacement_t &sprite : *m_pSprites )
		{
			if ( sprite.m_nObject < pFlat->size() )
			{
				( *pFlat )[ sprite.m_nObject ] = sprite.m_Tint;
			}
			m_SpriteObjects.push_back( sprite.m_nObject );
		}
		m_Frame.m_pFlatColors = pFlat;
	}
	std::ranges::sort( m_SpriteObjects );
	m_Frame.m_pSpriteMesh.reset();
	BuildEntityBoxes();
	m_nGeneration++;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapRenderData::SetSky( std::shared_ptr<const xv::sky::CCube> pSky )
{
	if ( pSky != m_Frame.m_pSky )
	{
		m_Frame.m_pSky = std::move( pSky );
		m_nGeneration++;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapFrame_t CMapRenderData::Snapshot( const xv::map::CViewCamera &camera, const xv::map::RasterOptions_t &options, const MapViewSettings_t &settings )
{
	const xv::map::CScene *pScene = m_Document.GetScene();
	if ( pScene != m_pBuiltFor )
	{
		Rebuild();
	}
	if ( pScene && m_nStateFor != m_Document.GetStateGeneration() )
	{
		m_nStateFor = m_Document.GetStateGeneration();
		auto pFlags = std::make_shared<std::vector<std::uint8_t>>( pScene->GetObjectCount(), 0 );
		for ( std::uint32_t n = 0; n < pScene->GetObjectCount(); n++ )
		{
			std::uint8_t nFlags = m_Document.IsShown( n ) ? xv::map::RASTER_OBJECT_SHOWN : 0;
			nFlags |= m_Document.IsSelected( n ) ? xv::map::RASTER_OBJECT_SELECTED : 0;
			nFlags |= pScene->IsSolidObject( n ) && !pScene->m_Solids[ n ].IsValid() ? xv::map::RASTER_OBJECT_INVALID : 0;
			( *pFlags )[ n ] = nFlags;
		}
		m_Frame.m_pFlags = pFlags;
		m_nGeneration++;
	}
	const std::uint64_t nSettings = settings.m_bUseGroupColors ? 1 : 0;
	if ( pScene && m_nSettingsFor != nSettings )
	{
		m_nSettingsFor = nSettings;
		auto pLines = std::make_shared<std::vector<std::array<std::uint8_t, 3>>>( pScene->GetObjectCount() );
		for ( std::uint32_t n = 0; n < pScene->GetObjectCount(); n++ )
		{
			const QColor color = MapLineColor( *pScene, n, settings, pScene->IsSolidObject( n ) && !pScene->m_Solids[ n ].IsValid() );
			( *pLines )[ n ] = { static_cast<std::uint8_t>( color.red() ), static_cast<std::uint8_t>( color.green() ), static_cast<std::uint8_t>( color.blue() ) };
		}
		m_Frame.m_pLineColors = pLines;
		m_nGeneration++;
	}
	// Sprites face the camera: made again whenever it turns.
	if ( m_pSprites && !m_pSprites->empty() )
	{
		const xv::map::Point_t right = camera.GetRight();
		const xv::map::Point_t up = camera.GetUp();
		if ( !m_Frame.m_pSpriteMesh || right != m_SpritesRight || up != m_SpritesUp )
		{
			m_SpritesRight = right;
			m_SpritesUp = up;
			m_Frame.m_pSpriteMesh = std::make_shared<const xv::map::CMapMesh>( xv::map::BuildSpriteMesh( *m_pSprites, right, up ) );
		}
	}
	MapFrame_t frame = m_Frame;
	frame.m_Camera = camera;
	frame.m_Options = options;
	frame.m_Options.m_bToolTextures = settings.m_bToolTextures;
	frame.m_Options.m_Shade.m_pSky = frame.m_pSky.get();
	frame.m_nGeneration = m_nGeneration;
	return frame;
}
