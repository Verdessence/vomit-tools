//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Sheet.h.
//
//===========================================================================//
#include "Sheet.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <sstream>

namespace xv::texture
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <typename T>
T ReadAt( std::span<const std::byte> data, std::size_t nOffset )
{
	T value{};
	std::memcpy( &value, data.data() + nOffset, sizeof( T ) );
	return value;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <typename T>
void Append( std::vector<std::byte> &out, T value )
{
	const auto *p = reinterpret_cast<const std::byte *>( &value );
	out.insert( out.end(), p, p + sizeof( T ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Lower( std::string_view szText )
{
	std::string szOut{ szText };
	for ( char &c : szOut )
	{
		c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
	}
	return szOut;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const CSheet::Sequence_t *CSheet::Sequence( int nNumber ) const
{
	const auto it = std::ranges::find( m_Sequences, nNumber, &Sequence_t::m_nNumber );
	return it == m_Sequences.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose: Input is untrusted; nullopt if the data doesn't add up.
//-----------------------------------------------------------------------------
std::optional<CSheet> ReadSheet( std::span<const std::byte> data )
{
	if ( data.size() < 8 )
	{
		return std::nullopt;
	}
	const auto nVersion = ReadAt<std::int32_t>( data, 0 );
	const auto nCount = ReadAt<std::int32_t>( data, 4 );
	if ( nVersion < 0 || nVersion > 1 || nCount < 0 || nCount > 4096 )
	{
		return std::nullopt;
	}
	// Version 1 stores four texture rects per frame (for blending sequences); the first is the image.
	const std::size_t nSamples = nVersion == 0 ? 1 : 4;
	CSheet sheet;
	sheet.m_nVersion = nVersion;
	std::size_t nAt = 8;
	for ( int i = 0; i < nCount; i++ )
	{
		if ( data.size() - nAt < 16 )
		{
			return std::nullopt;
		}
		CSheet::Sequence_t sequence;
		sequence.m_nNumber = ReadAt<std::int32_t>( data, nAt );
		sequence.m_bClamp = ReadAt<std::uint32_t>( data, nAt + 4 ) != 0;
		const auto nFrames = ReadAt<std::int32_t>( data, nAt + 8 );
		sequence.m_flTotalTime = ReadAt<float>( data, nAt + 12 );
		nAt += 16;
		const std::size_t nFrameSize = 4 + 16 * nSamples;
		if ( nFrames < 0 || static_cast<std::size_t>( nFrames ) > ( data.size() - nAt ) / nFrameSize )
		{
			return std::nullopt;
		}
		for ( int f = 0; f < nFrames; f++ )
		{
			CSheet::Frame_t frame;
			frame.m_flDuration = ReadAt<float>( data, nAt );
			for ( std::size_t c = 0; c < 4; c++ )
			{
				frame.m_Rect[ c ] = ReadAt<float>( data, nAt + 4 + c * 4 );
			}
			for ( std::size_t nSample = 1; nSample < nSamples; nSample++ )
			{
				for ( std::size_t c = 0; c < 4; c++ )
				{
					frame.m_Others[ nSample - 1 ][ c ] = ReadAt<float>( data, nAt + 4 + nSample * 16 + c * 4 );
				}
			}
			sequence.m_Frames.push_back( frame );
			nAt += nFrameSize;
		}
		sheet.m_Sequences.push_back( std::move( sequence ) );
	}
	return sheet;
}

//-----------------------------------------------------------------------------
// Purpose: The resource's data for a sheet, in its version.
//-----------------------------------------------------------------------------
std::vector<std::byte> WriteSheet( const CSheet &sheet )
{
	std::vector<std::byte> out;
	const bool bFour = sheet.m_nVersion >= 1;
	Append<std::int32_t>( out, bFour ? 1 : 0 );
	Append<std::int32_t>( out, static_cast<std::int32_t>( sheet.m_Sequences.size() ) );
	for ( const CSheet::Sequence_t &sequence : sheet.m_Sequences )
	{
		Append<std::int32_t>( out, sequence.m_nNumber );
		Append<std::uint32_t>( out, sequence.m_bClamp ? 1 : 0 );
		Append<std::int32_t>( out, static_cast<std::int32_t>( sequence.m_Frames.size() ) );
		Append<float>( out, sequence.m_flTotalTime );
		for ( const CSheet::Frame_t &frame : sequence.m_Frames )
		{
			Append<float>( out, frame.m_flDuration );
			for ( const float flEdge : frame.m_Rect )
			{
				Append<float>( out, flEdge );
			}
			for ( std::size_t nSample = 0; bFour && nSample < 3; nSample++ )
			{
				for ( const float flEdge : frame.m_Others[ nSample ] )
				{
					Append<float>( out, flEdge );
				}
			}
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Lines it doesn't understand are left out and listed in m_Problems.
//-----------------------------------------------------------------------------
CMks ParseMks( std::string_view szText )
{
	CMks mks;
	std::istringstream lines{ std::string{ szText } };
	std::string szLine;
	int nLine = 0;
	while ( std::getline( lines, szLine ) )
	{
		nLine++;
		if ( const std::size_t nComment = szLine.find( "//" ); nComment != std::string::npos )
		{
			szLine.erase( nComment );
		}
		std::istringstream words{ szLine };
		std::vector<std::string> parts;
		for ( std::string szWord; words >> szWord; )
		{
			parts.push_back( szWord );
		}
		if ( parts.empty() )
		{
			continue;
		}
		const std::string szCommand = Lower( parts[ 0 ] );
		const auto problem = [ &mks, nLine ]( std::string_view szWhy ) { mks.m_Problems.push_back( std::format( "line {}: {}", nLine, szWhy ) ); };
		if ( szCommand == "sequence" )
		{
			MksSequence_t sequence;
			sequence.m_nNumber = parts.size() > 1 ? std::atoi( parts[ 1 ].c_str() ) : static_cast<int>( mks.m_Sequences.size() );
			mks.m_Sequences.push_back( sequence );
		}
		else if ( szCommand == "loop" )
		{
			if ( mks.m_Sequences.empty() )
			{
				problem( "loop before any sequence" );
				continue;
			}
			mks.m_Sequences.back().m_bLoop = true;
		}
		else if ( szCommand == "frame" )
		{
			if ( mks.m_Sequences.empty() )
			{
				mks.m_Sequences.push_back( {} );
			}
			MksFrame_t frame;
			std::vector<std::string> files( parts.begin() + 1, parts.end() );
			// The last word is the duration when it is a number.
			if ( !files.empty() )
			{
				char *pEnd = nullptr;
				const float flDuration = std::strtof( files.back().c_str(), &pEnd );
				if ( pEnd && *pEnd == '\0' )
				{
					frame.m_flDuration = flDuration;
					files.pop_back();
				}
			}
			if ( files.empty() || files.size() > 2 || std::ranges::any_of( files, []( const std::string &szFile ) { return szFile.find( '{' ) != std::string::npos || szFile.find( ',' ) != std::string::npos; } ) )
			{
				problem( "frames take one or two pictures (channel packing like {g=a} isn't supported)" );
				continue;
			}
			frame.m_Files = std::move( files );
			mks.m_Sequences.back().m_Frames.push_back( std::move( frame ) );
		}
		else
		{
			problem( std::format( "\"{}\" isn't supported", parts[ 0 ] ) );
		}
	}
	return mks;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string WriteMks( const CMks &mks )
{
	std::string szText;
	for ( const MksSequence_t &sequence : mks.m_Sequences )
	{
		szText += std::format( "sequence {}\n", sequence.m_nNumber );
		if ( sequence.m_bLoop )
		{
			szText += "loop\n";
		}
		for ( const MksFrame_t &frame : sequence.m_Frames )
		{
			szText += "frame";
			for ( const std::string &szFile : frame.m_Files )
			{
				szText += ' ' + szFile;
			}
			szText += std::format( " {}\n", frame.m_flDuration );
		}
		szText += '\n';
	}
	return szText;
}

//-----------------------------------------------------------------------------
// Purpose: Packs the pictures the sequences name into one picture and a sheet.
//-----------------------------------------------------------------------------
std::optional<PackedSheet_t> PackSheet( const CMks &mks, const std::map<std::string, CImage> &pictures, std::string *pError, int nPadding )
{
	const auto fail = [ pError ]( std::string szMessage ) -> std::optional<PackedSheet_t> {
		if ( pError )
		{
			*pError = std::move( szMessage );
		}
		return std::nullopt;
	};
	// Each picture once, tallest first.
	std::vector<std::string> names;
	bool bTwo = false;
	for ( const MksSequence_t &sequence : mks.m_Sequences )
	{
		for ( const MksFrame_t &frame : sequence.m_Frames )
		{
			bTwo = bTwo || frame.m_Files.size() > 1;
			for ( const std::string &szFile : frame.m_Files )
			{
				if ( !pictures.contains( szFile ) )
				{
					return fail( std::format( "no picture {}", szFile ) );
				}
				if ( std::ranges::find( names, szFile ) == names.end() )
				{
					names.push_back( szFile );
				}
			}
		}
	}
	if ( names.empty() )
	{
		return fail( "no frames" );
	}
	std::ranges::stable_sort( names, std::greater{}, [ &pictures ]( const std::string &szName ) { return pictures.at( szName ).m_nHeight; } );

	// Shelves left to right, top to bottom, in the narrowest power-of-two width they fit.
	std::size_t nArea = 0;
	int nWidest = 0;
	for ( const std::string &szName : names )
	{
		const CImage &image = pictures.at( szName );
		nArea += static_cast<std::size_t>( image.m_nWidth + 2 * nPadding ) * static_cast<std::size_t>( image.m_nHeight + 2 * nPadding );
		nWidest = std::max( nWidest, image.m_nWidth + 2 * nPadding );
	}
	std::map<std::string, std::pair<int, int>> places;
	int nAtlasWidth = 1;
	int nAtlasHeight = 1;
	for ( nAtlasWidth = std::max( 1, NearestPowerOfTwo( static_cast<int>( std::sqrt( static_cast<double>( nArea ) ) ) ) ); nAtlasWidth <= 8192; nAtlasWidth *= 2 )
	{
		if ( nAtlasWidth < nWidest )
		{
			continue;
		}
		places.clear();
		int x = 0;
		int y = 0;
		int nShelf = 0;
		for ( const std::string &szName : names )
		{
			const CImage &image = pictures.at( szName );
			const int w = image.m_nWidth + 2 * nPadding;
			const int h = image.m_nHeight + 2 * nPadding;
			if ( x + w > nAtlasWidth )
			{
				x = 0;
				y += nShelf;
				nShelf = 0;
			}
			places[ szName ] = { x + nPadding, y + nPadding };
			x += w;
			nShelf = std::max( nShelf, h );
		}
		const int nUsed = y + nShelf;
		nAtlasHeight = 1;
		while ( nAtlasHeight < nUsed )
		{
			nAtlasHeight *= 2;
		}
		// Prefer roughly square: a sheet more than twice as tall as wide tries the next width.
		if ( nAtlasHeight <= 8192 && nAtlasHeight <= nAtlasWidth * 2 )
		{
			break;
		}
	}
	if ( nAtlasWidth > 8192 || nAtlasHeight > 8192 )
	{
		return fail( "the pictures don't fit in 8192×8192" );
	}

	PackedSheet_t packed;
	packed.m_Atlas = CImage{ nAtlasWidth, nAtlasHeight };
	for ( const std::string &szName : names )
	{
		const CImage &image = pictures.at( szName );
		const auto [ nLeft, nTop ] = places.at( szName );
		// The picture, and its edge texels repeated into the padding around it.
		for ( int y = -nPadding; y < image.m_nHeight + nPadding; y++ )
		{
			for ( int x = -nPadding; x < image.m_nWidth + nPadding; x++ )
			{
				const std::uint8_t *p = image.Pixel( std::clamp( x, 0, image.m_nWidth - 1 ), std::clamp( y, 0, image.m_nHeight - 1 ) );
				std::memcpy( packed.m_Atlas.Pixel( nLeft + x, nTop + y ), p, 4 );
			}
		}
	}
	const auto rectOf = [ & ]( const std::string &szName ) {
		const CImage &image = pictures.at( szName );
		const auto [ nLeft, nTop ] = places.at( szName );
		return std::array<float, 4>{ static_cast<float>( nLeft ) / static_cast<float>( nAtlasWidth ), static_cast<float>( nTop ) / static_cast<float>( nAtlasHeight ),
			static_cast<float>( nLeft + image.m_nWidth ) / static_cast<float>( nAtlasWidth ), static_cast<float>( nTop + image.m_nHeight ) / static_cast<float>( nAtlasHeight ) };
	};
	packed.m_Sheet.m_nVersion = bTwo ? 1 : 0;
	for ( const MksSequence_t &sequence : mks.m_Sequences )
	{
		CSheet::Sequence_t out;
		out.m_nNumber = sequence.m_nNumber;
		out.m_bClamp = !sequence.m_bLoop;
		for ( const MksFrame_t &frame : sequence.m_Frames )
		{
			CSheet::Frame_t packedFrame;
			packedFrame.m_flDuration = frame.m_flDuration;
			packedFrame.m_Rect = rectOf( frame.m_Files.front() );
			const auto second = rectOf( frame.m_Files.back() );
			packedFrame.m_Others = { second, packedFrame.m_Rect, second };
			out.m_flTotalTime += frame.m_flDuration;
			out.m_Frames.push_back( packedFrame );
		}
		packed.m_Sheet.m_Sequences.push_back( std::move( out ) );
	}
	return packed;
}

} // namespace xv::texture
