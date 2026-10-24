//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Preview.h. How the shaders combine their textures follows what the public SDK's
//          shaders and the VDC articles say they do; this is a CPU picture of it, not their code.
//
//===========================================================================//
#include "Preview.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <set>

#include "kv1/Kv1.h"

namespace xv::material
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: How a shader draws, as far as the preview goes; nullopt for shaders it doesn't know.
//-----------------------------------------------------------------------------
std::optional<Look_t> LookOf( const std::string &szKind )
{
	static const std::set<std::string, std::less<>> LIT = {
		"lightmappedgeneric", "vertexlitgeneric", "worldvertextransition", "worldtwotextureblend",
		"lightmappedtwotexture", "lightmappedreflective", "lightmapped_4wayblend", "cable", "eyes", "eyeball",
		"eyerefract", "teeth", "shatteredglass", "decalbasetimeslightmapalphablendselfillum", "vertexlitgeneric_dx6",
		"lightmappedgeneric_decal"
	};
	static const std::set<std::string, std::less<>> UNLIT = { "unlitgeneric", "unlittwotexture", "monitorscreen", "subrect" };
	static const std::set<std::string, std::less<>> SCREEN = {
		"screenspace_general", "wireframe", "compositor", "sample4x4", "sample4x4_blend", "downsample",
		"downsample_nohdr", "blurfilterx", "blurfiltery", "floattoscreen", "showdestalpha", "debugluxels",
		"debugtextureview", "vr_distort_texture", "vr_distort_hud", "volumetricfog", "shadow", "engine_post", "bloom",
		"motionblur"
	};
	if ( LIT.contains( szKind ) )
	{
		return Look_t::LOOK_LIT;
	}
	if ( UNLIT.contains( szKind ) )
	{
		return Look_t::LOOK_UNLIT;
	}
	if ( SCREEN.contains( szKind ) )
	{
		return Look_t::LOOK_SCREEN;
	}
	if ( szKind == "sprite" || szKind == "spritecard" || szKind == "splinecard" )
	{
		return Look_t::LOOK_SPRITE;
	}
	if ( szKind == "modulate" || szKind == "decalmodulate" )
	{
		return Look_t::LOOK_MODULATE;
	}
	if ( szKind == "sky" )
	{
		return Look_t::LOOK_SKY;
	}
	if ( szKind == "windowimposter" )
	{
		return Look_t::LOOK_WINDOW;
	}
	if ( szKind == "refract" )
	{
		return Look_t::LOOK_REFRACT;
	}
	if ( szKind == "water" )
	{
		return Look_t::LOOK_WATER;
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float Linear( float flGamma )
{
	flGamma = std::clamp( flGamma, 0.0f, 1.0f );
	return flGamma <= 0.04045f ? flGamma / 12.92f : std::pow( ( flGamma + 0.055f ) / 1.055f, 2.4f );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 Linear( const Rgba_t &gamma )
{
	return { Linear( gamma[ 0 ] ), Linear( gamma[ 1 ] ), Linear( gamma[ 2 ] ) };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 Times( CVec3 a, CVec3 b )
{
	return { a.m_flX * b.m_flX, a.m_flY * b.m_flY, a.m_flZ * b.m_flZ };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 Lerp( CVec3 a, CVec3 b, float t )
{
	return a + ( b - a ) * t;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 Lerp( CVec3 a, CVec3 b, CVec3 t )
{
	return { a.m_flX + ( b.m_flX - a.m_flX ) * t.m_flX, a.m_flY + ( b.m_flY - a.m_flY ) * t.m_flY, a.m_flZ + ( b.m_flZ - a.m_flZ ) * t.m_flZ };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 Rgb( const Rgba_t &value )
{
	return { value[ 0 ], value[ 1 ], value[ 2 ] };
}

//-----------------------------------------------------------------------------
// Purpose: A texel by wrapping or clamping the coordinate, as the texture's flags say.
//-----------------------------------------------------------------------------
int Wrap( int n, int nSize, bool bClamp )
{
	return bClamp ? std::clamp( n, 0, nSize - 1 ) : ( ( n % nSize ) + nSize ) % nSize;
}

//-----------------------------------------------------------------------------
// Purpose: The sampler for a texture parameter: the frame its frame variable picks, and the first
//          of the transform parameters that is set.
//-----------------------------------------------------------------------------
Sampler_t MakeSampler( const CMaterial &material, const CTextureSet &textures, std::string_view szParameter, std::string_view szFrame, std::initializer_list<std::string_view> transforms )
{
	Sampler_t sampler;
	const Texture_t *pTexture = textures.Find( szParameter );
	if ( !pTexture || pTexture->m_Frames.empty() || pTexture->m_Cube )
	{
		return sampler;
	}
	const int nFrames = static_cast<int>( pTexture->m_Frames.size() );
	const int nFrame = szFrame.empty() ? 0 : Wrap( static_cast<int>( material.Number( szFrame, 0.0f ) ), nFrames, false );
	sampler.m_pImage = &pTexture->m_Frames[ static_cast<std::size_t>( nFrame ) ];
	sampler.m_pLight = pTexture->m_Light.Empty() ? nullptr : &pTexture->m_Light;
	for ( std::string_view szTransform : transforms )
	{
		if ( const Parameter_t *pTransform = material.Find( szTransform ) )
		{
			sampler.m_Transform = sky::CUvTransform::Parse( pTransform->m_szValue );
			break;
		}
	}
	if ( pTexture->m_Info )
	{
		sampler.m_bClampS = ( pTexture->m_Info->m_nFlags & texture::vtf_flag::CLAMPS ) != 0;
		sampler.m_bClampT = ( pTexture->m_Info->m_nFlags & texture::vtf_flag::CLAMPT ) != 0;
		sampler.m_bPoint = ( pTexture->m_Info->m_nFlags & texture::vtf_flag::POINTSAMPLE ) != 0;
	}
	return sampler;
}

//-----------------------------------------------------------------------------
// Purpose: What stands in for the map's cube map (env_cubemap) and other things only the map has:
//          a plain sky over a dark floor. Linear light.
//-----------------------------------------------------------------------------
CVec3 StandInSky( CVec3 vecDirection )
{
	const float z = std::clamp( vecDirection.m_flZ, -1.0f, 1.0f );
	const CVec3 sky{ 0.32f, 0.45f, 0.70f };
	const CVec3 horizon{ 0.55f, 0.55f, 0.55f };
	const CVec3 floor{ 0.05f, 0.045f, 0.04f };
	if ( z >= 0.0f )
	{
		return Lerp( horizon, sky, std::sqrt( z ) );
	}
	return Lerp( horizon, floor, std::min( 1.0f, -z * 4.0f ) );
}

//-----------------------------------------------------------------------------
// Purpose: The preview's view space (x right, y up, z towards the viewer) in the game's world
//          (the viewer looks along +X, +Z is up).
//-----------------------------------------------------------------------------
CVec3 ToWorld( CVec3 v )
{
	return { -v.m_flZ, -v.m_flX, v.m_flY };
}

//-----------------------------------------------------------------------------
// Purpose: What a cube map texture (or the map's, standing in) shows in a direction. Linear light.
//-----------------------------------------------------------------------------
CVec3 Environment( const Texture_t *pTexture, CVec3 vecView )
{
	const CVec3 world = ToWorld( vecView );
	if ( pTexture && pTexture->m_Cube )
	{
		const auto colour = sky::Sample( *pTexture->m_Cube, world );
		return { Linear( colour[ 0 ] / 255.0f ), Linear( colour[ 1 ] / 255.0f ), Linear( colour[ 2 ] / 255.0f ) };
	}
	return StandInSky( world );
}

//-----------------------------------------------------------------------------
// Purpose: Linear light behind the surface.
//-----------------------------------------------------------------------------
CVec3 BackdropAt( Backdrop_t nBackdrop, float x, float y, int nSize )
{
	switch ( nBackdrop )
	{
		case Backdrop_t::BACKDROP_DARK: return { 0.012f, 0.012f, 0.014f };
		case Backdrop_t::BACKDROP_LIGHT: return { 0.62f, 0.62f, 0.62f };
		case Backdrop_t::BACKDROP_CHECKERS:
		default:
		{
			const int nCell = std::max( 8, nSize / 16 );
			const bool bOdd = ( ( static_cast<int>( std::floor( x ) ) / nCell ) + ( static_cast<int>( std::floor( y ) ) / nCell ) ) % 2 != 0;
			return bOdd ? CVec3{ 0.30f, 0.30f, 0.30f } : CVec3{ 0.13f, 0.13f, 0.13f };
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint8_t ToByte( float flLinear )
{
	return texture::FromLinear( std::clamp( flLinear, 0.0f, 1.0f ) );
}

//-----------------------------------------------------------------------------
// Purpose: A cube map's faces in a cross, for a thumbnail.
//-----------------------------------------------------------------------------
texture::CImage Cross( const sky::CCube &cube )
{
	const int nSize = cube.Face( sky::Face_t::FACE_FT ).m_nWidth;
	if ( nSize <= 0 )
	{
		return {};
	}
	texture::CImage out{ nSize * 4, nSize * 3 };
	const auto put = [ & ]( sky::Face_t nFace, int nColumn, int nRow ) {
		const texture::CImage &face = cube.Face( nFace );
		for ( int y = 0; y < std::min( nSize, face.m_nHeight ); y++ )
		{
			for ( int x = 0; x < std::min( nSize, face.m_nWidth ); x++ )
			{
				std::copy_n( face.Pixel( x, y ), 4, out.Pixel( nColumn * nSize + x, nRow * nSize + y ) );
			}
		}
	};
	put( sky::Face_t::FACE_UP, 1, 0 );
	put( sky::Face_t::FACE_LF, 0, 1 );
	put( sky::Face_t::FACE_FT, 1, 1 );
	put( sky::Face_t::FACE_RT, 2, 1 );
	put( sky::Face_t::FACE_BK, 3, 1 );
	put( sky::Face_t::FACE_DN, 1, 2 );
	return out;
}

// Where a pixel of the preview lands on the shape.
struct Surface_t
{
	CVec3 m_vecPosition; // the square and the ball span -1 to 1 across
	CVec3 m_vecNormal;
	CVec3 m_vecTangent;   // along u
	CVec3 m_vecBitangent; // along v (down the texture)
	float m_flU = 0.0f;
	float m_flV = 0.0f;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<Surface_t> SurfaceAt( const PreviewOptions_t &options, float x, float y )
{
	const float flSize = static_cast<float>( options.m_nSize );
	if ( options.m_nShape == Shape_t::SHAPE_FLAT )
	{
		return Surface_t{ { x / flSize * 2.0f - 1.0f, 1.0f - y / flSize * 2.0f, 0.0f }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, -1, 0 }, x / flSize * options.m_flRepeat, y / flSize * options.m_flRepeat };
	}
	const float flRadius = 0.92f;
	const float px = ( x / flSize * 2.0f - 1.0f ) / flRadius;
	const float py = ( 1.0f - y / flSize * 2.0f ) / flRadius;
	const float flRest = 1.0f - px * px - py * py;
	if ( flRest <= 0.0f )
	{
		return std::nullopt;
	}
	const CVec3 normal{ px, py, std::sqrt( flRest ) };
	const float flLongitude = std::atan2( normal.m_flX, normal.m_flZ );
	const CVec3 tangent{ std::cos( flLongitude ), 0.0f, -std::sin( flLongitude ) };
	return Surface_t{ normal * flRadius, normal, tangent, Normalized( Cross( tangent, normal ) ), ( 0.5f + flLongitude / (2.0f * std::numbers::pi_v<float>)) * 2.0f * options.m_flRepeat,
		std::acos( std::clamp( normal.m_flY, -1.0f, 1.0f ) ) / std::numbers::pi_v<float> * options.m_flRepeat };
}

//-----------------------------------------------------------------------------
// Purpose: A tangent-space normal (Source's: x along u, y down the texture, z out) on the surface.
//-----------------------------------------------------------------------------
CVec3 FromTangent( const Surface_t &surface, CVec3 n )
{
	return Normalized( surface.m_vecTangent * n.m_flX + surface.m_vecBitangent * n.m_flY + surface.m_vecNormal * n.m_flZ );
}

// The three directions a self-shadowing bump map ($ssbump) stores light from, in tangent space.
const std::array<CVec3, 3> BUMP_BASIS = { CVec3{ 0.81649658f, 0.0f, 0.57735027f }, CVec3{ -0.40824829f, 0.70710678f, 0.57735027f }, CVec3{ -0.40824829f, -0.70710678f, 0.57735027f } };

// Where the preview is seen from, in front of the square, so reflections and highlights move across it.
const CVec3 EYE{ 0.0f, 0.0f, 3.0f };

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Everything about a material the preview's shading needs.
//-----------------------------------------------------------------------------
Shading_t ReadShading( const CMaterial &material, const CTextureSet &textures, const PreviewOptions_t &options )
{
	Shading_t s;
	s.m_szKind = material.GetShaderKind();
	s.m_nLook = LookOf( s.m_szKind ).value_or( Look_t::LOOK_LIT );
	s.m_Base = MakeSampler( material, textures, "$basetexture", "$frame", { "$basetexturetransform" } );
	s.m_Base2 = MakeSampler( material, textures, material.Has( "$basetexture2" ) ? "$basetexture2" : "$texture2", "$frame2", { "$basetexturetransform2", "$texture2transform" } );
	s.m_Detail = MakeSampler( material, textures, "$detail", "$detailframe", { "$detailtexturetransform" } );
	const std::vector<float> detailScale = material.Numbers( "$detailscale" );
	s.m_Detail.m_flScaleU = detailScale.empty() ? 4.0f : detailScale[ 0 ];
	s.m_Detail.m_flScaleV = detailScale.size() > 1 ? detailScale[ 1 ] : s.m_Detail.m_flScaleU;
	// Water and Refract read $normalmap on DirectX 9; their $bumpmap is often DirectX 8's distortion map.
	const bool bNormalFirst = s.m_nLook == Look_t::LOOK_WATER || s.m_nLook == Look_t::LOOK_REFRACT;
	const char *pszBump = ( bNormalFirst ? material.Has( "$normalmap" ) : !material.Has( "$bumpmap" ) ) ? "$normalmap" : "$bumpmap";
	s.m_Bump = MakeSampler( material, textures, pszBump, "$bumpframe", { "$bumptransform", "$basetexturetransform" } );
	if ( const std::vector<float> scale = material.Numbers( "$scale" ); s.m_nLook == Look_t::LOOK_WATER && !scale.empty() )
	{
		s.m_Bump.m_flScaleU = scale[ 0 ];
		s.m_Bump.m_flScaleV = scale.size() > 1 ? scale[ 1 ] : scale[ 0 ];
	}
	s.m_EnvMask = MakeSampler( material, textures, "$envmapmask", "$envmapmaskframe", { "$envmapmasktransform", "$basetexturetransform" } );
	s.m_SelfIllumMask = MakeSampler( material, textures, "$selfillummask", {}, { "$basetexturetransform" } );
	s.m_PhongExponent = MakeSampler( material, textures, "$phongexponenttexture", {}, { "$basetexturetransform" } );
	s.m_RefractTint = MakeSampler( material, textures, "$refracttinttexture", "$refracttinttextureframe", { "$basetexturetransform" } );
	if ( options.m_bHdr )
	{
		s.m_Hdr = MakeSampler( material, textures, "$hdrcompressedtexture", "$frame", { "$basetexturetransform" } );
		s.m_bHdrCompressed = s.m_Hdr.IsValid();
		if ( !s.m_bHdrCompressed )
		{
			s.m_Hdr = MakeSampler( material, textures, "$hdrbasetexture", "$frame", { "$basetexturetransform" } );
			// 16-bit integer HDR textures hold a sixteenth of the light.
			const Texture_t *pTexture = textures.Find( "$hdrbasetexture" );
			s.m_flHdrScale = pTexture && pTexture->m_Info && pTexture->m_Info->m_nFormat == texture::VtfFormat_t::RGBA16161616 ? 16.0f : 1.0f;
		}
	}
	const std::string szEnvMap = material.Value( "$envmap" );
	s.m_bEnvMap = !szEnvMap.empty() && szEnvMap != "0";
	s.m_pEnvMap = textures.Find( "$envmap" );
	s.m_vecColor = material.Color( "$color" );
	s.m_vecColor2 = material.Color( "$color2" );
	s.m_flAlpha = material.Number( "$alpha", 1.0f );
	s.m_bTranslucent = material.Flag( "$translucent" );
	s.m_bAdditive = material.Flag( "$additive" );
	s.m_bAlphaTest = material.Flag( "$alphatest" );
	s.m_flAlphaReference = material.Has( "$alphatestreference" ) && material.Number( "$alphatestreference", 0.0f ) > 0.0f ? material.Number( "$alphatestreference", 0.7f ) : 0.7f;
	s.m_bSsBump = material.Flag( "$ssbump" );
	s.m_bPhong = material.Flag( "$phong" ) && s.m_szKind == "vertexlitgeneric";
	s.m_bHalfLambert = material.Flag( "$halflambert" ) || ( s.m_bPhong && !material.Flag( "$phongdisablehalflambert" ) );
	s.m_bSelfIllum = material.Flag( "$selfillum" );
	s.m_vecSelfIllumTint = material.Color( "$selfillumtint" );
	s.m_bBaseAlphaEnvMask = material.Flag( "$basealphaenvmapmask" );
	s.m_bNormalAlphaEnvMask = material.Flag( "$normalmapalphaenvmapmask" );
	s.m_vecEnvTint = material.Color( "$envmaptint" );
	s.m_flEnvContrast = material.Number( "$envmapcontrast", 0.0f );
	s.m_vecEnvSaturation = material.Color( "$envmapsaturation" );
	s.m_nDetailMode = static_cast<int>( material.Number( "$detailblendmode", 0.0f ) );
	s.m_flDetailFactor = material.Number( "$detailblendfactor", 1.0f );
	s.m_vecDetailTint = material.Color( "$detailtint" );
	s.m_flPhongExponent = material.Number( "$phongexponent", 5.0f );
	s.m_flPhongBoost = material.Number( "$phongboost", 1.0f );
	s.m_vecPhongFresnel = material.Color( "$phongfresnelranges", { 0.05f, 0.5f, 1.0f } );
	s.m_vecPhongTint = material.Color( "$phongtint" );
	s.m_bBaseAlphaPhongMask = material.Flag( "$basemapalphaphongmask" );
	s.m_bBlendTint = material.Flag( "$blendtintbybasealpha" );
	s.m_flTintReplace = material.Number( "$blendtintcoloroverbase", 0.0f );
	s.m_flOverbright = material.Number( "$overbrightfactor", 1.0f );
	s.m_flAddSelf = material.Number( "$addself", 0.0f );
	s.m_bMod2x = material.Flag( "$mod2x" ) || s.m_szKind == "decalmodulate";
	s.m_flRefractAmount = material.Number( "$refractamount", s.m_nLook == Look_t::LOOK_WATER ? 0.3f : 0.5f );
	s.m_vecRefractTint = material.Color( "$refracttint" );
	s.m_vecFog = material.Color( "$fogcolor", { 0.2f, 0.25f, 0.25f } );
	s.m_bTwoTextures = s.m_szKind == "unlittwotexture" || s.m_szKind == "lightmappedtwotexture";
	s.m_bBlendTextures = s.m_szKind == "worldvertextransition" || s.m_szKind == "lightmapped_4wayblend";
	if ( s.m_nLook == Look_t::LOOK_SPRITE )
	{
		if ( s.m_szKind == "sprite" )
		{
			// kRenderGlow, kRenderTransAdd and kRenderTransAddFrameBlend add; the rest blend. Without
			// a mode the entity's decides, and glows (the usual use) add.
			const int nMode = static_cast<int>( material.Number( "$spriterendermode", 5.0f ) );
			s.m_bAdditive = s.m_bAdditive || nMode == 3 || nMode == 5 || nMode == 9;
		}
		s.m_bTranslucent = !s.m_bAdditive;
	}
	return s;
}

namespace
{

//-----------------------------------------------------------------------------
// Purpose: The detail texture combined with the base before lighting, as $detailblendmode says.
//          albedo is linear; alpha is the base's.
//-----------------------------------------------------------------------------
void CombineDetail( const Shading_t &s, const Rgba_t &detail, float flBaseAlpha, CVec3 &albedo, float &flAlpha )
{
	const CVec3 raw = Times( Rgb( detail ), s.m_vecDetailTint );
	const CVec3 colour = Times( Linear( detail ), s.m_vecDetailTint );
	const float f = s.m_flDetailFactor;
	switch ( s.m_nDetailMode )
	{
		case 0: albedo = Times( albedo, Lerp( CVec3{ 1, 1, 1 }, raw * 2.0f, f ) ); break;
		case 1: albedo += colour * f; break;
		case 2: albedo = Lerp( albedo, colour, f * detail[ 3 ] ); break;
		case 3:
			albedo = Lerp( albedo, colour, f );
			flAlpha = flAlpha + ( detail[ 3 ] - flAlpha ) * f;
			break;
		case 4:
			albedo = Lerp( albedo, colour, f * ( 1.0f - flBaseAlpha ) );
			flAlpha = detail[ 3 ];
			break;
		case 7:
		{
			const float flPattern = detail[ 0 ] + ( detail[ 3 ] - detail[ 0 ] ) * flBaseAlpha;
			albedo = Times( albedo, Lerp( CVec3{ 1, 1, 1 }, CVec3{ 2, 2, 2 } * flPattern, f ) );
			break;
		}
		case 8:
			albedo = Lerp( albedo, Times( albedo, raw ), f );
			flAlpha = flAlpha + ( flAlpha * detail[ 3 ] - flAlpha ) * f;
			break;
		case 9: flAlpha = flAlpha + ( flAlpha * detail[ 3 ] - flAlpha ) * f; break;
		case 11: albedo = albedo * ( ( raw.m_flX + raw.m_flY + raw.m_flZ ) * ( 2.0f / 3.0f ) ); break;
		default: break; // 5 and 6 come after lighting; 10 is a second bump map
	}
}

//-----------------------------------------------------------------------------
// Purpose: The detail texture added after lighting (modes 5 and 6), unaffected by it.
//-----------------------------------------------------------------------------
CVec3 DetailAfterLighting( const Shading_t &s, const Rgba_t &detail )
{
	const CVec3 colour = Times( Linear( detail ), s.m_vecDetailTint );
	const float f = s.m_flDetailFactor;
	if ( s.m_nDetailMode == 5 )
	{
		return colour * f;
	}
	if ( s.m_nDetailMode == 6 && f > 0.0f )
	{
		// Past half, the top f of the detail's brightness is stretched over 0-1; below half, its
		// upper half is added, dimmer the lower the factor.
		const auto band = [ f ]( float c ) { return std::clamp( f >= 0.5f ? ( c - ( 1.0f - f ) ) / f : 4.0f * f * ( c - 0.5f ), 0.0f, 1.0f ); };
		return { band( colour.m_flX ), band( colour.m_flY ), band( colour.m_flZ ) };
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: The reflection a material adds ($envmap), masked and tinted.
//-----------------------------------------------------------------------------
CVec3 Reflection( const Shading_t &s, CVec3 normal, CVec3 view, CVec3 vecMask )
{
	const CVec3 reflected = normal * ( 2.0f * Dot( normal, view ) ) - view;
	CVec3 env = Times( Times( Environment( s.m_pEnvMap, reflected ), vecMask ), s.m_vecEnvTint );
	env = Lerp( env, Times( env, env ), s.m_flEnvContrast );
	const float flGrey = env.m_flX * 0.299f + env.m_flY * 0.587f + env.m_flZ * 0.114f;
	return Lerp( CVec3{ flGrey, flGrey, flGrey }, env, s.m_vecEnvSaturation );
}

// One pixel's result before it goes over the backdrop.
struct Fragment_t
{
	CVec3 m_vecColor;
	float m_flAlpha = 1.0f;
	bool m_bDiscard = false;
	bool m_bAdditive = false;
	bool m_bBlend = false;
	std::optional<CVec3> m_Modulate; // Modulate: multiplies what is behind
	std::optional<std::array<float, 2>> m_Refract; // Refract and Water: shifts what is behind, in pixels
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Fragment_t Shade( const Shading_t &s, const Surface_t &surface, const PreviewOptions_t &options, float flAcross )
{
	Fragment_t out;
	const float u = surface.m_flU;
	const float v = surface.m_flV;
	const CVec3 light = Normalized( options.m_vecLight );
	const CVec3 view = Normalized( EYE - surface.m_vecPosition );

	if ( s.m_nLook == Look_t::LOOK_SKY && s.m_Hdr.IsValid() )
	{
		CVec3 hdr;
		if ( s.m_bHdrCompressed )
		{
			// RGBS: colour times alpha times 8, read without sRGB.
			const Rgba_t raw = s.m_Hdr.At( u, v );
			hdr = Rgb( raw ) * ( raw[ 3 ] * 8.0f );
		}
		else
		{
			hdr = ( s.m_Hdr.m_pLight ? s.m_Hdr.LightAt( u, v ) : Linear( s.m_Hdr.At( u, v ) ) ) * s.m_flHdrScale;
		}
		out.m_vecColor = Times( hdr, s.m_vecColor ) * std::exp2( options.m_flExposure );
		return out;
	}
	if ( s.m_nLook == Look_t::LOOK_WINDOW )
	{
		// Seen from a little way in front: each pixel looks into the cube map through the window.
		const CVec3 ray = Normalized( CVec3{ surface.m_vecPosition.m_flX, surface.m_vecPosition.m_flY, -1.2f } );
		out.m_vecColor = Times( Environment( s.m_pEnvMap, ray ), s.m_vecColor );
		return out;
	}

	const Rgba_t base = s.m_Base.IsValid() ? s.m_Base.At( u, v ) : Rgba_t{ 1, 1, 1, 1 };
	CVec3 albedo = Linear( base );
	float flAlpha = base[ 3 ];
	if ( s.m_Base2.IsValid() && ( s.m_bTwoTextures || s.m_bBlendTextures ) )
	{
		const Rgba_t second = s.m_Base2.At( u, v );
		albedo = s.m_bTwoTextures ? Times( albedo, Linear( second ) ) : Lerp( albedo, Linear( second ), std::clamp( flAcross * 1.4f - 0.2f, 0.0f, 1.0f ) );
	}
	const Rgba_t detail = s.m_Detail.IsValid() ? s.m_Detail.At( u, v ) : Rgba_t{ 0.5f, 0.5f, 0.5f, 1.0f };
	if ( s.m_Detail.IsValid() )
	{
		CombineDetail( s, detail, base[ 3 ], albedo, flAlpha );
	}
	CVec3 tint = s.m_vecColor;
	if ( s.m_szKind == "vertexlitgeneric" )
	{
		tint = Times( tint, s.m_vecColor2 );
	}
	if ( s.m_bBlendTint )
	{
		const CVec3 tinted = Lerp( Times( albedo, tint ), tint, s.m_flTintReplace );
		albedo = Lerp( albedo, tinted, base[ 3 ] );
	}
	else
	{
		albedo = Times( albedo, tint );
	}
	if ( s.m_nLook == Look_t::LOOK_SPRITE )
	{
		albedo = albedo * s.m_flOverbright + Linear( base ) * s.m_flAddSelf;
	}
	// Base alpha that masks something else isn't transparency.
	float flOpacity = s.m_flAlpha * ( s.m_bBaseAlphaEnvMask || s.m_bSelfIllum || s.m_bBlendTint ? 1.0f : flAlpha );

	if ( s.m_nLook == Look_t::LOOK_MODULATE )
	{
		const CVec3 factor = s.m_bMod2x ? Rgb( base ) * 2.0f : Rgb( base );
		out.m_Modulate = Lerp( CVec3{ 1, 1, 1 }, factor, flOpacity );
		return out;
	}

	CVec3 normal = surface.m_vecNormal;
	Rgba_t bump{ 0.5f, 0.5f, 1.0f, 1.0f };
	CVec3 tangentNormal{ 0, 0, 1 };
	if ( s.m_Bump.IsValid() )
	{
		bump = s.m_Bump.At( u, v );
		if ( !s.m_bSsBump )
		{
			tangentNormal = Normalized( CVec3{ bump[ 0 ] * 2.0f - 1.0f, bump[ 1 ] * 2.0f - 1.0f, bump[ 2 ] * 2.0f - 1.0f } );
			normal = FromTangent( surface, tangentNormal );
		}
	}

	if ( s.m_nLook == Look_t::LOOK_REFRACT || s.m_nLook == Look_t::LOOK_WATER )
	{
		const float flShift = s.m_flRefractAmount * static_cast<float>( options.m_nSize ) * 0.06f;
		out.m_Refract = std::array<float, 2>{ tangentNormal.m_flX * flShift, tangentNormal.m_flY * flShift };
		CVec3 tintColour = s.m_vecRefractTint;
		if ( s.m_RefractTint.IsValid() )
		{
			tintColour = Times( tintColour, Linear( s.m_RefractTint.At( u, v ) ) );
		}
		out.m_vecColor = s.m_nLook == Look_t::LOOK_WATER ? s.m_vecFog : tintColour;
		if ( s.m_nLook == Look_t::LOOK_WATER || s.m_bEnvMap )
		{
			// Water reflects the map (a stand-in here) more at grazing angles.
			const float flFresnel = 0.1f + 0.9f * std::pow( 1.0f - std::max( 0.0f, Dot( normal, view ) ), 5.0f );
			const CVec3 reflected = normal * ( 2.0f * Dot( normal, view ) ) - view;
			out.m_Modulate = Environment( s.m_bEnvMap ? s.m_pEnvMap : nullptr, reflected ) * flFresnel;
		}
		return out;
	}

	CVec3 colour = albedo;
	if ( s.m_nLook == Look_t::LOOK_LIT )
	{
		float flDiffuse = 0.0f;
		if ( s.m_Bump.IsValid() && s.m_bSsBump )
		{
			// Light from each of the three directions, weighted by how much of it reaches the texel.
			const CVec3 tangentLight{ Dot( light, surface.m_vecTangent ), Dot( light, surface.m_vecBitangent ), Dot( light, surface.m_vecNormal ) };
			for ( std::size_t i = 0; i < 3; i++ )
			{
				flDiffuse += bump[ i ] * std::max( 0.0f, Dot( tangentLight, BUMP_BASIS[ i ] ) );
			}
		}
		else
		{
			const float flDot = Dot( normal, light );
			flDiffuse = s.m_bHalfLambert ? ( flDot * 0.5f + 0.5f ) * ( flDot * 0.5f + 0.5f ) : std::max( 0.0f, flDot );
		}
		const float flAmbient = 0.22f;
		colour = albedo * ( flAmbient + 0.95f * flDiffuse );
	}
	if ( s.m_Detail.IsValid() && ( s.m_nDetailMode == 5 || s.m_nDetailMode == 6 ) )
	{
		colour += DetailAfterLighting( s, detail );
	}
	if ( s.m_bSelfIllum )
	{
		const float flMask = s.m_SelfIllumMask.IsValid() ? s.m_SelfIllumMask.At( u, v )[ 0 ] : base[ 3 ];
		colour = Lerp( colour, Times( s.m_vecSelfIllumTint, albedo ), flMask );
	}
	if ( s.m_bPhong && s.m_nLook == Look_t::LOOK_LIT )
	{
		const float flMask = s.m_bBaseAlphaPhongMask ? base[ 3 ] : ( s.m_Bump.IsValid() ? bump[ 3 ] : 1.0f );
		const float flExponent = s.m_PhongExponent.IsValid() ? 1.0f + 149.0f * s.m_PhongExponent.At( u, v )[ 0 ] : s.m_flPhongExponent;
		const float f = std::pow( 1.0f - std::max( 0.0f, Dot( normal, view ) ), 2.0f );
		const float flFresnel = f > 0.5f ? s.m_vecPhongFresnel.m_flY + ( s.m_vecPhongFresnel.m_flZ - s.m_vecPhongFresnel.m_flY ) * ( f - 0.5f ) * 2.0f : s.m_vecPhongFresnel.m_flX + ( s.m_vecPhongFresnel.m_flY - s.m_vecPhongFresnel.m_flX ) * f * 2.0f;
		const CVec3 reflectedLight = normal * ( 2.0f * Dot( normal, light ) ) - light;
		const float flSpecular = std::pow( std::max( 0.0f, Dot( reflectedLight, view ) ), std::max( 1.0f, flExponent ) ) * ( Dot( normal, light ) > 0.0f ? 1.0f : 0.0f );
		colour += s.m_vecPhongTint * ( flSpecular * s.m_flPhongBoost * flMask * flFresnel );
	}
	if ( s.m_bEnvMap )
	{
		CVec3 mask{ 1, 1, 1 };
		if ( s.m_EnvMask.IsValid() )
		{
			mask = Rgb( s.m_EnvMask.At( u, v ) );
		}
		if ( s.m_bBaseAlphaEnvMask )
		{
			mask = mask * ( 1.0f - base[ 3 ] );
		}
		if ( s.m_bNormalAlphaEnvMask && s.m_Bump.IsValid() )
		{
			mask = mask * bump[ 3 ];
		}
		colour += Reflection( s, normal, view, mask );
	}

	out.m_vecColor = colour;
	out.m_flAlpha = std::clamp( flOpacity, 0.0f, 1.0f );
	if ( s.m_bAlphaTest )
	{
		out.m_bDiscard = flAlpha < s.m_flAlphaReference;
		out.m_flAlpha = 1.0f;
	}
	else if ( s.m_bAdditive )
	{
		out.m_bAdditive = true;
		if ( !s.m_bTranslucent && s.m_nLook != Look_t::LOOK_SPRITE )
		{
			out.m_flAlpha = 1.0f;
		}
	}
	else if ( s.m_bTranslucent || s.m_flAlpha < 1.0f )
	{
		out.m_bBlend = true;
	}
	return out;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: 0-1 per channel, as stored.
//-----------------------------------------------------------------------------
Rgba_t Sampler_t::At( float u, float v ) const
{
	const texture::CImage &image = *m_pImage;
	u *= m_flScaleU;
	v *= m_flScaleV;
	const float tu = m_Transform.m_flA * u + m_Transform.m_flB * v + m_Transform.m_flTx;
	const float tv = m_Transform.m_flC * u + m_Transform.m_flD * v + m_Transform.m_flTy;
	const float x = tu * static_cast<float>( image.m_nWidth ) - 0.5f;
	const float y = tv * static_cast<float>( image.m_nHeight ) - 0.5f;
	if ( m_bPoint )
	{
		const std::uint8_t *p = image.Pixel( Wrap( static_cast<int>( std::floor( x + 0.5f ) ), image.m_nWidth, m_bClampS ), Wrap( static_cast<int>( std::floor( y + 0.5f ) ), image.m_nHeight, m_bClampT ) );
		return { p[ 0 ] / 255.0f, p[ 1 ] / 255.0f, p[ 2 ] / 255.0f, p[ 3 ] / 255.0f };
	}
	const int x0 = static_cast<int>( std::floor( x ) );
	const int y0 = static_cast<int>( std::floor( y ) );
	const float fx = x - static_cast<float>( x0 );
	const float fy = y - static_cast<float>( y0 );
	const int xa = Wrap( x0, image.m_nWidth, m_bClampS );
	const int xb = Wrap( x0 + 1, image.m_nWidth, m_bClampS );
	const int ya = Wrap( y0, image.m_nHeight, m_bClampT );
	const int yb = Wrap( y0 + 1, image.m_nHeight, m_bClampT );
	const std::uint8_t *p00 = image.Pixel( xa, ya );
	const std::uint8_t *p10 = image.Pixel( xb, ya );
	const std::uint8_t *p01 = image.Pixel( xa, yb );
	const std::uint8_t *p11 = image.Pixel( xb, yb );
	Rgba_t out{};
	for ( int c = 0; c < 4; c++ )
	{
		const float top = p00[ c ] + ( p10[ c ] - p00[ c ] ) * fx;
		const float bottom = p01[ c ] + ( p11[ c ] - p01[ c ] ) * fx;
		out[ static_cast<std::size_t>( c ) ] = ( top + ( bottom - top ) * fy ) / 255.0f;
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: An HDR texture's light values, nearest texel.
//-----------------------------------------------------------------------------
CVec3 Sampler_t::LightAt( float u, float v ) const
{
	const texture::CFloatImage &light = *m_pLight;
	const float tu = m_Transform.m_flA * u + m_Transform.m_flB * v + m_Transform.m_flTx;
	const float tv = m_Transform.m_flC * u + m_Transform.m_flD * v + m_Transform.m_flTy;
	const float *p = light.Pixel( Wrap( static_cast<int>( std::floor( tu * static_cast<float>( light.m_nWidth ) ) ), light.m_nWidth, m_bClampS ), Wrap( static_cast<int>( std::floor( tv * static_cast<float>( light.m_nHeight ) ) ), light.m_nHeight, m_bClampT ) );
	return { p[ 0 ], p[ 1 ], p[ 2 ] };
}

//-----------------------------------------------------------------------------
// Purpose: Reads every texture the material names.
//-----------------------------------------------------------------------------
CTextureSet CTextureSet::Load( const CMaterial &material, const Reader_t &pfnRead, int nMaxSize, int nMaxFrames )
{
	CTextureSet set;
	for ( const Parameter_t &parameter : material.m_Parameters )
	{
		if ( !IsTexture( parameter.m_szKey, parameter.m_szValue ) )
		{
			continue;
		}
		Texture_t texture{ parameter.m_szKey, parameter.m_szValue, {}, TextureKind( parameter.m_szValue ), {}, {}, 0, {}, {}, {} };
		if ( texture.m_nKind != TextureKind_t::TEXTURE_KIND_FILE )
		{
			set.m_Textures.push_back( std::move( texture ) );
			continue;
		}
		texture.m_szPath = TexturePath( parameter.m_szValue );
		const auto bytes = pfnRead ? pfnRead( texture.m_szPath ) : std::nullopt;
		if ( !bytes )
		{
			texture.m_szProblem = "missing";
			set.m_Textures.push_back( std::move( texture ) );
			continue;
		}
		std::string szError;
		const auto vtf = texture::ReadVtf( *bytes, &szError );
		if ( !vtf )
		{
			texture.m_szProblem = szError;
			set.m_Textures.push_back( std::move( texture ) );
			continue;
		}
		texture.m_Info = vtf->m_Info;
		while ( texture.m_nMip + 1 < vtf->m_Info.m_nMipCount && std::max( vtf->m_Info.m_nWidth, vtf->m_Info.m_nHeight ) >> texture.m_nMip > nMaxSize )
		{
			texture.m_nMip++;
		}
		if ( sky::IsCubeMap( *vtf ) )
		{
			sky::CCube cube;
			for ( sky::Face_t nFace : sky::FACES )
			{
				if ( auto face = sky::CubeMapFace( *vtf, nFace, &szError ) )
				{
					cube.Face( nFace ) = std::move( *face );
				}
				else
				{
					texture.m_szProblem = szError;
				}
			}
			texture.m_Frames.push_back( Cross( cube ) );
			texture.m_Cube = std::move( cube );
			set.m_Textures.push_back( std::move( texture ) );
			continue;
		}
		for ( int nFrame = 0; nFrame < std::min( vtf->m_Info.m_nFrames, nMaxFrames ); nFrame++ )
		{
			auto image = vtf->Decode( texture.m_nMip, nFrame, 0, &szError );
			if ( !image )
			{
				texture.m_szProblem = szError;
				break;
			}
			texture.m_Frames.push_back( std::move( *image ) );
		}
		if ( texture::IsFloatFormat( vtf->m_Info.m_nFormat ) )
		{
			if ( auto light = vtf->DecodeFloat( texture.m_nMip, 0, 0, &szError ) )
			{
				texture.m_Light = std::move( *light );
			}
		}
		set.m_Textures.push_back( std::move( texture ) );
	}
	return set;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const Texture_t *CTextureSet::Find( std::string_view szParameter ) const
{
	const auto it = std::ranges::find_if( m_Textures, [ szParameter ]( const Texture_t &t ) { return kv1::EqualsIgnoreCase( t.m_szParameter, szParameter ); } );
	return it == m_Textures.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CTextureSet::FrameCount( std::string_view szParameter ) const
{
	const Texture_t *pTexture = Find( szParameter );
	return pTexture && !pTexture->m_Cube ? static_cast<int>( pTexture->m_Frames.size() ) : 0;
}

//-----------------------------------------------------------------------------
// Purpose: The material drawn into a square image.
//-----------------------------------------------------------------------------
texture::CImage RenderPreview( const CMaterial &material, const CTextureSet &textures, const PreviewOptions_t &options )
{
	const int nSize = std::max( 1, options.m_nSize );
	texture::CImage out{ nSize, nSize };
	const Shading_t shading = ReadShading( material, textures, options );
	// The backdrop is drawn first, so refraction can look at what is behind a pixel.
	std::vector<CVec3> backdrop( static_cast<std::size_t>( nSize ) * static_cast<std::size_t>( nSize ) );
	for ( int y = 0; y < nSize; y++ )
	{
		for ( int x = 0; x < nSize; x++ )
		{
			backdrop[ static_cast<std::size_t>( y ) * static_cast<std::size_t>( nSize ) + static_cast<std::size_t>( x ) ] = BackdropAt( options.m_nBackdrop, static_cast<float>( x ), static_cast<float>( y ), nSize );
		}
	}
	const auto behind = [ & ]( float x, float y ) {
		const int ix = std::clamp( static_cast<int>( std::floor( x ) ), 0, nSize - 1 );
		const int iy = std::clamp( static_cast<int>( std::floor( y ) ), 0, nSize - 1 );
		return backdrop[ static_cast<std::size_t>( iy ) * static_cast<std::size_t>( nSize ) + static_cast<std::size_t>( ix ) ];
	};
	for ( int y = 0; y < nSize; y++ )
	{
		for ( int x = 0; x < nSize; x++ )
		{
			const float fx = static_cast<float>( x ) + 0.5f;
			const float fy = static_cast<float>( y ) + 0.5f;
			CVec3 result = behind( fx, fy );
			if ( const auto surface = SurfaceAt( options, fx, fy ) )
			{
				const Fragment_t fragment = Shade( shading, *surface, options, fx / static_cast<float>( nSize ) );
				if ( fragment.m_Refract )
				{
					const CVec3 seen = behind( fx + ( *fragment.m_Refract )[ 0 ], fy + ( *fragment.m_Refract )[ 1 ] );
					result = Times( seen, shading.m_nLook == Look_t::LOOK_WATER ? Lerp( CVec3{ 1, 1, 1 }, fragment.m_vecColor * 3.0f, 0.6f ) : fragment.m_vecColor );
					if ( fragment.m_Modulate )
					{
						result += *fragment.m_Modulate;
					}
				}
				else if ( fragment.m_Modulate )
				{
					result = Times( result, *fragment.m_Modulate );
				}
				else if ( fragment.m_bDiscard )
				{
					// alpha-tested away: the backdrop shows
				}
				else if ( fragment.m_bAdditive )
				{
					result += fragment.m_vecColor * fragment.m_flAlpha;
				}
				else if ( fragment.m_bBlend )
				{
					result = Lerp( result, fragment.m_vecColor, fragment.m_flAlpha );
				}
				else
				{
					result = fragment.m_vecColor;
				}
			}
			std::uint8_t *p = out.Pixel( x, y );
			p[ 0 ] = ToByte( result.m_flX );
			p[ 1 ] = ToByte( result.m_flY );
			p[ 2 ] = ToByte( result.m_flZ );
			p[ 3 ] = 255;
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool HasHdr( const CMaterial &material, const CTextureSet &textures )
{
	if ( material.GetShaderKind() != "sky" )
	{
		return false;
	}
	for ( const char *pszParameter : { "$hdrcompressedtexture", "$hdrbasetexture" } )
	{
		if ( const Texture_t *pTexture = textures.Find( pszParameter ); pTexture && !pTexture->m_Frames.empty() )
		{
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: What the preview leaves out or only approximates for this material, a line each.
//-----------------------------------------------------------------------------
std::vector<std::string> PreviewNotes( const CMaterial &material, const CTextureSet &textures )
{
	std::vector<std::string> notes;
	const std::string szKind = material.GetShaderKind();
	const auto look = LookOf( szKind );
	if ( !look )
	{
		notes.push_back( std::format( "The preview doesn't know the {} shader; it is drawn like LightmappedGeneric.", material.m_szShader ) );
	}
	const Look_t nLook = look.value_or( Look_t::LOOK_LIT );
	switch ( nLook )
	{
		case Look_t::LOOK_LIT: notes.push_back( "Lit by one light you can move, with some ambient light; in game the map's lightmaps or a model's lighting do it." ); break;
		case Look_t::LOOK_SCREEN: notes.push_back( std::format( "{} draws over the screen, not on a surface; its first texture is shown.", material.m_szShader ) ); break;
		case Look_t::LOOK_REFRACT: notes.push_back( "Refract bends what is behind it (the backdrop here) by its normal map; blur isn't shown." ); break;
		case Look_t::LOOK_WATER: notes.push_back( "Water shows the backdrop bent by its normal map and a plain sky reflected; in game it reflects and fogs the map." ); break;
		case Look_t::LOOK_WINDOW: notes.push_back( "WindowImposter shows its cube map as if seen through a window from a little way off." ); break;
		default: break;
	}
	for ( const Texture_t &texture : textures.GetTextures() )
	{
		if ( texture.m_nKind == TextureKind_t::TEXTURE_KIND_MAP_CUBEMAP )
		{
			notes.push_back( std::format( "{} is env_cubemap, the map's nearest cube map; a plain sky stands in.", texture.m_szParameter ) );
		}
		else if ( texture.m_nKind == TextureKind_t::TEXTURE_KIND_RENDER_TARGET )
		{
			notes.push_back( std::format( "{} is {}, which the engine draws each frame; it isn't shown.", texture.m_szParameter, texture.m_szValue ) );
		}
		else if ( !texture.m_szProblem.empty() )
		{
			notes.push_back( std::format( "{} ({}) can't be shown: {}.", texture.m_szParameter, texture.m_szPath, texture.m_szProblem ) );
		}
	}
	static const std::vector<std::pair<std::string_view, std::string_view>> LEFT_OUT = {
		{ "$lightwarptexture", "the light warp texture" },
		{ "$phongwarptexture", "the phong warp texture" },
		{ "$rimlight", "rim lighting" },
		{ "$parallaxmap", "parallax" },
		{ "$flowmap", "flowing" },
		{ "$seamless_scale", "seamless mapping" },
		{ "$envmapfresnel", "the reflection's fresnel" },
		{ "$ambientocclusion", "ambient occlusion" },
		{ "$blendmodulatetexture", "the blend modulate texture" },
		{ "$distancealpha", "distance-field alpha" },
		{ "$depthblend", "soft edges against the world" },
		{ "$bluramount", "blur" },
		{ "$bumpmap2", "the second bump map" },
		{ "$detail2", "the second detail texture" },
		{ "$basetexturenoenvmap", "$basetexturenoenvmap" },
		{ "$lightwarpnoenvmap", "$lightwarpnoenvmap" },
	};
	for ( const auto &[ szKey, szWhat ] : LEFT_OUT )
	{
		if ( const Parameter_t *pParameter = material.Find( szKey ); pParameter && pParameter->m_szValue != "0" && !pParameter->m_szValue.empty() )
		{
			notes.push_back( std::format( "{} isn't shown ({}).", szWhat, pParameter->m_szKey ) );
		}
	}
	if ( szKind == "sprite" && !material.Has( "$spriterendermode" ) )
	{
		notes.push_back( "It doesn't set $spriterendermode, so the sprite's entity decides how it blends; it is drawn added, as glows are." );
	}
	if ( material.Flag( "$vertexcolor" ) || material.Flag( "$vertexalpha" ) )
	{
		notes.push_back( "$vertexcolor and $vertexalpha take colour from the model or particle drawing it; they aren't shown." );
	}
	if ( szKind == "worldvertextransition" || szKind == "lightmapped_4wayblend" )
	{
		notes.push_back( "The second texture fades in from left to right; in game the alpha painted on the displacement decides." );
	}
	if ( material.Has( "$detail" ) && static_cast<int>( material.Number( "$detailblendmode", 0.0f ) ) == 10 )
	{
		notes.push_back( "$detailblendmode 10 (the detail as a second, self-shadowing bump map) isn't shown." );
	}
	return notes;
}

} // namespace xv::material
