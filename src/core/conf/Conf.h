//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: vomit-tools.nut: settings written as Squirrel (the VScript language) tables, read as
//          data and never run, and written back without losing anything a person wrote.
//
//          general <- { single_window = true, graphics = "vulkan" }
//          map <- { fov = 90, colors = { grid = "#ff00ff00" } }   // map:colors:grid
//          map.fov = 100;                                        // one key on its own
//          local accent = "#e0a040";                             // used later as accent
//          IncludeScript( "keys.nut" );                          // another file, read there
//          binds <- [ [ "map", "Ctrl+Shift+E", "Brush_Extrude" ] ]
//          binds.append( [ "map", "Alt+K", "View_2D_X_Y" ] );
//
//          Tables nest into keys joined by ':'; a table set again adds to the keys it had, and a
//          key set again wins. binds, unbinds and rules are lists whose items all count, across
//          every file. Comments are //, /* */ and #; strings are "..." with \ escapes, or
//          @"..." taken as written (Windows paths). Semicolons and commas between slots are
//          optional, as in Squirrel.
//
//===========================================================================//
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace xv::conf
{

// The keywords whose items all count, in order, rather than the last one winning: source
// (IncludeScript), bind (binds), unbind (unbinds) and rule (rules).
[[nodiscard]] bool IsKeyword( std::string_view szKey );

// A value in a file: where it is and what it holds.
struct Node_t
{
	enum class Kind_t
	{
		KIND_NULL,
		KIND_BOOL,
		KIND_NUMBER,
		KIND_STRING,
		KIND_IDENTIFIER, // a local or const's name
		KIND_TABLE,
		KIND_ARRAY,
	};
	struct Slot_t; // a table's key and value
	Kind_t m_nKind = Kind_t::KIND_NULL;
	std::size_t m_nBegin = 0; // its text in the file
	std::size_t m_nEnd = 0;
	std::string m_szText; // a string's characters, a number or identifier as written, true or false
	std::vector<Slot_t> m_Slots;
	std::vector<Node_t> m_Items;
	std::vector<std::size_t> m_ItemEnds; // where each item ends, its comma included
};
struct Node_t::Slot_t
{
	std::string m_szName;
	std::size_t m_nBegin = 0; // the slot's text: from its name to its value's end, comma included
	std::size_t m_nEnd = 0;
	Node_t m_Value;
};

// One statement at the top of a file.
struct Statement_t
{
	enum class Kind_t
	{
		KIND_ASSIGN,  // a.b.c <- value, or = value
		KIND_LOCAL,   // local name = value, or const name = value
		KIND_APPEND,  // name.append( value )
		KIND_INCLUDE, // IncludeScript( "file" )
	};
	Kind_t m_nKind = Kind_t::KIND_ASSIGN;
	std::vector<std::string> m_Path;
	Node_t m_Value;
	std::size_t m_nBegin = 0; // the statement's text, its ';' included
	std::size_t m_nEnd = 0;
};

// One file, parsed. Text() is exactly what was given until it is edited.
class CConfFile
{
public:
	CConfFile() = default;
	explicit CConfFile( std::string szText );

	[[nodiscard]] const std::string &Text() const { return m_szText; }
	[[nodiscard]] const std::vector<Statement_t> &Statements() const { return m_Statements; }
	// What couldn't be read: the offset and the message.
	[[nodiscard]] const std::vector<std::pair<std::size_t, std::string>> &Problems() const { return m_Problems; }
	// The 1-based line an offset is on.
	[[nodiscard]] int LineOf( std::size_t nOffset ) const;

	// Replaces some of the text and parses the file again.
	void Replace( std::size_t nBegin, std::size_t nEnd, std::string_view szText );

private:
	void Parse();

	std::string m_szText;
	std::vector<Statement_t> m_Statements;
	std::vector<std::pair<std::size_t, std::string>> m_Problems;
	std::vector<std::size_t> m_LineStarts;
};

// Where something came from, for messages: file and 1-based line.
struct Location_t
{
	std::filesystem::path m_File;
	int m_nLine = 0;
};

struct Error_t
{
	Location_t m_Where;
	std::string m_szMessage;
};

// A key's value after locals are put in: "map:fov" and "90". A list's items are in m_Fields
// too, and m_szValue holds them joined by ", ".
struct Entry_t
{
	std::string m_szKey;
	std::string m_szValue;
	std::vector<std::string> m_Fields;
	bool m_bList = false;     // written as an array
	std::size_t m_nFile = 0;  // CConfig::File() index
	std::size_t m_nBegin = 0; // the value's text in that file
	std::size_t m_nEnd = 0;
	Location_t m_Where;
};

// The core file and every file it includes, resolved in order.
class CConfig
{
public:
	// Reads the core file and what it includes from disk. A missing core file is an empty
	// config; a missing included file is an error.
	void Load( const std::filesystem::path &core );
	// Takes the core file's text as given (included files are still read from disk).
	void LoadText( const std::filesystem::path &core, std::string_view szText );

	[[nodiscard]] const std::filesystem::path &GetCorePath() const { return m_CorePath; }
	// Every file read, the core first: the ones to watch for changes.
	[[nodiscard]] std::vector<std::filesystem::path> Files() const;
	[[nodiscard]] const CConfFile &File( std::size_t nFile ) const { return m_Files[ nFile ].m_File; }
	[[nodiscard]] const std::vector<Error_t> &Errors() const { return m_Errors; }

	// Every value in order, keyword items included.
	[[nodiscard]] const std::vector<Entry_t> &Entries() const { return m_Entries; }
	// The last value of a key (keywords aren't kept here: use Keyword()).
	[[nodiscard]] const Entry_t *Find( std::string_view szKey ) const;
	[[nodiscard]] std::optional<std::string> Get( std::string_view szKey ) const;
	[[nodiscard]] std::vector<const Entry_t *> Keyword( std::string_view szKeyword ) const;
	[[nodiscard]] const std::map<std::string, std::string> &Variables() const { return m_Variables; }

	// Sets a key where it was last set (in whichever file), else adds it to the core file.
	// The text is written as true/false or a number when it reads as one, else as a string; a
	// value written as a list stays a list (its items split at commas). Returns whether
	// anything changed.
	bool Set( std::string_view szKey, std::string_view szValue );
	// Replaces the core file's items of a keyword that pfnMatches picks with new ones (each
	// item's fields), in its first list for that keyword (else a new one at the end). Included
	// files are left alone.
	bool ReplaceKeyword( std::string_view szKeyword, const std::function<bool( const std::vector<std::string> &fields )> &pfnMatches, const std::vector<std::vector<std::string>> &items );
	// Writes the files changed since they were read; false (with pError) if one couldn't be.
	bool Save( std::string *pError = nullptr );
	[[nodiscard]] bool IsModified() const;

private:
	struct File_t
	{
		std::filesystem::path m_Path;
		CConfFile m_File;
		bool m_bModified = false;
	};
	void Resolve();
	void ResolveFile( std::size_t nFile, std::vector<std::filesystem::path> &stack, int nDepth );
	void Flatten( std::size_t nFile, const Node_t &node, const std::string &szKey );
	std::optional<Entry_t> MakeEntry( std::size_t nFile, const Node_t &node, std::string szKey );
	std::size_t AddFile( const std::filesystem::path &path, std::string szText );
	[[nodiscard]] Location_t Where( std::size_t nFile, std::size_t nOffset ) const;
	[[nodiscard]] std::vector<std::filesystem::path> IncludePaths( std::string_view szPattern, const std::filesystem::path &from, const Location_t &where );

	std::filesystem::path m_CorePath;
	std::vector<File_t> m_Files; // the core first; files keep their index across a Resolve
	std::vector<Entry_t> m_Entries;
	std::map<std::string, std::string> m_Variables;
	std::vector<Error_t> m_Errors;
	std::set<std::size_t> m_Used; // files reached by the last Resolve
};

// A value written as Squirrel: true/false, a number, or a string ("..." with escapes, or
// @"..." when it holds backslashes).
[[nodiscard]] std::string Literal( std::string_view szValue );
// Reads a value as a boolean: true, yes, on, 1 and false, no, off, 0, in any case.
[[nodiscard]] std::optional<bool> ParseBool( std::string_view szValue );
// Splits a list value at commas, trimming each part; empty parts are dropped.
[[nodiscard]] std::vector<std::string> SplitList( std::string_view szValue );

} // namespace xv::conf
