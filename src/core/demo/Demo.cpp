//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Demo.h.
//
//===========================================================================//
#include "Demo.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

#include "vfs/Vfs.h"

namespace fs = std::filesystem;

namespace xv::demo
{

namespace
{

constexpr std::string_view DEMO_IDENT{ "HL2DEMO\0", 8 };
// The header's four names are fixed fields of this many bytes each.
constexpr std::size_t NAME_SIZE = 260;
// A packet's camera block: flags, then origin, angles and local angles, each of those twice
// (as recorded, and as resampled when the demo was smoothed).
constexpr std::size_t CAMERA_SIZE = 4 + 6 * 12;
constexpr std::uint32_t CAMERA_USE_ORIGIN2 = 1u << 0;
constexpr std::uint32_t CAMERA_USE_ANGLES2 = 1u << 1;

// A cursor over the file that stops, and says so, at the end.
class CReader
{
public:
	explicit CReader( std::span<const std::byte> bytes, std::size_t nAt )
		: m_Bytes( bytes ),
		  m_nAt( nAt )
	{
	}

	[[nodiscard]] std::size_t GetOffset() const { return m_nAt; }
	[[nodiscard]] std::size_t GetLeft() const { return m_Bytes.size() - m_nAt; }

	template <typename T>
	[[nodiscard]] bool Read( T &value )
	{
		if ( GetLeft() < sizeof( T ) )
		{
			return false;
		}
		std::memcpy( &value, m_Bytes.data() + m_nAt, sizeof( T ) );
		m_nAt += sizeof( T );
		return true;
	}

	[[nodiscard]] bool Skip( std::size_t nCount )
	{
		if ( GetLeft() < nCount )
		{
			return false;
		}
		m_nAt += nCount;
		return true;
	}

