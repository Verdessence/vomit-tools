//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Editing displacements as Hammer's Displacement tab does: made on a face and taken
//          off it, their power and elevation changed, painted (geometry and alpha, with a
//          round brush that falls off to its edge), noise, smoothing, the alpha inverted, sewn
//          to their neighbours and subdivided together; and the triangle tags the game reads
//          for walkability, from the slopes. Every edit is a pure function over the grid, so
//          the views preview them and the editor undoes them as any other edit.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "map/Displacement.h"
#include "map/Edit.h"
#include "map/Faces.h"
#include "map/Geometry.h"
#include "map/Map.h"
#include "map/Scene.h"

namespace xv::map
{

// A fresh, flat displacement on a four-cornered face at nPower (2 to 4), its start at the
// face's lowest corner (then the first in its winding); none for any other face.
[[nodiscard]] std::optional<DispInfo_t> MakeDisplacement( const FaceGeometry_t &face, int nPower );

// The grid resampled to another power, the shape kept as near as the new grid allows.
void SetDisplacementPower( DispInfo_t &disp, int nPower );

// A point's field: what moves it off the flat face (its normal times its distance, plus its
// offset), and that set from a vector (the offset kept; the rest becomes normal and distance).
[[nodiscard]] Point_t DispField( const DispInfo_t &disp, std::size_t nPoint );
void SetDispField( DispInfo_t &disp, std::size_t nPoint, const Point_t &field );

// Hammer's paint brush: a sphere about a point on the surface, its effect full at the centre
// and gone at the radius. Raise/Lower moves points along the axis by the amount (negative
// lowers); Raise To puts them at the amount along it; Smooth moves each toward its neighbours
// (the amount, 0 to 1, how far).
enum class PaintEffect_t
{
	PAINT_EFFECT_RAISE_LOWER,
	PAINT_EFFECT_RAISE_TO,
	PAINT_EFFECT_SMOOTH,
};
struct DispBrush_t
{
	Point_t m_Centre{};
	double m_flRadius = 64.0;
	double m_flAmount = 8.0;
	Point_t m_Axis{ 0, 0, 1 }; // a unit vector; the face's normal is the usual choice
	PaintEffect_t m_nEffect = PaintEffect_t::PAINT_EFFECT_RAISE_LOWER;
	bool m_bSoftEdge = true;   // Hammer's Soft-Edge; Hard-Edge gives every point inside all of it
};
// The displacement on face painted; true when a point changed. Alpha painting takes the amount
// as alpha (0 to 255) added (Raise/Lower), set (Raise To) or averaged in (Smooth).
bool PaintGeometry( DispInfo_t &disp, const FaceGeometry_t &face, const DispBrush_t &brush );
bool PaintAlpha( DispInfo_t &disp, const FaceGeometry_t &face, const DispBrush_t &brush );

// Hammer's Scale attribute: every point's height off its face (its distance) times flScale.
void ScaleDisplacement( DispInfo_t &disp, double flScale );
// Hammer's Noise: every point moved along the axis by a random amount between the two.
void AddNoise( DispInfo_t &disp, double flMin, double flMax, const Point_t &axis, std::uint32_t nSeed );
void InvertAlpha( DispInfo_t &disp );

// The collision flags (Attributes: No Physics, No Hull, No Ray Collision).
inline constexpr int DISP_FLAG_NO_PHYSICS = 2;
inline constexpr int DISP_FLAG_NO_HULL = 4;
inline constexpr int DISP_FLAG_NO_RAY = 8;

// The tags the game reads (9: level enough to build on, 1: steep but walkable, 0: too steep),
// from each triangle's slope, as Hammer writes them.
void TagTriangles( DispInfo_t &disp, const FaceGeometry_t &face );
inline constexpr int DISP_TAG_STEEP = 0;
inline constexpr int DISP_TAG_WALKABLE = 1;
inline constexpr int DISP_TAG_LEVEL = 9;
// The tag of a triangle whose normal is given, by Hammer's thresholds (walkable within about
// 45 degrees of level, buildable within about 37).
[[nodiscard]] int TagOfNormal( const Point_t &normal );

// A face in an edit across several: its displacement (none for a plain face, which
// displacements can be sewn to) and its geometry.
struct DispFace_t
{
	FaceRef_t m_Ref;
	Side_t *m_pSide = nullptr;            // to give it a displacement, or take one away
	DispInfo_t *m_pDisp = nullptr;        // its displacement, when it has one
	const FaceGeometry_t *m_pFace = nullptr;
};
// Sew: where two faces' base edges meet (end to end, or one edge exactly half the other), the
// displacements' points along them are put together: both to their average when the grids are
// alike, the finer grid onto the coarser one otherwise, and onto the flat edge of a plain face.
// True when something moved.
bool SewDisplacements( const std::vector<DispFace_t> &faces );
// Subdivide: the displacements smoothed together as one surface, points shared across sewn
// edges moving as one, the outer boundary staying; nPasses rounds of it.
bool SubdivideDisplacements( const std::vector<DispFace_t> &faces, int nPasses = 3 );

// An edit of the faces' displacements: pfnEdit gets each face (its displacement a copy to
// change, or none) and says whether it changed anything; the solids that did are the edit.
// The tags of every changed displacement are worked out again.
[[nodiscard]] MapEdit_t EditDisplacements( const CScene &scene, const std::vector<FaceRef_t> &faces, const std::function<bool( std::vector<DispFace_t> & )> &pfnEdit );
// The faces among those given that carry a displacement.
[[nodiscard]] std::vector<FaceRef_t> DisplacementFaces( const CScene &scene, const std::vector<FaceRef_t> &faces );

} // namespace xv::map
