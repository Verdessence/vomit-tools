//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A solid as an editable mesh for the brush editing tools: corners, and faces that
//          remember the side they came from. Operations change the mesh; turning it back into
//          solids fits each face's plane, keeps the brush convex (splitting a concave shape
//          into convex pieces, or leaving it as it is, by choice) and gives new faces their
//          source's material aligned to the world.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "map/Brushes.h"
#include "map/Edit.h"
#include "map/Geometry.h"
#include "map/Map.h"
#include "map/Scene.h"

namespace xv::map
{

struct BrushFace_t
{
	std::vector<std::uint32_t> m_Corners; // into the mesh's vertices, wound as FaceGeometry_t's
	std::int32_t m_nSide = -1;            // the solid's side it is (or, when new, came from); -1 for none
	bool m_bNew = false;                  // made by an operation: its source's material, aligned to the world
	bool m_bNodraw = false;               // made between pieces: tools/toolsnodraw
};

class CBrushMesh
{
public:
	std::vector<Point_t> m_Vertices;
	std::vector<BrushFace_t> m_Faces;

	// The solid's corners as the vertex tool numbers them (VerticesOf), and a face per face.
	[[nodiscard]] static CBrushMesh FromGeometry( const SolidGeometry_t &shape );

	// Each edge once, by its corners (lower first), sorted.
	[[nodiscard]] std::vector<std::pair<std::uint32_t, std::uint32_t>> GetEdges() const;
	[[nodiscard]] Point_t FaceCenter( std::size_t nFace ) const;
	// The plane best through a face's corners (Newell's normal, outward by the winding), nullopt
	// when the face has no area.
	[[nodiscard]] std::optional<Plane_t> FacePlane( std::size_t nFace ) const;
	// Empty when indices, coordinates and the closed, consistently wound surface are usable.
	[[nodiscard]] std::string Validate() const;
	// Every corner of the face within flEpsilon of its plane.
	[[nodiscard]] bool IsFlat( std::size_t nFace, double flEpsilon ) const;
	// Every face flat and every corner behind (or on) every face's plane.
	[[nodiscard]] bool IsConvex( double flEpsilon ) const;
	// Corners nothing uses and faces with fewer than three distinct corners gone.
	void Tidy();
};

// Subdivide a face between two of its corners, preserving one side's identity.
[[nodiscard]] bool SplitFace( CBrushMesh &mesh, std::uint32_t nFace, std::uint32_t nFirst, std::uint32_t nSecond );
// Add coplanar subdivisions wherever a plane crosses the surface, keeping one brush.
[[nodiscard]] bool CutRing( CBrushMesh &mesh, const Plane_t &plane );
// Offset a convex face's boundary inward by a world-space distance, leaving a coplanar ring.
[[nodiscard]] bool InsetFace( CBrushMesh &mesh, std::uint32_t nFace, double flDistance );

// What turning a mesh back into solids does with a shape that isn't convex.
enum class BrushConcave_t
{
	BRUSH_CONCAVE_SPLIT, // into convex pieces, the faces between them nodraw
	BRUSH_CONCAVE_KEEP,  // one solid as it is, invalid (Hammer's way with the vertex tool)
};

struct BrushSolids_t
{
	std::vector<Solid_t> m_Solids; // the first takes the source's place; any others are new
	bool m_bConcave = false;       // the shape wasn't convex
	std::string m_szProblem;       // why there are none, if there are none
};

// The mesh as solids like source (its entity, editor notes and sides' materials and alignment).
// New sides have id 0 (for the caller to number).
[[nodiscard]] BrushSolids_t ToSolids( const CMap &map, const Solid_t &source, const CBrushMesh &mesh, BrushConcave_t nConcave );

// The planes of the smallest convex solid round the points (outward); nullopt when they don't
// enclose any volume.
[[nodiscard]] std::optional<std::vector<Plane_t>> ConvexHull( std::span<const Point_t> points, double flEpsilon = 1e-4 );

// A part of a brush, as the brush editing tools select it.
enum class BrushPartKind_t
{
	BRUSH_PART_VERTEX,
	BRUSH_PART_EDGE, // by its index in CBrushMesh::GetEdges
	BRUSH_PART_FACE,
};

struct BrushPart_t
{
	std::uint32_t m_nObject = 0; // the scene's solid object
	BrushPartKind_t m_nKind = BrushPartKind_t::BRUSH_PART_VERTEX;
	std::uint32_t m_nIndex = 0;

	friend bool operator==( const BrushPart_t &, const BrushPart_t & ) = default;
	friend auto operator<=>( const BrushPart_t &, const BrushPart_t & ) = default;
};

// The corners a part moves.
[[nodiscard]] std::vector<std::uint32_t> CornersOf( const CBrushMesh &mesh, BrushPartKind_t nKind, std::uint32_t nIndex );
// Where a part's handle is: the corner, the edge's middle, the face's centre.
[[nodiscard]] Point_t PartPosition( const CBrushMesh &mesh, BrushPartKind_t nKind, std::uint32_t nIndex );

// The edit moving the parts' corners by transform, solid by solid; pAdded gets the objects of
// pieces a split made (grouped with the solid they came from by the caller).
[[nodiscard]] MapEdit_t MoveParts( const CScene &scene, std::span<const BrushPart_t> parts, const Transform_t &transform, BrushConcave_t nConcave, std::vector<std::uint32_t> *pAdded = nullptr, std::string *pProblem = nullptr );

} // namespace xv::map
