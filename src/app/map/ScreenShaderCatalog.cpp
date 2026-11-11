//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ScreenShaderCatalog.h.
//
//===========================================================================//
#include "ScreenShaderCatalog.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include "material/Material.h"
#include "shader/ScreenEffect.h"
#include "vfs/Vfs.h"

namespace
{

//-----------------------------------------------------------------------------
// Purpose: Whether a VMT may be a screen effect: it says screenspace, or is a patch (whose
//          include might). Most materials are ruled out by this without being parsed.
//-----------------------------------------------------------------------------
bool MayBeScreenEffect( std::string_view szText )
{
	std::string sz{ szText.substr( 0, 4096 ) };
	std::ranges::transform( sz, sz.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	return sz.find( "screenspace" ) != std::string::npos || sz.find( "patch" ) != std::string::npos;
}

struct Request_t
{
	QPointer<const QObject> m_pReceiver;
	std::function<void( std::shared_ptr<const ScreenCatalog_t> )> m_pfnDone;
};

// By the game's gameinfo.txt: the catalog once read, and who waits for one being read.
struct Cache_t
{
	std::mutex m_Mutex;
	std::map<std::string, std::shared_ptr<const ScreenCatalog_t>> m_Read;
	std::map<std::string, std::vector<Request_t>> m_Waiting;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Cache_t &Cache()
{
	// Never destroyed: a reader thread may still be finishing as the program exits.
	static Cache_t *pCache = new Cache_t;
	return *pCache;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Every VMT under materials/ (the models' too: a screen effect may live anywhere),
//          those whose shader is screenspace_general kept with their pixel shader.
//-----------------------------------------------------------------------------
ScreenCatalog_t ReadScreenCatalog( const xv::vfs::CGameInfo &game, const std::atomic<bool> &bStop )
{
	ScreenCatalog_t catalog;
	xv::vfs::CFileSystem fileSystem;
	fileSystem.Mount( game );
	const xv::material::Reader_t read = [ &fileSystem ]( std::string_view szPath ) { return fileSystem.Read( szPath ); };
	std::vector<std::string> materials = fileSystem.Find( "materials", ".vmt", true );
	std::ranges::sort( materials );
	for ( const std::string &szPath : materials )
	{
		if ( bStop )
			break;
		const std::optional<std::string> text = fileSystem.ReadText( szPath );
		if ( !text || !MayBeScreenEffect( *text ) )
			continue;
		const std::optional<xv::material::CMaterial> material = xv::material::ParseMaterial( *text, read );
		if ( !material || !xv::shader::IsScreenEffect( *material ) )
			continue;
		ScreenMaterial_t entry;
		QString szName = QString::fromStdString( szPath ).mid( 10 ); // past "materials/"
		szName.chop( 4 );
		entry.m_szMaterial = szName;
		const xv::shader::ScreenEffect_t effect = xv::shader::ReadScreenEffect( *material );
		entry.m_szPixelShader = QString::fromStdString( effect.m_szPixelShader );
		if ( !effect.m_szPixelShader.empty() )
		{
			for ( const std::string &szShader : xv::shader::PixelShaderPaths( effect.m_szPixelShader ) )
			{
				if ( fileSystem.Resolve( szShader ) )
				{
					entry.m_szShaderPath = QString::fromStdString( szShader );
					break;
				}
			}
		}
		catalog.m_Materials.push_back( std::move( entry ) );
	}
	std::set<std::string> shaders;
	for ( const std::string &szPath : fileSystem.Find( "shaders/fxc", ".vcs", false ) )
		shaders.insert( szPath );
	for ( const std::string &szPath : shaders )
		catalog.m_Shaders << QString::fromStdString( szPath );
	return catalog;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void RequestScreenCatalog( const xv::vfs::CGameInfo &game, const QObject *pReceiver, std::function<void( std::shared_ptr<const ScreenCatalog_t> )> pfnDone )
{
	Cache_t &cache = Cache();
	const std::string szKey = game.m_File.string();
	std::unique_lock lock( cache.m_Mutex );
	if ( const auto it = cache.m_Read.find( szKey ); it != cache.m_Read.end() )
	{
		const std::shared_ptr<const ScreenCatalog_t> pCatalog = it->second;
		lock.unlock();
		pfnDone( pCatalog );
		return;
	}
	auto &waiting = cache.m_Waiting[ szKey ];
	waiting.push_back( { pReceiver, std::move( pfnDone ) } );
	if ( waiting.size() > 1 )
		return; // already being read
	// Read for the rest of the run: the thread outlives no one it calls, as calls go through the
	// receivers' QPointers on the main thread.
	std::thread( [ game, szKey ] {
		static const std::atomic<bool> NEVER_STOP{ false };
		auto pCatalog = std::make_shared<const ScreenCatalog_t>( ReadScreenCatalog( game, NEVER_STOP ) );
		QCoreApplication *pApplication = QCoreApplication::instance();
		if ( !pApplication )
			return;
		QMetaObject::invokeMethod( pApplication, [ pCatalog, szKey ] {
			Cache_t &done = Cache();
			std::vector<Request_t> requests;
			{
				const std::lock_guard guard( done.m_Mutex );
				done.m_Read[ szKey ] = pCatalog;
				requests = std::move( done.m_Waiting[ szKey ] );
				done.m_Waiting.erase( szKey );
			}
			for ( const Request_t &request : requests )
			{
				if ( request.m_pReceiver )
					request.m_pfnDone( pCatalog );
			}
		} );
	} ).detach();
}
