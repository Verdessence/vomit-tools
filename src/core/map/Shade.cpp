//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Shade.h. map.frag follows these functions step by step.
//
//===========================================================================//
#include "Shade.h"

#include <algorithm>
#include <cmath>

namespace xv::map
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: One mip level, bilinear, with the GPU's texel centres (coordinate * size - 0.5).
//-----------------------------------------------------------------------------
std::array<float, 4> SampleMip( const texture::CImage &image, float u, float v, bool bClampS, bool bClampT )
{
	const float flX = u * static_cast<float>( image.m_nWidth ) - 0.5f;
	const float flY = v * static_cast<float>( image.m_nHeight ) - 0.5f;
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
	const int x0 = wrap( static_cast<int>( flX0 ), image.m_nWidth, bClampS );
	const int x1 = wrap( static_cast<int>( flX0 ) + 1, image.m_nWidth, bClampS );
	const int y0 = wrap( static_cast<int>( flY0 ), image.m_nHeight, bClampT );
	const int y1 = wrap( static_cast<int>( flY0 ) + 1, image.m_nHeight, bClampT );
	const std::uint8_t *a = image.Pixel( x0, y0 );
	const std::uint8_t *b = image.Pixel( x1, y0 );
	const std::uint8_t *c = image.Pixel( x0, y1 );
	const std::uint8_t *d = image.Pixel( x1, y1 );
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
	if ( texture.m_Mips.empty() )
	{
		return false;
	}
	std::size_t nLevels = 1;
	for ( int w = texture.m_Mips.front().m_nWidth, h = texture.m_Mips.front().m_nHeight; w > 1 || h > 1; w = std::max( 1, w / 2 ), h = std::max( 1, h / 2 ) )
	{
		nLevels++;
	}
	return texture.m_Mips.size() >= nLevels;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::array<float, 4> SampleTexture( const RasterTexture_t &texture, float u, float v, float flLod )
{
	if ( texture.m_Mips.empty() )
	{
		return { 1.0f, 1.0f, 1.0f, 1.0f };
	}
	// The mips kept may start below the texture's own size; lod 0 is the full size.
	const float flSkipped = std::log2( static_cast<float>( std::max( texture.m_nWidth, 1 ) ) / static_cast<float>( std::max( texture.m_Mips.front().m_nWidth, 1 ) ) );
	const float flLevel = HasMipChain( texture ) ? std::clamp( flLod - flSkipped, 0.0f, static_cast<float>( texture.m_Mips.size() - 1 ) ) : 0.0f;
	const auto nLow = static_cast<std::size_t>( flLevel );
	const std::size_t nHigh = std::min( nLow + 1, texture.m_Mips.size() - 1 );
	const float t = flLevel - static_cast<float>( nLow );
	const std::array<float, 4> a = SampleMip( texture.m_Mips[ nLow ], u, v, texture.m_bClampS, texture.m_bClampT );
	if ( t <= 0.0f || nHigh == nLow )
	{
		return a;
	}
	const std::array<float, 4> b = SampleMip( texture.m_Mips[ nHigh ], u, v, texture.m_bClampS, texture.m_bClampT );
	std::array<float, 4> out{};
	for ( std::size_t i = 0; i < 4; i++ )
	{
		out[ i ] = a[ i ] + ( b[ i ] - a[ i ] ) * t;
	}
	return out;
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
std::optional<std::array<float, 4>> ShadeFragment( const ShadeMaterial_t &material, const ShadeInput_t &input, const ShadeOptions_t &options )
{
	std::array<float, 4> color{ input.m_vecObjectColor.m_flX, input.m_vecObjectColor.m_flY, input.m_vecObjectColor.m_flZ, 1.0f };
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
			color = SampleTexture( *material.m_pBase, u, v, input.m_flLod );
			if ( material.m_bBlend && material.m_pBase2 )
			{
				// The second texture uses the same texture coordinates, scaled to its own size.
				const float u2 = input.m_Texel[ 0 ] / static_cast<float>( std::max( material.m_pBase2->m_nWidth, 1 ) );
				const float v2 = input.m_Texel[ 1 ] / static_cast<float>( std::max( material.m_pBase2->m_nHeight, 1 ) );
				const std::array<float, 4> second = SampleTexture( *material.m_pBase2, u2, v2, input.m_flLod );
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
	if ( ( options.m_nMode == RasterMode_t::RASTER_MODE_FLAT || options.m_nMode == RasterMode_t::RASTER_MODE_TEXTURED_SHADED ) && !material.m_bUnlit )
	{
		const float flShade = FaceShade( input.m_vecNormal, options.m_vecFaceShade );
		color[ 0 ] *= flShade;
		color[ 1 ] *= flShade;
		color[ 2 ] *= flShade;
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
