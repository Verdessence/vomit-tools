//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Build.h.
//
//===========================================================================//
#include "Build.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <utility>

namespace xv::texture
{

//-----------------------------------------------------------------------------
// Purpose: A side's length after resizing.
//-----------------------------------------------------------------------------
int TargetSize( int nSize, ResizeMode_t nResize, int nMaxSize )
{
	int nTarget = std::max( nSize, 1 );
	switch ( nResize )
	{
		case ResizeMode_t::RESIZE_NEAREST: nTarget = NearestPowerOfTwo( nTarget ); break;
		case ResizeMode_t::RESIZE_UP: nTarget = static_cast<int>( std::bit_ceil( static_cast<unsigned>( nTarget ) ) ); break;
		case ResizeMode_t::RESIZE_DOWN: nTarget = static_cast<int>( std::bit_floor( static_cast<unsigned>( nTarget ) ) ); break;
		case ResizeMode_t::RESIZE_MULTIPLE_OF_4: nTarget = std::max( 4, ( nTarget + 2 ) / 4 * 4 ); break;
		case ResizeMode_t::RESIZE_KEEP: break;
	}
	if ( nMaxSize > 0 )
	{
		nTarget = std::min( nTarget, nResize == ResizeMode_t::RESIZE_MULTIPLE_OF_4 ? std::max( 4, nMaxSize / 4 * 4 ) : nMaxSize );
	}
	return std::clamp( nTarget, 1, 65535 );
}

//-----------------------------------------------------------------------------
// Purpose: Both sides after resizing, keeping the shape when the options say so.
//-----------------------------------------------------------------------------
std::pair<int, int> TargetSizes( int nWidth, int nHeight, const BuildOptions_t &options )
{
	if ( !options.m_bKeepAspect || nWidth == nHeight )
	{
		return { TargetSize( nWidth, options.m_nResize, options.m_nMaxSize ), TargetSize( nHeight, options.m_nResize, options.m_nMaxSize ) };
	}
	const bool bWide = nWidth > nHeight;
	const int nLong = bWide ? nWidth : nHeight;
	const int nShort = bWide ? nHeight : nWidth;
	const int nLongTarget = TargetSize( nLong, options.m_nResize, options.m_nMaxSize );
	const int nShortScaled = std::max( 1, static_cast<int>( std::lround( static_cast<double>( nShort ) * nLongTarget / nLong ) ) );
	const int nShortTarget = TargetSize( nShortScaled, options.m_nResize, 0 );
	return bWide ? std::pair{ nLongTarget, nShortTarget } : std::pair{ nShortTarget, nLongTarget };
}

namespace
{

//-----------------------------------------------------------------------------
// Purpose: Squared distances to the nearest marked cell along one line (Felzenszwalb and
//          Huttenlocher's lower envelope of parabolas). f holds 0 at marked cells, a big number
//          elsewhere.
//-----------------------------------------------------------------------------
void DistanceLine( std::vector<float> &f )
{
	const int n = static_cast<int>( f.size() );
	std::vector<float> d( f.size() );
	std::vector<int> v( f.size() );
	std::vector<float> z( f.size() + 1 );
	int k = 0;
	v[ 0 ] = 0;
	z[ 0 ] = -1e20f;
	z[ 1 ] = 1e20f;
	for ( int q = 1; q < n; q++ )
	{
		float s = 0.0f;
		while ( true )
		{
			const int p = v[ static_cast<std::size_t>( k ) ];
			s = ( ( f[ static_cast<std::size_t>( q ) ] + static_cast<float>( q * q ) ) - ( f[ static_cast<std::size_t>( p ) ] + static_cast<float>( p * p ) ) ) / static_cast<float>( 2 * q - 2 * p );
			if ( s > z[ static_cast<std::size_t>( k ) ] || k == 0 )
			{
				if ( s <= z[ static_cast<std::size_t>( k ) ] )
				{
					k--;
				}
				break;
			}
			k--;
		}
		if ( k < 0 )
		{
			k = 0;
		}
		else
		{
			k++;
		}
		v[ static_cast<std::size_t>( k ) ] = q;
		z[ static_cast<std::size_t>( k ) ] = s;
		z[ static_cast<std::size_t>( k ) + 1 ] = 1e20f;
	}
	k = 0;
	for ( int q = 0; q < n; q++ )
	{
		while ( z[ static_cast<std::size_t>( k ) + 1 ] < static_cast<float>( q ) )
		{
			k++;
		}
		const int p = v[ static_cast<std::size_t>( k ) ];
		d[ static_cast<std::size_t>( q ) ] = static_cast<float>( ( q - p ) * ( q - p ) ) + f[ static_cast<std::size_t>( p ) ];
	}
	f = std::move( d );
}

//-----------------------------------------------------------------------------
// Purpose: Each cell's distance to the nearest cell with bInside set as given.
//-----------------------------------------------------------------------------
std::vector<float> DistanceTo( const std::vector<bool> &marked, int nWidth, int nHeight )
{
	std::vector<float> grid( marked.size() );
	for ( std::size_t i = 0; i < marked.size(); i++ )
	{
		grid[ i ] = marked[ i ] ? 0.0f : 1e20f;
	}
	std::vector<float> line;
	for ( int x = 0; x < nWidth; x++ )
	{
		line.resize( static_cast<std::size_t>( nHeight ) );
		for ( int y = 0; y < nHeight; y++ )
		{
			line[ static_cast<std::size_t>( y ) ] = grid[ static_cast<std::size_t>( y ) * static_cast<std::size_t>( nWidth ) + static_cast<std::size_t>( x ) ];
		}
		DistanceLine( line );
		for ( int y = 0; y < nHeight; y++ )
		{
			grid[ static_cast<std::size_t>( y ) * static_cast<std::size_t>( nWidth ) + static_cast<std::size_t>( x ) ] = line[ static_cast<std::size_t>( y ) ];
		}
	}
	for ( int y = 0; y < nHeight; y++ )
	{
		line.assign( grid.begin() + static_cast<std::ptrdiff_t>( y ) * nWidth, grid.begin() + static_cast<std::ptrdiff_t>( y + 1 ) * nWidth );
		DistanceLine( line );
		std::copy( line.begin(), line.end(), grid.begin() + static_cast<std::ptrdiff_t>( y ) * nWidth );
	}
	for ( float &flValue : grid )
	{
		flValue = std::sqrt( flValue );
	}
	return grid;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Alpha as a signed distance field around the edge at 128.
//-----------------------------------------------------------------------------
CImage DistanceAlpha( const CImage &image, float flSpread )
{
	const std::size_t nCount = static_cast<std::size_t>( image.m_nWidth ) * static_cast<std::size_t>( image.m_nHeight );
	std::vector<bool> inside( nCount );
	std::vector<bool> outside( nCount );
	for ( std::size_t i = 0; i < nCount; i++ )
	{
		inside[ i ] = image.m_Pixels[ i * 4 + 3 ] >= 128;
		outside[ i ] = !inside[ i ];
	}
	const std::vector<float> toInside = DistanceTo( inside, image.m_nWidth, image.m_nHeight );
	const std::vector<float> toOutside = DistanceTo( outside, image.m_nWidth, image.m_nHeight );
	CImage out = image;
	for ( std::size_t i = 0; i < nCount; i++ )
	{
		// Positive inside the shape; the edge lies half a texel from each side's texels.
		const float flDistance = inside[ i ] ? toOutside[ i ] - 0.5f : -( toInside[ i ] - 0.5f );
		const float flAlpha = 0.5f + flDistance / ( 2.0f * std::max( flSpread, 0.01f ) );
		out.m_Pixels[ i * 4 + 3 ] = static_cast<std::uint8_t>( std::lround( std::clamp( flAlpha, 0.0f, 1.0f ) * 255.0f ) );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Whether any picture has a texel that isn't solid.
//-----------------------------------------------------------------------------
bool HasClearTexels( std::span<const SourceImage_t> sources )
{
	for ( const SourceImage_t &source : sources )
	{
		const auto &pixels = source.m_Image.m_Pixels;
		for ( std::size_t i = 3; i < pixels.size(); i += 4 )
		{
			if ( pixels[ i ] != 255 )
			{
				return true;
			}
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: A height map (brightness) as a normal map; the edges wrap.
//-----------------------------------------------------------------------------
CImage HeightToNormal( const CImage &height, float flScale, bool bGreenUp )
{
	CImage out{ height.m_nWidth, height.m_nHeight };
	const auto heightAt = [ &height ]( int x, int y ) {
		x = ( x % height.m_nWidth + height.m_nWidth ) % height.m_nWidth;
		y = ( y % height.m_nHeight + height.m_nHeight ) % height.m_nHeight;
		const std::uint8_t *p = height.Pixel( x, y );
		return ( p[ 0 ] * 0.299f + p[ 1 ] * 0.587f + p[ 2 ] * 0.114f ) / 255.0f;
	};
	for ( int y = 0; y < height.m_nHeight; y++ )
	{
		for ( int x = 0; x < height.m_nWidth; x++ )
		{
			// Slopes across and down the picture.
			const float flDx = ( heightAt( x + 1, y ) - heightAt( x - 1, y ) ) * 0.5f * flScale;
			const float flDy = ( heightAt( x, y + 1 ) - heightAt( x, y - 1 ) ) * 0.5f * flScale;
			// The surface leans away from where it rises; green says how it leans down the picture
			// (or up it).
			std::array<float, 3> n{ -flDx, bGreenUp ? flDy : -flDy, 1.0f };
			const float flLength = std::sqrt( n[ 0 ] * n[ 0 ] + n[ 1 ] * n[ 1 ] + n[ 2 ] * n[ 2 ] );
			std::uint8_t *d = out.Pixel( x, y );
			for ( std::size_t c = 0; c < 3; c++ )
			{
				d[ c ] = static_cast<std::uint8_t>( std::lround( ( n[ c ] / flLength * 0.5f + 0.5f ) * 255.0f ) );
			}
			d[ 3 ] = height.Pixel( x, y )[ 3 ];
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Every picture is sized to the first one's target size.
//-----------------------------------------------------------------------------
std::vector<std::byte> BuildVtf( std::span<const SourceImage_t> sources, const BuildOptions_t &options, std::string *pError )
{
	const auto fail = [ pError ]( std::string szMessage ) {
		if ( pError )
		{
			*pError = std::move( szMessage );
		}
		return std::vector<std::byte>{};
	};
	if ( sources.empty() )
	{
		return fail( "no pictures" );
	}
	if ( options.m_nFaces != 1 && options.m_nFaces != 6 )
	{
		return fail( "a texture has 1 face, or 6 for a cube map" );
	}
	const std::size_t nDepth = static_cast<std::size_t>( std::max( 1, options.m_Write.m_nDepth ) );
	if ( sources.size() % static_cast<std::size_t>( options.m_nFaces ) != 0 )
	{
		return fail( "a cube map takes 6 pictures for each frame" );
	}
	if ( nDepth > 1 && ( options.m_nFaces != 1 || options.m_Write.m_nVersionMinor < 2 ) )
	{
		return fail( "a volume texture has one face and needs version 7.2 or later" );
	}
	if ( sources.size() % nDepth != 0 )
	{
		return fail( "every frame of a volume texture needs all its slices" );
	}
	if ( !CanEncode( options.m_Write.m_nFormat ) )
	{
		return fail( "this format can't be written" );
	}
	const SourceImage_t &first = sources.front();
	const bool bFirstLight = !first.m_Light.Empty();
	const int nSourceWidth = bFirstLight ? first.m_Light.m_nWidth : first.m_Image.m_nWidth;
	const int nSourceHeight = bFirstLight ? first.m_Light.m_nHeight : first.m_Image.m_nHeight;
	if ( nSourceWidth <= 0 || nSourceHeight <= 0 )
	{
		return fail( "an empty picture" );
	}
	const auto [ nWidth, nHeight ] = TargetSizes( nSourceWidth, nSourceHeight, options );
	// The distance field is worked out at the pictures' own size, in texels of the made texture.
	const float flSpreadInSource = options.m_flDistanceSpread * static_cast<float>( nSourceWidth ) / static_cast<float>( std::max( 1, nWidth ) );

	// 8-bit pictures for the alpha flags and the 8-bit formats; light values for the float ones.
	std::vector<CImage> images;
	std::vector<CFloatImage> light;
	// Pictures with see-through parts can take another format.
	VtfFormat_t nFormat = options.m_Write.m_nFormat;
	if ( options.m_nAlphaFormat != VtfFormat_t::VTF_FORMAT_NONE && CanEncode( options.m_nAlphaFormat ) && !options.m_bStripAlpha && ( options.m_bDistanceAlpha || HasClearTexels( sources ) ) )
	{
		nFormat = options.m_nAlphaFormat;
	}
	const bool bFloat = IsFloatFormat( nFormat );
	for ( const SourceImage_t &source : sources )
	{
		const bool bLight = !source.m_Light.Empty() && !options.m_bHeightToNormal;
		CImage full = !source.m_Light.Empty() ? ToEightBit( source.m_Light ) : source.m_Image;
		if ( options.m_bDistanceAlpha )
		{
			full = DistanceAlpha( full, flSpreadInSource );
		}
		CImage image = Resize( full, nWidth, nHeight );
		if ( options.m_bHeightToNormal )
		{
			image = HeightToNormal( image, options.m_flNormalScale, options.m_bGreenUp );
		}
		if ( options.m_bStripAlpha )
		{
			for ( std::size_t i = 3; i < image.m_Pixels.size(); i += 4 )
			{
				image.m_Pixels[ i ] = 255;
			}
		}
		if ( bFloat )
		{
			CFloatImage values = bLight ? Resize( source.m_Light, nWidth, nHeight ) : ToFloat( image );
			if ( options.m_bStripAlpha )
			{
				for ( std::size_t i = 3; i < values.m_Pixels.size(); i += 4 )
				{
					values.m_Pixels[ i ] = 1.0f;
				}
			}
			light.push_back( std::move( values ) );
		}
		images.push_back( std::move( image ) );
	}

	VtfWriteOptions_t write = options.m_Write;
	write.m_nFormat = nFormat;
	write.m_nFlags &= ~( vtf_flag::ONEBITALPHA | vtf_flag::EIGHTBITALPHA | vtf_flag::ENVMAP );
	if ( options.m_bHeightToNormal )
	{
		write.m_nFlags |= vtf_flag::NORMAL;
	}
	write.m_bNormalMap = ( write.m_nFlags & vtf_flag::NORMAL ) != 0;
	if ( options.m_bAlphaFlags && HasAlpha( write.m_nFormat ) )
	{
		write.m_nFlags |= AlphaFlags( images );
	}
	auto bytes = bFloat ? WriteVtf( std::span<const CFloatImage>{ light }, options.m_nFaces, write ) : WriteVtf( std::span<const CImage>{ images }, options.m_nFaces, write );
	if ( bytes.empty() )
	{
		return fail( "the pictures couldn't be written" );
	}
	return bytes;
}

//-----------------------------------------------------------------------------
// Purpose: The full-size pictures of a VTF, frame after frame with its faces (without the sphere
//          map), to build it again.
//-----------------------------------------------------------------------------
std::vector<SourceImage_t> SourcesOf( const CVtf &vtf )
{
	const VtfInfo_t &info = vtf.m_Info;
	const int nFaces = info.m_nFaces == 7 ? 6 : info.m_nFaces;
	const bool bFloat = IsFloatFormat( info.m_nFormat );
	std::vector<SourceImage_t> sources;
	for ( int nFrame = 0; nFrame < info.m_nFrames; nFrame++ )
	{
		for ( int nFace = 0; nFace < nFaces; nFace++ )
		{
			for ( int nSlice = 0; nSlice < std::max( 1, info.m_nDepth ); nSlice++ )
			{
				SourceImage_t source;
				const auto bytes = vtf.Image( 0, nFrame, nFace, nSlice );
				if ( auto image = DecodeImage( info.m_nFormat, info.m_nWidth, info.m_nHeight, bytes ) )
				{
					source.m_Image = std::move( *image );
				}
				if ( bFloat )
				{
					if ( auto light = DecodeFloat( info.m_nFormat, info.m_nWidth, info.m_nHeight, bytes ) )
					{
						source.m_Light = std::move( *light );
					}
				}
				sources.push_back( std::move( source ) );
			}
		}
	}
	return sources;
}

//-----------------------------------------------------------------------------
// Purpose: The options a VTF was written with, as far as the file tells.
//-----------------------------------------------------------------------------
BuildOptions_t OptionsOf( const CVtf &vtf )
{
	const VtfInfo_t &info = vtf.m_Info;
	BuildOptions_t options;
	options.m_Write.m_nFormat = info.m_nFormat;
	options.m_Write.m_nFlags = info.m_nFlags & ~( vtf_flag::ONEBITALPHA | vtf_flag::EIGHTBITALPHA | vtf_flag::ENVMAP );
	options.m_Write.m_bMipmaps = info.m_nMipCount > 1;
	options.m_Write.m_bLowRes = info.m_nLowResFormat != VtfFormat_t::VTF_FORMAT_NONE && info.m_nLowResWidth > 0 && info.m_nLowResHeight > 0;
	options.m_Write.m_nVersionMinor = std::clamp( info.m_nVersionMinor, 1, 5 );
	options.m_Write.m_nFirstFrame = info.m_nFaces == 7 || info.m_nFirstFrame == 0xFFFF ? 0 : info.m_nFirstFrame;
	options.m_Write.m_flBumpScale = info.m_flBumpScale;
	options.m_Write.m_Resources = ExtraResources( vtf );
	options.m_nFaces = info.m_nFaces == 1 ? 1 : 6;
	options.m_Write.m_nDepth = std::max( 1, info.m_nDepth );
	options.m_nResize = ResizeMode_t::RESIZE_KEEP;
	options.m_bAlphaFlags = true;
	return options;
}

} // namespace xv::texture
