//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Compiling a map, as Hammer's Run Map does: a sequence of commands (the compilers,
//          a copy, the game) with Hammer's $variables in them; the sequence Hammer's Normal
//          tab means; Hammer++'s and Hammer's own sequence files; and what the compilers'
//          output says (leaks, the problems the Compile Errors article lists), with the leak's
//          pointfile and the portal file read for drawing. Nothing here runs a process: the
//          app and the CLI do that with what this gives them.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "map/Geometry.h"

namespace xv::map
{

// What Hammer's $variables stand for: $file is the map's name without its extension, $path its
// folder, $bspdir where the compiled map goes, $gamedir the folder with gameinfo.txt, $exedir
// the game's own (with hl2.exe or cstrike.sh), and the four programs.
struct CompileVariables_t
{
	std::string m_szFile;
	std::string m_szPath;
	std::string m_szBspDir;
	std::string m_szGameDir;
	std::string m_szExeDir;
	std::string m_szBspExe;
	std::string m_szVisExe;
	std::string m_szLightExe;
	std::string m_szGameExe;
};

// Hammer's text with its $variables filled in: a value with a space in it is quoted, $$ is a
// dollar, and Hammer's backslashes between a variable and a name become the system's separators.
[[nodiscard]] std::string ExpandParameters( std::string_view szText, const CompileVariables_t &variables );
// The text split as a command line: at spaces, quotes keeping their contents together.
[[nodiscard]] std::vector<std::string> SplitArguments( std::string_view szText );

// Hammer's special commands: the rest are programs to run.
enum CompileSpecial_t : int
{
	COMPILE_SPECIAL_RUN = 0,
	COMPILE_SPECIAL_CHANGE_DIR = 256,
	COMPILE_SPECIAL_COPY = 257,
	COMPILE_SPECIAL_DELETE = 258,
	COMPILE_SPECIAL_RENAME = 259,
};

struct CompileCommand_t
{
	bool m_bEnabled = true;
	int m_nSpecial = COMPILE_SPECIAL_RUN;
	std::string m_szRun;   // the program (a $variable, or a path); empty for a special
	std::string m_szParms; // its parameters, with $variables
	bool m_bEnsureFile = false;
	std::string m_szEnsure; // a file that must exist afterwards (Hammer's "ensure file post-exists")
	bool m_bWait = true;    // the sequence waits for it (Hammer doesn't wait for the game)
};

struct CompileSequence_t
{
	std::string m_szName;
	std::vector<CompileCommand_t> m_Commands;
};

// Hammer's Normal tab.
struct NormalOptions_t
{
	enum class Bsp_t
	{
		BSP_NO,
		BSP_NORMAL,
		BSP_ONLY_ENTITIES,
	};
	enum class Vis_t
	{
		VIS_NO,
		VIS_NORMAL,
		VIS_FAST,
	};
	enum class Rad_t
	{
		RAD_NO,
		RAD_NORMAL,
		RAD_FAST,
	};
	Bsp_t m_nBsp = Bsp_t::BSP_NORMAL;
	Vis_t m_nVis = Vis_t::VIS_NORMAL;
	Rad_t m_nRad = Rad_t::RAD_NORMAL;
	bool m_bHdr = false;   // both LDR and HDR lighting
	bool m_bRun = true;    // the game started on the map afterwards
	std::string m_szGameParameters; // added to the game's command line
};
// The sequence the Normal tab's choices mean: the compilers, the copy into $bspdir, the game.
[[nodiscard]] CompileSequence_t SequenceFromNormal( const NormalOptions_t &options );
// Hammer++'s stock sequences (Default, Fast, the full compiles, Only Entities, the game alone).
[[nodiscard]] std::vector<CompileSequence_t> BuiltinSequences();

// Hammer++'s hammerplusplus_sequences.cfg (KeyValues), and the same shape written back.
[[nodiscard]] std::vector<CompileSequence_t> ReadSequencesText( std::string_view szText );
[[nodiscard]] std::string WriteSequencesText( const std::vector<CompileSequence_t> &sequences );
// Hammer's CmdSeq.wc: "Worldcraft Command Sequences", version 0.2 (or 0.1, without the wait flag).
[[nodiscard]] std::vector<CompileSequence_t> ReadCmdSeq( std::span<const std::byte> bytes );

// A command with its variables filled in, ready to run: the program and its arguments, or a
// special's files.
struct ResolvedCommand_t
{
	int m_nSpecial = COMPILE_SPECIAL_RUN;
	std::string m_szProgram;
	std::vector<std::string> m_Arguments;
	std::string m_szFrom; // specials: the folder, the file, the source
	std::string m_szTo;   // copy and rename: the destination
	std::string m_szEnsure;
	bool m_bWait = true;
	std::string m_szShown; // the command line as the log shows it
};
[[nodiscard]] std::vector<ResolvedCommand_t> ResolveSequence( const CompileSequence_t &sequence, const CompileVariables_t &variables );

// What the compilers said that matters: from the Compile Errors article's list.
struct CompileProblem_t
{
	std::string m_szTool;    // vbsp, vvis, vrad, or empty when unknown
	bool m_bError = true;    // else a warning
	std::string m_szMessage; // the line as printed
	std::optional<int> m_nBrushId;   // "Brush 123: ..."
	std::optional<Point_t> m_Point;  // "... at (x, y, z)"
};

struct CompileReport_t
{
	bool m_bLeaked = false;
	std::string m_szLeakEntity;
	std::optional<Point_t> m_LeakAt;
	bool m_bNoPortalFile = false; // vvis found no .prt: the map leaked, or vbsp wasn't run
	std::vector<CompileProblem_t> m_Problems;
};
// Reads the whole log; each tool's lines are told apart by the banners the tools print.
[[nodiscard]] CompileReport_t ParseCompileOutput( std::string_view szLog );

// A leak's pointfile (<map>.lin): one point a line, from where vbsp started to the entity.
[[nodiscard]] std::vector<Point_t> ReadPointfile( std::string_view szText );
// A portal file (<map>.prt, PRT1): each portal's polygon between two leaves.
struct Portal_t
{
	int m_nLeafA = 0;
	int m_nLeafB = 0;
	std::vector<Point_t> m_Points;
};
[[nodiscard]] std::vector<Portal_t> ReadPortalFile( std::string_view szText );

} // namespace xv::map
