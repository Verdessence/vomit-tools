//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The parameters materials take (what kind of value each has, which shaders use it), read
//          from a table, and a material's text checked against them: lines the parser stumbles
//          on, misspelt or unused parameters, values of the wrong kind, textures that aren't there
//          and alpha settings the texture can't back up.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "material/Material.h"

namespace xv::material
{

enum class ParameterKind_t
{
	PARAMETER_KIND_TEXTURE,   // a texture under materials/, without .vtf
	PARAMETER_KIND_MATERIAL,  // another material, without .vmt
	PARAMETER_KIND_BOOL,      // 0 or 1
	PARAMETER_KIND_NUMBER,    // any number
	PARAMETER_KIND_INTEGER,   // a whole number
	PARAMETER_KIND_COLOR,     // "[r g b]" from 0 to 1, "{r g b}" from 0 to 255, or one number
	PARAMETER_KIND_VECTOR,    // one to four numbers, "[a b c]"
	PARAMETER_KIND_TRANSFORM, // "center .5 .5 scale 1 1 rotate 0 translate 0 0"
	PARAMETER_KIND_TEXT,      // anything
	PARAMETER_KIND_SURFACE,   // a surface property
	PARAMETER_KIND_CHOICE,    // one of a few named numbers
};

struct ParameterDef_t
{
	std::string m_szKey; // as written: "$basetexture"
	ParameterKind_t m_nKind = ParameterKind_t::PARAMETER_KIND_TEXT;
	std::string m_szStart;              // the value a newly added one starts with
	std::vector<std::string> m_Shaders; // the shaders that use it, lowercase; empty for every shader
};

class CParameterTable
{
public:
	// Reads "material_parameters" { "shaders" "<names>" "checked" "<names>" "groups" { <name> "<shaders>" }
	// "parameters" { <key> "<kind> [=<start>] <shaders or groups>" } }; see
	// app/data/material_parameters.txt.
	[[nodiscard]] static CParameterTable Parse( std::string_view szText );

	// Case-insensitive.
	[[nodiscard]] const ParameterDef_t *Find( std::string_view szKey ) const;
	[[nodiscard]] const std::vector<ParameterDef_t> &GetParameters() const { return m_Parameters; }
	// The shaders offered, as written.
	[[nodiscard]] const std::vector<std::string> &GetShaders() const { return m_Shaders; }
	[[nodiscard]] bool IsKnownShader( std::string_view szShader ) const;
	// Whether the table lists a shader's parameters fully enough to say one isn't its.
	[[nodiscard]] bool IsChecked( std::string_view szShader ) const;
	// Whether a shader (as a material names it: "LightmappedGeneric_DX9" counts as
	// LightmappedGeneric) uses the parameter.
	[[nodiscard]] static bool IsFor( const ParameterDef_t &parameter, std::string_view szShader );

private:
	std::vector<ParameterDef_t> m_Parameters;
	std::vector<std::string> m_Shaders;
	std::vector<std::string> m_Checked; // lowercase
};

// A shader's name as the table keys it: lowercase, without the _dx9, _hdr or _nobump of a
// fallback name.
[[nodiscard]] std::string ShaderKind( std::string_view szShader );

// Whether a block holds settings the engine reads as the material's own: a fallback or
// conditional block (">=dx90", "LightmappedGeneric_DX8") or a patch's insert/replace.
[[nodiscard]] bool IsParameterBlock( std::string_view szKey );

// What is wrong with a value for its kind, or nullopt when it is fine.
[[nodiscard]] std::optional<std::string> CheckParameterValue( ParameterKind_t nKind, std::string_view szValue );

struct MaterialIssue_t
{
	enum class Severity_t
	{
		SEVERITY_INFO,
		SEVERITY_WARNING,
		SEVERITY_ERROR,
	};

	Severity_t m_nSeverity = Severity_t::SEVERITY_WARNING;
	std::size_t m_nLine = 0; // 1-based; 0 for the material as a whole
	std::string m_szMessage;
};

struct MaterialLintContext_t
{
	// Reads the textures and materials named, to see that they are there and what they hold;
	// unset skips those checks.
	Reader_t m_pfnRead;
	// The game's surface properties, lowercase; empty skips the check.
	std::vector<std::string> m_SurfaceProps;
};

// Everything worth saying about a material's text, in the order of its lines.
[[nodiscard]] std::vector<MaterialIssue_t> LintMaterial( std::string_view szText, const CParameterTable &table, const MaterialLintContext_t &context );

} // namespace xv::material
