//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The world's brush faces that face the void: those only the outside of a sealed map
//          can see, which vbsp throws away but which still cost the mapper (and the lighting
//          preview) until they are nodraw. Found by filling a grid of cells from outside the
//          map's bounds through every cell no world solid touches (its instances' included); a
//          face whose outside is reached that way faces the void. When the fill reaches an
//          entity the map leaks, and nothing is found: the inside would be taken for the void.
//
//===========================================================================//
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "map/Edit.h"
#include "map/Scene.h"

namespace xv::map
{

struct VoidFaces_t
{
	std::vector<FaceRef_t> m_Faces;   // world faces facing the void (tool materials and displacements left out)
	bool m_bLeaks = false;            // the outside reaches an entity: m_Faces is empty
	bool m_bNothingInside = false;    // no entity marks the inside: m_Faces is empty
	std::string m_szLeakEntity;       // the first entity reached, and where
	Point_t m_LeakAt{};
	double m_flCellSize = 0.0;        // the grid's cell, in units
};

// Finds the faces. flCellSize 0 picks a cell (16 units, more for big maps, to keep the grid
// within about 256 cells a side).
[[nodiscard]] VoidFaces_t FindVoidFaces( const CScene &scene, double flCellSize = 0.0 );
// The faces given nodraw (TOOLS/TOOLSNODRAW), as one edit; empty when there are none.
[[nodiscard]] MapEdit_t NodrawFaces( const CScene &scene, const std::vector<FaceRef_t> &faces );

} // namespace xv::map
