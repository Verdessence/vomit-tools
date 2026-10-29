//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Writes a map back out. The text as read is copied, and only what was edited since is
//          patched into it: an edited solid's changed values (a plane, a texture axis, Hammer++'s
//          vertices, a displacement's start), an entity's changed keys one at a time, its
//          outputs and editor notes, deleted objects cut out with their lines, new ones written
//          out whole where Hammer puts them. Untouched bytes come out as they were, so a map goes
//          back and forth with Hammer with small diffs.
//
//===========================================================================//
#pragma once

#include <string>
#include <string_view>

#include "map/Map.h"

namespace xv::map
{

// The map's text with every edit since it was read (see Map.h's edit marks).
[[nodiscard]] std::string WriteVmf( const CMap &map );

// A map with nothing in it yet, as Hammer++ starts one: its version, view settings, an empty
// world with the keys Hammer++ gives a new one, no cameras and the cordon off. Hammer's line ends.
[[nodiscard]] std::string NewVmf();

// Objects written out whole as Hammer writes them: from their name to their closing brace,
// their lines indented one tab further than szIndent (the indentation of the line they start on).
[[nodiscard]] std::string WriteSolid( const CMap &map, const Solid_t &solid, std::string_view szIndent );
// A group and its editor notes, used when exporting a selection.
[[nodiscard]] std::string WriteGroup( const CMap &map, const Group_t &group, std::string_view szIndent );
// With its solids (those not deleted).
[[nodiscard]] std::string WriteEntity( const CMap &map, const Entity_t &entity, std::string_view szIndent );

} // namespace xv::map
