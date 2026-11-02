//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Materials.h. Transparency is read as the material preview reads it, so the two
//          agree.
//
//===========================================================================//
#include "Materials.h"

#include <cmath>

#include "kv1/Kv1.h"
#include "sky/Sky.h"

#include <array>
#include <utility>

#include "vfs/Vfs.h"

namespace xv::map
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: A texture transform's value ("center .5 .5 scale 1 1 rotate 0 translate 0 0") as
//          u' = a*u + b*v + tx, v' = c*u + d*v + ty.
//-----------------------------------------------------------------------------
std::array<float, 6> TransformOf( std::string_view szValue )
{
	const sky::CUvTransform transform = sky::CUvTransform::Parse( szValue );
	return { transform.m_flA, transform.m_flB, transform.m_flC, transform.m_flD, transform.m_flTx, transform.m_flTy };
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MaterialInfo_t DescribeMaterial( const material::CMaterial &material )
{
	static constexpr std::array<std::pair<std::string_view, MaterialCompile_t>, 22> COMPILE_KEYS = { {
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
		{ "%compilenolight", MATERIAL_COMPILE_NOLIGHT },
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
	if ( !material.Value( "$reflectivity" ).empty() )
	{
		info.m_Reflectivity = material.Color( "$reflectivity" );
	}
	info.m_bSelfIllum = material.Flag( "$selfillum" );
	if ( info.m_bSelfIllum )
	{
		const auto tint = material.Color( "$selfillumtint" );
		info.m_vecSelfIllumTint = tint;
	}
	info.m_bBlend = !info.m_szTexture2.empty() && ( info.m_szShader == "worldvertextransition" || info.m_szShader == "lightmapped_4wayblend" );
	if ( info.m_szShader == "refract" || info.m_szShader == "water" )
	{
		// As the material editor's preview shades them (material/Preview.cpp): DirectX 9 reads
		// $normalmap ($bumpmap is often DirectX 8's distortion map); water is tinted by its fog
		// and always reflects.
		const bool bWater = info.m_szShader == "water";
		info.m_bRefract = true;
		const std::string szNormal = material.Value( "$normalmap" );
		const std::string szBump = material.Value( "$bumpmap" );
		info.m_szTexture = !szNormal.empty() ? szNormal : !szBump.empty() ? szBump
																		  : szTool;
		info.m_szTexture2 = bWater ? std::string{} : material.Value( "$refracttinttexture" );
		if ( bWater )
		{
			const CVec3 fog = material.Color( "$fogcolor", { 0.2f, 0.25f, 0.25f } );
			info.m_vecRefractTint = CVec3{ 1, 1, 1 } + ( fog * 3.0f - CVec3{ 1, 1, 1 } ) * 0.6f;
		}
		else
		{
			info.m_vecRefractTint = material.Color( "$refracttint" );
		}
		const std::string szEnvMap = material.Value( "$envmap" );
		info.m_bEnvMap = bWater || ( !szEnvMap.empty() && szEnvMap != "0" );
		info.m_bReflectsSky = bWater && szEnvMap.empty();
		info.m_vecEnvTint = material.Color( "$envmaptint" );
	}
	if ( const std::string szDetail = material.Value( "$detail" ); !szDetail.empty() && !info.m_bRefract )
	{
		info.m_szDetail = szDetail;
		info.m_nDetailMode = static_cast<int>( material.Number( "$detailblendmode", 0.0f ) );
		info.m_flDetailBlend = material.Number( "$detailblendfactor", 1.0f );
		// One number scales both ways.
		const std::vector<float> scale = material.Numbers( "$detailscale" );
		if ( !scale.empty() )
		{
			info.m_DetailScale = { scale[ 0 ], scale.size() > 1 ? scale[ 1 ] : scale[ 0 ] };
		}
		if ( const material::Parameter_t *pTransform = material.Find( "$detailtexturetransform" ) )
		{
			info.m_DetailTransform = TransformOf( pTransform->m_szValue );
		}
	}
	info.m_szDetailType = material.Value( "%detailtype" );
	info.m_bTranslucent = material.Flag( "$translucent" );
	info.m_bAdditive = material.Flag( "$additive" );
	info.m_bAlphaTest = material.Flag( "$alphatest" );
	info.m_flAlphaReference = material.Has( "$alphatestreference" ) && material.Number( "$alphatestreference", 0.0f ) > 0.0f ? material.Number( "$alphatestreference", 0.7f ) : 0.7f;
	info.m_flAlpha = material.Number( "$alpha", 1.0f );
	info.m_vecColor = material.Color( "$color" );
	info.m_bNoCull = material.Flag( "$nocull" );
	info.m_flDecalScale = material.Number( "$decalscale", 1.0f );
	if ( auto pProxies = std::make_shared<const material::CProxies>( material ); pProxies->IsAnimated() )
	{
		info.m_pProxies = std::move( pProxies );
	}
	return info;
}

//-----------------------------------------------------------------------------
// Purpose: The proxies run at the time as the material editor's preview runs them; what the
//          map's views draw is read back from the parameters they write.
//-----------------------------------------------------------------------------
MaterialMotion_t MoveMaterial( const material::CProxies &proxies, float flTime, int nFrames, int nDetailFrames )
{
	const material::CMaterial moved = proxies.At( flTime, [ nFrames, nDetailFrames ]( std::string_view szParameter ) {
		return kv1::EqualsIgnoreCase( szParameter, "$basetexture" ) ? nFrames : kv1::EqualsIgnoreCase( szParameter, "$detail" ) ? nDetailFrames
																																: 0;
	} );
	MaterialMotion_t motion;
	const auto frameOf = []( float flFrame, int nCount ) {
		const int nFrame = static_cast<int>( std::floor( flFrame ) );
		return ( ( nFrame % nCount ) + nCount ) % nCount;
	};
	if ( nFrames > 0 )
	{
		motion.m_nFrame = frameOf( moved.Number( "$frame", 0.0f ), nFrames );
	}
	if ( nDetailFrames > 0 )
	{
		motion.m_nDetailFrame = frameOf( moved.Number( "$detailframe", 0.0f ), nDetailFrames );
	}
	if ( const material::Parameter_t *pDetail = moved.Find( "$detailtexturetransform" ) )
	{
		motion.m_DetailTransform = TransformOf( pDetail->m_szValue );
	}
	// Water and Refract scroll their normal map ($bumptransform) instead.
	const material::Parameter_t *pTransform = moved.Find( "$basetexturetransform" );
	if ( !pTransform )
		pTransform = moved.Find( "$bumptransform" );
	if ( pTransform )
	{
		motion.m_UvTransform = TransformOf( pTransform->m_szValue );
	}
	motion.m_vecColor = moved.Color( "$color" );
	motion.m_flAlpha = moved.Number( "$alpha", 1.0f );
	return motion;
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
