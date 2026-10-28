//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What Hammer's Face Edit sheet does to a side's texture: its axes aligned to the
//          world or to the face, justified to the face's edges or fitted across it, scaled,
//          shifted, turned, its lightmap scale and its material. Edits to faces are edits to
//          their solids (EditFaces).
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <array>
#include <string_view>
#include <vector>

#include "map/Edit.h"
#include "map/Geometry.h"
#include "map/Map.h"
#include "map/Scene.h"

namespace xv::map
{

// A side of a solid, by their indices in the map.
struct FaceRef_t
{
	std::uint32_t m_nSolid = 0;
	std::uint32_t m_nSide = 0;

	friend bool operator==( const FaceRef_t &, const FaceRef_t & ) = default;
	friend auto operator<=>( const FaceRef_t &, const FaceRef_t & ) = default;
};

// The texture axes Hammer gives a face aligned to the world: the two world axes of the plane the
// face most faces (floors and ceilings x and -y, walls their horizontal axis and -z).
[[nodiscard]] std::pair<Point_t, Point_t> WorldAxes( const Point_t &normal );
// Aligned to the face: v down the face (the world's down, or -y on a floor), u across it.
[[nodiscard]] std::pair<Point_t, Point_t> FaceAxes( const Point_t &normal );
// Whether a side's axes are the world's for its face.
[[nodiscard]] bool IsWorldAligned( const Side_t &side, const Point_t &normal );

enum class Justify_t
{
	JUSTIFY_LEFT,
	JUSTIFY_RIGHT,
	JUSTIFY_TOP,
	JUSTIFY_BOTTOM,
	JUSTIFY_CENTER,
	JUSTIFY_FIT, // scaled so the texture spans the face once, then at its top left
};

// The texel extent of points along the axes (as TexelCoord gives it with no shift): across
// several faces at once when they are treated as one.
struct TexelExtent_t
{
	double m_flMinU = 0.0, m_flMaxU = 0.0, m_flMinV = 0.0, m_flMaxV = 0.0;
};
[[nodiscard]] TexelExtent_t ExtentOf( const Side_t &side, const std::vector<Point_t> &points );

// The shifts (and for Fit the scales) that put the texture, nWidth by nHeight texels, where
// Hammer's buttons put it over the extent.
void Justify( Side_t &side, Justify_t nHow, const TexelExtent_t &extent, int nWidth, int nHeight );
// The axes turned in the face's plane (about normal) so the texture sits at flDegrees, which
// is kept as the side's rotation.
void SetRotation( Side_t &side, const Point_t &normal, double flDegrees );
// The axes aligned anew, keeping the scales and shifts.
void SetAlignment( Side_t &side, const Point_t &normal, bool bWorld );
// The material by its spelling (looked up or added when the edit is made).
void SetMaterial( const CMap &map, Side_t &side, std::string_view szMaterial );

// Hammer's Replace Textures.
enum class ReplaceHow_t
{
	REPLACE_EXACT,      // faces whose material is szFind get szReplace
	REPLACE_PARTIAL,    // faces whose material has szFind in it get szReplace
	REPLACE_SUBSTITUTE, // szFind inside the material's name becomes szReplace
};
struct ReplaceTextures_t
{
	std::string m_szFind;
	std::string m_szReplace;
	ReplaceHow_t m_nHow = ReplaceHow_t::REPLACE_EXACT;
	bool m_bRescale = false; // the scales changed so the new texture covers what the old did
};
// The faces among the objects' (all the map's when objects is empty) whose material matches
// szFind, whole or as a part, ignoring case: Hammer's "mark" action, and what a replace changes.
[[nodiscard]] std::vector<FaceRef_t> FindFaces( const CScene &scene, const std::vector<std::uint32_t> &objects, std::string_view szFind, bool bPartial );
// The edit replacing the faces' materials as how says; pfnSize (when given, for m_bRescale)
// answers a material's texel size by its name. pnChanged counts the faces changed.
[[nodiscard]] MapEdit_t ReplaceTextures( const CScene &scene, const std::vector<FaceRef_t> &faces, const ReplaceTextures_t &how, const std::function<std::array<int, 2>( std::string_view )> &pfnSize, std::size_t *pnChanged );

// An edit of the faces, each as it is now: pfnEdit gets the side and its face's points (wound
// as drawn), so it can measure the face.
[[nodiscard]] MapEdit_t EditFaces( const CScene &scene, const std::vector<FaceRef_t> &faces, const std::function<void( Side_t &, const std::vector<Point_t> & )> &pfnEdit );
// Every face of the objects (a brush entity's solids with it).
[[nodiscard]] std::vector<FaceRef_t> FacesOf( const CScene &scene, const std::vector<std::uint32_t> &objects );

} // namespace xv::map
