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
constexpr auto MAP_FGDS_KEY = "maps/fgds";
constexpr auto MAP_FOV_KEY = "maps/fov";
constexpr auto MAP_SENSITIVITY_KEY = "maps/sensitivity";
constexpr auto MAP_SPEED_KEY = "maps/forwardSpeed";
constexpr auto MAP_DISTANCE_KEY = "maps/renderDistance";
constexpr auto MAP_TEXTURE_SIZE_KEY = "maps/textureSize";
constexpr auto MAP_HIDE_SMALL_GRID_KEY = "maps/hideSmallGrid";
constexpr auto MAP_HIGHLIGHT_KEY = "maps/highlightEvery";
constexpr auto MAP_VERTICES_KEY = "maps/drawVertices";
constexpr auto MAP_MODELS_KEY = "maps/models2D";
constexpr auto MAP_GROUP_COLORS_KEY = "maps/groupColors";
constexpr auto MAP_SKYBOX_KEY = "maps/skybox3D";

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
	const QSettings settings;
	const MapPreferences_t defaults;
	m_Map.m_Fgds = settings.value( MAP_FGDS_KEY ).toStringList();
	m_Map.m_flFov = settings.value( MAP_FOV_KEY, defaults.m_flFov ).toDouble();
	m_Map.m_flSensitivity = settings.value( MAP_SENSITIVITY_KEY, defaults.m_flSensitivity ).toDouble();
	m_Map.m_flForwardSpeed = settings.value( MAP_SPEED_KEY, defaults.m_flForwardSpeed ).toDouble();
	m_Map.m_flRenderDistance = settings.value( MAP_DISTANCE_KEY, defaults.m_flRenderDistance ).toDouble();
	m_Map.m_nTextureSize = settings.value( MAP_TEXTURE_SIZE_KEY, defaults.m_nTextureSize ).toInt();
	m_Map.m_bHideSmallGrid = settings.value( MAP_HIDE_SMALL_GRID_KEY, defaults.m_bHideSmallGrid ).toBool();
	m_Map.m_nHighlightEvery = settings.value( MAP_HIGHLIGHT_KEY, defaults.m_nHighlightEvery ).toInt();
	m_Map.m_bDrawVertices = settings.value( MAP_VERTICES_KEY, defaults.m_bDrawVertices ).toBool();
	m_Map.m_bModels2D = settings.value( MAP_MODELS_KEY, defaults.m_bModels2D ).toBool();
	m_Map.m_bGroupColors = settings.value( MAP_GROUP_COLORS_KEY, defaults.m_bGroupColors ).toBool();
	m_Map.m_bSkybox3D = settings.value( MAP_SKYBOX_KEY, defaults.m_bSkybox3D ).toBool();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetMapPreferences( const MapPreferences_t &map )
{
	if ( map == m_Map )
	{
		return;
	}
	m_Map = map;
	QSettings settings;
	settings.setValue( MAP_FGDS_KEY, map.m_Fgds );
	settings.setValue( MAP_FOV_KEY, map.m_flFov );
	settings.setValue( MAP_SENSITIVITY_KEY, map.m_flSensitivity );
	settings.setValue( MAP_SPEED_KEY, map.m_flForwardSpeed );
	settings.setValue( MAP_DISTANCE_KEY, map.m_flRenderDistance );
	settings.setValue( MAP_TEXTURE_SIZE_KEY, map.m_nTextureSize );
	settings.setValue( MAP_HIDE_SMALL_GRID_KEY, map.m_bHideSmallGrid );
	settings.setValue( MAP_HIGHLIGHT_KEY, map.m_nHighlightEvery );
	settings.setValue( MAP_VERTICES_KEY, map.m_bDrawVertices );
	settings.setValue( MAP_MODELS_KEY, map.m_bModels2D );
	settings.setValue( MAP_GROUP_COLORS_KEY, map.m_bGroupColors );
	settings.setValue( MAP_SKYBOX_KEY, map.m_bSkybox3D );
	emit MapPreferencesChanged();
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
