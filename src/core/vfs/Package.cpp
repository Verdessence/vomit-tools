//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Package.h.
//
//===========================================================================//
#include "Package.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <mutex>

// Each format header registers its opener with vpkpp::PackFile::open; GCC takes that
// registration for a dangling reference.
#if defined( __GNUC__ ) && !defined( __clang__ )
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-reference"
#endif
#include <vpkpp/format/GCF.h>
#include <vpkpp/format/GMA.h>
#include <vpkpp/format/PAK.h>
#include <vpkpp/format/VPK.h>
#include <vpkpp/format/WAD3.h>
#include <vpkpp/format/XZP.h>
#include <vpkpp/format/ZIP.h>
#if defined( __GNUC__ ) && !defined( __clang__ )
#pragma GCC diagnostic pop
#endif

#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#include "Vfs.h"
#include "bsp/Bsp.h"

namespace fs = std::filesystem;

namespace xv::vfs
{

namespace
{

// The archives a Source modder runs into. vpkpp opens more (other engines' formats), but those
// are left out so a stray .pck or .007 isn't taken for one.
constexpr std::array<std::string_view, 8> PACKAGE_EXTENSIONS{ ".vpk", ".bsp", ".gcf", ".gma", ".pak", ".wad", ".xzp", ".zip" };

//-----------------------------------------------------------------------------
// Purpose: Whether a stored path would land outside the folder it's extracted to: absolute, or
//          climbing out with "..". Archives from the internet can hold such names.
//-----------------------------------------------------------------------------
bool EscapesFolder( std::string_view szPath )
{
	if ( szPath.empty() || szPath.front() == '/' || szPath.front() == '\\' || szPath.find( ':' ) != std::string_view::npos )
	{
		return true;
	}
	const fs::path relative = FromUtf8( szPath ).lexically_normal();
	return relative.empty() || *relative.begin() == "..";
}

//-----------------------------------------------------------------------------
// Purpose: Lowercase extension of a path, with its dot.
//-----------------------------------------------------------------------------
std::string LowerExtension( const fs::path &file )
{
	std::string szExtension = ToUtf8( file.extension() );
	std::ranges::transform( szExtension, szExtension.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	return szExtension;
}

struct ZipEntry_t
{
	std::string m_szName; // as stored
	std::uint16_t m_nMethod = 0;
	std::int64_t m_nLocalHeader = 0;
	std::int64_t m_nPacked = 0;
	std::int64_t m_nUnpacked = 0;
	std::uint32_t m_nCrc = 0;
};

// Bytes of a zip from an offset in it: nullopt when they run past its end or can't be read.
using ZipRange_t = std::function<std::optional<std::vector<std::byte>>( std::uint64_t nAt, std::size_t nSize )>;

//-----------------------------------------------------------------------------
// Purpose: Little-endian numbers in a zip's headers.
//-----------------------------------------------------------------------------
std::uint32_t ZipRead16( std::span<const std::byte> bytes, std::size_t nAt )
{
	return std::to_integer<std::uint32_t>( bytes[ nAt ] ) | ( std::to_integer<std::uint32_t>( bytes[ nAt + 1 ] ) << 8 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint32_t ZipRead32( std::span<const std::byte> bytes, std::size_t nAt )
{
	return ZipRead16( bytes, nAt ) | ( ZipRead16( bytes, nAt + 2 ) << 16 );
}

//-----------------------------------------------------------------------------
// Purpose: Stored and LZMA entries are read here, by their place in the zip rather than by name
//          (names can repeat). bspzip packs with LZMA streams that have no end marker, which
//          minizip-ng won't take, so those unpack with the size the zip gives. The data follows
//          the local header (30 bytes, the name and the extra field); an LZMA entry's data starts
//          with the LZMA SDK version, the properties' size and the properties.
//-----------------------------------------------------------------------------
std::optional<std::vector<std::byte>> ReadRawEntry( const ZipRange_t &pfnRange, const ZipEntry_t &entry )
{
	if ( entry.m_nLocalHeader < 0 )
	{
		return std::nullopt;
	}
	const auto nHeader = static_cast<std::uint64_t>( entry.m_nLocalHeader );
	const auto header = pfnRange( nHeader, 30 );
	if ( !header )
	{
		return std::nullopt;
	}
	const std::uint64_t nData = nHeader + 30 + ZipRead16( *header, 26 ) + ZipRead16( *header, 28 );
	const auto nPacked = static_cast<std::size_t>( std::max<std::int64_t>( entry.m_nPacked, 0 ) );
	auto data = pfnRange( nData, nPacked );
	if ( !data || entry.m_nMethod == MZ_COMPRESS_METHOD_STORE )
	{
		return data;
	}
	const std::span<const std::byte> packed( *data );
	if ( nPacked < 4 )
	{
		return std::nullopt;
	}
	const std::size_t nProperties = ZipRead16( packed, 2 );
	if ( nPacked < 4 + nProperties )
	{
		return std::nullopt;
	}
	return bsp::UnpackLzma( packed.subspan( 4, nProperties ), packed.subspan( 4 + nProperties ), static_cast<std::uint64_t>( entry.m_nUnpacked ) );
}

//-----------------------------------------------------------------------------
// Purpose: A zip's central directory, read without the rest of it: the end record (in the
//          last 64 kB), then the entries it lists. nullopt for one this doesn't read that way:
//          ZIP64, an entry packed other than stored or LZMA (minizip-ng reads those, from the
//          whole zip), or a first entry whose local header isn't where the directory says.
//-----------------------------------------------------------------------------
std::optional<std::vector<ZipEntry_t>> ReadZipDirectory( const ZipRange_t &pfnRange, std::uint64_t nZipSize )
{
	constexpr std::uint32_t END_SIGNATURE = 0x06054b50;
	constexpr std::uint32_t ENTRY_SIGNATURE = 0x02014b50;
	constexpr std::uint32_t LOCAL_SIGNATURE = 0x04034b50;
	constexpr std::size_t END_SIZE = 22;
	if ( nZipSize < END_SIZE )
	{
		return std::nullopt;
	}
	const std::size_t nTail = static_cast<std::size_t>( std::min<std::uint64_t>( nZipSize, END_SIZE + 0xffff ) );
	const auto tail = pfnRange( nZipSize - nTail, nTail );
	if ( !tail )
	{
		return std::nullopt;
	}
	std::optional<std::size_t> end;
	for ( std::size_t i = nTail - END_SIZE + 1; i-- > 0; )
	{
		if ( ZipRead32( *tail, i ) == END_SIGNATURE )
		{
			end = i;
			break;
		}
	}
	if ( !end )
	{
		return std::nullopt;
	}
	const std::uint32_t nCount = ZipRead16( *tail, *end + 10 );
	const std::uint32_t nSize = ZipRead32( *tail, *end + 12 );
	const std::uint32_t nOffset = ZipRead32( *tail, *end + 16 );
	if ( nCount == 0xffff || nOffset == 0xffffffff || static_cast<std::uint64_t>( nOffset ) + nSize > nZipSize )
	{
		return std::nullopt;
	}
	const auto directory = pfnRange( nOffset, nSize );
	if ( !directory )
	{
		return std::nullopt;
	}
	std::vector<ZipEntry_t> entries;
	entries.reserve( nCount );
	std::size_t nAt = 0;
	for ( std::uint32_t n = 0; n < nCount; n++ )
	{
		if ( nAt + 46 > directory->size() || ZipRead32( *directory, nAt ) != ENTRY_SIGNATURE )
		{
			return std::nullopt;
		}
		const std::size_t nName = ZipRead16( *directory, nAt + 28 );
		const std::size_t nNext = nAt + 46 + nName + ZipRead16( *directory, nAt + 30 ) + ZipRead16( *directory, nAt + 32 );
		if ( nNext > directory->size() )
		{
			return std::nullopt;
		}
		ZipEntry_t entry;
		entry.m_szName.assign( reinterpret_cast<const char *>( directory->data() + nAt + 46 ), nName );
		entry.m_nMethod = static_cast<std::uint16_t>( ZipRead16( *directory, nAt + 10 ) );
		entry.m_nCrc = ZipRead32( *directory, nAt + 16 );
		entry.m_nPacked = ZipRead32( *directory, nAt + 20 );
		entry.m_nUnpacked = ZipRead32( *directory, nAt + 24 );
		entry.m_nLocalHeader = ZipRead32( *directory, nAt + 42 );
		nAt = nNext;
		if ( entry.m_szName.empty() || entry.m_szName.ends_with( '/' ) )
		{
			continue; // a folder
		}
		if ( entry.m_nMethod != MZ_COMPRESS_METHOD_STORE && entry.m_nMethod != MZ_COMPRESS_METHOD_LZMA )
		{
			return std::nullopt;
		}
		entries.push_back( std::move( entry ) );
	}
	if ( !entries.empty() )
	{
		const auto local = pfnRange( static_cast<std::uint64_t>( entries.front().m_nLocalHeader ), 4 );
		if ( !local || ZipRead32( *local, 0 ) != LOCAL_SIGNATURE )
		{
			return std::nullopt;
		}
	}
	return entries;
}

//-----------------------------------------------------------------------------
// Purpose: Bytes of a file, from an offset.
//-----------------------------------------------------------------------------
std::optional<std::vector<std::byte>> ReadFileRange( const fs::path &file, std::uint64_t nAt, std::size_t nSize )
{
	std::ifstream in( file, std::ios::binary );
	std::vector<std::byte> bytes( nSize );
	if ( !in || !in.seekg( static_cast<std::streamoff>( nAt ) ) || !in.read( reinterpret_cast<char *>( bytes.data() ), static_cast<std::streamsize>( nSize ) ) )
	{
		return std::nullopt;
	}
	return bytes;
}

// A map's file as the pakfiles kept are known by: its path, size and time.
struct PakfileKey_t
{
	std::string m_szPath;
	std::uintmax_t m_nSize = 0;
	fs::file_time_type m_Time;
	auto operator<=>( const PakfileKey_t & ) const = default;
};

std::mutex g_PakfileMutex;
std::map<PakfileKey_t, std::weak_ptr<const std::vector<std::byte>>> g_Pakfiles;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
PakfileKey_t PakfileKey( const fs::path &file )
{
	std::error_code ec;
	const fs::path canonical = fs::weakly_canonical( file, ec );
	PakfileKey_t key{ ToUtf8( ec ? file : canonical ), 0, {} };
	key.m_nSize = fs::file_size( file, ec );
	key.m_Time = fs::last_write_time( file, ec );
	return key;
}

//-----------------------------------------------------------------------------
// Purpose: A map's pakfile, one copy however many mounts have the map open at once (a demo's
//          map is mounted by its sounds, its textures and its particles alike): the copy
//          already read for the same file, unchanged since; null when none is.
//-----------------------------------------------------------------------------
std::shared_ptr<const std::vector<std::byte>> FindPakfile( const PakfileKey_t &key )
{
	const std::lock_guard lock( g_PakfileMutex );
	const auto found = g_Pakfiles.find( key );
	return found != g_Pakfiles.end() ? found->second.lock() : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: A pakfile read, kept for the next mount of the same file (unless another mount
//          read it meanwhile: that one's copy then).
//-----------------------------------------------------------------------------
std::shared_ptr<const std::vector<std::byte>> KeepPakfile( const PakfileKey_t &key, std::vector<std::byte> bytes )
{
	const std::lock_guard lock( g_PakfileMutex );
	std::erase_if( g_Pakfiles, []( const auto &entry ) { return entry.second.expired(); } );
	if ( const auto found = g_Pakfiles.find( key ); found != g_Pakfiles.end() )
	{
		if ( auto pKept = found->second.lock() )
			return pKept;
	}
	auto pBytes = std::make_shared<const std::vector<std::byte>>( std::move( bytes ) );
	g_Pakfiles[ key ] = pBytes;
	return pBytes;
}

} // namespace

// A map's pakfile lump. Read where it lies in the map's file when it can be (its entries
// stored or LZMA, as bspzip packs them); else held in memory, one copy for every mount of
// the map, and listed by minizip-ng (which vpkpp links already).
struct CPackage::Zip_t
{
	std::filesystem::path m_File; // the map, when the entries are read from it
	std::uint64_t m_nAt = 0;      // where the lump starts in it
	std::uint64_t m_nSize = 0;
	std::shared_ptr<const std::vector<std::byte>> m_pBytes; // else the lump, unpacked
	void *m_pReader = mz_zip_reader_create();
	std::vector<ZipEntry_t> m_Entries; // by PackageEntry_t::m_nStored

	~Zip_t()
	{
		mz_zip_reader_close( m_pReader );
		mz_zip_reader_delete( &m_pReader );
	}

	[[nodiscard]] ZipRange_t Range() const
	{
		if ( m_pBytes )
		{
			return [ pBytes = m_pBytes ]( std::uint64_t nAt, std::size_t nSize ) -> std::optional<std::vector<std::byte>> {
				if ( nAt > pBytes->size() || nSize > pBytes->size() - nAt )
					return std::nullopt;
				const auto first = pBytes->begin() + static_cast<std::ptrdiff_t>( nAt );
				return std::vector<std::byte>( first, first + static_cast<std::ptrdiff_t>( nSize ) );
			};
		}
		return [ file = m_File, nStart = m_nAt, nLength = m_nSize ]( std::uint64_t nAt, std::size_t nSize ) -> std::optional<std::vector<std::byte>> {
			if ( nAt > nLength || nSize > nLength - nAt )
				return std::nullopt;
			return ReadFileRange( file, nStart + nAt, nSize );
		};
	}
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPackage::CPackage() = default;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPackage::~CPackage() = default;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CPackage::IsPackageFile( const fs::path &file )
{
	return std::ranges::find( PACKAGE_EXTENSIONS, LowerExtension( file ) ) != PACKAGE_EXTENSIONS.end();
}

//-----------------------------------------------------------------------------
// Purpose: A numbered VPK part (pak01_003.vpk) holds only data; vpkpp opens the _dir file beside
//          it instead, which lists every part's files.
//-----------------------------------------------------------------------------
std::unique_ptr<CPackage> CPackage::Open( const fs::path &file, std::string *pError )
{
	const auto fail = [ & ]( std::string szWhy ) -> std::unique_ptr<CPackage> {
		if ( pError )
		{
			*pError = std::move( szWhy );
		}
		return nullptr;
	};
	if ( !IsPackageFile( file ) )
	{
		return fail( "not an archive vomit-tools reads: " + ToUtf8( file.filename() ) );
	}
	std::error_code ec;
	if ( !fs::is_regular_file( file, ec ) )
	{
		return fail( "file not found: " + ToUtf8( file ) );
	}
	if ( LowerExtension( file ) == ".bsp" )
	{
		return OpenMap( file, pError );
	}
	// sourcepp takes UTF-8 paths in std::string; see CFileSystem::AddVpk.
	auto pPack = vpkpp::PackFile::open( ToUtf8( file ) );
	if ( !pPack )
	{
		return fail( "could not read the archive: " + ToUtf8( file ) );
	}

	std::unique_ptr<CPackage> pPackage( new CPackage );
	pPackage->m_File = FromUtf8( pPack->getFilepath() );
	const std::string szExtension = LowerExtension( file );
	pPackage->m_szFormat = szExtension.substr( 1 );
	std::ranges::transform( pPackage->m_szFormat, pPackage->m_szFormat.begin(), []( unsigned char ch ) { return static_cast<char>( std::toupper( ch ) ); } );
	const auto *pVpk = dynamic_cast<const vpkpp::VPK *>( pPack.get() );
	if ( pVpk )
	{
		pPackage->m_szFormat += " " + std::to_string( pVpk->getVersion() );
	}

	pPack->runForAllEntries( [ & ]( const std::string &szPath, const vpkpp::Entry &entry ) {
		PackageEntry_t &added = pPackage->m_Entries.emplace_back();
		added.m_szPath = szPath;
		added.m_nSize = entry.length;
		added.m_nCrc = entry.crc32;
		if ( pVpk && entry.archiveIndex != vpkpp::VPK_DIR_INDEX )
		{
			added.m_nArchive = static_cast<int>( entry.archiveIndex );
		}
		pPackage->m_nTotalSize += entry.length;
	} );
	// The oldest VPKs have no header, so vpkpp takes any file at all for one with nothing in it.
	if ( pPackage->m_Entries.empty() )
	{
		return fail( "no files found in " + ToUtf8( file.filename() ) + "; it's empty or not an archive" );
	}
	pPackage->m_pPack = std::move( pPack );
	pPackage->BuildIndex();
	return pPackage;
}

//-----------------------------------------------------------------------------
// Purpose: A map lists the files packed into its pakfile lump. One that packs nothing still
//          opens, for its lumps and entities.
//-----------------------------------------------------------------------------
std::unique_ptr<CPackage> CPackage::OpenMap( const fs::path &file, std::string *pError )
{
	auto map = bsp::CBsp::Load( file, pError, false );
	if ( !map )
	{
		return nullptr;
	}
	std::unique_ptr<CPackage> pPackage( new CPackage );
	pPackage->m_File = file;
	pPackage->m_szFormat = "BSP " + std::to_string( map->GetVersion() );
	auto pZip = std::make_unique<Zip_t>();
	const bsp::Lump_t &lump = map->GetLumps()[ bsp::LUMP_PAKFILE ];
	const auto add = [ & ]( ZipEntry_t entry ) {
		std::string szPath = entry.m_szName;
		std::ranges::replace( szPath, '\\', '/' );
		PackageEntry_t &added = pPackage->m_Entries.emplace_back();
		added.m_szPath = std::move( szPath );
		added.m_nSize = static_cast<std::uint64_t>( entry.m_nUnpacked );
		added.m_nCrc = entry.m_nCrc;
		pPackage->m_nTotalSize += added.m_nSize;
		added.m_nStored = pZip->m_Entries.size();
		pZip->m_Entries.push_back( std::move( entry ) );
	};
	bool bListed = false;
	if ( lump.m_nLength > 0 && lump.m_nUncompressed == 0 )
	{
		pZip->m_File = file;
		pZip->m_nAt = lump.m_nOffset;
		pZip->m_nSize = lump.m_nLength;
		if ( auto entries = ReadZipDirectory( pZip->Range(), pZip->m_nSize ) )
		{
			for ( ZipEntry_t &entry : *entries )
				add( std::move( entry ) );
			bListed = true;
		}
	}
	if ( !bListed && lump.m_nLength > 0 )
	{
		const PakfileKey_t key = PakfileKey( file );
		pZip->m_pBytes = FindPakfile( key );
		if ( !pZip->m_pBytes )
		{
			auto stored = ReadFileRange( file, lump.m_nOffset, lump.m_nLength );
			if ( stored && lump.m_nUncompressed != 0 )
				stored = bsp::UnpackIfCompressed( *stored );
			pZip->m_pBytes = KeepPakfile( key, stored ? std::move( *stored ) : std::vector<std::byte>{} );
		}
		const std::vector<std::byte> &bytes = *pZip->m_pBytes;
		const bool bOpen = !bytes.empty() && bytes.size() <= INT32_MAX &&
			mz_zip_reader_open_buffer( pZip->m_pReader, reinterpret_cast<const std::uint8_t *>( bytes.data() ), static_cast<std::int32_t>( bytes.size() ), 0 ) == MZ_OK;
		for ( std::int32_t nResult = bOpen ? mz_zip_reader_goto_first_entry( pZip->m_pReader ) : MZ_END_OF_LIST; nResult == MZ_OK; nResult = mz_zip_reader_goto_next_entry( pZip->m_pReader ) )
		{
			mz_zip_file *pInfo = nullptr;
			if ( mz_zip_reader_entry_is_dir( pZip->m_pReader ) == MZ_OK || mz_zip_reader_entry_get_info( pZip->m_pReader, &pInfo ) != MZ_OK || !pInfo->filename )
			{
				continue;
			}
			add( ZipEntry_t{ pInfo->filename, pInfo->compression_method, pInfo->disk_offset, pInfo->compressed_size, pInfo->uncompressed_size, pInfo->crc } );
		}
	}
	pPackage->m_pZip = std::move( pZip );
	pPackage->m_pBsp = std::make_unique<bsp::CBsp>( std::move( *map ) );
	pPackage->BuildIndex();
	return pPackage;
}

//-----------------------------------------------------------------------------
// Purpose: Sorts the entries by path and indexes them for Find.
//-----------------------------------------------------------------------------
void CPackage::BuildIndex()
{
	// Stable, so a repeated path keeps the archive's order and Find gives its first.
	std::ranges::stable_sort( m_Entries, []( const PackageEntry_t &a, const PackageEntry_t &b ) { return NormalizePath( a.m_szPath ) < NormalizePath( b.m_szPath ); } );
	m_Index.reserve( m_Entries.size() );
	for ( std::size_t i = 0; i < m_Entries.size(); i++ )
	{
		const auto [ it, bNew ] = m_Index.emplace( NormalizePath( m_Entries[ i ].m_szPath ), i );
		if ( !bNew )
		{
			m_Entries[ i ].m_bDuplicate = true;
			m_Entries[ it->second ].m_bDuplicate = true;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const PackageEntry_t *CPackage::Find( std::string_view szPath ) const
{
	const auto it = m_Index.find( NormalizePath( szPath ) );
	return it == m_Index.end() ? nullptr : &m_Entries[ it->second ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::vector<std::byte>> CPackage::Read( std::string_view szPath ) const
{
	const PackageEntry_t *pEntry = Find( szPath );
	return pEntry ? Read( *pEntry ) : std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: Other zip methods (deflate, from other packers) go through minizip-ng by name, which
//          finds the first of a repeated name.
//-----------------------------------------------------------------------------
std::optional<std::vector<std::byte>> CPackage::Read( const PackageEntry_t &entry ) const
{
	const PackageEntry_t *pEntry = &entry;
	if ( m_pZip )
	{
		if ( pEntry->m_nStored >= m_pZip->m_Entries.size() )
		{
			return std::nullopt;
		}
		const ZipEntry_t &stored = m_pZip->m_Entries[ pEntry->m_nStored ];
		if ( stored.m_nMethod == MZ_COMPRESS_METHOD_STORE || stored.m_nMethod == MZ_COMPRESS_METHOD_LZMA )
		{
			return ReadRawEntry( m_pZip->Range(), stored );
		}
		if ( !m_pZip->m_pBytes || mz_zip_reader_locate_entry( m_pZip->m_pReader, stored.m_szName.c_str(), 0 ) != MZ_OK )
		{
			return std::nullopt;
		}
		const std::int32_t nLength = mz_zip_reader_entry_save_buffer_length( m_pZip->m_pReader );
		std::vector<std::byte> bytes( static_cast<std::size_t>( std::max( nLength, 0 ) ) );
		if ( nLength < 0 || mz_zip_reader_entry_save_buffer( m_pZip->m_pReader, bytes.data(), nLength ) != MZ_OK )
		{
			return std::nullopt;
		}
		return bytes;
	}
	return m_pPack->readEntry( pEntry->m_szPath );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CPackage::Extract( std::string_view szPath, const fs::path &target, std::string *pError ) const
{
	const auto fail = [ & ]( std::string szWhy ) {
		if ( pError )
		{
			*pError = std::move( szWhy );
		}
		return false;
	};
	const auto bytes = Read( szPath );
	if ( !bytes )
	{
		return fail( "could not read " + std::string( szPath ) + " from the archive" );
	}
	std::error_code ec;
	if ( target.has_parent_path() )
	{
		fs::create_directories( target.parent_path(), ec );
	}
	std::ofstream out( target, std::ios::binary | std::ios::trunc );
	out.write( reinterpret_cast<const char *>( bytes->data() ), static_cast<std::streamsize>( bytes->size() ) );
	out.close();
	if ( !out )
	{
		return fail( "could not write " + ToUtf8( target ) );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CPackage::ExtractAll( const std::vector<std::string> &paths, const fs::path &folder, std::string_view szStripPrefix,
	const std::function<bool( std::size_t )> &pfnProgress, std::vector<std::string> *pFailed ) const
{
	std::size_t nWritten = 0;
	std::size_t nDone = 0;
	for ( const std::string &szPath : paths )
	{
		std::string_view szRelative = szPath;
		if ( !szStripPrefix.empty() && szRelative.starts_with( szStripPrefix ) )
		{
			szRelative.remove_prefix( szStripPrefix.size() );
		}
		while ( szRelative.starts_with( '/' ) )
		{
			szRelative.remove_prefix( 1 );
		}
		if ( EscapesFolder( szRelative ) || !Extract( szPath, folder / FromUtf8( szRelative ) ) )
		{
			if ( pFailed )
			{
				pFailed->push_back( szPath );
			}
		}
		else
		{
			nWritten++;
		}
		if ( pfnProgress && !pfnProgress( ++nDone ) )
		{
			break;
		}
	}
	return nWritten;
}

} // namespace xv::vfs
