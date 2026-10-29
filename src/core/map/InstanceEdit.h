//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Collapse the same resolved instance scenes the viewer draws into editable objects,
//          with name fixups, parameter values and the VDC's instance I/O aliases resolved.
//
//===========================================================================//
#pragma once

#include "map/Edit.h"

namespace xv::map
{

// Atomic: unresolved nested instances report an error and leave the whole selection untouched.
[[nodiscard]] MapEdit_t CollapseInstances( const CScene &scene, const std::vector<std::uint32_t> &objects,
	std::vector<std::uint32_t> *pAdded, std::string *pError = nullptr );
// Reread children after a source VMF is saved, retaining this map's edits and object identities.
[[nodiscard]] std::shared_ptr<CScene> RefreshInstances( const CScene &scene );

} // namespace xv::map