	// A 32-bit length and that many bytes after it.
	[[nodiscard]] bool ReadBlock( std::span<const std::byte> &block )
	{
		std::int32_t nLength = 0;
		if ( !Read( nLength ) || nLength < 0 || GetLeft() < static_cast<std::size_t>( nLength ) )
		{
			return false;
		}
		block = m_Bytes.subspan( m_nAt, static_cast<std::size_t>( nLength ) );
		m_nAt += static_cast<std::size_t>( nLength );
		return true;
	}

private:
	std::span<const std::byte> m_Bytes;
	std::size_t m_nAt = 0;
};

//-----------------------------------------------------------------------------
// Purpose: One of the header's fixed name fields, up to its first zero.
//-----------------------------------------------------------------------------
std::string ReadName( std::span<const std::byte> bytes, std::size_t nAt )
{
	const char *pszText = reinterpret_cast<const char *>( bytes.data() + nAt );
	return std::string( pszText, strnlen( pszText, NAME_SIZE ) );
}

//-----------------------------------------------------------------------------
// Purpose: Three floats at a place in a camera block.
//-----------------------------------------------------------------------------
CVec3 ReadVec3( const std::byte *pAt )
{
	std::array<float, 3> values{};
	std::memcpy( values.data(), pAt, sizeof( values ) );
	return { values[ 0 ], values[ 1 ], values[ 2 ] };
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view FrameKindName( FrameKind_t nKind )
{
	switch ( nKind )
	{
		case FrameKind_t::FRAME_KIND_SIGNON:
			return "Sign-on packet";
		case FrameKind_t::FRAME_KIND_PACKET:
			return "Packet";
		case FrameKind_t::FRAME_KIND_SYNC_TICK:
			return "Clock start";
		case FrameKind_t::FRAME_KIND_CONSOLE:
			return "Console command";
		case FrameKind_t::FRAME_KIND_USER_COMMAND:
			return "Player input";
		case FrameKind_t::FRAME_KIND_DATA_TABLES:
			return "Network tables";
		case FrameKind_t::FRAME_KIND_STOP:
			return "End";
		case FrameKind_t::FRAME_KIND_STRING_TABLES:
			return "String tables";
	}
	return "Unknown";
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CDemo::IsDemo( std::span<const std::byte> bytes )
{
	return bytes.size() >= DEMO_IDENT.size() && std::memcmp( bytes.data(), DEMO_IDENT.data(), DEMO_IDENT.size() ) == 0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<CDemo> CDemo::Load( const fs::path &file, std::string *pError )
{
	std::ifstream in( file, std::ios::binary );
	if ( !in )
	{
		if ( pError )
		{
			*pError = "could not open " + vfs::ToUtf8( file );
		}
		return std::nullopt;
	}
	std::vector<char> data( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
	return Parse( std::as_bytes( std::span( data ) ), pError );
}

//-----------------------------------------------------------------------------
// Purpose: The header, then frame after frame to the end frame. A demo whose recording was
//          cut off (the game crashed) has no end frame and zeros for its length: what was
//          read stands, with a warning.
//-----------------------------------------------------------------------------
std::optional<CDemo> CDemo::Parse( std::span<const std::byte> bytes, std::string *pError )
{
	const auto fail = [ & ]( const char *pszWhy ) -> std::optional<CDemo> {
		if ( pError )
		{
			*pError = pszWhy;
		}
		return std::nullopt;
	};
	if ( !IsDemo( bytes ) )
	{
		return fail( "not a Source demo (no HL2DEMO header)" );
	}
	if ( bytes.size() < HEADER_SIZE )
	{
		return fail( "the demo's header is cut short" );
	}

	CDemo demo;
	demo.m_nFileSize = bytes.size();
	Header_t &header = demo.m_Header;
	CReader reader( bytes, DEMO_IDENT.size() );
	(void)reader.Read( header.m_nDemoProtocol );
	(void)reader.Read( header.m_nNetworkProtocol );
	header.m_szServer = ReadName( bytes, reader.GetOffset() );
	header.m_szClient = ReadName( bytes, reader.GetOffset() + NAME_SIZE );
	header.m_szMap = ReadName( bytes, reader.GetOffset() + 2 * NAME_SIZE );
	header.m_szGameDir = ReadName( bytes, reader.GetOffset() + 3 * NAME_SIZE );
	(void)reader.Skip( 4 * NAME_SIZE );
	(void)reader.Read( header.m_flSeconds );
	(void)reader.Read( header.m_nTicks );
	(void)reader.Read( header.m_nFrames );
	(void)reader.Read( header.m_nSignonLength );
	demo.m_nLastTick = std::max( header.m_nTicks, 0 );

	if ( header.m_nDemoProtocol != FRAME_PROTOCOL )
	{
		demo.m_szWarning = "Demo protocol " + std::to_string( header.m_nDemoProtocol ) + " frames aren't read yet (only protocol " + std::to_string( FRAME_PROTOCOL ) + "'s): the header is all that is shown.";
		return demo;
	}

	bool bStopped = false;
	while ( reader.GetLeft() > 0 )
	{
		Frame_t frame;
		frame.m_nOffset = reader.GetOffset();
		std::uint8_t nKind = 0;
		(void)reader.Read( nKind );
		if ( nKind < 1 || nKind > FRAME_KIND_COUNT )
		{
			demo.m_szWarning = "A frame of an unknown kind (" + std::to_string( nKind ) + ") at byte " + std::to_string( frame.m_nOffset ) + ": the frames after it weren't read.";
			break;
		}
		frame.m_nKind = static_cast<FrameKind_t>( nKind );
		// The end frame's tick is written short of its four bytes in some games' demos; it is
		// the last thing in the file either way.
		if ( frame.m_nKind == FrameKind_t::FRAME_KIND_STOP )
		{
			std::int32_t nTick = 0;
			frame.m_nTick = reader.Read( nTick ) ? nTick : demo.m_nLastTick;
			frame.m_nSize = static_cast<std::uint32_t>( bytes.size() - frame.m_nOffset );
			demo.m_Frames.push_back( frame );
			bStopped = true;
			break;
		}

		std::int32_t nTick = 0;
		bool bWhole = reader.Read( nTick );
		frame.m_nTick = nTick;
		std::span<const std::byte> block;
		switch ( frame.m_nKind )
		{
			case FrameKind_t::FRAME_KIND_SIGNON:
			case FrameKind_t::FRAME_KIND_PACKET:
			{
				const std::size_t nCamera = reader.GetOffset();
				// The camera, then the sequence numbers in and out.
				bWhole = bWhole && reader.Skip( CAMERA_SIZE + 8 ) && reader.ReadBlock( block );
				if ( bWhole && frame.m_nKind == FrameKind_t::FRAME_KIND_PACKET )
				{
					const std::byte *pCamera = bytes.data() + nCamera;
					std::uint32_t nFlags = 0;
					std::memcpy( &nFlags, pCamera, sizeof( nFlags ) );
					View_t view;
					view.m_nTick = frame.m_nTick;
					view.m_vecOrigin = ReadVec3( pCamera + 4 + ( nFlags & CAMERA_USE_ORIGIN2 ? 36 : 0 ) );
					view.m_vecAngles = ReadVec3( pCamera + 16 + ( nFlags & CAMERA_USE_ANGLES2 ? 36 : 0 ) );
					demo.m_bCamera = demo.m_bCamera || view.m_vecOrigin.m_flX != 0.0f || view.m_vecOrigin.m_flY != 0.0f || view.m_vecOrigin.m_flZ != 0.0f;
					demo.m_Views.push_back( view );
				}
				break;
			}
			case FrameKind_t::FRAME_KIND_SYNC_TICK:
				break;
			case FrameKind_t::FRAME_KIND_CONSOLE:
				bWhole = bWhole && reader.ReadBlock( block );
				if ( bWhole )
				{
					const char *pszText = reinterpret_cast<const char *>( block.data() );
					demo.m_Commands.push_back( { frame.m_nTick, std::string( pszText, strnlen( pszText, block.size() ) ) } );
				}
				break;
			case FrameKind_t::FRAME_KIND_USER_COMMAND:
				// The command's number comes before its block.
				bWhole = bWhole && reader.Skip( 4 ) && reader.ReadBlock( block );
				break;
			case FrameKind_t::FRAME_KIND_DATA_TABLES:
			case FrameKind_t::FRAME_KIND_STRING_TABLES:
				bWhole = bWhole && reader.ReadBlock( block );
				break;
			case FrameKind_t::FRAME_KIND_STOP:
				break;
		}
		if ( !bWhole )
		{
			demo.m_szWarning = "The file ends in the middle of a frame (at byte " + std::to_string( frame.m_nOffset ) + "): the recording was cut off.";
			break;
		}
		frame.m_nSize = static_cast<std::uint32_t>( reader.GetOffset() - frame.m_nOffset );
		demo.m_Frames.push_back( frame );
		if ( frame.m_nKind != FrameKind_t::FRAME_KIND_SIGNON )
		{
			demo.m_nLastTick = std::max( demo.m_nLastTick, frame.m_nTick );
		}
	}
	if ( !bStopped && demo.m_szWarning.empty() )
	{
		demo.m_szWarning = "The demo has no end frame: the recording was cut off.";
	}
	if ( !demo.m_bCamera )
	{
		demo.m_Views.clear();
	}
	return demo;
}

//-----------------------------------------------------------------------------
// Purpose: SourceTV records under this name, whatever the server is called.
//-----------------------------------------------------------------------------
bool CDemo::IsSourceTv() const
{
	return m_Header.m_szClient == "SourceTV Demo";
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CDemo::GetTickInterval() const
{
	if ( m_Header.m_nTicks <= 0 || m_Header.m_flSeconds <= 0.0f )
	{
		return 0.0;
	}
	return static_cast<double>( m_Header.m_flSeconds ) / m_Header.m_nTicks;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CDemo::CountOf( FrameKind_t nKind ) const
{
	return static_cast<int>( std::count_if( m_Frames.begin(), m_Frames.end(), [ nKind ]( const Frame_t &frame ) { return frame.m_nKind == nKind; } ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint64_t CDemo::BytesOf( FrameKind_t nKind ) const
{
	std::uint64_t nBytes = 0;
	for ( const Frame_t &frame : m_Frames )
	{
		if ( frame.m_nKind == nKind )
		{
			nBytes += frame.m_nSize;
		}
	}
	return nBytes;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const View_t *CDemo::ViewAt( int nTick ) const
{
	const auto after = std::upper_bound( m_Views.begin(), m_Views.end(), nTick, []( int nAt, const View_t &view ) { return nAt < view.m_nTick; } );
	return after == m_Views.begin() ? nullptr : &*( after - 1 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::uint64_t> CDemo::PacketBytesOverTime( int nBuckets ) const
{
	std::vector<std::uint64_t> buckets( static_cast<std::size_t>( std::max( nBuckets, 0 ) ) );
	if ( buckets.empty() )
	{
		return buckets;
	}
	const std::int64_t nSpan = static_cast<std::int64_t>( m_nLastTick ) + 1;
	for ( const Frame_t &frame : m_Frames )
	{
		if ( frame.m_nKind != FrameKind_t::FRAME_KIND_PACKET )
		{
			continue;
		}
		const std::int64_t nTick = std::clamp<std::int64_t>( frame.m_nTick, 0, m_nLastTick );
		buckets[ static_cast<std::size_t>( nTick * static_cast<std::int64_t>( buckets.size() ) / nSpan ) ] += frame.m_nSize;
	}
	return buckets;
}

} // namespace xv::demo
