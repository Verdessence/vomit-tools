//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Valve materials (VMT): the shader a material names and the parameters it ends up with,
//          as a DirectX 9 PC reads it: a patch material's include resolved, the fallback and
//          conditional blocks for that hardware applied. Also runs the material proxies that only
//          depend on time, so a preview can animate.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/Vec3.h"

namespace xv::vfs
{
class CFileSystem;
}

namespace xv::material
{

struct Parameter_t
{
	std::string m_szKey; // as written: "$basetexture"
	std::string m_szValue;
	std::string m_szFrom; // where it came from when not the material's own block: an included material, a fallback block
};

struct Proxy_t
{
	std::string m_szName; // as written: "AnimatedTexture"
	std::vector<std::pair<std::string, std::string>> m_Settings;
};

class CMaterial
{
public:
	std::string m_szShader; // as written: "LightmappedGeneric"
	std::vector<Parameter_t> m_Parameters;
	std::vector<Proxy_t> m_Proxies;
	std::vector<std::string> m_Notes; // how it was read: what it includes, blocks used or left out

	// Lookups are case-insensitive, like the engine's.
	[[nodiscard]] const Parameter_t *Find( std::string_view szKey ) const;
	[[nodiscard]] bool Has( std::string_view szKey ) const { return Find( szKey ) != nullptr; }
	[[nodiscard]] std::string Value( std::string_view szKey ) const; // empty when not set
	[[nodiscard]] float Number( std::string_view szKey, float flDefault ) const;
	[[nodiscard]] bool Flag( std::string_view szKey ) const; // set to something other than 0
	// "[r g b]" (0-1, linear), "{r g b}" (0-255, gamma-space, made linear) or one number for all three.
	[[nodiscard]] CVec3 Color( std::string_view szKey, CVec3 vecDefault = { 1, 1, 1 } ) const;
	// Up to four numbers: "[1 2]", "1 2", or one.
	[[nodiscard]] std::vector<float> Numbers( std::string_view szKey ) const;
	// Adds the parameter or changes its value.
	void Set( std::string_view szKey, std::string_view szValue );
	// The shader's name, lowercase, without a _dx9-style suffix.
	[[nodiscard]] std::string GetShaderKind() const;
};

// Reads a file of the game ("materials/models/x.vmt"); nullopt when it isn't there.
using Reader_t = std::function<std::optional<std::vector<std::byte>>( std::string_view szGamePath )>;

// What a preview takes the hardware to be when choosing fallback blocks.
struct Hardware_t
{
	int m_nDxLevel = 95; // mat_dxlevel: 95 is DirectX 9 with shader model 2.0b or better
	bool m_bHdr = false;
};

// Reads a VMT. A patch material's include is read through pfnRead (when it can't be, the patch's own
// parameters are kept and a note says so). nullopt (with the reason in pError) when the text has no
// shader block.
[[nodiscard]] std::optional<CMaterial> ParseMaterial( std::string_view szText, const Reader_t &pfnRead, Hardware_t hardware = {}, std::string *pError = nullptr );

// Whether a parameter names a texture under materials/ ("$basetexture", "$bumpmap", ...): by its
// key, and a value that isn't a number ($rimmask is a switch, $envmapmask a texture).
[[nodiscard]] bool IsTexture( std::string_view szKey, std::string_view szValue );

enum class TextureKind_t
{
	TEXTURE_KIND_FILE,
	TEXTURE_KIND_MAP_CUBEMAP,   // env_cubemap: the cube map nearest to the surface, built into the map
	TEXTURE_KIND_RENDER_TARGET, // _rt_...: drawn by the engine each frame (water reflections, the screen)
};
[[nodiscard]] TextureKind_t TextureKind( std::string_view szValue );
// The VTF a texture value names: "materials/<value>.vtf", tidied.
[[nodiscard]] std::string TexturePath( std::string_view szValue );

// Reads game paths from the game folder a material on disk sits in (the parent of its materials
// folder), then from the mounted game, if any.
[[nodiscard]] Reader_t DiskReader( const std::filesystem::path &material, const vfs::CFileSystem *pFileSystem );

// Material proxies that need only the time: Sine, LinearRamp, AnimatedTexture, TextureScroll and
// the arithmetic ones. The ones that need an entity or the player are listed instead.
class CProxies
{
public:
	explicit CProxies( const CMaterial &material );

	// The number of frames of the texture in a parameter, for AnimatedTexture.
	using FrameCount_t = std::function<int( std::string_view szParameter )>;
	// The material with the proxies' results at a time (seconds) written into its parameters.
	[[nodiscard]] CMaterial At( float flTime, const FrameCount_t &pfnFrames ) const;
	// Whether any proxy that runs changes something over time.
	[[nodiscard]] bool IsAnimated() const { return m_bAnimated; }
	// Proxies the preview doesn't run, as written.
	[[nodiscard]] const std::vector<std::string> &GetSkipped() const { return m_Skipped; }

private:
	CMaterial m_Material;
	std::vector<std::string> m_Skipped;
	bool m_bAnimated = false;
};

} // namespace xv::material
