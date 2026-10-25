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
};

//-----------------------------------------------------------------------------
// Purpose: bspzip packs with LZMA streams that have no end marker, which minizip-ng won't take;
//          read here with the size the zip gives. After the local header (30 bytes, the name and
//          the extra field) come the LZMA SDK version, the properties' size and the properties.
//-----------------------------------------------------------------------------
std::optional<std::vector<std::byte>> ReadLzmaEntry( std::span<const std::byte> zip, const ZipEntry_t &entry )
{
	const auto read16 = [ &zip ]( std::size_t nAt ) { return static_cast<std::size_t>( std::to_integer<unsigned>( zip[ nAt ] ) | ( std::to_integer<unsigned>( zip[ nAt + 1 ] ) << 8 ) ); };
	const auto nHeader = static_cast<std::size_t>( entry.m_nLocalHeader );
	if ( entry.m_nLocalHeader < 0 || nHeader + 30 > zip.size() )
	{
		return std::nullopt;
	}
	const std::size_t nData = nHeader + 30 + read16( nHeader + 26 ) + read16( nHeader + 28 );
	if ( nData + 4 > zip.size() )
	{
		return std::nullopt;
	}
	const std::size_t nProperties = read16( nData + 2 );
	const auto nPacked = static_cast<std::size_t>( std::max<std::int64_t>( entry.m_nPacked, 0 ) );
	if ( nPacked < 4 + nProperties || nData + nPacked > zip.size() )
	{
		return std::nullopt;
	}
	return bsp::UnpackLzma( zip.subspan( nData + 4, nProperties ), zip.subspan( nData + 4 + nProperties, nPacked - 4 - nProperties ), static_cast<std::uint64_t>( entry.m_nUnpacked ) );
}

} // namespace

// A map's pakfile lump, read straight from memory with minizip-ng (which vpkpp links already).
struct CPackage::Zip_t
{
	std::vector<std::byte> m_Bytes;
	void *m_pReader = mz_zip_reader_create();
	std::unordered_map<std::string, ZipEntry_t> m_Entries; // path with forward slashes -> entry

	~Zip_t()
	{
		mz_zip_reader_close( m_pReader );
		mz_zip_reader_delete( &m_pReader );
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
	auto map = bsp::CBsp::Load( file, pError );
	if ( !map )
	{
		return nullptr;
	}
	std::unique_ptr<CPackage> pPackage( new CPackage );
	pPackage->m_File = file;
	pPackage->m_szFormat = "BSP " + std::to_string( map->GetVersion() );
	auto pZip = std::make_unique<Zip_t>();
	pZip->m_Bytes = map->GetPakfile();
	const bool bOpen = !pZip->m_Bytes.empty() && pZip->m_Bytes.size() <= INT32_MAX &&
		mz_zip_reader_open_buffer( pZip->m_pReader, reinterpret_cast<const std::uint8_t *>( pZip->m_Bytes.data() ), static_cast<std::int32_t>( pZip->m_Bytes.size() ), 0 ) == MZ_OK;
	for ( std::int32_t nResult = bOpen ? mz_zip_reader_goto_first_entry( pZip->m_pReader ) : MZ_END_OF_LIST; nResult == MZ_OK; nResult = mz_zip_reader_goto_next_entry( pZip->m_pReader ) )
	{
		mz_zip_file *pInfo = nullptr;
		if ( mz_zip_reader_entry_is_dir( pZip->m_pReader ) == MZ_OK || mz_zip_reader_entry_get_info( pZip->m_pReader, &pInfo ) != MZ_OK || !pInfo->filename )
		{
			continue;
		}
		std::string szPath = pInfo->filename;
		std::ranges::replace( szPath, '\\', '/' );
		PackageEntry_t &added = pPackage->m_Entries.emplace_back();
		added.m_szPath = szPath;
		added.m_nSize = static_cast<std::uint64_t>( pInfo->uncompressed_size );
		added.m_nCrc = pInfo->crc;
		pPackage->m_nTotalSize += added.m_nSize;
		pZip->m_Entries.emplace( std::move( szPath ), ZipEntry_t{ pInfo->filename, pInfo->compression_method, pInfo->disk_offset, pInfo->compressed_size, pInfo->uncompressed_size } );
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
	std::ranges::sort( m_Entries, []( const PackageEntry_t &a, const PackageEntry_t &b ) { return NormalizePath( a.m_szPath ) < NormalizePath( b.m_szPath ); } );
	m_Index.reserve( m_Entries.size() );
	for ( std::size_t i = 0; i < m_Entries.size(); i++ )
	{
		m_Index.emplace( NormalizePath( m_Entries[ i ].m_szPath ), i );
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
	if ( !pEntry )
	{
		return std::nullopt;
	}
	if ( m_pZip )
	{
		const auto it = m_pZip->m_Entries.find( pEntry->m_szPath );
		if ( it == m_pZip->m_Entries.end() )
		{
			return std::nullopt;
		}
		if ( it->second.m_nMethod == MZ_COMPRESS_METHOD_LZMA )
		{
			return ReadLzmaEntry( m_pZip->m_Bytes, it->second );
		}
		if ( mz_zip_reader_locate_entry( m_pZip->m_pReader, it->second.m_szName.c_str(), 0 ) != MZ_OK )
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
