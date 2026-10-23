//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What the simple interface calls particle functions and settings, how it takes their
//          values, and a line on what each does. The text lives in a language file (lang_en.txt,
//          in the layout of Valve's resource/*_english.txt) and the input kinds in
//          particle_inputs.txt; this reads them. Anything they leave out gets its key tidied.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "particles/Particles.h"

namespace xv::particles
{

struct PlainName_t
{
	std::string m_szName;    // "Max Particles"
	std::string m_szMeaning; // a line saying what it does; may be empty
};

// How a number is best typed in.
enum class InputKind_t
{
	INPUT_KIND_NUMBER,
	INPUT_KIND_SHARE,              // 0 to 1
	INPUT_KIND_SECONDS,
	INPUT_KIND_DEGREES,
	INPUT_KIND_DEGREES_PER_SECOND,
	INPUT_KIND_PER_SECOND,
	INPUT_KIND_ALPHA,              // 0 to 255
	INPUT_KIND_CONTROL_POINT,      // -1 for none
	INPUT_KIND_COUNT,              // a whole number, not below 0 (-1 where it means none)
};

// One value of a setting that picks from a few, with its name.
struct NamedValue_t
{
	std::string m_szValue; // as the file has it: "0", "1", "DEBRIS"
	std::string m_szName;
};

class CNames
{
public:
	// Reads the language file ("lang" { "Tokens" { key text } }) and the input kinds file
	// ("particle_inputs" { setting kind }). Either may be empty.
	[[nodiscard]] static CNames Parse( std::string_view szLanguage, std::string_view szInputs );

	// A function's name under any name or case the file uses ("Animated Sprites" for
	// render_animated_sprites), and what it does.
	[[nodiscard]] PlainName_t Function( Category_t nCategory, std::string_view szFunction ) const;
	// A setting's name, in a function (szFunction) or of the system itself (szFunction empty).
	[[nodiscard]] PlainName_t Setting( std::string_view szFunction, std::string_view szAttribute ) const;
	// The named values of a setting that picks from a few, in the file's order; empty for others.
	[[nodiscard]] const std::vector<NamedValue_t> &Choices( std::string_view szAttribute ) const;
	// A particle field's name ("Radius" for 3).
	[[nodiscard]] std::string FieldName( int nField ) const;
	[[nodiscard]] InputKind_t Input( std::string_view szAttribute ) const;
	// What follows a number of a kind (" s", "°"); empty for most.
	[[nodiscard]] std::string Unit( InputKind_t nKind ) const;
	// Entries read, for checking the files loaded.
	[[nodiscard]] std::size_t GetTokenCount() const { return m_Tokens.size(); }

private:
	[[nodiscard]] const std::string *Token( std::string_view szKey ) const;

	std::map<std::string, std::string> m_Tokens; // by lower-case key
	std::map<std::string, std::vector<NamedValue_t>> m_Choices;
	std::map<std::string, InputKind_t> m_Inputs;
};

// A key made readable: Title Case, underscores, camel case, switch hints ("0/1", "0=random ...")
// and notes in brackets gone; for a function, without render_.
[[nodiscard]] std::string TidyName( std::string_view szKey, bool bFunction = false );

// Settings that pick one of a particle's fields by number ("output field").
[[nodiscard]] bool IsFieldSetting( std::string_view szAttribute );
inline constexpr int FIELD_COUNT = 22;

// Settings the simple interface tucks away unless they have been changed: when an operator runs,
// the renderers' visibility tests, the system's simulation tuning and the like.
[[nodiscard]] bool IsAdvancedSetting( std::string_view szFunction, std::string_view szAttribute );

} // namespace xv::particles
