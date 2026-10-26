//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Validated convex model geometry exported as standard VMF brushes.
//===========================================================================//
#pragma once
#include "Export.h"
#include "Phy.h"
namespace xv::model
{
struct VmfOptions_t
{
	enum class HullSource_t
	{
		HULL_SOURCE_AUTO,
		HULL_SOURCE_PHYSICS,
		HULL_SOURCE_DECOMPOSE,
		HULL_SOURCE_CUTOUT
	};
	enum class Texturing_t
	{
		TEXTURING_MODEL,
		TEXTURING_NODRAW,
		TEXTURING_DEV
	};
	HullSource_t m_nHullSource = HullSource_t::HULL_SOURCE_AUTO;
	Texturing_t m_nTexturing = Texturing_t::TEXTURING_NODRAW;
	float m_flGrid = 0, m_flConcavity = 0.01f;
	int m_nMaxHulls = 128, m_nMaxSides = 64, m_nMaxVerticesPerHull = 32;
	int m_nVoxelResolution = 1000000;
	bool m_bNodrawIncompatibleMaterials = true, m_bHammerPlusPlusVertices = true;
	bool m_bDetail = true, m_bReferenceProp = true;
	CVec3 m_vecOffset;
	Selection_t m_Selection;
};
// Physics input is already in Source units; bone transforms are applied here. Throws if no
// brush survives. The lower-level overload also supports independently authored geometry.
[[nodiscard]] Export_t ExportVmf( const CModel &model, std::span<const Convex_t> convexes, const Reader_t &pfnRead = {}, const VmfOptions_t &options = {}, std::string_view szReferenceModel = {} );
[[nodiscard]] Export_t ExportVmf( std::string_view szPath, const Reader_t &pfnRead, const VmfOptions_t &options = {} );
// Closed meshes use CoACD; open or overlapping meshes use bounded surface voxel decomposition.
[[nodiscard]] std::vector<Convex_t> DecomposeModel( const CModel &model, const VmfOptions_t &options = {}, std::vector<std::string> *pNotes = nullptr );
// Recognizes matching triangulated caps of an extrusion, preserving their union and holes.
// No texture tracing or invented thickness. nullopt means the mesh is not such a cutout.
[[nodiscard]] std::optional<std::vector<Convex_t>> ExtractCutoutHulls( const CModel &model, const VmfOptions_t &options = {}, std::string *pError = nullptr );
[[nodiscard]] bool CanUseStaticReference( const CStudioFile &studio );
} // namespace xv::model
