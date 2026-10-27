//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What the 3D views draw beside the map's own mesh, kept up to date with the
//          document: each object's state and colours, the boxes of point entities, and the
//          materials. A frame takes a snapshot of it all, safe to draw on another thread.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <QColor>

#include "map/MapView.h"
#include "map/Raster.h"
#include "map/Scene.h"
#include "map/Sprites.h"

class CMapDocument;

// One picture's worth of everything; copies are cheap and share the data.
struct MapFrame_t
{
	std::shared_ptr<const xv::map::CScene> m_pScene;
	std::shared_ptr<const xv::map::CMapMesh> m_pEntityMesh;
	std::shared_ptr<const xv::map::CMapMesh> m_pPropMesh;
	std::shared_ptr<const xv::map::CMapMesh> m_pOverlayMesh;
	std::shared_ptr<const xv::map::CMapMesh> m_pSpriteMesh; // facing this frame's camera
	std::shared_ptr<const std::vector<xv::map::RasterMaterial_t>> m_pMaterials;
	std::shared_ptr<const void> m_pTextures; // keeps the materials' textures alive
	std::shared_ptr<const xv::sky::CCube> m_pSky;
	std::shared_ptr<const std::vector<std::uint8_t>> m_pFlags;
	std::shared_ptr<const std::vector<std::array<std::uint8_t, 3>>> m_pFlatColors;
	std::shared_ptr<const std::vector<std::array<std::uint8_t, 3>>> m_pLineColors;
	std::vector<xv::map::RasterLine_t> m_Lines;
	xv::map::CViewCamera m_Camera;
	xv::map::RasterOptions_t m_Options;
	std::uint64_t m_nGeneration = 0; // changes whenever anything but the camera does

	[[nodiscard]] xv::map::RasterScene_t MakeScene() const;
};

// The colour an object's lines are drawn in, as Hammer chooses it (2D views and wireframe):
// a visgroup's colour, the entity's, invalid red, or white for the world.
[[nodiscard]] QColor MapLineColor( const xv::map::CScene &scene, std::uint32_t nObject, const MapViewSettings_t &settings, bool bInvalid );

class CMapRenderData
{
public:
	explicit CMapRenderData( const CMapDocument &document );

	// The frame for a camera, from what the document shows now.
	[[nodiscard]] MapFrame_t Snapshot( const xv::map::CViewCamera &camera, const xv::map::RasterOptions_t &options, const MapViewSettings_t &settings );
	// The materials changed (textures arrived): later frames use these, one per material of
	// the map; the entity boxes' own is added after them.
	void SetMaterials( std::shared_ptr<const std::vector<xv::map::RasterMaterial_t>> pMaterials, std::shared_ptr<const void> pTextures );
	[[nodiscard]] std::uint32_t GetEntityMaterial() const;
	// The map's sky, drawn on its sky faces.
	void SetSky( std::shared_ptr<const xv::sky::CCube> pSky );
	// The props placed in the world, and the entities they draw: those show no box.
	void SetProps( std::shared_ptr<const xv::map::CMapMesh> pProps, std::vector<std::uint32_t> objects );
	// The overlays and decals laid on the faces.
	void SetOverlays( std::shared_ptr<const xv::map::CMapMesh> pOverlays );
	// The entities drawn as sprites, tinted by their own colours: those show no box either.
	void SetSprites( std::shared_ptr<const std::vector<xv::map::SpritePlacement_t>> pSprites );

private:
	void Rebuild();
	void BuildEntityBoxes();

	const CMapDocument &m_Document;
	const xv::map::CScene *m_pBuiltFor = nullptr;
	std::uint64_t m_nStateFor = ~0ull;
	std::uint64_t m_nSettingsFor = ~0ull;
	std::uint64_t m_nGeneration = 0;
	MapFrame_t m_Frame;
	std::vector<std::uint32_t> m_PropObjects; // sorted
	std::shared_ptr<const std::vector<xv::map::SpritePlacement_t>> m_pSprites;
	std::vector<std::uint32_t> m_SpriteObjects; // sorted
	xv::map::Point_t m_SpritesRight{};          // the camera the sprite mesh faces
	xv::map::Point_t m_SpritesUp{};
};
