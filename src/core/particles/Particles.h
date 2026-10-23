//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Particle systems as PCF files hold them: a DMX document whose root lists
//          DmeParticleSystemDefinition elements. Each system has properties (max_particles,
//          material, radius...), six ordered lists of DmeParticleOperator elements (one per
//          category, each operator named by its functionName) and…
//
//===========================================================================//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dmx/Dmx.h"
#include "kv1/Kv1.h"

// Particle systems as PCF files hold them: a DMX document whose root lists
// DmeParticleSystemDefinition elements. Each system has properties (max_particles, material,
// radius...), six ordered lists of DmeParticleOperator elements (one per category, each operator
// named by its functionName) and DmeParticleChild links to the systems it spawns.
namespace xv::particles
{

inline constexpr std::string_view SYSTEM_TYPE = "DmeParticleSystemDefinition";
inline constexpr std::string_view OPERATOR_TYPE = "DmeParticleOperator";
inline constexpr std::string_view CHILD_TYPE = "DmeParticleChild";
inline constexpr std::string_view DEFINITIONS = "particleSystemDefinitions"; // the root's list of systems
inline constexpr std::string_view CHILDREN = "children";
inline constexpr std::string_view FUNCTION_NAME = "functionName";

enum class Category_t
{
	CATEGORY_EMITTERS,
	CATEGORY_INITIALIZERS,
	CATEGORY_OPERATORS,
	CATEGORY_RENDERERS,
	CATEGORY_FORCES,
	CATEGORY_CONSTRAINTS
};
inline constexpr std::array<Category_t, 6> CATEGORIES{
	Category_t::CATEGORY_EMITTERS,
	Category_t::CATEGORY_INITIALIZERS,
	Category_t::CATEGORY_OPERATORS,
	Category_t::CATEGORY_RENDERERS,
	Category_t::CATEGORY_FORCES,
	Category_t::CATEGORY_CONSTRAINTS,
};
// The system attribute holding the category's list: "emitters", "initializers", ...
[[nodiscard]] std::string_view AttributeName( Category_t nCategory );
[[nodiscard]] std::optional<Category_t> CategoryOf( std::string_view szAttributeName );

// The fade window every operator has ("operator start fadein" and so on), plus functionName.
[[nodiscard]] bool IsCommonOperatorAttribute( std::string_view szName );

// Reading
// The particle systems of a document, as element indices: the root's list in order, then systems
// only reachable as children.
[[nodiscard]] std::vector<std::int32_t> Systems( const dmx::Document_t &document );
[[nodiscard]] std::optional<std::int32_t> FindSystem( const dmx::Document_t &document, std::string_view szName );
[[nodiscard]] bool IsParticleFile( const dmx::Document_t &document );
// The elements an element-array attribute points at (null and external references left out).
[[nodiscard]] std::vector<std::int32_t> References( const dmx::CElement &element, std::string_view szAttribute );
[[nodiscard]] std::string_view FunctionName( const dmx::CElement &op );

struct ChildLink_t
{
	std::int32_t m_nLink = -1;   // the DmeParticleChild element
	std::int32_t m_nSystem = -1; // the system it spawns; -1 if it points nowhere
	float m_flDelay = 0.0f;
};
[[nodiscard]] std::vector<ChildLink_t> Children( const dmx::Document_t &document, std::int32_t nSystem );

// What the game's own files use: every operator function with its attributes, and the system
// properties. A value is the one used most often, which stands in for the default (Valve's editor
// writes every attribute, defaults included).
struct AttributeInfo_t
{
	std::string m_szName;
	dmx::AttrType_t m_nType = dmx::AttrType_t::ATTR_TYPE_UNKNOWN;
	dmx::Value_t m_Value;
};
struct CFunctionInfo
{
	Category_t m_nCategory = Category_t::CATEGORY_OPERATORS;
	std::string m_szName;
	std::vector<AttributeInfo_t> m_Attributes; // in the order the files have them
	int m_nUses = 0;

