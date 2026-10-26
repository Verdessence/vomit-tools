//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Preferences.h.
//
//===========================================================================//
#include "Preferences.h"

#include "DebugLog.h"

#include <QSettings>

namespace
{

constexpr auto GRID_ANIMATION_KEY = "particleGridAnimatesAll";
constexpr auto GRAPHICS_KEY = "graphicsApi";
constexpr auto SINGLE_WINDOW_KEY = "singleWindow";
constexpr auto MATERIAL_VISUAL_KEY = "materialVisual";
constexpr auto MANIFEST_REMINDER_KEY = "manifestReminder";
constexpr auto REOPEN_SESSION_KEY = "reopenSession";
constexpr auto DEBUG_LOG_KEY = "debugLog";
constexpr auto MATERIAL_LIVE_KEY = "materialLivePreview";
constexpr auto DESKTOP_FILE_CHOOSER_KEY = "desktopFileChooser";

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPreferences &CPreferences::Instance()
{
	static CPreferences preferences;
	return preferences;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPreferences::CPreferences()
	: m_bGridAnimatingAll( QSettings{}.value( GRID_ANIMATION_KEY, false ).toBool() ),
	  m_nGraphicsApi( GraphicsApiFromKey( QSettings{}.value( GRAPHICS_KEY ).toString().toStdString() ).value_or( GraphicsApi_t::GRAPHICS_API_AUTO ) ),
	  m_bSingleWindow( QSettings{}.value( SINGLE_WINDOW_KEY, true ).toBool() ),
	  m_bMaterialVisual( QSettings{}.value( MATERIAL_VISUAL_KEY, true ).toBool() ),
	  m_bMaterialLive( QSettings{}.value( MATERIAL_LIVE_KEY, true ).toBool() ),
	  m_bManifestReminder( QSettings{}.value( MANIFEST_REMINDER_KEY, true ).toBool() ),
	  m_bReopenSession( QSettings{}.value( REOPEN_SESSION_KEY, true ).toBool() ),
	  m_bDesktopFileChooser( QSettings{}.value( DESKTOP_FILE_CHOOSER_KEY, true ).toBool() ),
	  m_bDebugLog( QSettings{}.value( DEBUG_LOG_KEY, false ).toBool() )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetGridAnimatingAll( bool bAll )
{
	if ( bAll == m_bGridAnimatingAll )
	{
		return;
	}
	m_bGridAnimatingAll = bAll;
	QSettings{}.setValue( GRID_ANIMATION_KEY, bAll );
	emit GridAnimationChanged( bAll );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetGraphicsApi( GraphicsApi_t nApi )
{
	m_nGraphicsApi = nApi;
	QSettings{}.setValue( GRAPHICS_KEY, QString::fromLatin1( GraphicsApiKey( nApi ) ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetSingleWindow( bool bSingle )
{
	m_bSingleWindow = bSingle;
	QSettings{}.setValue( SINGLE_WINDOW_KEY, bSingle );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetMaterialVisual( bool bVisual )
{
	if ( bVisual == m_bMaterialVisual )
	{
		return;
	}
	m_bMaterialVisual = bVisual;
	QSettings{}.setValue( MATERIAL_VISUAL_KEY, bVisual );
	emit MaterialEditingChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetMaterialLive( bool bLive )
{
	if ( bLive == m_bMaterialLive )
	{
		return;
	}
	m_bMaterialLive = bLive;
	QSettings{}.setValue( MATERIAL_LIVE_KEY, bLive );
	emit MaterialEditingChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetManifestReminder( bool bRemind )
{
	m_bManifestReminder = bRemind;
	QSettings{}.setValue( MANIFEST_REMINDER_KEY, bRemind );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetReopenSession( bool bReopen )
{
	m_bReopenSession = bReopen;
	QSettings{}.setValue( REOPEN_SESSION_KEY, bReopen );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetDesktopFileChooser( bool bDesktop )
{
	m_bDesktopFileChooser = bDesktop;
	QSettings{}.setValue( DESKTOP_FILE_CHOOSER_KEY, bDesktop );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetDebugLog( bool bOn )
{
	m_bDebugLog = bOn;
	QSettings{}.setValue( DEBUG_LOG_KEY, bOn );
	SetDebugLogging( bOn );
	DebugLog() << "debug log on";
}
