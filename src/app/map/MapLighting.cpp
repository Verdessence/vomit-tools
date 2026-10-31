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

#include "util/Memory.h"

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
void CMapLighting::Bake( std::shared_ptr<const xv::map::CScene> pScene, std::shared_ptr<const std::vector<xv::map::LightMaterial_t>> pMaterials, const xv::map::LightingOptions_t &options, xv::map::LightingExtras_t extras )
{
	if ( !pScene )
	{
		return;
	}
	const int nGeneration = ++m_nGeneration;
	{
		std::lock_guard lock( m_Mutex );
		m_Pending = Request_t{ std::move( pScene ), std::move( pMaterials ), options, std::move( extras ), nGeneration, m_Game, m_Map };
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
			m_Wake.wait( lock, [ this ] { return m_bQuit || m_Pending.has_value(); } );
			if ( m_bQuit )
			{
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
		// The direct light alone first, so a big map shows something while its light bounces.
		options.m_pfnPartial = [ this, nGeneration ]( std::shared_ptr<const xv::map::CLighting> pPartial ) {
			QMetaObject::invokeMethod(
				this, [ this, nGeneration, pPartial = std::move( pPartial ) ]() mutable {
					if ( nGeneration != m_nGeneration )
					{
						return;
					}
					m_pLighting = std::move( pPartial );
					emit Baked();
				},
				Qt::QueuedConnection );
		};
		QElapsedTimer timer;
		timer.start();
		const std::vector<xv::map::LightMaterial_t> none;
		std::shared_ptr<const xv::map::CLighting> pLighting = xv::map::BakeLighting( *request.m_pScene, request.m_pMaterials ? std::span<const xv::map::LightMaterial_t>{ *request.m_pMaterials } : std::span<const xv::map::LightMaterial_t>{ none }, options, request.m_Extras );
		const double flMs = static_cast<double>( timer.nsecsElapsed() ) / 1e6;
		request = {};
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
				m_bBaking = false;
				m_nPercent = 100;
				m_flBakeMs = flMs;
				emit Baked();
			},
			Qt::QueuedConnection );
	}
}
