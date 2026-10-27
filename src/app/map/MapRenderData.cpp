//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapRenderData.h.
//
//===========================================================================//
#include "MapRenderData.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>

#include "Preferences.h"
#include "kv1/Kv1.h"
#include "map/MapResources.h"
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
		scene.m_Meshes.push_back( &m_pScene->GetMesh() );
		scene.m_AsRead.push_back( &m_pScene->GetMesh() );
		if ( !m_pScene->GetInstanceMesh().m_Vertices.empty() )
		{
			scene.m_Meshes.push_back( &m_pScene->GetInstanceMesh() );
		}
		if ( !m_pScene->GetEditMesh().m_Vertices.empty() )
		{
			scene.m_Meshes.push_back( &m_pScene->GetEditMesh() );
		}
	}
	if ( m_pEntityMesh )
	{
		scene.m_Meshes.push_back( m_pEntityMesh.get() );
	}
	if ( m_pPropMesh )
	{
		scene.m_Meshes.push_back( m_pPropMesh.get() );
		scene.m_Models.push_back( m_pPropMesh.get() );
		if ( !m_bModelEdges )
		{
			scene.m_NoWireframe.push_back( m_pPropMesh.get() );
		}
	}
	if ( m_pOverlayMesh )
	{
		scene.m_Meshes.push_back( m_pOverlayMesh.get() );
	}
	if ( m_pSpriteMesh )
	{
		scene.m_Meshes.push_back( m_pSpriteMesh.get() );
	}
	if ( m_pModelBoxMesh )
	{
		scene.m_Meshes.push_back( m_pModelBoxMesh.get() );
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
// Purpose: The colour Hammer++ gives a group of its own: a bright hue picked from its id, so it
//          stays the same each time the map is opened.
//-----------------------------------------------------------------------------
QColor MapGroupColor( int nGroupId )
{
	const double flHue = std::fmod( static_cast<double>( static_cast<std::uint32_t>( nGroupId ) * 2654435761u ) / 4294967296.0 + 0.1, 1.0 );
	const QColor color = QColor::fromHsvF( static_cast<float>( flHue ), 0.65f, 1.0f );
	return QColor{ color.red(), color.green(), color.blue() }; // as 8-bit RGB, which is what is drawn
}

//-----------------------------------------------------------------------------
// Purpose: A prop's edges are its shape; on a model that only stands for an entity (a player
//          start, an NPC), they are a mass of triangles over everything, and its box says more.
//-----------------------------------------------------------------------------
bool MapSelectedModelEdges( const xv::map::CScene &scene, std::uint32_t nObject )
{
	if ( nObject >= scene.GetObjectCount() || scene.IsSolidObject( nObject ) )
	{
		return true;
	}
	const std::size_t nEntity = scene.GetIndex( nObject );
	if ( scene.m_Entities[ nEntity ].m_szModel.empty() )
	{
		return true;
	}
	switch ( CPreferences::Instance().GetMapPreferences().m_nSelectedModels )
	{
		case MapSelectedModels_t::MAP_SELECTED_MODELS_EDGES: return true;
		case MapSelectedModels_t::MAP_SELECTED_MODELS_BOX: return false;
		case MapSelectedModels_t::MAP_SELECTED_MODELS_PROPS: break;
	}
	const std::string_view szClass = scene.m_Map.m_Entities[ nEntity ].GetClassname();
	return szClass.size() > 5 && xv::kv1::EqualsIgnoreCase( szClass.substr( 0, 5 ), "prop_" );
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's precedence for line colours (the selection's red is the caller's): broken
//          solids, then (with group colours on) the outermost group's colour, the first
//          visgroup's, then the entity's, then the world's.
//-----------------------------------------------------------------------------
std::vector<std::array<std::uint8_t, 3>> MapLineColors( const xv::map::CScene &scene, const MapViewSettings_t &settings )
{
	const MapPreferences_t &preferences = CPreferences::Instance().GetMapPreferences();
	const xv::map::CMap &map = scene.m_Map;
	std::unordered_map<int, std::size_t> groups;
	for ( std::size_t i = 0; i < map.m_Groups.size(); i++ )
	{
		groups.emplace( map.m_Groups[ i ].m_nId, i );
	}
	std::unordered_map<int, QColor> visgroups;
	for ( const xv::map::Visgroup_t &visgroup : map.m_Visgroups )
	{
		visgroups.emplace( visgroup.m_nId, QColor{ visgroup.m_Color.m_nR, visgroup.m_Color.m_nG, visgroup.m_Color.m_nB } );
	}
	// A group's colour, from the group a nested one sits in at the top.
	auto groupColor = [ & ]( int nGroup ) -> std::optional<QColor> {
		int nTop = 0;
		for ( int nDepth = 0; nGroup != 0 && nDepth < 64; nDepth++ )
		{
			const auto it = groups.find( nGroup );
			if ( it == groups.end() )
			{
				break;
			}
			nTop = nGroup;
			nGroup = map.m_Groups[ it->second ].m_Editor.m_nGroup;
		}
		if ( nTop == 0 )
		{
			return std::nullopt;
		}
		if ( preferences.m_nGroupColors == MapGroupColors_t::MAP_GROUP_COLORS_SAVED )
		{
			const xv::map::EditorInfo_t &editor = map.m_Groups[ groups.at( nTop ) ].m_Editor;
			return editor.m_bHasColor ? std::optional<QColor>{ QColor{ editor.m_Color.m_nR, editor.m_Color.m_nG, editor.m_Color.m_nB } } : std::nullopt;
		}
		return MapGroupColor( nTop );
	};
	const bool bGroups = settings.m_bUseGroupColors && preferences.m_nGroupColors != MapGroupColors_t::MAP_GROUP_COLORS_NONE;
	std::vector<std::array<std::uint8_t, 3>> colors( scene.GetObjectCount() );
	for ( std::uint32_t n = 0; n < scene.GetObjectCount(); n++ )
	{
		QColor color = preferences.m_Colors.m_Brush;
		const int nEntity = scene.GetObjectEntity( n );
		const xv::map::EditorInfo_t &editor = nEntity >= 0 ? map.m_Entities[ static_cast<std::size_t>( nEntity ) ].m_Editor : scene.GetObjectEditor( n );
		std::optional<QColor> grouped = bGroups ? groupColor( editor.m_nGroup ) : std::nullopt;
		if ( scene.IsSolidObject( n ) && !scene.m_Solids[ scene.GetIndex( n ) ].IsValid() )
		{
			color = preferences.m_Colors.m_Invalid;
		}
		else if ( grouped )
		{
			color = *grouped;
		}
		else if ( settings.m_bUseGroupColors && !editor.m_Visgroups.empty() && visgroups.contains( editor.m_Visgroups.front() ) )
		{
			color = visgroups.at( editor.m_Visgroups.front() );
		}
		else if ( nEntity >= 0 )
		{
			const xv::map::EntityVisual_t &visual = scene.m_Entities[ static_cast<std::size_t>( nEntity ) ];
			color = visual.m_bClassColor ? QColor{ visual.m_Color.m_nR, visual.m_Color.m_nG, visual.m_Color.m_nB } : preferences.m_Colors.m_Entity;
		}
		colors[ n ] = { static_cast<std::uint8_t>( color.red() ), static_cast<std::uint8_t>( color.green() ), static_cast<std::uint8_t>( color.blue() ) };
	}
	return colors;
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
	return m_nEntityMaterial;
}

//-----------------------------------------------------------------------------
// Purpose: What depends only on the scene: the entity boxes, the flat colours, and plain
//          materials until textures come.
//-----------------------------------------------------------------------------
void CMapRenderData::Rebuild()
{
	const xv::map::CScene *pScene = m_Document.GetScene();
	if ( pScene && m_pBuiltFor && pScene->m_pMesh.get() == m_pBuiltMesh )
	{
		Refresh();
		return;
	}
	m_pBuiltFor = pScene;
	m_pBuiltMesh = pScene ? pScene->m_pMesh.get() : nullptr;
	m_nEntityMaterial = pScene ? static_cast<std::uint32_t>( pScene->m_Materials.size() ) + MAP_MATERIAL_RESERVE : 0;
	m_nStateFor = ~0ull;
	m_LineColorsFor = {};
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
	m_PropBounds.clear();
	m_pSprites.reset();
	m_pReceivedSprites.reset();
	m_SpriteObjects.clear();
	BuildEntityBoxes();

	auto pMaterials = std::make_shared<std::vector<xv::map::RasterMaterial_t>>( m_nEntityMaterial + 1 );
	pMaterials->back().m_bObjectColor = true;
	m_Frame.m_pMaterials = pMaterials;

	UpdateFlatColors();
}

//-----------------------------------------------------------------------------
// Purpose: The same map, edited: the frame takes the new scene, and what is worked out per object
//          is made again; the textures, props, sprites and overlays stay.
//-----------------------------------------------------------------------------
void CMapRenderData::Refresh()
{
	m_pBuiltFor = m_Document.GetScene();
	m_Frame.m_pScene = m_Document.GetSharedScene();
	m_nStateFor = ~0ull;
	m_LineColorsFor = {};
	BuildEntityBoxes();
	UpdateFlatColors();
	m_nGeneration++;
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's flat colour for each object: the solid's own editor colour, or the
//          entity's (the preferences' for classes without one); sprites take their tint, which
//          they are multiplied by.
//-----------------------------------------------------------------------------
void CMapRenderData::UpdateFlatColors()
{
	const xv::map::CScene *pScene = m_Document.GetScene();
	m_nFlatFor = CPreferences::Instance().GetMapRevision();
	if ( !pScene )
	{
		return;
	}
	const QColor entity = CPreferences::Instance().GetMapPreferences().m_Colors.m_Entity;
	auto pFlat = std::make_shared<std::vector<std::array<std::uint8_t, 3>>>( pScene->GetObjectCount() );
	for ( std::uint32_t n = 0; n < pScene->GetObjectCount(); n++ )
	{
		xv::map::Color_t color = pScene->GetObjectEditor( n ).m_Color;
		if ( !pScene->IsSolidObject( n ) )
		{
			const xv::map::EntityVisual_t &visual = pScene->m_Entities[ pScene->GetIndex( n ) ];
			color = visual.m_bClassColor ? visual.m_Color : xv::map::Color_t{ static_cast<std::uint8_t>( entity.red() ), static_cast<std::uint8_t>( entity.green() ), static_cast<std::uint8_t>( entity.blue() ) };
		}
		( *pFlat )[ n ] = { color.m_nR, color.m_nG, color.m_nB };
	}
	for ( const xv::map::SpritePlacement_t &sprite : m_pSprites ? *m_pSprites : std::vector<xv::map::SpritePlacement_t>{} )
	{
		if ( sprite.m_nObject < pFlat->size() )
		{
			( *pFlat )[ sprite.m_nObject ] = sprite.m_Tint;
		}
	}
	m_Frame.m_pFlatColors = pFlat;
	m_nGeneration++;
}

//-----------------------------------------------------------------------------
// Purpose: The boxes of point entities, but for those drawn as their model, sprite or
//          instance.
//-----------------------------------------------------------------------------
void CMapRenderData::BuildEntityBoxes()
{
	const xv::map::CScene *pScene = m_Document.GetScene();
	auto pEntities = std::make_shared<xv::map::CMapMesh>();
	// func_instances whose file was read show what it holds instead.
	std::vector<std::uint32_t> instances;
	if ( pScene )
	{
		for ( const xv::map::PlacedInstance_t &placed : pScene->m_Instances )
		{
			instances.push_back( placed.m_nObject );
		}
	}
	std::ranges::sort( instances );
	for ( std::size_t e = 0; pScene && e < pScene->m_Entities.size(); e++ )
	{
		const std::uint32_t nObject = pScene->EntityObject( e );
		if ( pScene->m_Entities[ e ].m_bPoint && !std::ranges::binary_search( instances, nObject ) && !std::ranges::binary_search( m_PropObjects, nObject ) && !std::ranges::binary_search( m_SpriteObjects, nObject ) )
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
void CMapRenderData::SetMaterials( std::shared_ptr<const std::vector<xv::map::RasterMaterial_t>> pMaterials, std::shared_ptr<const void> pTextures, std::uint32_t nEntityMaterial )
{
	if ( m_Document.GetScene() != m_pBuiltFor )
	{
		Rebuild();
	}
	if ( nEntityMaterial != m_nEntityMaterial )
	{
		m_nEntityMaterial = nEntityMaterial;
		BuildEntityBoxes();
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
	m_PropBounds.assign( m_PropObjects.size(), {} );
	if ( m_Frame.m_pPropMesh )
	{
		for ( const xv::map::MeshVertex_t &vertex : m_Frame.m_pPropMesh->m_Vertices )
		{
			const auto it = std::ranges::lower_bound( m_PropObjects, vertex.m_nObject );
			if ( it != m_PropObjects.end() && *it == vertex.m_nObject )
			{
				m_PropBounds[ static_cast<std::size_t>( it - m_PropObjects.begin() ) ].Add( xv::map::ToPoint( vertex.m_vecPosition ) );
			}
		}
	}
	BuildEntityBoxes();
	m_nStateFor = ~0ull; // which selected entities are models changed
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
// Purpose:
//-----------------------------------------------------------------------------
void CMapRenderData::SetSprites( std::shared_ptr<const std::vector<xv::map::SpritePlacement_t>> pSprites )
{
	if ( m_Document.GetScene() != m_pBuiltFor )
	{
		Rebuild();
	}
	if ( pSprites == m_pReceivedSprites )
	{
		return;
	}
	m_pReceivedSprites = std::move( pSprites );
	ApplySprites();
}

//-----------------------------------------------------------------------------
// Purpose: The sprites drawn, unless Preferences > Maps turns them off (their entities then show
//          their boxes). Their tints go in as their entities' flat colours.
//-----------------------------------------------------------------------------
void CMapRenderData::ApplySprites()
{
	m_bSpritesShown = CPreferences::Instance().GetMapPreferences().m_bSprites;
	m_pSprites = m_bSpritesShown ? m_pReceivedSprites : nullptr;
	m_SpriteObjects.clear();
	for ( const xv::map::SpritePlacement_t &sprite : m_pSprites ? *m_pSprites : std::vector<xv::map::SpritePlacement_t>{} )
	{
		m_SpriteObjects.push_back( sprite.m_nObject );
	}
	std::ranges::sort( m_SpriteObjects );
	UpdateFlatColors();
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
MapFrame_t CMapRenderData::GetShared()
{
	if ( m_Document.GetScene() != m_pBuiltFor )
	{
		Rebuild();
	}
	return m_Frame;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::shared_ptr<const std::vector<std::array<std::uint8_t, 3>>> CMapRenderData::GetLineColors( const MapViewSettings_t &settings )
{
	const xv::map::CScene *pScene = m_Document.GetScene();
	if ( pScene != m_pBuiltFor )
	{
		Rebuild();
	}
	if ( !pScene )
	{
		return nullptr;
	}
	const std::pair<bool, std::uint64_t> key{ settings.m_bUseGroupColors, CPreferences::Instance().GetMapRevision() };
	if ( !m_Frame.m_pLineColors || key != m_LineColorsFor )
	{
		m_LineColorsFor = key;
		m_Frame.m_pLineColors = std::make_shared<const std::vector<std::array<std::uint8_t, 3>>>( MapLineColors( *pScene, settings ) );
		m_nGeneration++;
	}
	return m_Frame.m_pLineColors;
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
	if ( pScene && ( m_nStateFor != m_Document.GetStateGeneration() || m_nStateRevision != CPreferences::Instance().GetMapRevision() ) )
	{
		m_nStateFor = m_Document.GetStateGeneration();
		m_nStateRevision = CPreferences::Instance().GetMapRevision();
		auto pFlags = std::make_shared<std::vector<std::uint8_t>>( pScene->GetObjectCount(), 0 );
		for ( const xv::map::PlacedInstance_t &placed : pScene->m_Instances )
		{
			( *pFlags )[ placed.m_nObject ] = xv::map::RASTER_OBJECT_INSTANCE;
		}
		if ( pScene->m_Skybox3D )
		{
			for ( const std::uint32_t n : pScene->m_Skybox3D->m_Objects )
			{
				( *pFlags )[ n ] |= xv::map::RASTER_OBJECT_SKYBOX;
			}
		}
		for ( const std::uint32_t nSolid : pScene->m_EditedSolids )
		{
			( *pFlags )[ pScene->SolidObject( nSolid ) ] |= xv::map::RASTER_OBJECT_EDITED;
		}
		for ( std::uint32_t n = 0; n < pScene->GetObjectCount(); n++ )
		{
			std::uint8_t nFlags = ( *pFlags )[ n ] | ( m_Document.IsShown( n ) ? xv::map::RASTER_OBJECT_SHOWN : 0 );
			nFlags |= m_Document.IsSelected( n ) ? xv::map::RASTER_OBJECT_SELECTED : 0;
			nFlags |= pScene->IsSolidObject( n ) && !pScene->m_Solids[ pScene->GetIndex( n ) ].IsValid() ? xv::map::RASTER_OBJECT_INVALID : 0;
			( *pFlags )[ n ] = nFlags;
		}
		// Selected models that show their box: flagged, and their boxes' edges in a mesh of their own.
		auto pBoxes = std::make_shared<xv::map::CMapMesh>();
		for ( const std::uint32_t n : m_Document.GetSelection() )
		{
			const auto it = std::ranges::lower_bound( m_PropObjects, n );
			if ( it != m_PropObjects.end() && *it == n && !MapSelectedModelEdges( *pScene, n ) )
			{
				( *pFlags )[ n ] |= xv::map::RASTER_OBJECT_BOXED;
				const xv::map::Box_t &model = m_PropBounds[ static_cast<std::size_t>( it - m_PropObjects.begin() ) ];
				AddBox( *pBoxes, model.IsEmpty() ? m_Document.GetObjectBounds( n ) : model, n );
			}
		}
		pBoxes->m_Indices.clear();
		m_Frame.m_pModelBoxMesh = pBoxes->m_Lines.empty() ? nullptr : std::move( pBoxes );
		m_Frame.m_pFlags = pFlags;
		m_nGeneration++;
	}
	if ( pScene )
	{
		static_cast<void>( GetLineColors( settings ) );
		if ( CPreferences::Instance().GetMapPreferences().m_bSprites != m_bSpritesShown )
		{
			ApplySprites();
		}
		if ( m_nFlatFor != CPreferences::Instance().GetMapRevision() )
		{
			UpdateFlatColors();
		}
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
	// Hammer++ draws the 3D skybox by default; Alt+V turns it off.
	frame.m_Options.m_SkyboxCamera.reset();
	if ( settings.m_bSkybox3D && pScene && pScene->m_Skybox3D )
	{
		frame.m_Options.m_SkyboxCamera = xv::map::SkyboxCamera( camera, *pScene->m_Skybox3D );
		frame.m_Options.m_SkyboxRoom = pScene->m_Skybox3D->m_Room;
	}
	frame.m_nGeneration = m_nGeneration;
	return frame;
}
