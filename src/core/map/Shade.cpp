//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Shade.h. map.frag follows these functions step by step.
//
//===========================================================================//
#include "Shade.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "map/Lighting.h"

namespace xv::map
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: One mip level, bilinear, with the GPU's texel centres (coordinate * size - 0.5);
//          pfnTexel( x, y ) gives a texel's RGBA.
//-----------------------------------------------------------------------------
template <class Texel_t>
std::array<float, 4> SampleMip( int nWidth, int nHeight, float u, float v, bool bClampS, bool bClampT, Texel_t &&pfnTexel )
{
	const float flX = u * static_cast<float>( nWidth ) - 0.5f;
	const float flY = v * static_cast<float>( nHeight ) - 0.5f;
	const float flX0 = std::floor( flX );
	const float flY0 = std::floor( flY );
	const float fx = flX - flX0;
	const float fy = flY - flY0;
	auto wrap = []( int n, int nSize, bool bClamp ) {
		if ( bClamp )
		{
			return std::clamp( n, 0, nSize - 1 );
		}
		const int m = n % nSize;
		return m < 0 ? m + nSize : m;
	};
	const int x0 = wrap( static_cast<int>( flX0 ), nWidth, bClampS );
	const int x1 = wrap( static_cast<int>( flX0 ) + 1, nWidth, bClampS );
	const int y0 = wrap( static_cast<int>( flY0 ), nHeight, bClampT );
	const int y1 = wrap( static_cast<int>( flY0 ) + 1, nHeight, bClampT );
	const std::array<std::uint8_t, 4> a = pfnTexel( x0, y0 );
	const std::array<std::uint8_t, 4> b = pfnTexel( x1, y0 );
	const std::array<std::uint8_t, 4> c = pfnTexel( x0, y1 );
	const std::array<std::uint8_t, 4> d = pfnTexel( x1, y1 );
	std::array<float, 4> out{};
	for ( std::size_t i = 0; i < 4; i++ )
	{
		const float flTop = a[ i ] + ( b[ i ] - a[ i ] ) * fx;
		const float flBottom = c[ i ] + ( d[ i ] - c[ i ] ) * fx;
		out[ i ] = ( flTop + ( flBottom - flTop ) * fy ) / 255.0f;
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: A 5:6:5 colour as 8 bits a channel.
//-----------------------------------------------------------------------------
std::array<int, 3> Expand565( unsigned nColor )
{
	const unsigned r = ( nColor >> 11 ) & 31;
	const unsigned g = ( nColor >> 5 ) & 63;
	const unsigned b = nColor & 31;
	return { static_cast<int>( ( r << 3 ) | ( r >> 2 ) ), static_cast<int>( ( g << 2 ) | ( g >> 4 ) ), static_cast<int>( ( b << 3 ) | ( b >> 2 ) ) };
}

//-----------------------------------------------------------------------------
// Purpose: A texel of a DXT mip, decoded from its 4x4 block alone (S3TC): a colour half of two
//          5:6:5 end colours and a 2-bit index a texel, after DXT3's 4-bit alphas or DXT5's two
//          end alphas with 3-bit indices.
//-----------------------------------------------------------------------------
std::array<std::uint8_t, 4> CompressedTexel( const CompressedMip_t &mip, texture::VtfFormat_t nFormat, int x, int y )
{
	const bool bAlphaBlock = nFormat == texture::VtfFormat_t::DXT3 || nFormat == texture::VtfFormat_t::DXT5;
	const std::size_t nBlockSize = bAlphaBlock ? 16 : 8;
	const std::size_t nBlock = static_cast<std::size_t>( y / 4 ) * static_cast<std::size_t>( ( mip.m_nWidth + 3 ) / 4 ) + static_cast<std::size_t>( x / 4 );
	if ( ( nBlock + 1 ) * nBlockSize > mip.m_Data.size() )
	{
		return { 255, 0, 255, 255 };
	}
	const auto *p = reinterpret_cast<const std::uint8_t *>( mip.m_Data.data() ) + nBlock * nBlockSize;
	const int nTexel = ( y & 3 ) * 4 + ( x & 3 );
	int nAlpha = 255;
	if ( nFormat == texture::VtfFormat_t::DXT3 )
	{
		nAlpha = ( ( p[ nTexel / 2 ] >> ( ( nTexel & 1 ) * 4 ) ) & 15 ) * 17;
	}
	else if ( nFormat == texture::VtfFormat_t::DXT5 )
	{
		const int a0 = p[ 0 ];
		const int a1 = p[ 1 ];
		std::uint64_t nBits = 0;
		for ( int i = 0; i < 6; i++ )
		{
			nBits |= static_cast<std::uint64_t>( p[ 2 + i ] ) << ( 8 * i );
		}
		const int nIndex = static_cast<int>( ( nBits >> ( 3 * nTexel ) ) & 7 );
		if ( nIndex == 0 || nIndex == 1 )
		{
			nAlpha = nIndex == 0 ? a0 : a1;
		}
		else if ( a0 > a1 )
		{
			nAlpha = ( ( 8 - nIndex ) * a0 + ( nIndex - 1 ) * a1 ) / 7;
		}
		else
		{
			nAlpha = nIndex == 6 ? 0 : nIndex == 7 ? 255
												   : ( ( 6 - nIndex ) * a0 + ( nIndex - 1 ) * a1 ) / 5;
		}
	}
	const std::uint8_t *c = p + ( bAlphaBlock ? 8 : 0 );
	const unsigned nColor0 = c[ 0 ] | ( c[ 1 ] << 8 );
	const unsigned nColor1 = c[ 2 ] | ( c[ 3 ] << 8 );
	const unsigned nIndices = c[ 4 ] | ( c[ 5 ] << 8 ) | ( c[ 6 ] << 16 ) | ( static_cast<unsigned>( c[ 7 ] ) << 24 );
	const unsigned nIndex = ( nIndices >> ( 2 * nTexel ) ) & 3;
	const std::array<int, 3> e0 = Expand565( nColor0 );
	const std::array<int, 3> e1 = Expand565( nColor1 );
	std::array<int, 3> rgb{};
	// DXT1 with the first colour not above the second has three colours and transparent black.
	const bool bFour = bAlphaBlock || nColor0 > nColor1;
	for ( std::size_t i = 0; i < 3; i++ )
	{
		switch ( nIndex )
		{
			case 0: rgb[ i ] = e0[ i ]; break;
			case 1: rgb[ i ] = e1[ i ]; break;
			case 2: rgb[ i ] = bFour ? ( 2 * e0[ i ] + e1[ i ] ) / 3 : ( e0[ i ] + e1[ i ] ) / 2; break;
			default: rgb[ i ] = bFour ? ( e0[ i ] + 2 * e1[ i ] ) / 3 : 0; break;
		}
	}
	if ( !bFour && nIndex == 3 )
	{
		nAlpha = 0;
	}
	return { static_cast<std::uint8_t>( rgb[ 0 ] ), static_cast<std::uint8_t>( rgb[ 1 ] ), static_cast<std::uint8_t>( rgb[ 2 ] ), static_cast<std::uint8_t>( nAlpha ) };
}

//-----------------------------------------------------------------------------
// Purpose: A mip level of whichever set the texture has: decoded, or DXT read as it is.
//-----------------------------------------------------------------------------
std::array<float, 4> SampleLevel( const RasterTexture_t &texture, std::size_t nLevel, float u, float v )
{
	if ( !texture.m_Mips.empty() )
	{
		const texture::CImage &image = texture.m_Mips[ nLevel ];
		return SampleMip( image.m_nWidth, image.m_nHeight, u, v, texture.m_bClampS, texture.m_bClampT, [ & ]( int x, int y ) {
			const std::uint8_t *p = image.Pixel( x, y );
			return std::array<std::uint8_t, 4>{ p[ 0 ], p[ 1 ], p[ 2 ], p[ 3 ] };
		} );
	}
	const CompressedMip_t &mip = texture.m_Compressed[ nLevel ];
	return SampleMip( mip.m_nWidth, mip.m_nHeight, u, v, texture.m_bClampS, texture.m_bClampT, [ & ]( int x, int y ) { return CompressedTexel( mip, texture.m_nCompressedFormat, x, y ); } );
}

//-----------------------------------------------------------------------------
// Purpose: Source's missing-texture checker: magenta and black, four squares a texture.
//-----------------------------------------------------------------------------
std::array<float, 4> MissingChecker( float u, float v )
{
	const int nU = static_cast<int>( std::floor( u * 4.0f ) );
	const int nV = static_cast<int>( std::floor( v * 4.0f ) );
	return ( ( nU + nV ) & 1 ) == 0 ? std::array<float, 4>{ 1.0f, 0.0f, 1.0f, 1.0f } : std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f };
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool HasMipChain( const RasterTexture_t &texture )
{
	if ( texture.GetMipCount() == 0 )
	{
		return false;
	}
	std::size_t nLevels = 1;
	for ( int w = texture.GetTopWidth(), h = texture.GetTopHeight(); w > 1 || h > 1; w = std::max( 1, w / 2 ), h = std::max( 1, h / 2 ) )
	{
		nLevels++;
	}
	return texture.GetMipCount() >= nLevels;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::array<float, 4> SampleTexture( const RasterTexture_t &texture, float u, float v, float flLod, bool bMipBlend )
{
	const std::size_t nLevels = texture.GetMipCount();
	if ( nLevels == 0 )
	{
		return { 1.0f, 1.0f, 1.0f, 1.0f };
	}
	// The mips kept may start below the texture's own size; lod 0 is the full size.
	const float flSkipped = std::log2( static_cast<float>( std::max( texture.m_nWidth, 1 ) ) / static_cast<float>( std::max( texture.GetTopWidth(), 1 ) ) );
	float flLevel = HasMipChain( texture ) ? std::clamp( flLod - flSkipped, 0.0f, static_cast<float>( nLevels - 1 ) ) : 0.0f;
	if ( !bMipBlend )
	{
		flLevel = std::floor( flLevel + 0.5f );
	}
	const auto nLow = static_cast<std::size_t>( flLevel );
	const std::size_t nHigh = std::min( nLow + 1, nLevels - 1 );
	const float t = flLevel - static_cast<float>( nLow );
	const std::array<float, 4> a = SampleLevel( texture, nLow, u, v );
	if ( t <= 0.0f || nHigh == nLow )
	{
		return a;
	}
	const std::array<float, 4> b = SampleLevel( texture, nHigh, u, v );
	std::array<float, 4> out{};
	for ( std::size_t i = 0; i < 4; i++ )
	{
		out[ i ] = a[ i ] + ( b[ i ] - a[ i ] ) * t;
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: The footprint's long side is the larger of the two gradients; the samples spread
//          along it, each at the level its share of the side (or the short side) needs.
//-----------------------------------------------------------------------------
std::array<float, 4> SampleFiltered( const RasterTexture_t &texture, std::array<float, 2> texel, const ShadeInput_t &input, const ShadeOptions_t &options )
{
	const float flWidth = static_cast<float>( std::max( texture.m_nWidth, 1 ) );
	const float flHeight = static_cast<float>( std::max( texture.m_nHeight, 1 ) );
	if ( options.m_nAnisotropy <= 1 )
	{
		return SampleTexture( texture, texel[ 0 ] / flWidth, texel[ 1 ] / flHeight, input.m_flLod, options.m_bMipBlend );
	}
	const std::array<float, 2> across{ input.m_Gradient[ 0 ], input.m_Gradient[ 1 ] };
	const std::array<float, 2> down{ input.m_Gradient[ 2 ], input.m_Gradient[ 3 ] };
	const float flAcross = std::hypot( across[ 0 ], across[ 1 ] );
	const float flDown = std::hypot( down[ 0 ], down[ 1 ] );
	const float flMajor = std::max( flAcross, flDown );
	const float flMinor = std::min( flAcross, flDown );
	const std::array<float, 2> &axis = flAcross >= flDown ? across : down;
	const float flMost = static_cast<float>( options.m_nAnisotropy );
	const float flCount = std::max( 1.0f, flMinor > 0.0f ? std::min( std::ceil( flMajor / flMinor ), flMost ) : flMost );
	const float flSpan = std::max( flMajor / flCount, flMinor );
	const float flLod = flSpan > 1.0f ? std::log2( flSpan ) : 0.0f;
	std::array<float, 4> sum{};
	const int nCount = static_cast<int>( flCount );
	for ( int i = 0; i < nCount; i++ )
	{
		const float t = ( static_cast<float>( i ) + 0.5f ) / flCount - 0.5f;
		const std::array<float, 4> sample = SampleTexture( texture, ( texel[ 0 ] + axis[ 0 ] * t ) / flWidth, ( texel[ 1 ] + axis[ 1 ] * t ) / flHeight, flLod );
		for ( std::size_t c = 0; c < 4; c++ )
		{
			sum[ c ] += sample[ c ];
		}
	}
	for ( float &flChannel : sum )
	{
		flChannel /= flCount;
	}
	return sum;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float FaceShade( CVec3 vecNormal, CVec3 vecShade )
{
	return vecNormal.m_flX * vecNormal.m_flX * vecShade.m_flX + vecNormal.m_flY * vecNormal.m_flY * vecShade.m_flY + vecNormal.m_flZ * vecNormal.m_flZ * vecShade.m_flZ;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float LightFactor( float flLight, float flScale )
{
	return 2.0f * std::pow( std::max( flLight * flScale * 0.5f, 0.0f ), 1.0f / 2.2f );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::array<float, 4>> ShadeFragment( const ShadeMaterial_t &material, const ShadeInput_t &input, const ShadeOptions_t &options )
{
	std::array<float, 4> color{ input.m_vecObjectColor.m_flX, input.m_vecObjectColor.m_flY, input.m_vecObjectColor.m_flZ, 1.0f };
	float flGlow = 0.0f;           // $selfillum's mask: the base texture's alpha
	std::array<float, 3> albedo{}; // the texture's colour, before light
	const bool bTextured = options.m_nMode != RasterMode_t::RASTER_MODE_FLAT && options.m_nMode != RasterMode_t::RASTER_MODE_WIREFRAME;
	if ( bTextured && material.m_bSky && options.m_pSky )
	{
		// Hammer++ shows the map's sky where its sky faces are, the way the game does.
		const std::array<float, 3> sky = sky::Sample( *options.m_pSky, input.m_vecDirection );
		color = { sky[ 0 ] / 255.0f, sky[ 1 ] / 255.0f, sky[ 2 ] / 255.0f, 1.0f };
	}
	else if ( bTextured )
	{
		const int nWidth = material.m_pBase ? material.m_pBase->m_nWidth : material.m_nWidth;
		const int nHeight = material.m_pBase ? material.m_pBase->m_nHeight : material.m_nHeight;
		const float u = input.m_Texel[ 0 ] / static_cast<float>( std::max( nWidth, 1 ) );
		const float v = input.m_Texel[ 1 ] / static_cast<float>( std::max( nHeight, 1 ) );
		if ( material.m_bMissing )
		{
			color = MissingChecker( u, v );
		}
		else if ( material.m_pBase )
		{
			color = SampleFiltered( *material.m_pBase, input.m_Texel, input, options );
			if ( material.m_bBlend && material.m_pBase2 )
			{
				// The second texture uses the same texture coordinates, scaled to its own size.
				const std::array<float, 4> second = SampleFiltered( *material.m_pBase2, input.m_Texel, input, options );
				for ( std::size_t i = 0; i < 4; i++ )
				{
					color[ i ] += ( second[ i ] - color[ i ] ) * input.m_flBlend;
				}
			}
		}
		else
		{
			color = { 1.0f, 1.0f, 1.0f, 1.0f };
		}
		if ( material.m_bAlphaTest && color[ 3 ] < material.m_flAlphaReference )
		{
			return std::nullopt;
		}
		color[ 0 ] *= material.m_vecColor.m_flX;
		color[ 1 ] *= material.m_vecColor.m_flY;
		color[ 2 ] *= material.m_vecColor.m_flZ;
		if ( material.m_bTinted )
		{
			color[ 0 ] *= input.m_vecObjectColor.m_flX;
			color[ 1 ] *= input.m_vecObjectColor.m_flY;
			color[ 2 ] *= input.m_vecObjectColor.m_flZ;
		}
		flGlow = material.m_bSelfIllum ? color[ 3 ] : 0.0f;
		albedo = { color[ 0 ], color[ 1 ], color[ 2 ] };
		color[ 3 ] = material.m_bTranslucent || material.m_bAdditive ? color[ 3 ] * material.m_flAlpha : material.m_flAlpha;
		if ( options.m_nMode == RasterMode_t::RASTER_MODE_LIGHTMAP_GRID && !material.m_bUnlit )
		{
			// Every other luxel darker, so the lightmap's resolution shows on the face.
			const int nU = static_cast<int>( std::floor( input.m_Luxel[ 0 ] ) );
			const int nV = static_cast<int>( std::floor( input.m_Luxel[ 1 ] ) );
			const float flShade = ( ( nU + nV ) & 1 ) != 0 ? options.m_flLuxelShade : 1.0f;
			color[ 0 ] *= flShade;
			color[ 1 ] *= flShade;
			color[ 2 ] *= flShade;
		}
	}
	const float flKind = input.m_Light[ 3 ];
	const bool bVertexLit = flKind > 1.5f && flKind < 2.5f;
	if ( options.m_nMode == RasterMode_t::RASTER_MODE_LIGHTING && ( !material.m_bUnlit || bVertexLit ) && !( material.m_bSky && options.m_pSky ) )
	{
		std::array<float, 3> light{};
		bool bLit = false;
		if ( flKind > 0.5f && flKind < 1.5f && options.m_pLighting )
		{
			light = options.m_pLighting->Sample( input.m_Light[ 0 ], input.m_Light[ 1 ] );
			bLit = true;
		}
		else if ( bVertexLit )
		{
			light = { input.m_Light[ 0 ], input.m_Light[ 1 ], input.m_Light[ 2 ] };
			bLit = true;
		}
		if ( bLit )
		{
			const std::array<float, 3> tint{ material.m_vecSelfIllumTint.m_flX, material.m_vecSelfIllumTint.m_flY, material.m_vecSelfIllumTint.m_flZ };
			for ( std::size_t i = 0; i < 3; i++ )
			{
				color[ i ] *= LightFactor( light[ i ], options.m_flLightScale );
				// Self-illuminated: toward the texture times the tint, by the mask, as the game.
				color[ i ] += ( albedo[ i ] * tint[ i ] - color[ i ] ) * flGlow;
			}
		}
		else if ( flKind < 0.5f || !options.m_pLighting )
		{
			// Not baked (yet): the shaded look. Fullbright faces (tool textures) stay as they are.
			const float flShade = FaceShade( input.m_vecNormal, options.m_vecFaceShade );
			color[ 0 ] *= flShade;
			color[ 1 ] *= flShade;
			color[ 2 ] *= flShade;
		}
	}
	if ( ( options.m_nMode == RasterMode_t::RASTER_MODE_FLAT || options.m_nMode == RasterMode_t::RASTER_MODE_TEXTURED_SHADED ) && !material.m_bUnlit )
	{
		const float flShade = FaceShade( input.m_vecNormal, options.m_vecFaceShade );
		color[ 0 ] *= flShade;
		color[ 1 ] *= flShade;
		color[ 2 ] *= flShade;
	}
	if ( input.m_bInstance )
	{
		color[ 0 ] += ( options.m_vecInstance.m_flX - color[ 0 ] ) * options.m_flInstanceTint;
		color[ 1 ] += ( options.m_vecInstance.m_flY - color[ 1 ] ) * options.m_flInstanceTint;
		color[ 2 ] += ( options.m_vecInstance.m_flZ - color[ 2 ] ) * options.m_flInstanceTint;
	}
	if ( input.m_bInvalid )
	{
		color[ 0 ] += ( options.m_vecInvalid.m_flX - color[ 0 ] ) * options.m_flInvalidTint;
		color[ 1 ] += ( options.m_vecInvalid.m_flY - color[ 1 ] ) * options.m_flInvalidTint;
		color[ 2 ] += ( options.m_vecInvalid.m_flZ - color[ 2 ] ) * options.m_flInvalidTint;
	}
	if ( input.m_bSelected )
	{
		color[ 0 ] += ( options.m_vecSelection.m_flX - color[ 0 ] ) * options.m_flSelectionTint;
		color[ 1 ] += ( options.m_vecSelection.m_flY - color[ 1 ] ) * options.m_flSelectionTint;
		color[ 2 ] += ( options.m_vecSelection.m_flZ - color[ 2 ] ) * options.m_flSelectionTint;
	}
	return color;
}

} // namespace xv::map