	[[nodiscard]] const AttributeInfo_t *Attribute( std::string_view szAttributeName ) const;
};

class CSchema
{
public:
	// Collects one file. Call finish() once every file is in.
	void Add( const dmx::Document_t &document );
	void Finish();

	[[nodiscard]] bool Empty() const { return m_Functions.empty(); }
	[[nodiscard]] int GetFileCount() const { return m_nFiles; }
	[[nodiscard]] const CFunctionInfo *Function( Category_t nCategory, std::string_view szName ) const;
	// A category's functions, most used first.
	[[nodiscard]] std::vector<const CFunctionInfo *> Functions( Category_t nCategory ) const;
	// The categories a function is used in.
	[[nodiscard]] std::vector<Category_t> CategoriesOf( std::string_view szName ) const;
	[[nodiscard]] const std::vector<AttributeInfo_t> &GetSystemAttributes() const { return m_System; }
	[[nodiscard]] const AttributeInfo_t *SystemAttribute( std::string_view szName ) const;
	// System names and the file each comes from, for spotting names used twice.
	[[nodiscard]] const std::map<std::string, std::string> &GetSystemFiles() const { return m_SystemFiles; }
	void AddSystemFile( std::string_view szSystemName, std::string_view szFile );

private:
	struct Tally_t
	{
		dmx::AttrType_t m_nType = dmx::AttrType_t::ATTR_TYPE_UNKNOWN;
		std::size_t m_nOrder = 0;
		std::map<std::string, std::pair<int, dmx::Value_t>> m_Values; // by valueText
	};
	struct Collected_t
	{
		Category_t m_nCategory = Category_t::CATEGORY_OPERATORS;
		std::string m_szName;
		int m_nUses = 0;
		std::map<std::string, Tally_t> m_Attributes;
	};
	static std::vector<AttributeInfo_t> MostCommon( const std::map<std::string, Tally_t> &tallies );
	static void Count( std::map<std::string, Tally_t> &tallies, const dmx::CElement &element );

