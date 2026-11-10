//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Source's compiled shader files (.vcs under shaders/fxc/ and shaders/psh, vsh): each
//          combination of the shader's static and dynamic options as Direct3D 9 bytecode.
//          Version 6 (and 5) keep a static combo's dynamic combos in blocks, LZMA packed or not;
//          version 4 (assembly shaders) a dictionary of each combo's bytecode.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace xv::shader
{

struct VcsCombo_t
{
	std::uint32_t m_nStatic = 0;  // the static combo's number
	std::uint32_t m_nDynamic = 0; // the dynamic combo's, within it
	std::vector<std::uint32_t> m_Tokens; // the bytecode
};

struct VcsFile_t
{
	int m_nVersion = 0;
	int m_nTotalCombos = 0;
	int m_nDynamicCombos = 0;
	std::uint32_t m_nFlags = 0;
	std::uint32_t m_nCentroidMask = 0;
	std::uint32_t m_nSourceCrc = 0;
	std::vector<VcsCombo_t> m_Combos; // by static combo, then dynamic
	// Static combos that are another's bytecode: this one and the one it is the same as.
	std::vector<std::pair<std::uint32_t, std::uint32_t>> m_Aliases;

	// A combo's bytecode by its numbers (an alias's own source when it is one); null for none.
	[[nodiscard]] const VcsCombo_t *Find( std::uint32_t nStatic, std::uint32_t nDynamic ) const;
};

// nullopt, with why, when the bytes aren't a .vcs this reads.
[[nodiscard]] std::optional<VcsFile_t> ReadVcs( std::span<const std::byte> bytes, std::string *pError = nullptr );

} // namespace xv::shader
