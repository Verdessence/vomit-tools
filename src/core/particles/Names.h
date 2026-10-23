//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Plain names for particle functions and settings, each with a line saying what it does,
//          for the editor's simple interface and its tooltips. Written for this tool from the VDC
//          articles and what the preview does; not Valve's text.
//
//===========================================================================//
#pragma once

#include <string>
#include <string_view>

#include "particles/Particles.h"

namespace xv::particles
{

struct PlainName_t
{
	std::string m_szName;    // "Particles per second"; empty when there is none
	std::string m_szMeaning; // a line saying what it does; may be empty
};

// A function's plain name ("Burst" for emit_instantaneously), under any name or case the file
// uses.
[[nodiscard]] PlainName_t PlainFunction( Category_t nCategory, std::string_view szFunction );
// A setting's plain name, in a function (szFunction) or of the system itself (szFunction empty).
// Settings without one of their own get their key made readable ("start_fadeout_min" -> "Start
// fadeout min") and no meaning.
[[nodiscard]] PlainName_t PlainSetting( std::string_view szFunction, std::string_view szAttribute );
// A list's plain title ("Starting state" for initializers).
[[nodiscard]] std::string_view PlainListTitle( std::string_view szList );

// Settings that pick one of a particle's fields by number ("output field").
[[nodiscard]] bool IsFieldSetting( std::string_view szAttribute );
// A particle field's name ("Radius" for 3); empty for numbers no field has.
[[nodiscard]] std::string_view FieldName( int nField );
inline constexpr int FIELD_COUNT = 22;

// Settings the simple interface tucks away unless they have been changed: when an operator runs,
// the renderers' visibility tests, the system's simulation tuning and the like.
[[nodiscard]] bool IsAdvancedSetting( std::string_view szFunction, std::string_view szAttribute );

} // namespace xv::particles
