//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Vfs.h.
//
//===========================================================================//
#include "Vfs.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <unordered_map>

#include <vpkpp/format/VPK.h>

#include "../kv1/Kv1.h"

namespace xv::vfs
{

namespace fs = std::filesystem;

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string LowerAscii( std::string_view szText )
{
	std::string szOut{ szText };
	std::ranges::transform( szOut, szOut.begin(), []( char c ) {
		return ( c >= 'A' && c <= 'Z' ) ? static_cast<char>( c - 'A' + 'a' ) : c;
	} );
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::string> SplitIds( std::string_view szKeys )
{
	std::vector<std::string> ids;
	std::size_t nStart = 0;
	while ( nStart <= szKeys.size() )
	{
		const auto nEnd = std::min( szKeys.find( '+', nStart ), szKeys.size() );
		if ( nEnd > nStart )
		{
			ids.push_back( LowerAscii( szKeys.substr( nStart, nEnd - nStart ) ) );
		}
		nStart = nEnd + 1;
	}
	return ids;
}

//-----------------------------------------------------------------------------
// Purpose: Handles [$X] and [!$X]; anything more elaborate is assumed to apply.
//-----------------------------------------------------------------------------
bool ConditionHolds( std::optional<std::string_view> condition )
{
	if ( !condition )
	{
		return true;
	}
	std::string_view c = *condition;
	if ( c.starts_with( '[' ) )
	{
		c.remove_prefix( 1 );
	}
	if ( c.ends_with( ']' ) )
	{
		c.remove_suffix( 1 );
	}
	const bool bNegate = c.starts_with( '!' );
	if ( bNegate )
	{
		c.remove_prefix( 1 );
	}
	if ( !c.starts_with( '$' ) || c.find_first_of( " |&" ) != std::string_view::npos )
	{
		return true;
	}
	c.remove_prefix( 1 );
#if defined( _WIN32 )
	const bool bHolds = c == "WIN32" || c == "WIN64" || c == "WINDOWS";
#elif defined( __APPLE__ )
	const bool bHolds = c == "OSX" || c == "POSIX";
#else
	const bool bHolds = c == "LINUX" || c == "POSIX";
#endif
	return bNegate != bHolds;
}

//-----------------------------------------------------------------------------
// Purpose: Directory entries, or none if the folder can't be read; never throws.
//-----------------------------------------------------------------------------
std::vector<fs::directory_entry> EntriesOf( const fs::path &directory )
{
	std::vector<fs::directory_entry> entries;
	std::error_code ec;
	for ( fs::directory_iterator it{ directory, ec }; !ec && it != fs::directory_iterator{}; it.increment( ec ) )
	{
		entries.push_back( *it );
	}
	return entries;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsVpkArchivePart( const fs::path &file )
{
	// name_000.vpk, name_001.vpk, ... hold data for name_dir.vpk and are not mounted on their own.
	const std::string szStem = ToUtf8( file.stem() );
	return szStem.size() > 4 && szStem[ szStem.size() - 4 ] == '_' && std::all_of( szStem.end() - 3, szStem.end(), []( char c ) { return c >= '0' && c <= '9'; } );
}

class CDirectoryMount final : public CMount
{
public:
	CDirectoryMount( const fs::path &location, std::vector<std::string> ids )
		: CMount( Kind_t::KIND_DIRECTORY, location, std::move( ids ) )
	{
		std::error_code ec;
		auto it = fs::recursive_directory_iterator( location, fs::directory_options::skip_permission_denied, ec );
		for ( ; !ec && it != fs::recursive_directory_iterator(); it.increment( ec ) )
		{
			std::error_code typeError;
			if ( it->is_regular_file( typeError ) )
			{
				m_Files.emplace( NormalizePath( ToUtf8( it->path().lexically_relative( location ) ) ), it->path() );
			}
		}
	}

	[[nodiscard]] bool Contains( const std::string &szPath ) const override
	{
		return m_Files.contains( szPath );
	}

	[[nodiscard]] std::optional<std::vector<std::byte>> Read( const std::string &szPath ) const override
	{
		const auto it = m_Files.find( szPath );
		if ( it == m_Files.end() )
		{
			return std::nullopt;
		}
		std::ifstream stream{ it->second, std::ios::binary };
		if ( !stream )
		{
			return std::nullopt;
		}
		std::vector<char> chars{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
		std::vector<std::byte> bytes( chars.size() );
		std::ranges::transform( chars, bytes.begin(), []( char c ) { return static_cast<std::byte>( c ); } );
		return bytes;
	}

	void ForEachFile( const std::function<void( const std::string & )> &pfnCallback ) const override
	{
		for ( const auto &[ path, _ ] : m_Files )
		{
			pfnCallback( path );
		}
	}

	[[nodiscard]] std::size_t GetFileCount() const override
	{
		return m_Files.size();
	}

	[[nodiscard]] std::optional<fs::path> DiskPath( const std::string &szPath ) const override
	{
		const auto it = m_Files.find( szPath );
		return it == m_Files.end() ? std::nullopt : std::optional{ it->second };
	}

private:
	std::unordered_map<std::string, fs::path> m_Files; // normalized path -> real file
};

class CVpkMount final : public CMount
{
public:
	CVpkMount( const fs::path &location, std::vector<std::string> ids, std::unique_ptr<vpkpp::PackFile> pPack )
		: CMount( Kind_t::KIND_VPK, location, std::move( ids ) ),
		  m_pPack( std::move( pPack ) )
	{
		m_pPack->runForAllEntries( [ this ]( const std::string &szPath, const vpkpp::Entry & ) {
			m_Entries.emplace( NormalizePath( szPath ), szPath );
		} );
	}

	[[nodiscard]] bool Contains( const std::string &szPath ) const override
	{
		return m_Entries.contains( szPath );
	}

	[[nodiscard]] std::optional<std::vector<std::byte>> Read( const std::string &szPath ) const override
	{
		const auto it = m_Entries.find( szPath );
		if ( it == m_Entries.end() )
		{
			return std::nullopt;
		}
		return m_pPack->readEntry( it->second );
	}

	void ForEachFile( const std::function<void( const std::string & )> &pfnCallback ) const override
	{
		for ( const auto &[ path, _ ] : m_Entries )
		{
			pfnCallback( path );
		}
	}

	[[nodiscard]] std::size_t GetFileCount() const override
	{
		return m_Entries.size();
	}

private:
	std::unique_ptr<vpkpp::PackFile> m_pPack;
	std::unordered_map<std::string, std::string> m_Entries; // normalized path -> path inside the VPK
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Filesystem paths as UTF-8, the encoding used for every std::string path in vomit-tools.
//          (On Windows,
//          std::filesystem::path::string() would use the ANSI code page instead.)
//-----------------------------------------------------------------------------
std::string ToUtf8( const fs::path &path )
{
	const std::u8string szUtf8 = path.u8string();
	return { szUtf8.begin(), szUtf8.end() };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
fs::path FromUtf8( std::string_view szUtf8 )
{
	return fs::path{ std::u8string{ szUtf8.begin(), szUtf8.end() } };
}

//-----------------------------------------------------------------------------
// Purpose: The real file behind a path, for folder mounts; nullopt for files inside a VPK.
//-----------------------------------------------------------------------------
std::optional<fs::path> CMount::DiskPath( const std::string & ) const
{
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: "Materials\\Skybox/Foo.VMT" -> "materials/skybox/foo.vmt"
//-----------------------------------------------------------------------------
std::string NormalizePath( std::string_view szPath )
{
	std::string szOut;
	szOut.reserve( szPath.size() );
	for ( const char c : szPath )
	{
		const char chSlash = c == '\\' ? '/' : c;
		if ( chSlash == '/' && ( szOut.empty() || szOut.back() == '/' ) )
		{
			continue;
		}
		szOut += chSlash;
	}
	return LowerAscii( szOut );
}

//-----------------------------------------------------------------------------
// Purpose: Resolve a path whose components may differ in case from what is on disk (gameinfo.txt
//          is written for case-insensitive Windows). Returns the input unchanged if nothing
//          matches.
//-----------------------------------------------------------------------------
fs::path FindCaseInsensitive( const fs::path &path )
{
	std::error_code ec;
	if ( fs::exists( path, ec ) )
	{
		return path;
	}
	fs::path current = path.root_path();
	for ( const fs::path &component : path.relative_path() )
	{
		if ( fs::exists( current / component, ec ) )
		{
			current /= component;
			continue;
		}
		const std::string szWanted = LowerAscii( ToUtf8( component ) );
		bool bFound = false;
		for ( const auto &entry : EntriesOf( current ) )
		{
			if ( LowerAscii( ToUtf8( entry.path().filename() ) ) == szWanted )
			{
				current = entry.path();
				bFound = true;
				break;
			}
		}
		if ( !bFound )
		{
			return path;
		}
	}
	return current;
}

//-----------------------------------------------------------------------------
// Purpose: baseDir defaults to the parent of the gameinfo folder, which holds for Valve's own
//          games.
//          Source mods running on an SDK base need it passed in.
//-----------------------------------------------------------------------------
std::optional<CGameInfo> CGameInfo::Load( const fs::path &file, std::string *pError, fs::path baseDir )
{
	std::ifstream stream{ file, std::ios::binary };
	if ( !stream )
	{
		if ( pError )
		{
			*pError = "cannot open " + ToUtf8( file );
		}
		return std::nullopt;
	}
	const std::string szText{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
	return Parse( szText, file, pError, std::move( baseDir ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<CGameInfo> CGameInfo::Parse( std::string_view szText, const fs::path &file, std::string *pError, fs::path baseDir )
{
	const auto fail = [ pError ]( std::string szMessage ) -> std::optional<CGameInfo> {
		if ( pError )
		{
			*pError = std::move( szMessage );
		}
		return std::nullopt;
	};

	const auto parsed = kv1::Parse( szText );
	const kv1::CNode *pRoot = parsed.m_Document.Find( "GameInfo" );
	if ( !pRoot || !pRoot->IsBlock() )
	{
		return fail( "no GameInfo block" );
	}
	const kv1::CNode *pFileSystem = pRoot->Find( "FileSystem" );
	const kv1::CNode *pSearchPaths = pFileSystem ? pFileSystem->Find( "SearchPaths" ) : nullptr;
	if ( !pSearchPaths || !pSearchPaths->IsBlock() )
	{
		return fail( "no FileSystem/SearchPaths block" );
	}

	CGameInfo info;
	info.m_File = fs::absolute( file );
	info.m_GameDir = info.m_File.parent_path();
	info.m_BaseDir = baseDir.empty() ? info.m_GameDir.parent_path() : fs::absolute( baseDir );
	if ( const kv1::CNode *pGame = pRoot->Find( "game" ) )
	{
		info.m_szGame = pGame->GetValue();
	}
	if ( const kv1::CNode *pTitle = pRoot->Find( "title" ) )
	{
		info.m_szTitle = pTitle->GetValue();
	}
	for ( const kv1::CNode &entry : pSearchPaths->GetChildren() )
	{
		if ( entry.IsPair() && ConditionHolds( entry.GetCondition() ) )
		{
			info.m_SearchPaths.push_back( { SplitIds( entry.GetKey() ), entry.GetValue() } );
		}
	}
	return info;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMount::CMount( Kind_t nKind, fs::path location, std::vector<std::string> ids )
	: m_nKind( nKind ),
	  m_Location( std::move( location ) ),
	  m_Ids( std::move( ids ) )
{}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMount::HasId( std::string_view szId ) const
{
	return std::ranges::find( m_Ids, LowerAscii( szId ) ) != m_Ids.end();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CFileSystem::Clear()
{
	m_Records.clear();
	m_Mounts.clear();
}

//-----------------------------------------------------------------------------
// Purpose: Replaces whatever was mounted before.
//-----------------------------------------------------------------------------
void CFileSystem::Mount( const CGameInfo &info )
{
	Clear();
	for ( const SearchPath_t &searchPath : info.m_SearchPaths )
	{
		MountSearchPath( info, searchPath );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const CMount *CFileSystem::AddDirectory( const fs::path &directory, std::vector<std::string> ids, std::string *pError )
{
	std::error_code ec;
	if ( !fs::is_directory( directory, ec ) )
	{
		if ( pError )
		{
			*pError = "not a directory: " + ToUtf8( directory );
		}
		return nullptr;
	}
	auto &pMount = m_Mounts.emplace_back( std::make_unique<CDirectoryMount>( directory, ids ) );
	m_Records.push_back( { ToUtf8( directory ), directory, std::move( ids ), MountRecord_t::Status_t::STATUS_MOUNTED, {}, pMount.get() } );
	return pMount.get();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const CMount *CFileSystem::AddVpk( const fs::path &file, std::vector<std::string> ids, std::string *pError )
{
	// sourcepp takes paths as std::string, which std::filesystem reads in the process code page.
	// The Windows build declares that code page to be UTF-8 in its manifest, so this is lossless there too.
	auto pPack = vpkpp::VPK::open( ToUtf8( file ) );
	if ( !pPack )
	{
		if ( pError )
		{
			*pError = "could not open VPK: " + ToUtf8( file );
		}
		return nullptr;
	}
	auto &pMount = m_Mounts.emplace_back( std::make_unique<CVpkMount>( file, ids, std::move( pPack ) ) );
	m_Records.push_back( { ToUtf8( file ), file, std::move( ids ), MountRecord_t::Status_t::STATUS_MOUNTED, {}, pMount.get() } );
	return pMount.get();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CFileSystem::Record( const SearchPath_t &searchPath, const fs::path &location, MountRecord_t::Status_t nStatus, std::string szMessage, const CMount *pMount )
{
	if ( pMount )
	{
		// addDirectory/addVpk already recorded it; attribute it to the gameinfo entry.
		m_Records.back().m_szSearchPath = searchPath.m_szPath;
		return;
	}
	m_Records.push_back( { searchPath.m_szPath, location, searchPath.m_Ids, nStatus, std::move( szMessage ), nullptr } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CFileSystem::MountSearchPath( const CGameInfo &info, const SearchPath_t &searchPath )
{
	std::string szExpanded = searchPath.m_szPath;
	const auto substitute = [ &szExpanded ]( std::string_view szToken, const fs::path &value ) {
		for ( auto nPos = szExpanded.find( szToken ); nPos != std::string::npos; nPos = szExpanded.find( szToken ) )
		{
			szExpanded.replace( nPos, szToken.size(), ToUtf8( value ) + "/" );
		}
	};
	substitute( "|gameinfo_path|", info.m_GameDir );
	substitute( "|all_source_engine_paths|", info.m_BaseDir );

	fs::path path = FromUtf8( szExpanded );
	if ( path.is_relative() )
	{
		path = info.m_BaseDir / path;
	}
	path = path.lexically_normal();
	std::error_code ec;

	// "custom/*": every folder and VPK inside, alphabetically.
	if ( path.filename() == "*" )
	{
		const fs::path directory = FindCaseInsensitive( path.parent_path() );
		if ( !fs::is_directory( directory, ec ) )
		{
			Record( searchPath, directory, MountRecord_t::Status_t::STATUS_MISSING, "folder not found", nullptr );
			return;
		}
		std::vector<fs::directory_entry> entries = EntriesOf( directory );
		std::ranges::sort( entries, {}, []( const fs::directory_entry &e ) { return LowerAscii( ToUtf8( e.path().filename() ) ); } );
		for ( const auto &entry : entries )
		{
			std::string szError;
			const CMount *pMount = nullptr;
			if ( entry.is_directory( ec ) )
			{
				pMount = AddDirectory( entry.path(), searchPath.m_Ids, &szError );
			}
			else if ( LowerAscii( ToUtf8( entry.path().extension() ) ) == ".vpk" && !IsVpkArchivePart( entry.path() ) )
			{
				pMount = AddVpk( entry.path(), searchPath.m_Ids, &szError );
			}
			else
			{
				continue;
			}
			Record( searchPath, entry.path(), MountRecord_t::Status_t::STATUS_FAILED, std::move( szError ), pMount );
		}
		return;
	}

	// "cstrike/cstrike_pak.vpk" means cstrike_pak_dir.vpk when the VPK is split into archives.
	if ( LowerAscii( ToUtf8( path.extension() ) ) == ".vpk" )
	{
		const fs::path split = FindCaseInsensitive( path.parent_path() / FromUtf8( ToUtf8( path.stem() ) + "_dir.vpk" ) );
		const fs::path single = FindCaseInsensitive( path );
		const fs::path file = fs::exists( split, ec ) ? split : single;
		if ( !fs::exists( file, ec ) )
		{
			Record( searchPath, path, MountRecord_t::Status_t::STATUS_MISSING, "VPK not found", nullptr );
			return;
		}
		std::string szError;
		const CMount *pMount = AddVpk( file, searchPath.m_Ids, &szError );
		Record( searchPath, file, MountRecord_t::Status_t::STATUS_FAILED, std::move( szError ), pMount );
		return;
	}

	const fs::path directory = FindCaseInsensitive( path );
	if ( !fs::is_directory( directory, ec ) )
	{
		Record( searchPath, directory, MountRecord_t::Status_t::STATUS_MISSING, "folder not found", nullptr );
		return;
	}
	std::string szError;
	const CMount *pMount = AddDirectory( directory, searchPath.m_Ids, &szError );
	Record( searchPath, directory, MountRecord_t::Status_t::STATUS_FAILED, std::move( szError ), pMount );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const CMount *CFileSystem::Resolve( std::string_view szPath, std::string_view szPathId ) const
{
	const std::string szNormalized = NormalizePath( szPath );
	for ( const auto &pMount : m_Mounts )
	{
		if ( pMount->HasId( szPathId ) && pMount->Contains( szNormalized ) )
		{
			return pMount.get();
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::vector<std::byte>> CFileSystem::Read( std::string_view szPath, std::string_view szPathId ) const
{
	const CMount *pMount = Resolve( szPath, szPathId );
	return pMount ? pMount->Read( NormalizePath( szPath ) ) : std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::string> CFileSystem::ReadText( std::string_view szPath, std::string_view szPathId ) const
{
	const auto bytes = Read( szPath, szPathId );
	if ( !bytes )
	{
		return std::nullopt;
	}
	return std::string{ reinterpret_cast<const char *>( bytes->data() ), bytes->size() };
}

//-----------------------------------------------------------------------------
// Purpose: Every file visible under pathId, with the mount it is read from.
//-----------------------------------------------------------------------------
std::map<std::string, const CMount *> CFileSystem::Files( std::string_view szPathId ) const
{
	std::map<std::string, const CMount *> out;
	for ( const auto &pMount : m_Mounts )
	{
		if ( pMount->HasId( szPathId ) )
		{
			pMount->ForEachFile( [ &out, m = pMount.get() ]( const std::string &szPath ) { out.emplace( szPath, m ); } );
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Visible files under a directory with the given extension ("" for all), e.g. ("scripts",
//          ".txt").
//-----------------------------------------------------------------------------
std::vector<std::string> CFileSystem::Find( std::string_view szDirectory, std::string_view szExtension, bool bRecursive, std::string_view szPathId ) const
{
	std::string szPrefix = NormalizePath( szDirectory );
	if ( !szPrefix.empty() && !szPrefix.ends_with( '/' ) )
	{
		szPrefix += '/';
	}
	const std::string szSuffix = LowerAscii( szExtension );
	std::set<std::string> found;
	for ( const auto &pMount : m_Mounts )
	{
		if ( !pMount->HasId( szPathId ) )
		{
			continue;
		}
		pMount->ForEachFile( [ & ]( const std::string &szPath ) {
			if ( szPath.starts_with( szPrefix ) && szPath.ends_with( szSuffix ) && ( bRecursive || szPath.find( '/', szPrefix.size() ) == std::string::npos ) )
			{
				found.insert( szPath );
			}
		} );
	}
	return { found.begin(), found.end() };
}

} // namespace xv::vfs
