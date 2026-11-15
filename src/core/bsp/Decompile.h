//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A compiled map (.bsp) written back as a Hammer map (VMF text), so the map viewer
//          can show it: the brushes the compiler kept (each with the entity that owns it,
//          its materials and texture axes), displacements, the entities and their outputs,
//          and what the compiler took out of the entity list and stored elsewhere (static
//          props, overlays, cubemaps, detail brushes, ladders, areaportals' brushes).
//          What a compile throws away stays lost: hint and skip brushes, visgroups, groups,
//          instances, and which brushes were one before being cut.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace xv::bsp
{

struct DecompileReport_t
{
	int m_nSolids = 0;
	int m_nEntities = 0;
	int m_nDisplacements = 0;
	int m_nStaticProps = 0;
	int m_nOverlays = 0;
	int m_nCubemaps = 0;
	// What couldn't be brought back, in words ("3 brushes had no volume").
	std::vector<std::string> m_Warnings;
};

// The map as VMF text; nullopt (with the reason in pError) when the file isn't a map this
// reads, or has no brushes to read (their lumps are missing or cut short).
[[nodiscard]] std::optional<std::string> DecompileToVmf( std::span<const std::byte> file, DecompileReport_t *pReport = nullptr, std::string *pError = nullptr );

} // namespace xv::bsp
