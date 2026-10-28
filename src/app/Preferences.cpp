//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Preferences.h.
//
//===========================================================================//
#include "Preferences.h"

#include "DebugLog.h"

#include <type_traits>

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

//-----------------------------------------------------------------------------
// Purpose: Calls pfnVisit( key, member ) for every map preference, so reading and saving them
//          go through one list.
//-----------------------------------------------------------------------------
template <class Preferences_t, class Visit_t>
void ForEachMapPreference( Preferences_t &map, Visit_t &&pfnVisit )
{
	pfnVisit( "maps/fgds", map.m_Fgds );
	pfnVisit( "maps/textureSize", map.m_nTextureSize );
	pfnVisit( "maps/fov", map.m_flFov );
	pfnVisit( "maps/sensitivity", map.m_flSensitivity );
	pfnVisit( "maps/forwardSpeed", map.m_flForwardSpeed );
	pfnVisit( "maps/renderDistance", map.m_flRenderDistance );
	pfnVisit( "maps/wheelStep", map.m_flWheelStep );
	pfnVisit( "maps/panScale", map.m_flPanScale );
	pfnVisit( "maps/farStep", map.m_flFarStep );
	pfnVisit( "maps/textureFilter", map.m_nTextureFilter );
	pfnVisit( "maps/selectionTint", map.m_flSelectionTint );
	pfnVisit( "maps/invalidTint", map.m_flInvalidTint );
	pfnVisit( "maps/instanceTint", map.m_flInstanceTint );
	pfnVisit( "maps/lightmapGridShade", map.m_flLightmapGridShade );
	pfnVisit( "maps/skybox3D", map.m_bSkybox3D );
	pfnVisit( "maps/sprites", map.m_bSprites );
	pfnVisit( "maps/wireframeModels", map.m_bWireframeModels );
	pfnVisit( "maps/selectedModels", map.m_nSelectedModels );
	pfnVisit( "maps/arrowsNudge", map.m_bArrowsNudge );
	pfnVisit( "maps/textureLock", map.m_bTextureLock );
	pfnVisit( "maps/scaleLock", map.m_bScaleLock );
	pfnVisit( "maps/rotationSnap", map.m_flRotationSnap );
	pfnVisit( "maps/hideSmallGrid", map.m_bHideSmallGrid );
	pfnVisit( "maps/hideGridBelow", map.m_flHideGridBelow );
	pfnVisit( "maps/highlightEvery", map.m_nHighlightEvery );
	pfnVisit( "maps/drawVertices", map.m_bDrawVertices );
	pfnVisit( "maps/vertexSize", map.m_nVertexSize );
	pfnVisit( "maps/verticesFrom", map.m_nVerticesFromPixels );
	pfnVisit( "maps/centerHandles", map.m_bCenterHandles );
	pfnVisit( "maps/handleSize", map.m_nHandleSize );
	pfnVisit( "maps/centerHandlesFrom", map.m_nCenterHandlesFromPixels );
	pfnVisit( "maps/namesFrom", map.m_nNamesFromPixels );
	pfnVisit( "maps/models2D", map.m_bModels2D );
	pfnVisit( "maps/texturedModels2D", map.m_bTexturedModels2D );
	pfnVisit( "maps/modelsFrom", map.m_nModelsFromPixels );
	pfnVisit( "maps/groupColors", map.m_bGroupColors );
	pfnVisit( "maps/groupColorsFrom", map.m_nGroupColors );
	pfnVisit( "maps/instanceColor2D", map.m_bInstanceColor2D );
	pfnVisit( "maps/cameraPixels", map.m_nCameraPixels );
	pfnVisit( "maps/wheelZoom", map.m_flWheelZoom );
	pfnVisit( "maps/keyZoom", map.m_flKeyZoom );
	pfnVisit( "maps/scrollStep", map.m_nScrollStep );
	pfnVisit( "maps/zoomSmallest", map.m_flZoomSmallest );
	pfnVisit( "maps/zoomLargest", map.m_flZoomLargest );
	auto &colors = map.m_Colors;
	pfnVisit( "maps/colors/background", colors.m_Background );
	pfnVisit( "maps/colors/grid", colors.m_Grid );
	pfnVisit( "maps/colors/gridHighlight", colors.m_GridHighlight );
	pfnVisit( "maps/colors/grid1024", colors.m_Grid1024 );
	pfnVisit( "maps/colors/axis", colors.m_Axis );
	pfnVisit( "maps/colors/brush", colors.m_Brush );
	pfnVisit( "maps/colors/entity", colors.m_Entity );
	pfnVisit( "maps/colors/invalid", colors.m_Invalid );
	pfnVisit( "maps/colors/vertex", colors.m_Vertex );
	pfnVisit( "maps/colors/centerHandle", colors.m_CenterHandle );
	pfnVisit( "maps/colors/selection", colors.m_Selection );
	pfnVisit( "maps/colors/selectionFill", colors.m_SelectionFill );
	pfnVisit( "maps/colors/toolSelection", colors.m_ToolSelection );
	pfnVisit( "maps/colors/toolHandle", colors.m_ToolHandle );
	pfnVisit( "maps/colors/toolBlock", colors.m_ToolBlock );
	pfnVisit( "maps/colors/camera", colors.m_Camera );
	pfnVisit( "maps/colors/instance2D", colors.m_Instance2D );
	pfnVisit( "maps/colors/background3D", colors.m_Background3D );
	pfnVisit( "maps/colors/selection3D", colors.m_Selection3D );
	pfnVisit( "maps/colors/selectionEdge3D", colors.m_SelectionEdge3D );
	pfnVisit( "maps/colors/instance", colors.m_Instance );
}

