//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The brush editing tools' selection extras (loops and rings, growing and shrinking,
//          linked and similar parts) and the moves beyond one transform (proportional editing,
//          edge and vertex slides, each part about its own centre).
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "map/BrushMesh.h"

namespace xv::map
{

// One brush's edges round a loop, as Blender's: on from each end through corners where four
// edges meet, across to the edge that shares no face with the last. When neither end goes on (a
// box's corners), the loop is round a face beside the edge instead: nFace, or the first.
[[nodiscard]] std::vector<std::uint32_t> EdgeLoop( const CBrushMesh &mesh, std::uint32_t nEdge, std::optional<std::uint32_t> nFace = std::nullopt );
// The edges across four-sided faces from this one (Loop Cut's ring); the edge alone without one.
[[nodiscard]] std::vector<std::uint32_t> EdgeRing( const CBrushMesh &mesh, std::uint32_t nEdge );
// The faces between an edge's ring: a band round the brush.
[[nodiscard]] std::vector<std::uint32_t> FaceLoop( const CBrushMesh &mesh, std::uint32_t nEdge );
// The faces an edge borders.
[[nodiscard]] std::vector<std::uint32_t> FacesOfEdge( const CBrushMesh &mesh, std::uint32_t nEdge );
// An edge's index in GetEdges, by its corners in either order.
[[nodiscard]] std::optional<std::uint32_t> EdgeIndex( const CBrushMesh &mesh, std::uint32_t nFirst, std::uint32_t nSecond );

enum class BrushLoop_t
{
	BRUSH_LOOP_LOOP, // edges along: EdgeLoop (in face mode, the band FaceLoop)
	BRUSH_LOOP_RING, // edges across: EdgeRing (in face mode, the same band)
};

// The loop or ring through an edge of a solid object, as parts of nKind (the edges' corners,
// the edges, or the band's faces), carried on into the other objects in solids wherever one of
// their edges lies along one of its edges (brushes meeting flush). nFace picks a box's face loop.
[[nodiscard]] std::vector<BrushPart_t> SelectLoop( const CScene &scene, std::span<const std::uint32_t> objects, std::uint32_t nObject, std::uint32_t nEdge, std::optional<std::uint32_t> nFace, BrushLoop_t nLoop, BrushPartKind_t nKind );

// The parts and those beside them (corners joined by an edge, edges sharing a corner, faces
// sharing an edge), on the same brushes.
[[nodiscard]] std::vector<BrushPart_t> GrowParts( const CScene &scene, std::span<const BrushPart_t> parts );
// The parts without those at the selection's edge (beside a part not selected). A selection
// with no edge (a brush's every face) stays.
[[nodiscard]] std::vector<BrushPart_t> ShrinkParts( const CScene &scene, std::span<const BrushPart_t> parts );
// Every part of nKind of the solid objects.
[[nodiscard]] std::vector<BrushPart_t> LinkedParts( const CScene &scene, std::span<const std::uint32_t> objects, BrushPartKind_t nKind );

enum class BrushSimilar_t
{
	BRUSH_SIMILAR_NORMAL,    // faces facing the same way (within a degree); edges along the same line's direction
	BRUSH_SIMILAR_COPLANAR,  // faces on the same plane
	BRUSH_SIMILAR_MATERIAL,  // faces of the same material
	BRUSH_SIMILAR_SIZE,      // faces of the same area, edges of the same length (within 1%)
};

// The parts of the solid objects like one of the selected parts (of the same kind) in that way.
[[nodiscard]] std::vector<BrushPart_t> SimilarParts( const CScene &scene, std::span<const std::uint32_t> objects, std::span<const BrushPart_t> parts, BrushSimilar_t nSimilar );
// Every face of the map's solids (not deleted or hidden by the caller's filter) in one of the
// materials, as parts; the solids' objects in pObjects.
[[nodiscard]] std::vector<BrushPart_t> FacesWithMaterials( const CScene &scene, std::span<const std::string> materials, std::vector<std::uint32_t> *pObjects = nullptr );
// The materials of the selected faces.
[[nodiscard]] std::vector<std::string> MaterialsOf( const CScene &scene, std::span<const BrushPart_t> parts );

enum class BrushFalloff_t
{
	BRUSH_FALLOFF_SMOOTH,
	BRUSH_FALLOFF_SPHERE,
	BRUSH_FALLOFF_ROOT,
	BRUSH_FALLOFF_SHARP,
	BRUSH_FALLOFF_LINEAR,
	BRUSH_FALLOFF_CONSTANT,
};
// The solid objects' meshes, each built once for a call that asks about many parts (a fine
// sphere's mesh takes milliseconds to build, and a selection can hold a thousand of its faces).
class CMeshCache
{
public:
	explicit CMeshCache( const CScene &scene )
		: m_Scene( scene )
	{}

	// The solid object's mesh, or none for anything else.
	[[nodiscard]] const BrushMeshParts_t *Get( std::uint32_t nObject );

private:
	const CScene &m_Scene;
	std::map<std::uint32_t, std::optional<BrushMeshParts_t>> m_Meshes; // a map: its entries stay put
};

// How much of a move a corner flDistance from the selection gets: 1 at the selection, 0 at
// the radius and beyond.
[[nodiscard]] double FalloffWeight( BrushFalloff_t nFalloff, double flDistance, double flRadius );

// How the parts move: each corner to where transform puts it, about one pivot (the transform's)
// or each part about its own centre; and the corners of the objects in nearby (proportional
// editing, when flRadius > 0) the part of the move their distance gives.
struct BrushMoveOptions_t
{
	bool m_bIndividual = false;
	double m_flRadius = 0;
	BrushFalloff_t m_nFalloff = BrushFalloff_t::BRUSH_FALLOFF_SMOOTH;
	std::span<const std::uint32_t> m_Nearby; // solid objects whose corners may follow
};
[[nodiscard]] std::vector<BrushCornerMove_t> PartMoves( const CScene &scene, std::span<const BrushPart_t> parts, const Transform_t &transform, const BrushMoveOptions_t &options );

// A slide: each selected corner along one of its edges (vertices: the edge most along the
// drag; edges: both corners along the face beside them the drag goes into), by how far the drag
// goes along it, never past the next corner. Faces don't slide.
[[nodiscard]] std::vector<BrushCornerMove_t> SlideMoves( const CScene &scene, std::span<const BrushPart_t> parts, Point_t drag );

} // namespace xv::map
