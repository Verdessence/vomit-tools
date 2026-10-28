//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's Check for Problems: what is wrong with a map that the compilers or the game
//          would trip over (broken solids, empty brush entities, keys the game data doesn't
//          know, no player start, outputs to nothing), and the fixes Hammer offers. Also the
//          Entity Report's filter and Find Entities.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "map/Edit.h"
#include "map/Scene.h"

namespace xv::map
{

enum class ProblemKind_t
{
	PROBLEM_INVALID_SOLID,       // not convex, or a plane that can't be read
	PROBLEM_EMPTY_SOLID_ENTITY,  // a brush entity with no solids (fix: delete it)
	PROBLEM_UNUSED_KEY,          // a key the class doesn't have (fix: remove it)
	PROBLEM_UNKNOWN_CLASS,       // no such class in the game data
	PROBLEM_POINT_CLASS_SOLIDS,  // a point class with solids
	PROBLEM_SOLID_CLASS_POINT,   // a brush class without
	PROBLEM_MISSING_TARGET,      // an output to a name nothing has
	PROBLEM_NO_PLAYER_START,     // no info_player_* entity
};

struct Problem_t
{
	ProblemKind_t m_nKind = ProblemKind_t::PROBLEM_INVALID_SOLID;
	std::optional<std::uint32_t> m_nObject; // the solid or entity, when there is one
	std::string m_szMessage;                // as Hammer words it
	std::string m_szKey;                    // PROBLEM_UNUSED_KEY: the key
	bool m_bFixable = false;
};

// Every problem, in object order; pfnInclude (when given) leaves out objects it says no to
// (Hammer's "check visible parts of the map only"). Without game data the class checks are
// skipped.
[[nodiscard]] std::vector<Problem_t> CheckMap( const CScene &scene, const std::function<bool( std::uint32_t )> &pfnInclude );
// Hammer's Fix: an empty edit for what can't be fixed.
[[nodiscard]] MapEdit_t FixProblem( const CScene &scene, const Problem_t &problem );
// Fix all of the kind: the fixes together in one edit.
[[nodiscard]] MapEdit_t FixProblems( const CScene &scene, const std::vector<Problem_t> &problems, ProblemKind_t nKind );

// The Entity Report's filter.
struct EntityFilter_t
{
	enum class Kind_t
	{
		KIND_ALL,
		KIND_POINT,
		KIND_BRUSH,
	};
	Kind_t m_nKind = Kind_t::KIND_ALL;
	std::string m_szClass;    // by class: a part of the class name, ignoring case
	std::string m_szKey;      // by key/value: the key (any when empty)...
	std::string m_szValue;    // ...and the value
	bool m_bExact = false;    // the value whole (else a part)
};
// The entities' object numbers that pass, in map order.
[[nodiscard]] std::vector<std::uint32_t> FilterEntities( const CScene &scene, const EntityFilter_t &filter );
// Find Entities: the entities whose targetname is szName exactly (ignoring case).
[[nodiscard]] std::vector<std::uint32_t> FindEntities( const CScene &scene, std::string_view szName );

} // namespace xv::map
