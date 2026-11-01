//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Stripper:Source configs brought into a Hammer map: its entities seen as the compiled
//          map has them, the configs run over them (bsp/Stripper, as the BSP tab does), and the
//          result made one edit. What a config adds becomes entities, what it modifies is
//          changed, and what it filters out stays (hidden) so the map loses nothing: each kind
//          in a visgroup of its own under "Stripper: <config>".
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bsp/Stripper.h"
#include "map/Edit.h"

namespace xv::map
{

// The map's entities as vbsp writes them, the world first: their keys, "hammerid" (which configs
// match by) and outputs as "target,input,parameter,delay,times". pIndices gets each one's index in
// map.m_Entities, -1 for the world. Deleted entities are left out. Brush entities have no
// "model" (*n is given only when the map is compiled), so configs matching one by model miss it.
[[nodiscard]] std::vector<bsp::Entity_t> StripperEntitiesOf( const CMap &map, std::vector<int> *pIndices = nullptr );

// Whether a pair of a compiled map's entity is an output: its value splits into the five fields
// of one, the delay and times numbers.
[[nodiscard]] bool IsStripperOutput( std::string_view szValue );

struct StripperImport_t
{
	MapEdit_t m_Edit;
	int m_nAdded = 0;
	int m_nModified = 0;
	int m_nRemoved = 0;
	int m_nGroupId = 0;   // "Stripper: <name>"
	int m_nRemovedId = 0; // its "Removed", hidden (0 when nothing was removed)
	// Each kind's new and changed objects, to select.
	std::vector<std::uint32_t> m_Added;
};

// The configs, in order (the global one first, as Stripper reads them), made an edit of scene.
// szName names the visgroup ("Stripper: <szName>"); a group of that name already there is used
// again, with its Added, Modified and Removed. Empty (IsEmpty) when the configs change nothing.
[[nodiscard]] StripperImport_t ImportStripper( const CScene &scene, const std::vector<bsp::StripperConfig_t> &configs, std::string_view szName );

} // namespace xv::map
