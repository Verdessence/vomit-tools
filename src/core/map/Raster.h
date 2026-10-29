//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The 3D view drawn in software: the map's meshes clipped at the near plane, depth
//          tested, shaded by Shade.h, see-through faces last and back to front, then lines.
//          It is the reference the graphics card's renderer is compared with, and what draws
//          where there is no graphics card.
//
//===========================================================================//
#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <cstdint>
#include <span>
#include <vector>

#include "map/Mesh.h"
#include "map/Shade.h"
#include "map/ViewCamera.h"
#include "texture/Image.h"

namespace xv::map
{

// Per object: drawn at all, and selected.
enum RasterObject_t : std::uint8_t
{
	RASTER_OBJECT_SHOWN = 1u << 0,
	RASTER_OBJECT_SELECTED = 1u << 1,
	RASTER_OBJECT_INVALID = 1u << 2,
	RASTER_OBJECT_INSTANCE = 1u << 3, // a func_instance's contents: tinted
	RASTER_OBJECT_SKYBOX = 1u << 4,   // in the 3D skybox's room: drawn again behind the world
	RASTER_OBJECT_EDITED = 1u << 5,   // drawn from the edit mesh: its shape in the meshes as read is old
	RASTER_OBJECT_OVERRIDDEN = 1u << 7, // latest shape is in an independent solid mesh
	RASTER_OBJECT_BOXED = 1u << 6,    // a model that, selected, shows its box rather than its own edges
};

// A material as the renderers use it, from MaterialInfo_t and the textures read for it.
struct RasterMaterial_t
{
	ShadeMaterial_t m_Shade;
	bool m_bTool = false;        // hidden when tool textures are toggled off
	bool m_bNoCull = false;      // both sides drawn
	bool m_bObjectColor = false; // entity boxes: the object's colour, shaded, in every mode
};

struct RasterLine_t
{
	Point_t m_From{};
	Point_t m_To{};
	std::array<std::uint8_t, 3> m_Color{};
	bool m_bOnTop = false;       // drawn over everything (Hammer's selection edges)
};

struct RasterScene_t
{
	std::vector<const CMapMesh *> m_Meshes;            // the map's, then entity boxes and the like
	std::vector<const CMapMesh *> m_NoWireframe;       // of those, the ones whose edges the wireframe view leaves out
	std::vector<const CMapMesh *> m_AsRead;            // of those, the ones from before any edit (edited objects' old shapes)
	std::vector<const CMapMesh *> m_Overridden; // bulk meshes whose painted solids have separate meshes
	std::vector<const CMapMesh *> m_Models;            // of those, the models': a boxed object's edges there aren't the selection's
	std::span<const RasterMaterial_t> m_Materials;     // by the meshes' material indices
	std::span<const std::uint8_t> m_ObjectFlags;       // RasterObject_t bits, by object
	std::span<const std::array<std::uint8_t, 3>> m_ObjectColors; // flat colours, by object
	std::span<const std::array<std::uint8_t, 3>> m_LineColors;   // wireframe colours, by object
	std::span<const RasterLine_t> m_Lines;
};

struct RasterOptions_t
{
	ShadeOptions_t m_Shade;
	std::array<std::uint8_t, 3> m_Background{ 30, 30, 30 };
	bool m_bTransparent = false; // the background left see-through (alpha 0), to draw over something else
	bool m_bToolTextures = true;
	bool m_bSelectionEdges = true;
	std::array<std::uint8_t, 3> m_SelectionEdge{ 255, 255, 0 };
	std::array<std::uint8_t, 3> m_InvalidEdge{ 255, 0, 0 }; // broken solids' edges, over everything
	// The 3D skybox, drawn first from here (map::SkyboxCamera): its objects behind everything,
	// showing where the world's sky faces are, as the game shows it. Textured views only.
	std::optional<CViewCamera> m_SkyboxCamera;
	Box_t m_SkyboxRoom; // only what reaches into it is drawn in the skybox's pass
};

// Whether an object's triangles and lines in pMesh are drawn: it is shown, and pMesh doesn't hold
// an old shape of it (an edited object's, in a mesh as read).
[[nodiscard]] inline bool IsDrawn( const RasterScene_t &scene, const CMapMesh *pMesh, std::uint8_t nFlags )
{
	if ( ( nFlags & RASTER_OBJECT_SHOWN ) == 0 )
	{
		return false;
	}
	return ( ( nFlags & RASTER_OBJECT_EDITED ) == 0 || std::find( scene.m_AsRead.begin(), scene.m_AsRead.end(), pMesh ) == scene.m_AsRead.end() ) &&
		( ( nFlags & RASTER_OBJECT_OVERRIDDEN ) == 0 || std::find( scene.m_Overridden.begin(), scene.m_Overridden.end(), pMesh ) == scene.m_Overridden.end() );
}

// Whether an object's lines in pMesh are drawn as the selection's edges (over everything): it is
// selected, and pMesh isn't a model mesh it is boxed in.
[[nodiscard]] inline bool IsSelectionEdge( const RasterScene_t &scene, const CMapMesh *pMesh, std::uint8_t nFlags )
{
	if ( ( nFlags & RASTER_OBJECT_SELECTED ) == 0 )
	{
		return false;
	}
	return ( nFlags & RASTER_OBJECT_BOXED ) == 0 || std::find( scene.m_Models.begin(), scene.m_Models.end(), pMesh ) == scene.m_Models.end();
}

// Draws into out, sized to the camera's image.
void RenderView( const RasterScene_t &scene, const CViewCamera &camera, const RasterOptions_t &options, texture::CImage &out );

} // namespace xv::map