//-----------------------------------------------------------------------------
// Purpose: A setting as the type of the member it goes into; enums are kept as numbers and
//          colours as #aarrggbb, and anything unreadable leaves the member as it was.
//-----------------------------------------------------------------------------
template <class Value_t>
void ReadSetting( const QSettings &settings, const char *pszKey, Value_t &value )
{
	const QVariant stored = settings.value( QLatin1String( pszKey ) );
	if ( !stored.isValid() )
	{
		return;
	}
	if constexpr ( std::is_enum_v<Value_t> )
	{
		value = static_cast<Value_t>( stored.toInt() );
	}
	else if constexpr ( std::is_same_v<Value_t, QColor> )
	{
		if ( const QColor color = QColor::fromString( stored.toString() ); color.isValid() )
		{
			value = color;
		}
	}
	else if constexpr ( std::is_same_v<Value_t, QStringList> )
	{
		value = stored.toStringList();
	}
	else
	{
		value = stored.value<Value_t>();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <class Value_t>
void WriteSetting( QSettings &settings, const char *pszKey, const Value_t &value )
{
	if constexpr ( std::is_enum_v<Value_t> )
	{
		settings.setValue( QLatin1String( pszKey ), static_cast<int>( value ) );
	}
	else if constexpr ( std::is_same_v<Value_t, QColor> )
	{
		settings.setValue( QLatin1String( pszKey ), value.name( QColor::HexArgb ) );
	}
	else
	{
		settings.setValue( QLatin1String( pszKey ), value );
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int MapAnisotropy( MapTextureFilter_t nFilter )
{
	switch ( nFilter )
	{
		case MapTextureFilter_t::MAP_TEXTURE_FILTER_ANISOTROPIC_2X: return 2;
		case MapTextureFilter_t::MAP_TEXTURE_FILTER_ANISOTROPIC_4X: return 4;
		case MapTextureFilter_t::MAP_TEXTURE_FILTER_ANISOTROPIC_8X: return 8;
		case MapTextureFilter_t::MAP_TEXTURE_FILTER_ANISOTROPIC_16X: return 16;
		default: return 1;
	}
}

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
	ForEachMapPreference( m_Map, [ & ]( const char *pszKey, auto &value ) { ReadSetting( settings, pszKey, value ); } );
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
	m_nMapRevision++;
	QSettings settings;
	ForEachMapPreference( m_Map, [ & ]( const char *pszKey, const auto &value ) { WriteSetting( settings, pszKey, value ); } );
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
