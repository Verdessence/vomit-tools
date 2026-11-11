//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A Valve model made into a Garry's Mod playermodel: its source project (Export.h)
//          with GMod's player animations included (m_anm, f_anm or z_anm), its ragdoll kept
//          from the .phy (each piece on its bone, the joints' limits as they were), and the Lua
//          that lists it in GMod's player model menu. Only models already on the ValveBiped
//          skeleton those animations move (HL2's humans, CS:S's players...) can be: others are
//          refused with the bones they lack.
//
//===========================================================================//
#pragma once

#include "Export.h"

namespace xv::model
{

enum class PlayerAnimations_t
{
	PLAYER_ANIMATIONS_MALE,   // m_anm.mdl
	PLAYER_ANIMATIONS_FEMALE, // f_anm.mdl
	PLAYER_ANIMATIONS_ZOMBIE, // z_anm.mdl
};

struct PlayermodelOptions_t
{
	std::string m_szName = "my_player";           // models/player/<name>.mdl, and the Lua file's name
	std::string m_szDisplayName;                  // in the menu; the name when empty
	PlayerAnimations_t m_nAnimations = PlayerAnimations_t::PLAYER_ANIMATIONS_MALE;
	std::string m_szHands = "models/weapons/c_arms_citizen.mdl"; // the first-person arms
};

// The ValveBiped bones a model lacks (empty: it can be a playermodel).
[[nodiscard]] std::vector<std::string> MissingPlayerBones( const CModel &model );
// The include for the animations ("m_anm.mdl").
[[nodiscard]] std::string_view PlayerAnimationModel( PlayerAnimations_t nAnimations );
// The project: model.qc and its SMDs (collision.smd the ragdoll) at the top, and
// lua/autorun/<name>_playermodel.lua. Throws when the model can't be one.
[[nodiscard]] Export_t ExportPlayermodel( std::string_view szPath, const Reader_t &pfnRead, const PlayermodelOptions_t &options );

} // namespace xv::model
