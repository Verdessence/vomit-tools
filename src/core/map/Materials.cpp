//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Materials.h. Transparency is read as the material preview reads it, so the two
//          agree.
//
//===========================================================================//
#include "Materials.h"

#include <array>
#include <utility>

#include "vfs/Vfs.h"

namespace xv::map
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MaterialInfo_t DescribeMaterial( const material::CMaterial &material )
{
	static constexpr std::array<std::pair<std::string_view, MaterialCompile_t>, 21> COMPILE_KEYS = { {
		{ "%compilenodraw", MATERIAL_COMPILE_NODRAW },
		{ "%compileclip", MATERIAL_COMPILE_CLIP },
		{ "%compileplayerclip", MATERIAL_COMPILE_PLAYERCLIP },
		{ "%playerclip", MATERIAL_COMPILE_PLAYERCLIP }, // what the game's own toolsplayerclip sets
		{ "%compilenpcclip", MATERIAL_COMPILE_NPCCLIP },
		{ "%npcclip", MATERIAL_COMPILE_NPCCLIP },
		{ "%compiletrigger", MATERIAL_COMPILE_TRIGGER },
		{ "%compilehint", MATERIAL_COMPILE_HINT },
		{ "%compileskip", MATERIAL_COMPILE_SKIP },
		{ "%compileareaportal", MATERIAL_COMPILE_AREAPORTAL },
		{ "%compilesky", MATERIAL_COMPILE_SKY },
		{ "%compile2dsky", MATERIAL_COMPILE_2DSKY },
		{ "%compileorigin", MATERIAL_COMPILE_ORIGIN },
		{ "%compilefog", MATERIAL_COMPILE_FOG },
		{ "%compileladder", MATERIAL_COMPILE_LADDER },
		{ "%compilewater", MATERIAL_COMPILE_WATER },
		{ "%compileinvisible", MATERIAL_COMPILE_INVISIBLE },
		{ "%compileoccluder", MATERIAL_COMPILE_OCCLUDER },
		{ "%compiledetail", MATERIAL_COMPILE_DETAIL },
		{ "%compilenonsolid", MATERIAL_COMPILE_NONSOLID },
		{ "%compileblocklight", MATERIAL_COMPILE_BLOCKLIGHT },
	} };
	MaterialInfo_t info;
	info.m_bFound = true;
	info.m_szShader = material.GetShaderKind();
	for ( const auto &[ szKey, nFlag ] : COMPILE_KEYS )
	{
		if ( material.Flag( szKey ) )
		{
			info.m_nCompile |= nFlag;
		}
	}
	const std::string szBase = material.Value( "$basetexture" );
	const std::string szTool = material.Value( "%tooltexture" );
	// Water and other shaders without a texture of their own show their tool texture.
	info.m_szTexture = !szBase.empty() && material::TextureKind( szBase ) == material::TextureKind_t::TEXTURE_KIND_FILE ? szBase : szTool;
	info.m_szTexture2 = material.Value( "$basetexture2" );
	info.m_bBlend = !info.m_szTexture2.empty() && ( info.m_szShader == "worldvertextransition" || info.m_szShader == "lightmapped_4wayblend" );
	info.m_bTranslucent = material.Flag( "$translucent" );
	info.m_bAdditive = material.Flag( "$additive" );
	info.m_bAlphaTest = material.Flag( "$alphatest" );
	info.m_flAlphaReference = material.Has( "$alphatestreference" ) && material.Number( "$alphatestreference", 0.0f ) > 0.0f ? material.Number( "$alphatestreference", 0.7f ) : 0.7f;
	info.m_flAlpha = material.Number( "$alpha", 1.0f );
	info.m_vecColor = material.Color( "$color" );
	info.m_bNoCull = material.Flag( "$nocull" );
	info.m_flDecalScale = material.Number( "$decalscale", 1.0f );
	return info;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MaterialInfo_t ReadMaterialInfo( std::string_view szMaterial, const material::Reader_t &pfnRead )
{
	if ( !pfnRead || szMaterial.empty() )
	{
		return {};
	}
	const auto bytes = pfnRead( "materials/" + vfs::NormalizePath( szMaterial ) + ".vmt" );
	if ( !bytes )
	{
		return {};
	}
	const auto material = material::ParseMaterial( { reinterpret_cast<const char *>( bytes->data() ), bytes->size() }, pfnRead );
	if ( !material )
	{
		return {};
	}
	return DescribeMaterial( *material );
}

} // namespace xv::map
