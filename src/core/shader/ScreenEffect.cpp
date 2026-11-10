//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ScreenEffect.h. How screenspace_general sets its registers and blends is taken
//          from the shader's behaviour as the SDK describes it, not its code.
//
//===========================================================================//
#include "ScreenEffect.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "kv1/Kv1.h"
#include "util/Numbers.h"
#include "util/Parallel.h"

namespace xv::shader
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: sRGB's curve, both ways.
//-----------------------------------------------------------------------------
float ToLinear( float f )
{
	return f <= 0.04045f ? f / 12.92f : std::pow( ( f + 0.055f ) / 1.055f, 2.4f );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float ToSrgb( float f )
{
	f = std::clamp( f, 0.0f, 1.0f );
	return f <= 0.0031308f ? f * 12.92f : 1.055f * std::pow( f, 1.0f / 2.4f ) - 0.055f;
}

//-----------------------------------------------------------------------------
// Purpose: A texture read between its texels, clamped at its edges (as the frame's are);
//          colour made linear unless bLinear.
//-----------------------------------------------------------------------------
Vec4_t Sample( const texture::CImage &image, float u, float v, bool bLinear )
{
	if ( image.Empty() )
		return {};
	const float x = u * static_cast<float>( image.m_nWidth ) - 0.5f;
	const float y = v * static_cast<float>( image.m_nHeight ) - 0.5f;
	const int x0 = static_cast<int>( std::floor( x ) );
	const int y0 = static_cast<int>( std::floor( y ) );
	const float fx = x - static_cast<float>( x0 );
	const float fy = y - static_cast<float>( y0 );
	const auto texel = [ & ]( int tx, int ty ) {
		const std::uint8_t *p = image.Pixel( std::clamp( tx, 0, image.m_nWidth - 1 ), std::clamp( ty, 0, image.m_nHeight - 1 ) );
		Vec4_t c{};
		for ( std::size_t i = 0; i < 4; i++ )
		{
			const float f = static_cast<float>( p[ i ] ) / 255.0f;
			c[ i ] = i < 3 && !bLinear ? ToLinear( f ) : f;
		}
		return c;
	};
	const Vec4_t a = texel( x0, y0 );
	const Vec4_t b = texel( x0 + 1, y0 );
	const Vec4_t c = texel( x0, y0 + 1 );
	const Vec4_t d = texel( x0 + 1, y0 + 1 );
	Vec4_t out{};
	for ( std::size_t i = 0; i < 4; i++ )
		out[ i ] = ( a[ i ] * ( 1 - fx ) + b[ i ] * fx ) * ( 1 - fy ) + ( c[ i ] * ( 1 - fx ) + d[ i ] * fx ) * fy;
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: A parameter as a number (0 when it isn't one), and whether it is set non-zero.
//-----------------------------------------------------------------------------
float NumberOf( const material::CMaterial &material, std::string_view szKey )
{
	const material::Parameter_t *pFound = material.Find( szKey );
	return pFound ? ParseNumber<float>( pFound->m_szValue ).value_or( 0.0f ) : 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool FlagOf( const material::CMaterial &material, std::string_view szKey )
{
	return NumberOf( material, szKey ) != 0.0f;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsScreenEffect( const material::CMaterial &material )
{
	return kv1::EqualsIgnoreCase( material.m_szShader, "screenspace_general" );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
ScreenEffect_t ReadScreenEffect( const material::CMaterial &material )
{
	ScreenEffect_t effect;
	if ( const material::Parameter_t *pFound = material.Find( "$pixshader" ) )
		effect.m_szPixelShader = pFound->m_szValue;
	constexpr std::array<std::string_view, 4> TEXTURES{ "$basetexture", "$texture1", "$texture2", "$texture3" };
	constexpr std::array<std::string_view, 4> LINEAR{ "$linearread_basetexture", "$linearread_texture1", "$linearread_texture2", "$linearread_texture3" };
	for ( std::size_t i = 0; i < 4; i++ )
	{
		if ( const material::Parameter_t *pFound = material.Find( TEXTURES[ i ] ) )
			effect.m_Textures[ i ] = pFound->m_szValue;
		effect.m_LinearRead[ i ] = FlagOf( material, LINEAR[ i ] );
	}
	effect.m_bLinearWrite = FlagOf( material, "$linearwrite" );
	effect.m_bCopyAlpha = FlagOf( material, "$copyalpha" );
	// The last one set wins, as the shader applies them in this order.
	if ( FlagOf( material, "$additive" ) )
		effect.m_nBlend = ScreenBlend_t::SCREEN_BLEND_ADDITIVE;
	if ( FlagOf( material, "$alpha_blend_color_overlay" ) )
		effect.m_nBlend = ScreenBlend_t::SCREEN_BLEND_COLOR_OVERLAY;
	if ( FlagOf( material, "$alpha_blend" ) )
		effect.m_nBlend = ScreenBlend_t::SCREEN_BLEND_ALPHA;
	if ( effect.m_bCopyAlpha )
		effect.m_nBlend = ScreenBlend_t::SCREEN_BLEND_NONE;
	for ( int c = 0; c < 4; c++ )
		for ( int i = 0; i < 4; i++ )
			effect.m_Constants[ static_cast<std::size_t>( c ) ][ static_cast<std::size_t>( i ) ] = NumberOf( material, std::format( "$c{}_{}", c, "xyzw"[ i ] ) );
	return effect;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::string> PixelShaderPaths( std::string_view szPixelShader )
{
	std::string szName{ szPixelShader };
	std::ranges::transform( szName, szName.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	if ( szName.ends_with( ".vcs" ) )
		szName.resize( szName.size() - 4 );
	std::vector<std::string> paths;
	if ( szName.ends_with( "_ps20" ) )
		paths.push_back( "shaders/fxc/" + szName + "b.vcs" );
	paths.push_back( "shaders/fxc/" + szName + ".vcs" );
	return paths;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsFrameTexture( std::string_view szTexture )
{
	std::string sz{ szTexture };
	std::ranges::transform( sz, sz.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	return sz.starts_with( "_rt_fullframefb" ) || sz.starts_with( "_rt_smallfb" ) || sz.starts_with( "_rt_fullscreen" ) || sz == "_rt_powoftwofb";
}

//-----------------------------------------------------------------------------
// Purpose: Each pixel run as the full-screen quad's would be: t0 its place on the screen (0 to 1
//          across), t1 nothing, t2 its place again (the bloom offset unset), vPos its pixel.
//-----------------------------------------------------------------------------
std::optional<texture::CImage> ApplyScreenEffect( const CPixelShader &shader, const ScreenEffect_t &effect, const texture::CImage &frame, const std::array<const texture::CImage *, 4> &textures, std::string *pError )
{
	if ( !shader.CanRun() )
	{
		if ( pError )
			*pError = shader.GetProblem();
		return std::nullopt;
	}
	if ( frame.Empty() )
		return frame;
	CPixelShader prepared = shader;
	for ( int c = 0; c < 4; c++ )
		prepared.SetConstant( c, effect.m_Constants[ static_cast<std::size_t>( c ) ] );
	for ( int i = 0; i < 4; i++ )
	{
		const texture::CImage *pTexture = textures[ static_cast<std::size_t>( i ) ];
		if ( pTexture && !pTexture->Empty() )
			prepared.SetConstant( 4 + i, { 1.0f / static_cast<float>( pTexture->m_nWidth ), 1.0f / static_cast<float>( pTexture->m_nHeight ), 0.0f, 0.0f } );
	}
	const Sample_t sample = [ & ]( int nSampler, const Vec4_t &coord, float ) -> Vec4_t {
		if ( nSampler < 0 || nSampler >= 4 || !textures[ static_cast<std::size_t>( nSampler ) ] )
			return {};
		return Sample( *textures[ static_cast<std::size_t>( nSampler ) ], coord[ 0 ], coord[ 1 ], effect.m_LinearRead[ static_cast<std::size_t>( nSampler ) ] );
	};
	texture::CImage out = frame;
	const int nWidth = frame.m_nWidth;
	const int nHeight = frame.m_nHeight;
	ParallelFor( static_cast<std::size_t>( nHeight ), 4, [ & ]( std::size_t nBegin, std::size_t nEnd ) {
		for ( std::size_t y = nBegin; y < nEnd; y++ )
		{
			for ( int x = 0; x < nWidth; x++ )
			{
				PixelInputs_t inputs;
				const float u = ( static_cast<float>( x ) + 0.5f ) / static_cast<float>( nWidth );
				const float v = ( static_cast<float>( y ) + 0.5f ) / static_cast<float>( nHeight );
				inputs.m_Texcoords[ 0 ] = { u, v, 0.0f, 1.0f };
				inputs.m_Texcoords[ 2 ] = { u, v, 0.0f, 1.0f };
				inputs.m_Colors[ 0 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
				inputs.m_Position = { static_cast<float>( x ), static_cast<float>( y ), 0.0f, 1.0f };
				const std::optional<Vec4_t> result = prepared.Run( inputs, sample );
				if ( !result )
					continue; // killed: the frame shows
				std::uint8_t *p = out.Pixel( x, static_cast<int>( y ) );
				// What is behind, in the shader's terms (linear unless it writes as it is).
				Vec4_t colour = *result;
				const float flAlpha = std::clamp( colour[ 3 ], 0.0f, 1.0f );
				for ( std::size_t i = 0; i < 3; i++ )
				{
					const float flBehind = static_cast<float>( p[ i ] ) / 255.0f;
					const float flDest = effect.m_bLinearWrite ? flBehind : ToLinear( flBehind );
					float f = std::clamp( colour[ i ], 0.0f, 1.0f );
					switch ( effect.m_nBlend )
					{
						case ScreenBlend_t::SCREEN_BLEND_ADDITIVE: f = f + flDest; break;
						case ScreenBlend_t::SCREEN_BLEND_COLOR_OVERLAY: f = f + flDest * ( 1.0f - flAlpha ); break;
						case ScreenBlend_t::SCREEN_BLEND_ALPHA: f = f * flAlpha + flDest * ( 1.0f - flAlpha ); break;
						default: break;
					}
					f = effect.m_bLinearWrite ? std::clamp( f, 0.0f, 1.0f ) : ToSrgb( f );
					p[ i ] = static_cast<std::uint8_t>( std::lround( f * 255.0f ) );
				}
				if ( effect.m_bCopyAlpha )
					p[ 3 ] = static_cast<std::uint8_t>( std::lround( flAlpha * 255.0f ) );
			}
		}
	} );
	return out;
}

} // namespace xv::shader
