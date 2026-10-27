//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's game data files (.fgd): which entity classes a game has, how the editor
//          draws them (their helpers: colour, size, model, sprite...), and their keys, flags,
//          inputs and outputs. Syntax from the VDC's FGD article, Hammer++'s additions
//          included.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xv::map
{

enum class FgdClassKind_t
{
	FGD_CLASS_KIND_BASE,
	FGD_CLASS_KIND_POINT,
	FGD_CLASS_KIND_SOLID,
	FGD_CLASS_KIND_NPC,
	FGD_CLASS_KIND_KEYFRAME,
	FGD_CLASS_KIND_MOVE,
	FGD_CLASS_KIND_FILTER,
	FGD_CLASS_KIND_EXTEND, // Hammer++: adds to a class defined elsewhere
};

// A helper on a class's line: "studio("models/x.mdl")", "size(-8 -8 -8, 8 8 8)", "halfgridsnap".
struct FgdHelper_t
{
	std::string m_szName;              // lower case
	std::vector<std::string> m_Arguments; // split at commas, trimmed, quotes removed
};

// One entry of a choices or flags list.
struct FgdChoice_t
{
	std::string m_szValue;
	std::string m_szName;
	bool m_bDefault = false;           // flags: ticked on a new entity
	std::string m_szDescription;
};

struct FgdKey_t
{
	std::string m_szName;
	std::string m_szType;              // lower case: "string", "choices", "flags", "studio"...
	std::string m_szDisplayName;
	std::string m_szDefault;
	std::string m_szDescription;
	bool m_bReadOnly = false;
	bool m_bReport = false;
	std::vector<FgdChoice_t> m_Choices;
};

struct FgdIo_t
{
	std::string m_szName;
	std::string m_szType;              // lower case: "void", "integer", "target_destination"...
	std::string m_szDescription;
};

struct FgdClass_t
{
	FgdClassKind_t m_nKind = FgdClassKind_t::FGD_CLASS_KIND_POINT;
	std::string m_szName;              // as written; lookups ignore case
	std::string m_szDescription;
	std::vector<std::string> m_Bases;
	std::vector<FgdHelper_t> m_Helpers;
	std::vector<FgdKey_t> m_Keys;
	std::vector<FgdIo_t> m_Inputs;
	std::vector<FgdIo_t> m_Outputs;
	std::string m_szFile;
	std::size_t m_nLine = 0;

	// The last helper of that name, for helpers that only mean something once (color, size).
	[[nodiscard]] const FgdHelper_t *FindHelper( std::string_view szName ) const;
	[[nodiscard]] std::vector<const FgdHelper_t *> FindHelpers( std::string_view szName ) const;
	[[nodiscard]] const FgdKey_t *FindKey( std::string_view szName ) const;
	[[nodiscard]] bool IsPointClass() const;
};

// Groups for the Filter Control's Auto tab: m_Classes go in m_szName, under m_szParent.
struct FgdAutoVisGroup_t
{
	std::string m_szParent;
	std::string m_szName;
	std::vector<std::string> m_Classes;
};

struct FgdDiagnostic_t
{
	std::string m_szFile;
	std::size_t m_nLine = 0;
	bool m_bError = true;
	std::string m_szMessage;
};

class CFgd
{
public:
	// Text of an included file, by its name as written and the file that names it; the
	// resolved path goes in szPath (for further includes and for messages).
	using Includer_t = std::function<std::optional<std::string>( std::string_view szName, const std::string &szFrom, std::string &szPath )>;

	// Reads a file from disk and every file it includes (from the same folder, ignoring case).
	bool AddFile( const std::filesystem::path &path );
	// Reads text. Classes already known are replaced by later ones of the same name.
	void AddText( std::string_view szText, const std::string &szFile, const Includer_t &pfnInclude = {} );
	// Applies Hammer++'s @ExtendClass entries and works out every class's inherited keys and
	// helpers. Call once all files are added.
	void Finish();

	// The class with its bases folded in (after Finish), or nullptr.
	[[nodiscard]] const FgdClass_t *Find( std::string_view szClassname ) const;
	[[nodiscard]] const std::map<std::string, FgdClass_t> &GetClasses() const { return m_Resolved; }
	[[nodiscard]] const std::vector<FgdDiagnostic_t> &GetDiagnostics() const { return m_Diagnostics; }
	[[nodiscard]] const std::vector<std::string> &GetFiles() const { return m_Files; }
	[[nodiscard]] const std::vector<std::string> &GetMaterialExclusions() const { return m_MaterialExclusions; }
	[[nodiscard]] const std::vector<FgdAutoVisGroup_t> &GetAutoVisGroups() const { return m_AutoVisGroups; }
	[[nodiscard]] std::optional<std::pair<double, double>> GetMapSize() const { return m_MapSize; }
	[[nodiscard]] bool HasErrors() const;

private:
	friend class CFgdParser;

	void AddDiagnostic( std::string szFile, std::size_t nLine, bool bError, std::string szMessage );
	FgdClass_t Resolve( const std::string &szKey, int nDepth ) const;

	std::map<std::string, FgdClass_t> m_Classes;  // by lower-case name, as written
	std::vector<FgdClass_t> m_Extensions;
	std::map<std::string, FgdClass_t> m_Resolved; // by lower-case name, bases folded in
	std::vector<FgdDiagnostic_t> m_Diagnostics;
	std::vector<std::string> m_Files;
	std::vector<std::string> m_MaterialExclusions;
	std::vector<FgdAutoVisGroup_t> m_AutoVisGroups;
	std::optional<std::pair<double, double>> m_MapSize;
	int m_nIncludeDepth = 0;
};

} // namespace xv::map
