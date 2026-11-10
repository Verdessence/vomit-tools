//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Convex brush merging, vertex welding and polygon prisms for Hammer's editing tools.
//===========================================================================//
#pragma once
#include "map/BrushMesh.h"
#include "map/Primitives.h"
namespace xv::map
{
[[nodiscard]] bool PartsWouldMerge( const CScene &scene, std::span<const BrushPart_t> parts, const Transform_t &transform );
[[nodiscard]] MapEdit_t MergeBrushVertices( const CScene &scene, std::span<const BrushPart_t> parts, bool bAtCenter, double flDistance, BrushConcave_t nConcave, std::string *pProblem );
[[nodiscard]] MapEdit_t MergeBrushes( const CScene &scene, const std::vector<std::uint32_t> &objects, std::string *pProblem );
[[nodiscard]] std::optional<Solid_t> MakePolygonPrism( const CMap &map, std::span<const Point_t> points, int nAxis, double flDepth, const FaceDefaults_t &faces, std::string *pProblem );
} // namespace xv::map
