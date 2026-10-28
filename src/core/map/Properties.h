//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What Hammer's Object Properties shows for a selection: the entities' class and keys
//          (with SmartEdit, the FGD's names for the keys and their choices), their flags, their
//          outputs and the outputs that reach them. Several entities show what they share;
//          values they disagree on are left blank.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xv::map
{

class CScene;

struct PropertyRow_t
{
	std::string m_szKey;
	std::string m_szName;        // with SmartEdit, the FGD's name for the key; the key otherwise
	std::string m_szValue;       // as stored; empty where the entities differ
	std::string m_szShown;       // as listed: with SmartEdit, a choice's name for its value
	std::string m_szHelp;        // the FGD's description of the key
	std::string m_szType;        // the FGD's type, lower case ("choices", "integer", "angle"...); empty when unknown
	std::vector<std::pair<std::string, std::string>> m_Choices; // choices: value and name
	bool m_bDiffers = false;     // the selected entities don't agree
	bool m_bDefault = false;     // no entity has the key: the FGD's default is shown
};

enum class PropertyFlagState_t
{
	PROPERTY_FLAG_STATE_OFF,
	PROPERTY_FLAG_STATE_ON,
	PROPERTY_FLAG_STATE_MIXED, // on for some of the selected entities
};

struct PropertyFlag_t
{
	std::uint32_t m_nBit = 0;
	std::string m_szName;
	PropertyFlagState_t m_nState = PropertyFlagState_t::PROPERTY_FLAG_STATE_OFF;
};

// One output: from a selected entity (its outputs), or to one (its inputs).
struct PropertyConnection_t
{
	std::string m_szEntity;      // outputs: the target; inputs: the entity it comes from
	std::string m_szOutput;
	std::string m_szInput;
	std::string m_szParameter;
	float m_flDelay = 0.0f;
	bool m_bOnce = false;
	std::uint32_t m_nObject = 0; // the selected entity (outputs) or the sender (inputs)
};

struct PropertySheet_t
{
	std::size_t m_nEntities = 0;
	std::size_t m_nWorldSolids = 0; // selected world brushes: they have no keys of their own
	std::string m_szClass;          // empty when the entities' classes differ
	std::string m_szClassHelp;      // the FGD's description of the class
	bool m_bKnownClass = false;     // the FGD has the class
	std::vector<PropertyRow_t> m_Rows;
	std::vector<PropertyFlag_t> m_Flags;
	std::uint32_t m_nFlagsDefault = 0;   // the flags the game data ticks by default
	std::vector<std::string> m_OutputNames; // the class's outputs, for new ones
	std::vector<PropertyConnection_t> m_Outputs;
	std::vector<PropertyConnection_t> m_Inputs;
	std::string m_szComments;       // the editor's comments, for one entity
};

// The sheet for the selected objects (a brush entity's solids stand for their entity).
[[nodiscard]] PropertySheet_t DescribeSelection( const CScene &scene, const std::vector<std::uint32_t> &objects, bool bSmartEdit );
// The sheet for the world (Hammer's Map Properties): its keys by the FGD's worldspawn class; no
// inputs, and its outputs' object is WORLD_OBJECT.
inline constexpr std::uint32_t WORLD_OBJECT = ~0u;
[[nodiscard]] PropertySheet_t DescribeWorld( const CScene &scene, bool bSmartEdit );

// Whether an output's target names the entity: names compare ignoring case, and a target ending
// in '*' takes every name that starts with the rest, as the game matches them.
[[nodiscard]] bool TargetMatches( std::string_view szTarget, std::string_view szName );

} // namespace xv::map
