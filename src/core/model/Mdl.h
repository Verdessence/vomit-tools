//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Read-only studio metadata, skeletons and sequence descriptions for the model viewer.
//===========================================================================//
#pragma once
#include "Model.h"
#include <memory>

namespace xv::model
{
struct StudioBone_t
{
	std::string m_szName;
	int m_nParent = -1;
	CVec3 m_vecPosition, m_vecAngles, m_vecPositionScale, m_vecRotationScale;
	std::array<float, 4> m_Quaternion{ 0, 0, 0, 1 };
	CTransform m_PoseToBone;
	int m_nProcedure = 0;
};
struct Event_t
{
	float m_flCycle = 0;
	int m_nEvent = 0;
	bool m_bValidCycle = true;
	std::string m_szName, m_szOptions;
	int m_nType = 0;
};
struct PoseParameter_t
{
	std::string m_szName;
	float m_flStart = 0, m_flEnd = 0, m_flLoop = 0;
};
struct Sequence_t
{
	std::string m_szName, m_szActivity;
	int m_nWeight = 0, m_nFlags = 0;
	float m_flFadeIn = 0, m_flFadeOut = 0;
	std::array<int, 2> m_Size{ 1, 1 }, m_Parameters{ -1, -1 };
	std::array<float, 2> m_Start{}, m_End{};
	std::array<std::vector<float>, 2> m_Keys;
	std::vector<int> m_Animations;
	std::vector<Event_t> m_Events;
	std::vector<float> m_BoneWeights;
	bool m_bHasLayers = false;
};
struct Animation_t
{
	std::string m_szName;
	float m_flFps = 30;
	int m_nFrames = 1, m_nFlags = 0;
	std::size_t m_nOffset = 0;
};
struct Bodygroup_t
{
	std::string m_szName;
	std::vector<std::string> m_Models;
};
struct HitboxSet_t
{
	std::string m_szName;
	std::vector<Hitbox_t> m_Boxes;
};
struct Controller_t
{
	int m_nBone = 0, m_nType = 0, m_nInput = 0;
	float m_flStart = 0, m_flEnd = 0;
};
struct Eyeball_t
{
	std::string m_szName;
	int m_nBone = 0;
	CVec3 m_vecOrigin;
	float m_flRadius = 0;
};
class CStudioFile
{
public:
	std::string m_szPath, m_szName, m_szSurface, m_szKeyValues, m_szAnimationFile;
	int m_nVersion = 0, m_nChecksum = 0, m_nFlags = 0, m_nContents = 0;
	float m_flMass = 0;
	CVec3 m_vecEye, m_vecIllumination, m_vecHullMin, m_vecHullMax, m_vecViewMin, m_vecViewMax;
	std::vector<StudioBone_t> m_Bones;
	std::vector<Bodygroup_t> m_Bodygroups;
	std::vector<HitboxSet_t> m_HitboxSets;
	std::vector<Controller_t> m_Controllers;
	std::vector<Eyeball_t> m_Eyeballs;
	std::vector<std::vector<int>> m_Skins;
	std::vector<std::string> m_Folders, m_Flexes, m_Includes, m_Problems;
	std::vector<PoseParameter_t> m_Parameters;
	std::vector<Animation_t> m_Animations;
	std::vector<Sequence_t> m_Sequences;
	std::shared_ptr<const std::vector<std::byte>> m_pBytes, m_pAnimationBytes;
	std::vector<std::pair<int, int>> m_AnimationBlocks;
};
[[nodiscard]] std::optional<CStudioFile> ReadStudio( std::string_view szPath, const Reader_t &pfnRead, std::string *pError = nullptr );
// Includes are bounded and deduplicated, so cyclic include lists cannot recurse forever.
[[nodiscard]] std::vector<CStudioFile> LoadIncludes( const CStudioFile &file, const Reader_t &pfnRead, std::vector<std::string> &problems );
} // namespace xv::model
