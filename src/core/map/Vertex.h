//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's Vertex Tool, the geometry side: a solid's corners and edges as handles, and
//          a solid reshaped by moving some of them, each side's plane rebuilt from its moved
//          face (a face moved out of flat makes an invalid solid, as Hammer lets you).
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "map/Brushes.h"
#include "map/Edit.h"
#include "map/Geometry.h"
#include "map/Map.h"
#include "map/Scene.h"

namespace xv::map
{

// A solid's handles: each corner once, each edge once (by its corners), and each face's
// corners in its winding (by face of the geometry it was made from).
struct SolidVertices_t
{
	std::vector<Point_t> m_Vertices;
	std::vector<std::pair<std::uint32_t, std::uint32_t>> m_Edges;
	std::vector<std::vector<std::uint32_t>> m_FaceVertices;

	[[nodiscard]] Point_t EdgeMiddle( std::size_t nEdge ) const;
};
[[nodiscard]] SolidVertices_t VerticesOf( const SolidGeometry_t &shape );

// The solid with the corners (indices into VerticesOf's list) moved by offset: each side with a
// face gets the plane through its moved corners and Hammer++'s exact vertices; sides without a
// face keep theirs.
[[nodiscard]] Solid_t MoveVertices( const Solid_t &solid, const SolidGeometry_t &shape, const std::vector<std::uint32_t> &vertices, const Point_t &offset );

// A handle on an object of the scene (a solid): the vertex, or the edge (both its corners move).
struct VertexRef_t
{
	std::uint32_t m_nObject = 0;
	bool m_bEdge = false;
	std::uint32_t m_nIndex = 0;

	friend bool operator==( const VertexRef_t &, const VertexRef_t & ) = default;
	friend auto operator<=>( const VertexRef_t &, const VertexRef_t & ) = default;
};
// The edit moving the handles by offset, solid by solid.
[[nodiscard]] MapEdit_t MoveHandles( const CScene &scene, const std::vector<VertexRef_t> &handles, const Point_t &offset );

} // namespace xv::map
