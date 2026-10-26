//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Portable model/source exports and dependency collection; compiling stays external.
//===========================================================================//
#pragma once
#include "Anim.h"
#include "Edit.h"
#include <span>
namespace xv::model
{
struct Export_t
{
	// Relative, portable paths below the chosen output directory.
	std::map<std::string, Bytes_t> m_Files;
	std::vector<std::string> m_Notes;
};
struct Clip_t
{
	int m_nSequence = -1, m_nFirst = 0, m_nLast = -1;
	float m_flFps = 0;
	bool m_bAllowBaseBlend = false;
	std::vector<float> m_Parameters;
};
// Local XYZ rotations in radians, for SMD and source QC attachment angles.
[[nodiscard]] CVec3 EulerAngles( const CTransform &transform );
[[nodiscard]] std::string ExportSmd( const CModel &model );
// Name-based retargeting preserves the target bind pose and applies source-local motion deltas.
[[nodiscard]] std::string ExportAnimationSmd( const CStudioFile &source, const CModel &target, const Clip_t &clip );
[[nodiscard]] Export_t ExportObj( const CModel &model, const Reader_t &pfnRead = {} );
[[nodiscard]] Export_t ExportGltf( const CModel &model, const Reader_t &pfnRead = {}, const CStudioFile *pSource = nullptr, const Clip_t &clip = {} );
// An editable source project, not a lossless decompile. The original compiled files stay intact.
struct SourceOptions_t
{
	std::string m_szModelName = "edited/model.mdl";
	std::string m_szMeshSource, m_szCollisionSource;
	const CModel *m_pReplacement = nullptr;
	int m_nBodygroup = 0, m_nBodyModel = 0;
	bool m_bAppendBodygroup = false;
	const CModel *m_pCollision = nullptr;
	const CStudioFile *m_pAnimationSource = nullptr;
	Clip_t m_Clip;
};
[[nodiscard]] Export_t ExportSourceProject( std::string_view szPath, const Reader_t &pfnRead, const SourceOptions_t &options = {} );
// Compiled companions, included models, material patches and their referenced textures.
[[nodiscard]] Export_t CollectModelFiles( std::string_view szPath, const Reader_t &pfnRead );
// File names from untrusted model/material data must not escape an export directory.
[[nodiscard]] bool IsExportPath( std::string_view szPath );
} // namespace xv::model
