//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Read-only VPHY collision solids and their trailing KeyValues text.
//===========================================================================//
#pragma once
#include "Model.h"
#include <span>
namespace xv::model
{
struct Convex_t
{
	int m_nSolid = 0, m_nBone = -1;
	std::vector<CVec3> m_Vertices;
	std::vector<std::uint32_t> m_Indices;
};
struct Physics_t
{
	int m_nChecksum = 0, m_nSolids = 0;
	std::vector<Convex_t> m_Convexes;
	std::string m_szText;
};
[[nodiscard]] std::optional<Physics_t> ReadPhysics( std::span<const std::byte> bytes, std::string *pError = nullptr );
} // namespace xv::model
