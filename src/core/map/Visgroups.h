//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's automatic visgroups (the Filter Control's Auto tab): the map's objects
//          sorted by kind, from their classes and tool textures, plus the groups the FGDs add
//          with @AutoVisGroup. None of it is saved in the map; it is worked out on opening.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xv::map
{

class CScene;

struct AutoVisgroup_t
{
	std::string m_szName;
	std::vector<AutoVisgroup_t> m_Children;
	std::vector<std::uint32_t> m_Objects; // its own members, sorted; its children's are theirs
};

// The tree under "Auto". Groups that end up empty are left out, as Hammer leaves them out.
[[nodiscard]] AutoVisgroup_t BuildAutoVisgroups( const CScene &scene );
// The objects of a group and of every group inside it, sorted.
[[nodiscard]] std::vector<std::uint32_t> AutoVisgroupMembers( const AutoVisgroup_t &group );

} // namespace xv::map