	std::map<std::pair<Category_t, std::string>, Collected_t> m_Collected;
	std::map<std::string, Tally_t> m_SystemTallies;
	std::vector<CFunctionInfo> m_Functions;
	std::vector<AttributeInfo_t> m_System;
	std::map<std::string, std::string> m_SystemFiles;
	int m_nFiles = 0;
};

// Editing. Elements are only ever appended, never removed: something taken out of a list is left
// unreferenced, and Compact() drops it when the file is written.
[[nodiscard]] dmx::Guid_t NewGuid();
// A name not used by any system yet: base, base_2, base_3...
[[nodiscard]] std::string UniqueSystemName( const dmx::Document_t &document, std::string_view szBase );
// An empty particle file: a root with an empty system list, binary encoding 2, format pcf 1.
[[nodiscard]] dmx::Document_t EmptyFile();
// A new system at the end of the root's list, with the schema's properties (or a basic set when
// the schema is empty).
std::int32_t AddSystem( dmx::Document_t &document, std::string_view szName, const CSchema &schema );
// A copy of a system with its own copies of operators and child links (child systems are shared).
std::int32_t DuplicateSystem( dmx::Document_t &document, std::int32_t nSystem, std::string_view szName );
// Takes a system out of the root's list and out of every child link pointing at it.
void RemoveSystem( dmx::Document_t &document, std::int32_t nSystem );
// Renames a system and everything naming it in this file: child links and
// cull_replacement_definition.
void RenameSystem( dmx::Document_t &document, std::int32_t nSystem, std::string_view szName );
// Adds an operator with the function's attributes at position (the end when nullopt).
std::int32_t AddOperator( dmx::Document_t &document, std::int32_t nSystem, Category_t nCategory, const CFunctionInfo &function,
	std::optional<std::size_t> position = std::nullopt );
// Copies an element with no references (an operator) into a system's list at position.
std::int32_t InsertCopy( dmx::Document_t &document, std::int32_t nSystem, std::string_view szAttribute, const dmx::CElement &element,
	std::optional<std::size_t> position = std::nullopt );
std::int32_t AddChild( dmx::Document_t &document, std::int32_t nParent, std::int32_t nChild, float flDelay = 0.0f );
bool RemoveReference( dmx::Document_t &document, std::int32_t nElement, std::string_view szAttribute, std::size_t nPosition );
bool MoveReference( dmx::Document_t &document, std::int32_t nElement, std::string_view szAttribute, std::size_t nFrom, std::size_t nTo );
// Sets an attribute, adding it when missing.
void SetAttribute( dmx::CElement &element, std::string_view szName, dmx::AttrType_t nType, dmx::Value_t value );

// The document without unreferenced elements, indices renumbered. Unchanged when every element is
// reachable from the root.
[[nodiscard]] dmx::Document_t Compact( const dmx::Document_t &document );

// Copy and paste between files. Extract() makes a small particle file holding the systems, their
// operators and every system they spawn; paste() adds such a file's systems. A pasted child system
// whose name already exists links to the existing one; a pasted top-level system whose name exists
// is renamed. Returns the pasted top-level systems.
[[nodiscard]] dmx::Document_t Extract( const dmx::Document_t &document, std::span<const std::int32_t> systems );
std::vector<std::int32_t> Paste( dmx::Document_t &document, const dmx::Document_t &clip );

// Searching
// What a system is, for finding it among many files.
struct SystemSummary_t
{
	std::string m_szName;
	std::string m_szFile; // "particles/fire_01.pcf"
	std::string m_szMaterial;
	std::vector<std::string> m_Functions; // its operators' functionName, in list order
	std::vector<std::string> m_Children;  // the systems it spawns
};
[[nodiscard]] std::vector<SystemSummary_t> Summarize( const dmx::Document_t &document, std::string_view szFile );

struct SearchHit_t
{
	const SystemSummary_t *m_pSystem = nullptr;
	std::string m_szMatched; // what the query was found in: "name", "material", or the operator or child
};
// Systems whose name, material, operators or children contain every word of query (ignoring case),
// those matching by name first.
[[nodiscard]] std::vector<SearchHit_t> Search( std::span<const SystemSummary_t> systems, std::string_view szQuery );

// Checks
struct Issue_t
{
	enum class Severity_t
	{
		SEVERITY_INFO,
		SEVERITY_WARNING,
		SEVERITY_ERROR
	};

	Severity_t m_nSeverity = Severity_t::SEVERITY_WARNING;
	std::string m_szMessage;
	std::int32_t m_nSystem = -1;  // the system the issue is about, or -1 for the whole file
	std::int32_t m_nElement = -1; // the operator or child link, if any
};

struct LintContext_t
{
	// The game's operators and systems; nullptr skips the checks that need them.
	const CSchema *m_pGame = nullptr;
	// This file's path in the game ("particles/x.pcf"), so its own systems aren't reported as also
	// defined elsewhere.
	std::string m_szPath;
	// Whether a file exists in the game, e.g. "materials/effects/fire_cloud1.vmt". Unset skips the check.
	std::function<bool( std::string_view szGamePath )> m_pfnFileExists;
	std::optional<bool> m_ListedInManifest;
};

[[nodiscard]] std::vector<Issue_t> Lint( const dmx::Document_t &document, const LintContext_t &context );

// "effects/fire_cloud1.vmt" or "effects/fire_cloud1" -> "materials/effects/fire_cloud1.vmt"
[[nodiscard]] std::string MaterialPath( std::string_view szMaterial );

// The particle manifest: particles/particles_manifest.txt, a block of "file" entries. A '!' before
// the path ("!particles/muzzleflashes.pcf") precaches every system in the file.
inline constexpr std::string_view MANIFEST_PATH = "particles/particles_manifest.txt";
// The files a manifest lists, normalized and without the '!'.
[[nodiscard]] std::vector<std::string> ManifestFiles( const kv1::CDocument &manifest );
// Adds path to the manifest's block, creating the block in an empty manifest. Returns false if it
// was already listed.
bool AddToManifest( kv1::CDocument &manifest, std::string_view szPath, bool bPrecache = false );

} // namespace xv::particles
