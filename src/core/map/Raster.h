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
	bool m_bToolTextures = true;
	bool m_bSelectionEdges = true;
	std::array<std::uint8_t, 3> m_SelectionEdge{ 255, 255, 0 };
	std::array<std::uint8_t, 3> m_InvalidEdge{ 255, 0, 0 }; // broken solids' edges, over everything
	// The 3D skybox, drawn first from here (map::SkyboxCamera): its objects behind everything,
	// showing where the world's sky faces are, as the game shows it. Textured views only.
	std::optional<CViewCamera> m_SkyboxCamera;
	Box_t m_SkyboxRoom; // only what reaches into it is drawn in the skybox's pass
};

// Draws into out, sized to the camera's image.
void RenderView( const RasterScene_t &scene, const CViewCamera &camera, const RasterOptions_t &options, texture::CImage &out );

} // namespace xv::map
