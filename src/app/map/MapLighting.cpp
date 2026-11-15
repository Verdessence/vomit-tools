//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapLighting.h.
//
//===========================================================================//
#include "MapLighting.h"

#include <QElapsedTimer>
#include <QFile>
#include <QMetaObject>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include "DebugLog.h"
#include "util/Memory.h"

namespace
{

// Finished bakes kept in memory to be found again, and how much they may take together.
constexpr std::size_t MAX_EARLIER_BAKES = 6;
constexpr std::size_t MAX_EARLIER_BYTES = std::size_t{ 512 } << 20;
// Kept bakes in the cache folder: the newest of this many maps.
constexpr int MAX_CACHE_FILES = 12;

//-----------------------------------------------------------------------------
// Purpose: What a bake read, in one number: the same for two bakes that would come out the
//          same.
//-----------------------------------------------------------------------------
std::uint64_t Fingerprint( const xv::map::CLighting &lighting )
{
	std::uint64_t nHash = 0xcbf29ce484222325ull;
	const auto add = [ &nHash ]( std::uint64_t nValue ) { nHash = ( nHash ^ nValue ) * 0x100000001b3ull; };
	add( lighting.m_nSettingsHash );
	add( lighting.m_nLightsHash );
	add( lighting.m_nInstanceHash );
	// By what was read of each face, in an order that doesn't go by the solids' numbers.
	std::uint64_t nFaces = 0;
	for ( const xv::map::CLighting::BakedFace_t &face : lighting.m_Baked )
	{
		nFaces += face.m_nHash * 0x9e3779b97f4a7c15ull;
	}
	add( nFaces );
	add( lighting.m_Baked.size() );
	return nHash == 0 ? 1 : nHash;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapLighting::CMapLighting( QObject *pParent )
	: QObject( pParent )
{
	m_Worker = std::thread( [ this ] { Run(); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapLighting::~CMapLighting()
{
	{
		std::lock_guard lock( m_Mutex );
		m_bQuit = true;
		m_Pending.reset();
	}
	m_bCancel = true;
	m_Wake.notify_all();
	m_Worker.join();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapLighting::Bake( std::shared_ptr<const xv::map::CScene> pScene, std::shared_ptr<const std::vector<xv::map::LightMaterial_t>> pMaterials, const xv::map::LightingOptions_t &options, xv::map::LightingExtras_t extras, Start_t nStart, bool bSettled )
{
	if ( !pScene )
	{
		return;
	}
	// From the last result a bake can start from: a whole one, or what a bake after an edit had
	// done when it was last heard from (not a whole bake's direct light alone). A bake of
	// everything forgets it, so a bake that cancels this one doesn't start from older settings.
	if ( nStart != Start_t::START_LAST )
	{
		m_pFinished.reset();
	}
	const bool bEarlier = m_bKeepBakes && nStart != Start_t::START_NOTHING;
	const int nGeneration = ++m_nGeneration;
	{
		std::lock_guard lock( m_Mutex );
		m_Pending = Request_t{ std::move( pScene ), std::move( pMaterials ), options, std::move( extras ), nGeneration, m_Game, m_Map, m_bKeepBakes ? CacheFileFor( m_Map ) : std::filesystem::path{}, bEarlier, bSettled };
		m_Pending->m_Options.m_pPrevious = m_pFinished;
		if ( bEarlier )
		{
			m_Pending->m_Options.m_Cache = m_Earlier;
		}
	}
	m_bCancel = true;
	m_Wake.notify_all();
	m_bBaking = true;
	m_nPercent = 0;
	m_szStage = tr( "Waiting" );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapLighting::SetKeepBakes( bool bKeep )
{
	m_bKeepBakes = bKeep;
	if ( !bKeep )
	{
		m_Earlier.clear();
	}
}

//-----------------------------------------------------------------------------
// Purpose: <cache>/lighting/<the path's hash>.light.
//-----------------------------------------------------------------------------
std::filesystem::path CMapLighting::CacheFileFor( const std::filesystem::path &map )
{
	if ( map.empty() )
	{
		return {};
	}
	QString szFolder = qEnvironmentVariable( "VOMIT_TOOLS_LIGHTING_CACHE" );
	if ( szFolder == u"off" )
	{
		return {};
	}
	if ( szFolder.isEmpty() )
	{
		szFolder = QStandardPaths::writableLocation( QStandardPaths::CacheLocation ) + QStringLiteral( "/lighting" );
	}
	const QString szMap = QFileInfo( QString::fromStdU16String( map.u16string() ) ).absoluteFilePath();
	const QByteArray hash = QCryptographicHash::hash( szMap.toUtf8(), QCryptographicHash::Sha1 ).toHex();
	return std::filesystem::path( ( szFolder + u'/' + QString::fromLatin1( hash ) + QStringLiteral( ".light" ) ).toStdU16String() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapLighting::SetRadSources( std::optional<xv::vfs::CGameInfo> game, std::filesystem::path map )
{
	m_Game = std::move( game );
	m_Map = std::move( map );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapLighting::Clear()
{
	++m_nGeneration;
	{
		std::lock_guard lock( m_Mutex );
		m_Pending.reset();
	}
	m_bCancel = true;
	m_pLighting.reset();
	m_pFinished.reset();
	m_Earlier.clear();
	m_bBaking = false;
	m_nPercent = 0;
	m_szStage.clear();
}

//-----------------------------------------------------------------------------
// Purpose: The worker: waits for a request, bakes it unless a newer one cancels it, and hands
//          the result to the main thread if it is still the newest.
//-----------------------------------------------------------------------------
void CMapLighting::Run()
{
	for ( ;; )
	{
		Request_t request;
		{
			std::unique_lock lock( m_Mutex );
			// A finished bake goes to its file once nothing has been asked for a moment (an
			// edit is often followed by another), and before leaving.
			if ( m_pToWrite && !m_Wake.wait_for( lock, std::chrono::milliseconds( 500 ), [ this ] { return m_bQuit || m_Pending.has_value(); } ) )
			{
				lock.unlock();
				WriteCacheFile( m_WriteFile, *m_pToWrite );
				m_pToWrite.reset();
				lock.lock();
			}
			m_Wake.wait( lock, [ this ] { return m_bQuit || m_Pending.has_value(); } );
			if ( m_bQuit )
			{
				if ( m_pToWrite )
				{
					lock.unlock();
					WriteCacheFile( m_WriteFile, *m_pToWrite );
				}
				return;
			}
			request = std::move( *m_Pending );
			m_Pending.reset();
			m_bCancel = false;
		}
		const int nGeneration = request.m_nGeneration;
		xv::map::LightingOptions_t options = request.m_Options;
		// The RAD files: the game's lights.rad (as the game's own search paths find it, its VPKs
		// included), then the map's <map>.rad, whose rules win.
		const std::filesystem::path gameDir = request.m_Game ? request.m_Game->m_File : std::filesystem::path{};
		if ( !m_bRadRead || gameDir != m_RadGame || request.m_Map != m_RadMap )
		{
			m_Rad = {};
			if ( request.m_Game )
			{
				xv::vfs::CFileSystem files;
				files.Mount( *request.m_Game );
				if ( const auto text = files.Read( "lights.rad" ) )
				{
					xv::map::ParseRadFile( std::string_view( reinterpret_cast<const char *>( text->data() ), text->size() ), m_Rad );
				}
			}
			if ( !request.m_Map.empty() )
			{
				std::filesystem::path own = request.m_Map;
				own.replace_extension( ".rad" );
				QFile file( QString::fromStdString( own.string() ) );
				if ( file.open( QIODevice::ReadOnly ) )
				{
					const QByteArray text = file.readAll();
					xv::map::ParseRadFile( std::string_view( text.constData(), static_cast<std::size_t>( text.size() ) ), m_Rad );
				}
			}
			m_RadGame = gameDir;
			m_RadMap = request.m_Map;
			m_bRadRead = true;
		}
		options.m_Rad = m_Rad;
		options.m_pCancel = &m_bCancel;
		// The bake kept from the last time the map was open, among the earlier ones.
		if ( request.m_CacheFile != m_ReadFile )
		{
			ReadCacheFile( request.m_CacheFile );
		}
		if ( m_pFromFile && request.m_bUseCache )
		{
			options.m_Cache.push_back( m_pFromFile );
		}
		std::atomic<int> nLastPercent{ -1 };
		options.m_pfnProgress = [ this, nGeneration, &nLastPercent ]( int nPercent, std::string_view szStage ) {
			// Called from the bake's threads at once: only whole steps go to the main thread.
			if ( nLastPercent.exchange( nPercent ) == nPercent )
			{
				return;
			}
			QMetaObject::invokeMethod(
				this, [ this, nGeneration, nPercent, szStageText = QString::fromUtf8( szStage.data(), static_cast<qsizetype>( szStage.size() ) ) ] {
					if ( nGeneration != m_nGeneration )
					{
						return;
					}
					m_nPercent = nPercent;
					m_szStage = szStageText;
					emit Progress( nPercent, szStageText );
				},
				Qt::QueuedConnection );
		};
		// The direct light alone first, so a big map shows something while its light bounces;
		// after an edit, the chunks lit so far, now and then.
		options.m_pfnPartial = [ this, nGeneration ]( std::shared_ptr<const xv::map::CLighting> pPartial ) {
			QMetaObject::invokeMethod(
				this, [ this, nGeneration, pPartial = std::move( pPartial ) ]() mutable {
					if ( nGeneration != m_nGeneration )
					{
						return;
					}
					m_pLighting = std::move( pPartial );
					// A bake after an edit, chunks done so far: should another edit cut it
					// short, the next starts from here and not from before this one.
					if ( m_pLighting->m_bUnfinished )
					{
						m_pFinished = m_pLighting;
					}
					emit Baked();
				},
				Qt::QueuedConnection );
		};
		QElapsedTimer timer;
		timer.start();
		const std::vector<xv::map::LightMaterial_t> none;
		std::shared_ptr<const xv::map::CLighting> pLighting = xv::map::BakeLighting( *request.m_pScene, request.m_pMaterials ? std::span<const xv::map::LightMaterial_t>{ *request.m_pMaterials } : std::span<const xv::map::LightMaterial_t>{ none }, options, request.m_Extras );
		const double flMs = static_cast<double>( timer.nsecsElapsed() ) / 1e6;
		if ( pLighting )
		{
			QStringList stages;
			for ( const xv::map::BakeStage_t &stage : pLighting->m_Stages )
				stages << QStringLiteral( "%1 %2 ms" ).arg( QString::fromStdString( stage.m_szName ) ).arg( stage.m_flMs, 0, 'f', 0 );
			DebugLog() << "lighting baked in" << qRound( flMs ) << "ms" << ( pLighting->m_bFromEarlier ? "(an earlier bake, as it was)" : "" ) << ( options.m_pPrevious ? "from the bake before:" : ":" ) << pLighting->m_nRelitFaces << "faces lit," << pLighting->m_nKeptFaces << "kept;" << pLighting->m_nLuxels << "luxels," << pLighting->m_nTriangles << "triangles," << pLighting->m_nLights << "lights;" << stages.join( QStringLiteral( ", " ) );
		}
		if ( pLighting && request.m_bSettled )
		{
			// The file's bake has done its part; this one takes its place there, unless it is
			// what the file holds already.
			m_pFromFile.reset();
			m_pToWrite.reset();
			if ( !request.m_CacheFile.empty() && Fingerprint( *pLighting ) != m_nOnDisk )
			{
				m_pToWrite = pLighting;
				m_WriteFile = request.m_CacheFile;
			}
		}
		request = {};
		options.m_pPrevious.reset();
		options.m_Cache.clear();
		xv::ReleaseFreedMemory();
		if ( !pLighting )
		{
			continue;
		}
		QMetaObject::invokeMethod(
			this, [ this, nGeneration, pLighting = std::move( pLighting ), flMs ]() mutable {
				if ( nGeneration != m_nGeneration )
				{
					return;
				}
				m_pLighting = std::move( pLighting );
				m_pFinished = m_pLighting;
				// Kept to be found again: a few, within a budget (an atlas is 8 bytes a luxel).
				// One that was found among them is there already.
				if ( m_bKeepBakes && !m_pLighting->m_bFromEarlier )
				{
					m_Earlier.insert( m_Earlier.begin(), m_pLighting );
					std::size_t nBytes = 0;
					std::size_t nKeep = 0;
					for ( ; nKeep < m_Earlier.size() && nKeep < MAX_EARLIER_BAKES; nKeep++ )
					{
						nBytes += m_Earlier[ nKeep ]->m_Texels.size() * sizeof( std::uint16_t );
						if ( nKeep > 0 && nBytes > MAX_EARLIER_BYTES )
						{
							break;
						}
					}
					m_Earlier.resize( nKeep );
				}
				m_bBaking = false;
				m_nPercent = 100;
				m_flBakeMs = flMs;
				emit Baked();
			},
			Qt::QueuedConnection );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The map's kept bake, if it has one this version can read.
//-----------------------------------------------------------------------------
void CMapLighting::ReadCacheFile( const std::filesystem::path &file )
{
	m_ReadFile = file;
	m_pFromFile.reset();
	m_nOnDisk = 0;
	if ( file.empty() )
	{
		return;
	}
	QFile in( QString::fromStdU16String( file.u16string() ) );
	if ( !in.open( QIODevice::ReadOnly ) )
	{
		return;
	}
	const QByteArray bytes = qUncompress( in.readAll() );
	m_pFromFile = xv::map::ReadLighting( std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t *>( bytes.constData() ), static_cast<std::size_t>( bytes.size() ) } );
	if ( m_pFromFile )
	{
		m_nOnDisk = Fingerprint( *m_pFromFile );
		DebugLog() << "lighting: a bake kept from before," << m_pFromFile->m_nWidth << "by" << m_pFromFile->m_nHeight << "luxels";
	}
}

//-----------------------------------------------------------------------------
// Purpose: Writes a finished bake for the map's next opening (compressed lightly: an atlas is
//          mostly smooth or empty), and keeps the folder to the newest few maps.
//-----------------------------------------------------------------------------
void CMapLighting::WriteCacheFile( const std::filesystem::path &file, const xv::map::CLighting &lighting )
{
	const QString szFile = QString::fromStdU16String( file.u16string() );
	const QFileInfo info( szFile );
	if ( !QDir().mkpath( info.absolutePath() ) )
	{
		return;
	}
	QByteArray packed;
	{
		const std::vector<std::uint8_t> bytes = xv::map::WriteLighting( lighting );
		// qCompress takes an int's worth: a bigger atlas goes unkept.
		if ( bytes.size() > static_cast<std::size_t>( std::numeric_limits<int>::max() ) - 64 )
		{
			return;
		}
		packed = qCompress( reinterpret_cast<const uchar *>( bytes.data() ), static_cast<qsizetype>( bytes.size() ), 1 );
	}
	QSaveFile out( szFile );
	if ( !out.open( QIODevice::WriteOnly ) || out.write( packed ) != packed.size() || !out.commit() )
	{
		return;
	}
	m_nOnDisk = Fingerprint( lighting );
	const QFileInfoList kept = QDir( info.absolutePath() ).entryInfoList( { QStringLiteral( "*.light" ) }, QDir::Files, QDir::Time );
	for ( qsizetype i = MAX_CACHE_FILES; i < kept.size(); i++ )
	{
		QFile::remove( kept[ i ].absoluteFilePath() );
	}
}
