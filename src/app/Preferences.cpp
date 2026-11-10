//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Preferences.h.
//
//===========================================================================//
#include "Preferences.h"

#include "DebugLog.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <span>
#include <type_traits>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QKeySequence>
#include <QPalette>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>

namespace
{

// The file's name, beside Qt's own settings file.
constexpr auto CONFIG_NAME = "vomit-tools.nut";
constexpr auto CONFIG_VARIABLE = "VOMIT_TOOLS_CONFIG";
constexpr auto KEYS_PRESET_KEY = "map:keys_preset";
// The map preferences the keys' lines stand for, not a setting of their own in the file.
constexpr std::string_view KEY_PRESET_LEGACY = "maps/keyPreset";
constexpr std::string_view CUSTOM_KEYS_LEGACY = "maps/customKeys";
// What an editor's block can change.
constexpr std::array<const char *, 4> CODE_OVERRIDES{ "code:scheme", "code:font", "code:font_size", "code:completion" };

constexpr const char *CODE_SCHEMES[] = { "system", "gruvbox_dark", "gruvbox_dark_hard", "gruvbox_light" };
constexpr const char *THEME_SCHEMES[] = { "system", "light", "dark" };
constexpr const char *TEXTURE_FILTERS[] = { "bilinear", "trilinear", "anisotropic_2x", "anisotropic_4x", "anisotropic_8x", "anisotropic_16x" };
constexpr const char *SELECTED_MODELS[] = { "props", "edges", "box" };
constexpr const char *CARVES[] = { "auto", "hammer", "box_first", "fan" };
constexpr const char *GROUP_COLORS[] = { "random", "saved", "none" };
constexpr const char *KEY_PRESETS[] = { "hammer++", "hammer", "blender" }; // custom is the binds
constexpr GraphicsApi_t GRAPHICS_APIS[] = { GraphicsApi_t::GRAPHICS_API_AUTO, GraphicsApi_t::GRAPHICS_API_VULKAN, GraphicsApi_t::GRAPHICS_API_OPENGL, GraphicsApi_t::GRAPHICS_API_DIRECT3D11,
	GraphicsApi_t::GRAPHICS_API_DIRECT3D12, GraphicsApi_t::GRAPHICS_API_METAL, GraphicsApi_t::GRAPHICS_API_SOFTWARE };

//-----------------------------------------------------------------------------
// Purpose: The names an enum's values are written as, in order.
//-----------------------------------------------------------------------------
std::span<const char *const> NamesOf( CodeScheme_t )
{
	return CODE_SCHEMES;
}
std::span<const char *const> NamesOf( ThemeScheme_t )
{
	return THEME_SCHEMES;
}
std::span<const char *const> NamesOf( MapTextureFilter_t )
{
	return TEXTURE_FILTERS;
}
std::span<const char *const> NamesOf( MapSelectedModels_t )
{
	return SELECTED_MODELS;
}
std::span<const char *const> NamesOf( MapCarve_t )
{
	return CARVES;
}
std::span<const char *const> NamesOf( MapGroupColors_t )
{
	return GROUP_COLORS;
}
// The preset is written as map:keys_preset, and custom as binds; the visitors still see it.
std::span<const char *const> NamesOf( MapKeyPreset_t )
{
	return KEY_PRESETS;
}

//-----------------------------------------------------------------------------
// Purpose: Calls pfnVisit( old key, member... ) for every map preference, with that member of each
//          set of preferences given, so reading, saving and comparing go through one list. The
//          key is the one QSettings used; the file's is MapConfKey's.
//-----------------------------------------------------------------------------
template <class Visit_t, class... Preferences_t>
void ForEachMapPreference( Visit_t &&pfnVisit, Preferences_t &...maps )
{
	pfnVisit( "maps/fgds", maps.m_Fgds... );
	pfnVisit( "maps/textureSize", maps.m_nTextureSize... );
	pfnVisit( "maps/fov", maps.m_flFov... );
	pfnVisit( "maps/sensitivity", maps.m_flSensitivity... );
	pfnVisit( "maps/forwardSpeed", maps.m_flForwardSpeed... );
	pfnVisit( "maps/renderDistance", maps.m_flRenderDistance... );
	pfnVisit( "maps/wheelStep", maps.m_flWheelStep... );
	pfnVisit( "maps/panScale", maps.m_flPanScale... );
	pfnVisit( "maps/farStep", maps.m_flFarStep... );
	pfnVisit( "maps/textureFilter", maps.m_nTextureFilter... );
	pfnVisit( "maps/selectionTint", maps.m_flSelectionTint... );
	pfnVisit( "maps/invalidTint", maps.m_flInvalidTint... );
	pfnVisit( "maps/instanceTint", maps.m_flInstanceTint... );
	pfnVisit( "maps/lightmapGridShade", maps.m_flLightmapGridShade... );
	pfnVisit( "maps/lightBrightness", maps.m_flLightBrightness... );
	pfnVisit( "maps/lightBounces", maps.m_nLightBounces... );
	pfnVisit( "maps/lightLuxels", maps.m_nLightLuxels... );
	pfnVisit( "maps/lightHdr", maps.m_bLightHdr... );
	pfnVisit( "maps/lightFog", maps.m_bFog... ); // named when only the lighting preview had fog
	pfnVisit( "maps/lightOnly", maps.m_bLightOnly... );
	pfnVisit( "maps/lightPropShadows", maps.m_bLightPropShadows... );
	pfnVisit( "maps/lightSmoothShadows", maps.m_bLightSmoothShadows... );
	pfnVisit( "maps/lightAutoBake", maps.m_bLightAutoBake... );
	pfnVisit( "maps/lightInRadius", maps.m_bLightInRadius... );
	pfnVisit( "maps/lightRadius", maps.m_flLightRadius... );
	pfnVisit( "maps/lightDelay", maps.m_nLightDelay... );
	pfnVisit( "maps/lightSkySamples", maps.m_nLightSkySamples... );
	pfnVisit( "maps/lightMonitorGamma", maps.m_flLightMonitorGamma... );
	pfnVisit( "maps/skybox3D", maps.m_bSkybox3D... );
	pfnVisit( "maps/sky2D", maps.m_bSky2D... );
	pfnVisit( "maps/originAxes", maps.m_bOriginAxes... );
	pfnVisit( "maps/propertiesFollowSelection", maps.m_bPropertiesFollowSelection... );
	pfnVisit( "maps/physicsDrag", maps.m_flPhysicsDrag... );
	pfnVisit( "maps/physicsBullet", maps.m_flPhysicsBullet... );
	pfnVisit( "maps/physicsExplode", maps.m_flPhysicsExplode... );
	pfnVisit( "maps/physicsGravityX", maps.m_flPhysicsGravityX... );
	pfnVisit( "maps/physicsGravityY", maps.m_flPhysicsGravityY... );
	pfnVisit( "maps/physicsGravityZ", maps.m_flPhysicsGravityZ... );
	pfnVisit( "maps/physicsAirDensity", maps.m_flPhysicsAirDensity... );
	pfnVisit( "maps/physicsTimeScale", maps.m_flPhysicsTimeScale... );
	pfnVisit( "maps/physicsSleep", maps.m_bPhysicsSleep... );
	pfnVisit( "maps/physicsAutoMass", maps.m_bPhysicsAutoMass... );
	pfnVisit( "maps/physicsPropsOnly", maps.m_bPhysicsPropsOnly... );
	pfnVisit( "maps/physicsNoSelfCollisions", maps.m_bPhysicsNoSelfCollisions... );
	pfnVisit( "maps/physicsDrawMoving", maps.m_bPhysicsDrawMoving... );
	pfnVisit( "maps/physicsDrawStatic", maps.m_bPhysicsDrawStatic... );
	pfnVisit( "maps/sprites", maps.m_bSprites... );
	pfnVisit( "maps/wireframeModels", maps.m_bWireframeModels... );
	pfnVisit( "maps/selectedModels", maps.m_nSelectedModels... );
	pfnVisit( "maps/gizmoPixels", maps.m_flGizmoPixels... );
	pfnVisit( "maps/gizmoThickness", maps.m_flGizmoThickness... );
	pfnVisit( "maps/gizmoAlpha", maps.m_nGizmoAlpha... );
	pfnVisit( "maps/gizmoFollowsHandles", maps.m_bGizmoFollowsHandles... );
	pfnVisit( "maps/gizmoAll", maps.m_bGizmoAll... );
	pfnVisit( "maps/arrowsNudge", maps.m_bArrowsNudge... );
	pfnVisit( "maps/textureLock", maps.m_bTextureLock... );
	pfnVisit( "maps/scaleLock", maps.m_bScaleLock... );
	pfnVisit( "maps/rotationSnap", maps.m_flRotationSnap... );
	pfnVisit( "maps/pivot", maps.m_bPivot... );
	pfnVisit( "maps/carve", maps.m_nCarve... );
	pfnVisit( "maps/groupCarve", maps.m_bGroupCarve... );
	pfnVisit( "maps/faceEditWindow", maps.m_bFaceEditWindow... );
	pfnVisit( "maps/onePanel", maps.m_bOnePanel... );
	pfnVisit( "maps/autosave", maps.m_bAutosave... );
	pfnVisit( "maps/autosaveMinutes", maps.m_nAutosaveMinutes... );
	pfnVisit( "maps/keyPreset", maps.m_nKeyPreset... );
	pfnVisit( "maps/customKeys", maps.m_CustomKeys... );
	pfnVisit( "maps/showDisplacementBrush", maps.m_bShowDisplacementBrush... );
	pfnVisit( "maps/hideSmallGrid", maps.m_bHideSmallGrid... );
	pfnVisit( "maps/gridBeyond", maps.m_bGridBeyond... );
	pfnVisit( "maps/hideGridBelow", maps.m_flHideGridBelow... );
	pfnVisit( "maps/highlightEvery", maps.m_nHighlightEvery... );
	pfnVisit( "maps/drawVertices", maps.m_bDrawVertices... );
	pfnVisit( "maps/brushKeepConcave", maps.m_bBrushKeepConcave... );
	pfnVisit( "maps/vertexSize", maps.m_nVertexSize... );
	pfnVisit( "maps/verticesFrom", maps.m_nVerticesFromPixels... );
	pfnVisit( "maps/centerHandles", maps.m_bCenterHandles... );
	pfnVisit( "maps/handleSize", maps.m_nHandleSize... );
	pfnVisit( "maps/handleReach", maps.m_nHandleReach... );
	pfnVisit( "maps/centerHandlesFrom", maps.m_nCenterHandlesFromPixels... );
	pfnVisit( "maps/namesFrom", maps.m_nNamesFromPixels... );
	pfnVisit( "maps/models2D", maps.m_bModels2D... );
	pfnVisit( "maps/texturedModels2D", maps.m_bTexturedModels2D... );
	pfnVisit( "maps/modelsFrom", maps.m_nModelsFromPixels... );
	pfnVisit( "maps/groupColors", maps.m_bGroupColors... );
	pfnVisit( "maps/groupColorsFrom", maps.m_nGroupColors... );
	pfnVisit( "maps/instanceColor2D", maps.m_bInstanceColor2D... );
	pfnVisit( "maps/cameraPixels", maps.m_nCameraPixels... );
	pfnVisit( "maps/wheelZoom", maps.m_flWheelZoom... );
	pfnVisit( "maps/keyZoom", maps.m_flKeyZoom... );
	pfnVisit( "maps/scrollStep", maps.m_nScrollStep... );
	pfnVisit( "maps/zoomSmallest", maps.m_flZoomSmallest... );
	pfnVisit( "maps/zoomLargest", maps.m_flZoomLargest... );
	pfnVisit( "maps/colors/background", maps.m_Colors.m_Background... );
	pfnVisit( "maps/colors/grid", maps.m_Colors.m_Grid... );
	pfnVisit( "maps/colors/gridHighlight", maps.m_Colors.m_GridHighlight... );
	pfnVisit( "maps/colors/grid1024", maps.m_Colors.m_Grid1024... );
	pfnVisit( "maps/colors/gridBeyond", maps.m_Colors.m_GridBeyond... );
	pfnVisit( "maps/colors/axis", maps.m_Colors.m_Axis... );
	pfnVisit( "maps/colors/brush", maps.m_Colors.m_Brush... );
	pfnVisit( "maps/colors/entity", maps.m_Colors.m_Entity... );
	pfnVisit( "maps/colors/invalid", maps.m_Colors.m_Invalid... );
	pfnVisit( "maps/colors/vertex", maps.m_Colors.m_Vertex... );
	pfnVisit( "maps/colors/centerHandle", maps.m_Colors.m_CenterHandle... );
	pfnVisit( "maps/colors/selection", maps.m_Colors.m_Selection... );
	pfnVisit( "maps/colors/selectionFill", maps.m_Colors.m_SelectionFill... );
	pfnVisit( "maps/colors/toolSelection", maps.m_Colors.m_ToolSelection... );
	pfnVisit( "maps/colors/toolHandle", maps.m_Colors.m_ToolHandle... );
	pfnVisit( "maps/colors/toolBlock", maps.m_Colors.m_ToolBlock... );
	pfnVisit( "maps/colors/entityMarker", maps.m_Colors.m_EntityMarker... );
	pfnVisit( "maps/colors/pivot", maps.m_Colors.m_Pivot... );
	pfnVisit( "maps/colors/pointfile", maps.m_Colors.m_Pointfile... );
	pfnVisit( "maps/colors/cordon", maps.m_Colors.m_Cordon... );
	pfnVisit( "maps/colors/camera", maps.m_Colors.m_Camera... );
	pfnVisit( "maps/colors/instance2D", maps.m_Colors.m_Instance2D... );
	pfnVisit( "maps/colors/background3D", maps.m_Colors.m_Background3D... );
	pfnVisit( "maps/colors/selection3D", maps.m_Colors.m_Selection3D... );
	pfnVisit( "maps/colors/selectionEdge3D", maps.m_Colors.m_SelectionEdge3D... );
	pfnVisit( "maps/colors/instance", maps.m_Colors.m_Instance... );
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

//-----------------------------------------------------------------------------
// Purpose: Calls pfnVisit( key, old key, note, member... ) for every setting that isn't the map
//          viewer's: its key in the file, its QSettings key before the file (for the first
//          run; null when it had none), a note for the file's comment, and that member of each
//          set of preferences given.
//-----------------------------------------------------------------------------
template <class Visit_t, class... General_t>
void ForEachGeneralPreference( Visit_t &&pfnVisit, General_t &...generals )
{
	pfnVisit( "general:graphics", "graphicsApi", "what the previews draw with, from the next start", generals.m_nGraphicsApi... );
	pfnVisit( "general:single_window", "singleWindow", "files opened from outside join the window already open", generals.m_bSingleWindow... );
	pfnVisit( "general:reopen_session", "reopenSession", "the files open at closing open again", generals.m_bReopenSession... );
	pfnVisit( "general:tab_groups", "tabGroups/on", "tabs grouped under a strip by kind", generals.m_bTabGroups... );
	pfnVisit( "general:manifest_reminder", "manifestReminder", "offer to add saved files to the game's manifest", generals.m_bManifestReminder... );
	pfnVisit( "general:desktop_file_chooser", "desktopFileChooser", "the desktop's own open and save windows", generals.m_bDesktopFileChooser... );
	pfnVisit( "general:debug_log", "debugLog", "log what goes on in the background", generals.m_bDebugLog... );
	pfnVisit( "general:simple_interface", "simpleInterface", "fewer, plainer controls", generals.m_bSimpleInterface... );
	pfnVisit( "theme:color_scheme", nullptr, "light or dark, else the desktop's", generals.m_nThemeScheme... );
	pfnVisit( "theme:accent", nullptr, "#rrggbb for selections and highlights; empty: the desktop's", generals.m_ThemeAccent... );
	pfnVisit( "theme:style", nullptr, "a Qt style (Fusion, Windows...); empty: the desktop's", generals.m_szThemeStyle... );
	pfnVisit( "code:scheme", "codeEditor/scheme", "the text editors' colours", generals.m_nCodeScheme... );
	pfnVisit( "code:font", "codeEditor/fontFamily", "empty: the system's fixed-width font", generals.m_szCodeFontFamily... );
	pfnVisit( "code:font_size", "codeEditor/fontSize", "points; 0: the system's", generals.m_nCodeFontSize... );
	pfnVisit( "code:completion", "codeEditor/completion", "suggest words while typing", generals.m_bCodeCompletion... );
	pfnVisit( "material:visual", "materialVisual", "edit materials as a list of parameters (else as text)", generals.m_bMaterialVisual... );
	pfnVisit( "material:live_preview", "materialLivePreview", "redraw the preview at every edit in the list", generals.m_bMaterialLive... );
	pfnVisit( "particles:grid_animates_all", "particleGridAnimatesAll", "every system in the grid plays, not only the selected one", generals.m_bGridAnimatingAll... );
	pfnVisit( "tools:studiomdl", "sourceTools/studiomdl", "empty: found beside the game and in PATH", generals.m_szStudiomdl... );
	pfnVisit( "tools:bspzip", "sourceTools/bspzip", nullptr, generals.m_szBspzip... );
	pfnVisit( "tools:vbsp", "sourceTools/vbsp", nullptr, generals.m_szVbsp... );
	pfnVisit( "tools:vvis", "sourceTools/vvis", nullptr, generals.m_szVvis... );
	pfnVisit( "tools:vrad", "sourceTools/vrad", nullptr, generals.m_szVrad... );
	pfnVisit( "tools:game", "sourceTools/game", "what runs the game on a map", generals.m_szGameExe... );
	pfnVisit( "tools:bsp_dir", "sourceTools/bspDir", "where Run Map copies the compiled map; empty: the game's maps folder", generals.m_szBspDir... );
}

//-----------------------------------------------------------------------------
// Purpose: A map preference's key in the file, from its QSettings key: "maps/lightBounces" is
//          map:light_bounces, "maps/colors/background3D" map:colors:background_3d.
//-----------------------------------------------------------------------------
std::string MapConfKey( std::string_view szLegacy )
{
	if ( szLegacy == "maps/lightFog" )
	{
		return "map:fog"; // its old name dates from when only the lighting preview had fog
	}
	std::string szKey = "map";
	if ( szLegacy.starts_with( "maps/" ) )
	{
		szLegacy.remove_prefix( 5 );
	}
	szKey += ':';
	for ( std::size_t i = 0; i < szLegacy.size(); i++ )
	{
		const char c = szLegacy[ i ];
		const char chPrevious = i > 0 ? szLegacy[ i - 1 ] : '/';
		if ( c == '/' )
		{
			szKey += ':';
		}
		else if ( std::isupper( static_cast<unsigned char>( c ) ) )
		{
			if ( std::islower( static_cast<unsigned char>( chPrevious ) ) )
			{
				szKey += '_';
			}
			szKey += static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
		}
		else if ( std::isdigit( static_cast<unsigned char>( c ) ) )
		{
			if ( std::isalpha( static_cast<unsigned char>( chPrevious ) ) )
			{
				szKey += '_';
			}
			szKey += c;
		}
		else
		{
			szKey += c;
		}
	}
	return szKey;
}

//-----------------------------------------------------------------------------
// Purpose: A value as the file writes it.
//-----------------------------------------------------------------------------
template <class Value_t>
std::string ToText( const Value_t &value )
{
	if constexpr ( std::is_same_v<Value_t, bool> )
	{
		return value ? "true" : "false";
	}
	else if constexpr ( std::is_same_v<Value_t, GraphicsApi_t> )
	{
		return std::string{ GraphicsApiKey( value ) };
	}
	else if constexpr ( std::is_enum_v<Value_t> )
	{
		const auto names = NamesOf( value );
		const auto nIndex = static_cast<std::size_t>( value );
		return nIndex < names.size() ? names[ nIndex ] : names[ 0 ];
	}
	else if constexpr ( std::is_same_v<Value_t, int> )
	{
		return std::to_string( value );
	}
	else if constexpr ( std::is_same_v<Value_t, double> )
	{
		// The shortest text that reads back the same; a value that came from a float (most
		// defaults) as that float, so 0.4 isn't written 0.40000000596.
		std::array<char, 64> buffer{};
		const float flNarrow = static_cast<float>( value );
		const auto result = static_cast<double>( flNarrow ) == value ? std::to_chars( buffer.data(), buffer.data() + buffer.size(), flNarrow ) : std::to_chars( buffer.data(), buffer.data() + buffer.size(), value );
		return std::string( buffer.data(), result.ptr );
	}
	else if constexpr ( std::is_same_v<Value_t, QString> )
	{
		return value.toStdString();
	}
	else if constexpr ( std::is_same_v<Value_t, QStringList> )
	{
		return value.join( QStringLiteral( ", " ) ).toStdString();
	}
	else if constexpr ( std::is_same_v<Value_t, QColor> )
	{
		return !value.isValid() ? std::string{} : value.alpha() == 255 ? value.name( QColor::HexRgb ).toStdString()
																	   : value.name( QColor::HexArgb ).toStdString();
	}
}

//-----------------------------------------------------------------------------
// Purpose: The choices an enum setting takes, "a | b | c", for the file's comments and messages.
//-----------------------------------------------------------------------------
template <class Value_t>
std::string ChoicesOf( const Value_t &value )
{
	std::string szChoices;
	if constexpr ( std::is_same_v<Value_t, GraphicsApi_t> )
	{
		for ( const GraphicsApi_t nApi : GRAPHICS_APIS )
		{
			szChoices += ( szChoices.empty() ? "" : " | " ) + std::string{ GraphicsApiKey( nApi ) };
		}
	}
	else if constexpr ( std::is_enum_v<Value_t> )
	{
		for ( const char *pszName : NamesOf( value ) )
		{
			szChoices += ( szChoices.empty() ? "" : " | " ) + std::string{ pszName };
		}
	}
	return szChoices;
}

//-----------------------------------------------------------------------------
// Purpose: Reads a value written in the file into a member of its type; on failure the member
//          is left as it was and the reason comes back.
//-----------------------------------------------------------------------------
template <class Value_t>
std::optional<std::string> FromText( const std::string &szText, Value_t &value )
{
	const QString szValue = QString::fromStdString( szText ).trimmed();
	if constexpr ( std::is_same_v<Value_t, bool> )
	{
		const auto parsed = xv::conf::ParseBool( szText );
		if ( !parsed )
		{
			return "expected true or false, not \"" + szText + "\"";
		}
		value = *parsed;
	}
	else if constexpr ( std::is_same_v<Value_t, GraphicsApi_t> )
	{
		const auto parsed = GraphicsApiFromKey( szValue.toLower().toStdString() );
		if ( !parsed )
		{
			return "expected " + ChoicesOf( value ) + ", not \"" + szText + "\"";
		}
		value = *parsed;
	}
	else if constexpr ( std::is_enum_v<Value_t> )
	{
		const auto names = NamesOf( value );
		const auto found = std::ranges::find_if( names, [ &szValue ]( const char *pszName ) { return szValue.compare( QLatin1String( pszName ), Qt::CaseInsensitive ) == 0; } );
		if ( found == names.end() )
		{
			return "expected " + ChoicesOf( value ) + ", not \"" + szText + "\"";
		}
		value = static_cast<Value_t>( found - names.begin() );
	}
	else if constexpr ( std::is_same_v<Value_t, int> )
	{
		bool bOk = false;
		const int n = szValue.toInt( &bOk );
		if ( !bOk )
		{
			return "expected a whole number, not \"" + szText + "\"";
		}
		value = n;
	}
	else if constexpr ( std::is_same_v<Value_t, double> )
	{
		bool bOk = false;
		const double fl = szValue.toDouble( &bOk );
		if ( !bOk || !std::isfinite( fl ) )
		{
			return "expected a number, not \"" + szText + "\"";
		}
		value = fl;
	}
	else if constexpr ( std::is_same_v<Value_t, QString> )
	{
		value = szValue;
	}
	else if constexpr ( std::is_same_v<Value_t, QStringList> )
	{
		QStringList list;
		for ( const std::string &szPart : xv::conf::SplitList( szText ) )
		{
			list << QString::fromStdString( szPart );
		}
		value = list;
	}
	else if constexpr ( std::is_same_v<Value_t, QColor> )
	{
		if ( szValue.isEmpty() )
		{
			value = QColor{};
			return std::nullopt;
		}
		const QColor color = QColor::fromString( szValue );
		if ( !color.isValid() )
		{
			return "expected a colour such as #e0a040 (or #aarrggbb), not \"" + szText + "\"";
		}
		value = color;
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: Where a problem is, as messages give it: the file's name and the line.
//-----------------------------------------------------------------------------
QString Where( const xv::conf::Location_t &where )
{
	return QStringLiteral( "%1:%2" ).arg( QFileInfo( QString::fromStdString( where.m_File.string() ) ).fileName() ).arg( where.m_nLine );
}

//-----------------------------------------------------------------------------
// Purpose: A key as a bind line writes it ("Ctrl+Shift+E").
//-----------------------------------------------------------------------------
std::string KeyText( QKeyCombination key )
{
	return QKeySequence( key ).toString( QKeySequence::PortableText ).toStdString();
}

//-----------------------------------------------------------------------------
// Purpose: A key preset's name in the file, and back (custom is never written: it is binds).
//-----------------------------------------------------------------------------
std::optional<MapKeyPreset_t> KeyPresetFromText( const std::string &szText )
{
	const QString szValue = QString::fromStdString( szText ).trimmed();
	for ( std::size_t i = 0; i < std::size( KEY_PRESETS ); i++ )
	{
		if ( szValue.compare( QLatin1String( KEY_PRESETS[ i ] ), Qt::CaseInsensitive ) == 0 )
		{
			return static_cast<MapKeyPreset_t>( i );
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: Whether a bind or unbind item is for the map views.
//-----------------------------------------------------------------------------
bool IsMapKeyItem( const std::vector<std::string> &fields )
{
	return !fields.empty() && QString::fromStdString( fields.front() ).trimmed().compare( u"map", Qt::CaseInsensitive ) == 0;
}

//-----------------------------------------------------------------------------
// Purpose: The action a bind names, by its id (Hammer++'s keybinding name), in any case.
//-----------------------------------------------------------------------------
std::optional<MapAction_t> MapActionById( const QString &szId )
{
	for ( const MapActionInfo_t &info : MapActions() )
	{
		if ( szId.compare( QLatin1String( info.m_pszId ), Qt::CaseInsensitive ) == 0 )
		{
			return info.m_nAction;
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: The text above everything in a new file.
//-----------------------------------------------------------------------------
constexpr const char *CONFIG_HEADER = R"(// vomit-tools settings
//
// Squirrel (the VScript language), read as data: nothing here is run. Tables nest into settings
// ("map <- { fov = 90 }" is map's fov, and "map.fov = 90;" sets it alone); "local name = value;"
// gives a value a name to use below; IncludeScript( "other.nut" ) reads another file there
// (beside this one unless the path says otherwise; ~ and * work). Text goes in quotes, or in
// @"..." to take backslashes as written (Windows paths).
//
// Preferences writes its changes back here, leaving the rest as it is, and edits made here apply
// as soon as the file is saved. Settings left commented out keep their defaults.
//
// Keys (map views): binds.append( [ "map", "Ctrl+Shift+E", "Brush_Extrude" ] ); gives an action
// that key in place of the preset's (more binds for the action add keys), and
// unbinds.append( [ "map", "<action>" ] ); leaves it none. "binds <- [ [ ... ], [ ... ] ]" works
// too. The actions are named as in Hammer++'s hammerplusplus_keybindings.cfg.
//
// Rules: rules.append( [ "group:Maps", "*.vmf" ] ); puts the tabs of matching files in that
// group; [ "editor:stripper", "*/stripper/*.cfg" ] opens them in that editor (map, model, detail
// or stripper). A pattern with a '/' is matched against the whole path, one without against the
// name.
//
// Per editor: editor <- { sound = { code = { font_size = 12 } } } changes code's scheme, font,
// font_size and completion in that editor only (map, model, texture, material, particle, sound,
// sky, detail, archive, stripper).
)";

//-----------------------------------------------------------------------------
// Purpose: A setting's value as the file writes it: a list as a list, the rest through
//          ToText.
//-----------------------------------------------------------------------------
template <class Value_t>
std::string NutValue( const Value_t &value )
{
	if constexpr ( std::is_same_v<Value_t, QStringList> )
	{
		std::string szText = "[";
		for ( qsizetype i = 0; i < value.size(); i++ )
		{
			szText += ( i == 0 ? " " : ", " ) + xv::conf::Literal( value[ i ].toStdString() );
		}
		return szText + ( value.isEmpty() ? "]" : " ]" );
	}
	else
	{
		return xv::conf::Literal( ToText( value ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A new file holding every setting, in tables by category: the ones at their defaults
//          commented out, each with its choices or a note.
//-----------------------------------------------------------------------------
std::string GenerateConfig( const GeneralPreferences_t &general, const MapPreferences_t &map )
{
	std::string szText = CONFIG_HEADER;
	std::vector<std::string> open; // the tables now open, outermost first
	const auto close = [ & ]( std::size_t nKeep ) {
		while ( open.size() > nKeep )
		{
			open.pop_back();
			szText += std::string( open.size(), '\t' ) + ( open.empty() ? "}\n" : "},\n" );
		}
	};
	const auto line = [ & ]( const std::string &szKey, const std::string &szValue, bool bDefault, const std::string &szNote ) {
		std::vector<std::string> parts;
		for ( std::size_t nAt = 0; nAt <= szKey.size(); )
		{
			const std::size_t nColon = std::min( szKey.find( ':', nAt ), szKey.size() );
			parts.push_back( szKey.substr( nAt, nColon - nAt ) );
			nAt = nColon + 1;
		}
		std::size_t nCommon = 0;
		while ( nCommon < open.size() && nCommon + 1 < parts.size() && open[ nCommon ] == parts[ nCommon ] )
		{
			nCommon++;
		}
		close( nCommon );
		while ( open.size() + 1 < parts.size() )
		{
			const std::string &szTable = parts[ open.size() ];
			szText += open.empty() ? "\n" + szTable + " <- {\n" : std::string( open.size(), '\t' ) + szTable + " = {\n";
			open.push_back( szTable );
		}
		std::string szLine = std::string( open.size(), '\t' ) + ( bDefault ? "// " : "" ) + parts.back() + " = " + szValue + ",";
		if ( !szNote.empty() )
		{
			const std::size_t nWidth = szLine.size() + 3 * open.size(); // tabs as four columns
			szLine += std::string( nWidth < 44 ? 44 - nWidth : 1, ' ' ) + "// " + szNote;
		}
		szText += szLine + "\n";
	};
	const GeneralPreferences_t defaults;
	ForEachGeneralPreference(
		[ & ]( const char *pszKey, const char *, const char *pszNote, const auto &value, const auto &fallback ) {
			std::string szNote = ChoicesOf( value );
			if ( pszNote )
			{
				szNote += ( szNote.empty() ? "" : ": " ) + std::string{ pszNote };
			}
			line( pszKey, NutValue( value ), value == fallback, szNote );
		},
		general, defaults );
	const MapPreferences_t mapDefaults;
	line( KEYS_PRESET_KEY, xv::conf::Literal( map.m_nKeyPreset == MapKeyPreset_t::MAP_KEY_PRESET_CUSTOM ? "hammer++" : KEY_PRESETS[ static_cast<std::size_t>( map.m_nKeyPreset ) ] ),
		map.m_nKeyPreset == MapKeyPreset_t::MAP_KEY_PRESET_HAMMER_PLUS_PLUS || map.m_nKeyPreset == MapKeyPreset_t::MAP_KEY_PRESET_CUSTOM, "hammer++ | hammer | blender: the keys binds start from" );
	ForEachMapPreference(
		[ & ]( const char *pszLegacy, const auto &value, const auto &fallback ) {
			if ( pszLegacy != KEY_PRESET_LEGACY && pszLegacy != CUSTOM_KEYS_LEGACY )
			{
				line( MapConfKey( pszLegacy ), NutValue( value ), value == fallback, ChoicesOf( value ) );
			}
		},
		map, mapDefaults );
	close( 0 );
	return szText;
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
// Purpose: Reads the file, making it from the old settings the first time, and watches it.
//-----------------------------------------------------------------------------
CPreferences::CPreferences()
{
	m_szConfigPath = qEnvironmentVariable( CONFIG_VARIABLE );
	if ( m_szConfigPath.isEmpty() )
	{
		// Beside Qt's own file (where tests point it, too). Windows keeps Qt's in the registry.
		const QSettings settings;
		const QString szFolder = settings.format() == QSettings::NativeFormat && QSysInfo::productType() == u"windows" ? QStandardPaths::writableLocation( QStandardPaths::AppConfigLocation )
																													   : QFileInfo( settings.fileName() ).absolutePath();
		// Other programs built on the app's code (the tests) keep a file of their own, as Qt keeps
		// their settings apart by program name.
		const QString szApp = QCoreApplication::applicationName();
		const QString szName = szApp.isEmpty() || szApp == u"vomit-tools" ? QLatin1String( CONFIG_NAME ) : szApp + u'.' + QLatin1String( CONFIG_NAME );
		m_szConfigPath = QDir( szFolder ).filePath( szName );
	}
	m_pWatcher = new QFileSystemWatcher( this );
	m_pReloadTimer = new QTimer( this );
	m_pReloadTimer->setSingleShot( true );
	m_pReloadTimer->setInterval( 200 );
	// Editors save by replacing the file, and Qt's own file beside it changes often: a change is
	// only read when one of the config's files really differs from what was read.
	const auto later = [ this ] { m_pReloadTimer->start(); };
	connect( m_pWatcher, &QFileSystemWatcher::fileChanged, this, later );
	connect( m_pWatcher, &QFileSystemWatcher::directoryChanged, this, later );
	connect( m_pReloadTimer, &QTimer::timeout, this, [ this ] {
		for ( const auto &file : m_Config.Files() )
		{
			const QFileInfo info( QString::fromStdString( file.string() ) );
			const QString szStamp = info.exists() ? QStringLiteral( "%1 %2" ).arg( info.lastModified().toMSecsSinceEpoch() ).arg( info.size() ) : QString{};
			if ( m_pWatcher->property( qPrintable( QStringLiteral( "stamp:" ) + info.absoluteFilePath() ) ).toString() != szStamp )
			{
				ReloadConfig();
				return;
			}
		}
	} );

	// The settings file's first name, before the state file beside it gave up this one.
	if ( const QString szEarlier = QFileInfo( m_szConfigPath ).absoluteDir().filePath( QStringLiteral( "vomit_tools.nut" ) );
		!QFileInfo::exists( m_szConfigPath ) && QFileInfo::exists( szEarlier ) && QCoreApplication::applicationName() == u"vomit-tools" )
	{
		QFile::rename( szEarlier, m_szConfigPath );
	}
	if ( QFileInfo::exists( m_szConfigPath ) )
	{
		m_Config.Load( m_szConfigPath.toStdString() );
	}
	else
	{
		Migrate();
	}
	ApplyConfig();
	WatchConfig();
}

//-----------------------------------------------------------------------------
// Purpose: The first run: what QSettings held becomes the file, and leaves QSettings.
//-----------------------------------------------------------------------------
void CPreferences::Migrate()
{
	QSettings settings;
	GeneralPreferences_t general;
	ForEachGeneralPreference( [ & ]( const char *, const char *pszLegacy, const char *, auto &value ) {
		using Value_t = std::remove_cvref_t<decltype( value )>;
		if ( !pszLegacy || !settings.contains( QLatin1String( pszLegacy ) ) )
		{
			return;
		}
		if constexpr ( std::is_same_v<Value_t, GraphicsApi_t> )
		{
			value = GraphicsApiFromKey( settings.value( QLatin1String( pszLegacy ) ).toString().toStdString() ).value_or( value );
		}
		else if constexpr ( std::is_enum_v<Value_t> )
		{
			const int n = settings.value( QLatin1String( pszLegacy ) ).toInt();
			if ( n >= 0 && static_cast<std::size_t>( n ) < NamesOf( value ).size() )
			{
				value = static_cast<Value_t>( n );
			}
		}
		else
		{
			ReadSetting( settings, pszLegacy, value );
		}
	},
		general );
	MapPreferences_t map;
	ForEachMapPreference( [ & ]( const char *pszKey, auto &value ) { ReadSetting( settings, pszKey, value ); }, map );

	const std::string szText = GenerateConfig( general, map );
	QSaveFile file( m_szConfigPath );
	QDir().mkpath( QFileInfo( m_szConfigPath ).absolutePath() );
	const bool bWritten = file.open( QIODevice::WriteOnly ) && file.write( szText.data(), static_cast<qint64>( szText.size() ) ) == static_cast<qint64>( szText.size() ) && file.commit();
	m_Config.LoadText( m_szConfigPath.toStdString(), szText );
	if ( map.m_nKeyPreset == MapKeyPreset_t::MAP_KEY_PRESET_CUSTOM )
	{
		m_Map = map;
		StoreKeys();
	}
	if ( !bWritten )
	{
		DebugLog() << "couldn't write" << m_szConfigPath;
		return;
	}
	ForEachGeneralPreference(
		[ & ]( const char *, const char *pszLegacy, const char *, const auto & ) {
			if ( pszLegacy )
			{
				settings.remove( QLatin1String( pszLegacy ) );
			}
		},
		general );
	ForEachMapPreference( [ & ]( const char *pszKey, const auto & ) { settings.remove( QLatin1String( pszKey ) ); }, map );
	DebugLog() << "settings moved to" << m_szConfigPath;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CPreferences::GetConfigPath() const
{
	return m_szConfigPath;
}

//-----------------------------------------------------------------------------
// Purpose: Reads the file again from disk.
//-----------------------------------------------------------------------------
void CPreferences::ReloadConfig()
{
	m_Config.Load( m_szConfigPath.toStdString() );
	ApplyConfig();
	WatchConfig();
}

//-----------------------------------------------------------------------------
// Purpose: Watches every file the config reads, and the folders they're in (an editor that
//          saves by replacing the file leaves the old one unwatched), remembering each file's
//          time and size to tell a real change.
//-----------------------------------------------------------------------------
void CPreferences::WatchConfig()
{
	if ( !m_pWatcher->files().isEmpty() )
	{
		m_pWatcher->removePaths( m_pWatcher->files() );
	}
	if ( !m_pWatcher->directories().isEmpty() )
	{
		m_pWatcher->removePaths( m_pWatcher->directories() );
	}
	QStringList files;
	QStringList folders;
	for ( const auto &file : m_Config.Files() )
	{
		const QFileInfo info( QString::fromStdString( file.string() ) );
		const QString szStamp = info.exists() ? QStringLiteral( "%1 %2" ).arg( info.lastModified().toMSecsSinceEpoch() ).arg( info.size() ) : QString{};
		m_pWatcher->setProperty( qPrintable( QStringLiteral( "stamp:" ) + info.absoluteFilePath() ), szStamp );
		if ( info.exists() )
		{
			files << info.absoluteFilePath();
		}
		if ( QFileInfo::exists( info.absolutePath() ) && !folders.contains( info.absolutePath() ) )
		{
			folders << info.absolutePath();
		}
	}
	if ( !files.isEmpty() )
	{
		m_pWatcher->addPaths( files );
	}
	if ( !folders.isEmpty() )
	{
		m_pWatcher->addPaths( folders );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Reads every setting out of the config, says what can't be read, and tells the views
//          about what changed.
//-----------------------------------------------------------------------------
void CPreferences::ApplyConfig()
{
	QStringList problems;
	for ( const auto &error : m_Config.Errors() )
	{
		problems << QStringLiteral( "%1: %2" ).arg( Where( error.m_Where ), QString::fromStdString( error.m_szMessage ) );
	}
	std::set<std::string> known{ KEYS_PRESET_KEY };
	// Every line of a key is read in turn, so each bad one is reported and the last good one wins.
	std::map<std::string, std::vector<const xv::conf::Entry_t *>> byKey;
	for ( const xv::conf::Entry_t &entry : m_Config.Entries() )
	{
		byKey[ entry.m_szKey ].push_back( &entry );
	}
	const auto read = [ & ]( const std::string &szKey, auto &value ) {
		known.insert( szKey );
		const auto found = byKey.find( szKey );
		if ( found == byKey.end() )
		{
			return;
		}
		for ( const xv::conf::Entry_t *pEntry : found->second )
		{
			if ( const auto szProblem = FromText( pEntry->m_szValue, value ) )
			{
				problems << QStringLiteral( "%1: %2: %3" ).arg( Where( pEntry->m_Where ), QString::fromStdString( szKey ), QString::fromStdString( *szProblem ) );
			}
		}
	};

	GeneralPreferences_t general;
	ForEachGeneralPreference( [ & ]( const char *pszKey, const char *, const char *, auto &value ) { read( pszKey, value ); }, general );
	general.m_nCodeFontSize = std::clamp( general.m_nCodeFontSize, 0, 72 );
	if ( !general.m_szThemeStyle.isEmpty() && !QStyleFactory::keys().contains( general.m_szThemeStyle, Qt::CaseInsensitive ) )
	{
		problems << QStringLiteral( "theme:style: no style \"%1\" here (there are %2)" ).arg( general.m_szThemeStyle, QStyleFactory::keys().join( QStringLiteral( ", " ) ) );
		general.m_szThemeStyle.clear();
	}

	MapPreferences_t map;
	ForEachMapPreference(
		[ & ]( const char *pszLegacy, auto &value ) {
			if ( pszLegacy != KEY_PRESET_LEGACY && pszLegacy != CUSTOM_KEYS_LEGACY )
			{
				read( MapConfKey( pszLegacy ), value );
			}
		},
		map );

	// The keys: a preset, and binds over it.
	MapKeyPreset_t nBase = MapKeyPreset_t::MAP_KEY_PRESET_HAMMER_PLUS_PLUS;
	if ( const xv::conf::Entry_t *pEntry = m_Config.Find( KEYS_PRESET_KEY ) )
	{
		if ( const auto preset = KeyPresetFromText( pEntry->m_szValue ) )
		{
			nBase = *preset;
		}
		else
		{
			problems << QStringLiteral( "%1: %2: expected hammer++, hammer or blender" ).arg( Where( pEntry->m_Where ), QLatin1String( KEYS_PRESET_KEY ) );
		}
	}
	map.m_nKeyPreset = nBase;
	std::vector<MapKeyBinding_t> keys = MapPresetKeys( nBase );
	std::set<MapAction_t> touched;
	std::vector<MapKeyBinding_t> bound;
	for ( const char *pszKeyword : { "unbind", "bind" } )
	{
		const bool bBind = std::string_view{ pszKeyword } == "bind";
		for ( const xv::conf::Entry_t *pEntry : m_Config.Keyword( pszKeyword ) )
		{
			const std::vector<std::string> &fields = pEntry->m_Fields;
			if ( fields.size() != ( bBind ? 3u : 2u ) )
			{
				problems << QStringLiteral( "%1: %2 takes %3" ).arg( Where( pEntry->m_Where ), bBind ? QStringLiteral( "a bind" ) : QStringLiteral( "an unbind" ), bBind ? QStringLiteral( "[ \"map\", key, action ]" ) : QStringLiteral( "[ \"map\", action ]" ) );
				continue;
			}
			const QString szContext = QString::fromStdString( fields.front() ).trimmed();
			const QString szAction = QString::fromStdString( fields.back() ).trimmed();
			if ( szContext.compare( u"map", Qt::CaseInsensitive ) != 0 )
			{
				problems << QStringLiteral( "%1: only map keys can be bound so far, not \"%2\"" ).arg( Where( pEntry->m_Where ), szContext );
				continue;
			}
			const auto nAction = MapActionById( szAction );
			if ( !nAction )
			{
				problems << QStringLiteral( "%1: no map action \"%2\" (they are named as in Hammer++'s keybindings)" ).arg( Where( pEntry->m_Where ), szAction );
				continue;
			}
			touched.insert( *nAction );
			if ( !bBind )
			{
				continue;
			}
			const QString szKey = QString::fromStdString( fields[ 1 ] ).trimmed();
			const QKeySequence sequence = QKeySequence::fromString( szKey, QKeySequence::PortableText );
			if ( sequence.count() != 1 || sequence[ 0 ].key() == Qt::Key_unknown )
			{
				problems << QStringLiteral( "%1: \"%2\" isn't a key (such as Ctrl+Shift+E)" ).arg( Where( pEntry->m_Where ), szKey );
				continue;
			}
			bound.push_back( { *nAction, sequence[ 0 ] } );
		}
	}
	if ( !touched.empty() )
	{
		std::erase_if( keys, [ &touched ]( const MapKeyBinding_t &key ) { return touched.contains( key.m_nAction ); } );
		keys.insert( keys.end(), bound.begin(), bound.end() );
		map.m_nKeyPreset = MapKeyPreset_t::MAP_KEY_PRESET_CUSTOM;
		map.m_CustomKeys = MapKeysToSettings( keys );
	}

	// Each editor's own block.
	QHash<QString, CodeOverrides_t> overrides;
	for ( const QString &szEditor : EditorNames() )
	{
		CodeOverrides_t override;
		const std::string szPrefix = "editor:" + szEditor.toStdString() + ":";
		const auto readOverride = [ & ]( const char *pszKey, auto &optional, auto fallback ) {
			known.insert( szPrefix + pszKey );
			if ( m_Config.Find( szPrefix + pszKey ) )
			{
				read( szPrefix + pszKey, fallback );
				optional = fallback;
			}
		};
		readOverride( CODE_OVERRIDES[ 0 ], override.m_nScheme, general.m_nCodeScheme );
		readOverride( CODE_OVERRIDES[ 1 ], override.m_szFontFamily, general.m_szCodeFontFamily );
		readOverride( CODE_OVERRIDES[ 2 ], override.m_nFontSize, general.m_nCodeFontSize );
		readOverride( CODE_OVERRIDES[ 3 ], override.m_bCompletion, general.m_bCodeCompletion );
		if ( override != CodeOverrides_t{} )
		{
			overrides.insert( szEditor, override );
		}
	}

	// File and tab rules.
	std::vector<FileRule_t> rules;
	for ( const xv::conf::Entry_t *pEntry : m_Config.Keyword( "rule" ) )
	{
		const QString szEffect = pEntry->m_Fields.size() == 2 ? QString::fromStdString( pEntry->m_Fields[ 0 ] ).trimmed() : QString{};
		const QString szGlob = pEntry->m_Fields.size() == 2 ? QString::fromStdString( pEntry->m_Fields[ 1 ] ).trimmed() : QString{};
		const qsizetype nColon = szEffect.indexOf( u':' );
		FileRule_t rule{ szEffect.left( nColon ).trimmed().toLower(), nColon < 0 ? QString{} : szEffect.mid( nColon + 1 ).trimmed(), szGlob };
		if ( szGlob.isEmpty() || rule.m_szArgument.isEmpty() || ( rule.m_szEffect != u"group" && rule.m_szEffect != u"editor" ) )
		{
			problems << QStringLiteral( "%1: a rule takes [ \"group:<name>\", pattern ] or [ \"editor:<name>\", pattern ]" ).arg( Where( pEntry->m_Where ) );
			continue;
		}
		static const QStringList OPENERS{ QStringLiteral( "map" ), QStringLiteral( "model" ), QStringLiteral( "detail" ), QStringLiteral( "stripper" ) };
		if ( rule.m_szEffect == u"editor" && !OPENERS.contains( rule.m_szArgument, Qt::CaseInsensitive ) )
		{
			problems << QStringLiteral( "%1: files can be sent to the map, model, detail or stripper editor, not \"%2\"" ).arg( Where( pEntry->m_Where ), rule.m_szArgument );
			continue;
		}
		if ( rule.m_szEffect == u"editor" )
		{
			rule.m_szArgument = rule.m_szArgument.toLower();
		}
		rules.push_back( std::move( rule ) );
	}

	// Anything else the file sets is a typo or a setting this version doesn't have.
	for ( const xv::conf::Entry_t &entry : m_Config.Entries() )
	{
		if ( !xv::conf::IsKeyword( entry.m_szKey ) && !known.contains( entry.m_szKey ) )
		{
			problems << QStringLiteral( "%1: no setting \"%2\"" ).arg( Where( entry.m_Where ), QString::fromStdString( entry.m_szKey ) );
		}
	}

	// In the order they come in the files: "name:line: ..." by name, then line.
	std::ranges::stable_sort( problems, []( const QString &a, const QString &b ) {
		const QString szFileA = a.section( u':', 0, 0 );
		const QString szFileB = b.section( u':', 0, 0 );
		return szFileA != szFileB ? szFileA < szFileB : a.section( u':', 1, 1 ).toInt() < b.section( u':', 1, 1 ).toInt();
	} );

	const GeneralPreferences_t old = m_General;
	const bool bCode = old.m_nCodeScheme != general.m_nCodeScheme || old.m_szCodeFontFamily != general.m_szCodeFontFamily || old.m_nCodeFontSize != general.m_nCodeFontSize ||
		old.m_bCodeCompletion != general.m_bCodeCompletion || overrides != m_CodeOverrides;
	const bool bTheme = old.m_nThemeScheme != general.m_nThemeScheme || old.m_ThemeAccent != general.m_ThemeAccent || old.m_szThemeStyle != general.m_szThemeStyle;
	const bool bRules = rules != m_Rules;
	m_General = general;
	m_CodeOverrides = overrides;
	m_Rules = std::move( rules );
	if ( old.m_bDebugLog != general.m_bDebugLog )
	{
		SetDebugLogging( general.m_bDebugLog );
	}
	if ( old.m_bGridAnimatingAll != general.m_bGridAnimatingAll )
	{
		emit GridAnimationChanged( general.m_bGridAnimatingAll );
	}
	if ( old.m_bMaterialVisual != general.m_bMaterialVisual || old.m_bMaterialLive != general.m_bMaterialLive )
	{
		emit MaterialEditingChanged();
	}
	if ( old.m_bTabGroups != general.m_bTabGroups )
	{
		emit TabGroupsChanged( general.m_bTabGroups );
	}
	if ( old.m_bSimpleInterface != general.m_bSimpleInterface )
	{
		emit SimpleInterfaceChanged( general.m_bSimpleInterface );
	}
	if ( bCode )
	{
		emit CodeEditorChanged();
	}
	if ( bTheme )
	{
		emit ThemeChanged();
	}
	if ( bRules )
	{
		emit RulesChanged();
	}
	if ( map != m_Map )
	{
		m_Map = map;
		m_nMapRevision++;
		emit MapPreferencesChanged();
	}
	if ( problems != m_ConfigProblems )
	{
		m_ConfigProblems = problems;
		for ( const QString &szProblem : problems )
		{
			DebugLog() << "config:" << szProblem;
		}
		emit ConfigProblemsChanged();
	}
}

//-----------------------------------------------------------------------------
// Purpose: One setting written where the file has it (or added), then saved.
//-----------------------------------------------------------------------------
template <class Value_t>
void CPreferences::Store( const char *pszKey, const Value_t &value )
{
	if ( m_Config.Set( pszKey, ToText( value ) ) )
	{
		SaveConfig();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Saves what changed, and notes it if the file can't be written.
//-----------------------------------------------------------------------------
void CPreferences::SaveConfig()
{
	std::string szError;
	if ( !m_Config.Save( &szError ) )
	{
		m_ConfigProblems << QStringLiteral( "%1: %2" ).arg( QFileInfo( m_szConfigPath ).fileName(), QString::fromStdString( szError ) );
		emit ConfigProblemsChanged();
		return;
	}
	WatchConfig();
}

//-----------------------------------------------------------------------------
// Purpose: The keys as the file writes them: the preset, and for the user's own keys a bind
//          (or an unbind) for each action whose keys differ from the preset's.
//-----------------------------------------------------------------------------
void CPreferences::StoreKeys()
{
	if ( m_Map.m_nKeyPreset != MapKeyPreset_t::MAP_KEY_PRESET_CUSTOM )
	{
		m_Config.Set( KEYS_PRESET_KEY, KEY_PRESETS[ static_cast<std::size_t>( m_Map.m_nKeyPreset ) ] );
		m_Config.ReplaceKeyword( "bind", IsMapKeyItem, {} );
		m_Config.ReplaceKeyword( "unbind", IsMapKeyItem, {} );
		SaveConfig();
		return;
	}
	MapKeyPreset_t nBase = MapKeyPreset_t::MAP_KEY_PRESET_HAMMER_PLUS_PLUS;
	if ( const auto szPreset = m_Config.Get( KEYS_PRESET_KEY ) )
	{
		nBase = KeyPresetFromText( *szPreset ).value_or( nBase );
	}
	const std::vector<MapKeyBinding_t> base = MapPresetKeys( nBase );
	const std::vector<MapKeyBinding_t> wanted = MapPresetKeys( MapKeyPreset_t::MAP_KEY_PRESET_CUSTOM, m_Map.m_CustomKeys );
	std::vector<std::vector<std::string>> binds;
	std::vector<std::vector<std::string>> unbinds;
	for ( const MapActionInfo_t &info : MapActions() )
	{
		const auto keysOf = [ &info ]( const std::vector<MapKeyBinding_t> &keys ) {
			std::vector<QKeyCombination> out;
			for ( const MapKeyBinding_t &key : keys )
			{
				if ( key.m_nAction == info.m_nAction )
				{
					out.push_back( key.m_Key );
				}
			}
			return out;
		};
		const auto wantedKeys = keysOf( wanted );
		if ( wantedKeys == keysOf( base ) )
		{
			continue;
		}
		if ( wantedKeys.empty() )
		{
			unbinds.push_back( { "map", info.m_pszId } );
		}
		for ( const QKeyCombination key : wantedKeys )
		{
			binds.push_back( { "map", KeyText( key ), info.m_pszId } );
		}
	}
	m_Config.ReplaceKeyword( "unbind", IsMapKeyItem, unbinds );
	m_Config.ReplaceKeyword( "bind", IsMapKeyItem, binds );
	SaveConfig();
}

//-----------------------------------------------------------------------------
// Purpose: The light or dark look, accent and style, onto the application. An accent sets only
//          the highlight roles, so the rest of the desktop's palette stays.
//-----------------------------------------------------------------------------
void CPreferences::ApplyTheme() const
{
	auto *pApp = qobject_cast<QApplication *>( QCoreApplication::instance() );
	if ( !pApp )
	{
		return;
	}
	static const QString s_szDesktopStyle = QApplication::style() ? QApplication::style()->name() : QString{};
	const QString szStyle = m_General.m_szThemeStyle.isEmpty() ? s_szDesktopStyle : m_General.m_szThemeStyle;
	if ( !szStyle.isEmpty() && QApplication::style() && QApplication::style()->name().compare( szStyle, Qt::CaseInsensitive ) != 0 )
	{
		QApplication::setStyle( szStyle );
	}
#if QT_VERSION >= QT_VERSION_CHECK( 6, 8, 0 )
	const Qt::ColorScheme nScheme = m_General.m_nThemeScheme == ThemeScheme_t::THEME_SCHEME_LIGHT ? Qt::ColorScheme::Light
		: m_General.m_nThemeScheme == ThemeScheme_t::THEME_SCHEME_DARK                            ? Qt::ColorScheme::Dark
																								  : Qt::ColorScheme::Unknown;
	QGuiApplication::styleHints()->setColorScheme( nScheme );
#endif
	// The desktop's palette is left alone until an accent is set, so it keeps following the
	// desktop's light and dark switches.
	static bool s_bAccentSet = false;
	if ( !m_General.m_ThemeAccent.isValid() )
	{
		if ( s_bAccentSet )
		{
			QApplication::setPalette( QPalette() );
			s_bAccentSet = false;
		}
		return;
	}
	QPalette palette;
	{
		const QColor accent = m_General.m_ThemeAccent;
		const QColor text = accent.lightnessF() > 0.6 ? Qt::black : Qt::white;
		for ( const QPalette::ColorGroup nGroup : { QPalette::Active, QPalette::Inactive } )
		{
			palette.setColor( nGroup, QPalette::Highlight, accent );
			palette.setColor( nGroup, QPalette::HighlightedText, text );
			palette.setColor( nGroup, QPalette::Accent, accent );
		}
	}
	QApplication::setPalette( palette );
	s_bAccentSet = true;
}

//-----------------------------------------------------------------------------
// Purpose: The first rule with that effect whose pattern matches the path.
//-----------------------------------------------------------------------------
QString CPreferences::RuleFor( const QString &szEffect, const QString &szPath ) const
{
	const QString szNormal = QDir::fromNativeSeparators( szPath );
	const QString szName = QFileInfo( szNormal ).fileName();
	for ( const FileRule_t &rule : m_Rules )
	{
		if ( rule.m_szEffect != szEffect )
		{
			continue;
		}
		const bool bWhole = rule.m_szGlob.contains( u'/' );
		const QRegularExpression pattern( QRegularExpression::wildcardToRegularExpression( rule.m_szGlob, QRegularExpression::NonPathWildcardConversion ), QRegularExpression::CaseInsensitiveOption );
		if ( pattern.match( bWhole ? szNormal : szName ).hasMatch() )
		{
			return rule.m_szArgument;
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: The editors as the file names them.
//-----------------------------------------------------------------------------
const QStringList &CPreferences::EditorNames()
{
	static const QStringList NAMES{ QStringLiteral( "map" ), QStringLiteral( "model" ), QStringLiteral( "texture" ), QStringLiteral( "material" ), QStringLiteral( "particle" ),
		QStringLiteral( "sound" ), QStringLiteral( "sky" ), QStringLiteral( "detail" ), QStringLiteral( "archive" ), QStringLiteral( "stripper" ) };
	return NAMES;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetSimpleInterface( bool bSimple )
{
	if ( bSimple == m_General.m_bSimpleInterface )
	{
		return;
	}
	m_General.m_bSimpleInterface = bSimple;
	Store( "general:simple_interface", bSimple );
	emit SimpleInterfaceChanged( bSimple );
}

//-----------------------------------------------------------------------------
// Purpose: The member a Source tool's path is kept in, and its key in the file.
//-----------------------------------------------------------------------------
QString CPreferences::GetSourceToolPath( SourceTool_t nTool ) const
{
	switch ( nTool )
	{
		case SourceTool_t::SOURCE_TOOL_STUDIOMDL: return m_General.m_szStudiomdl;
		case SourceTool_t::SOURCE_TOOL_BSPZIP: return m_General.m_szBspzip;
		case SourceTool_t::SOURCE_TOOL_VBSP: return m_General.m_szVbsp;
		case SourceTool_t::SOURCE_TOOL_VVIS: return m_General.m_szVvis;
		case SourceTool_t::SOURCE_TOOL_VRAD: return m_General.m_szVrad;
		case SourceTool_t::SOURCE_TOOL_GAME: return m_General.m_szGameExe;
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetSourceToolPath( SourceTool_t nTool, const QString &szPath )
{
	const QString szTrimmed = szPath.trimmed();
	QString *pValue = nullptr;
	const char *pszKey = nullptr;
	switch ( nTool )
	{
		case SourceTool_t::SOURCE_TOOL_STUDIOMDL: pValue = &m_General.m_szStudiomdl, pszKey = "tools:studiomdl"; break;
		case SourceTool_t::SOURCE_TOOL_BSPZIP: pValue = &m_General.m_szBspzip, pszKey = "tools:bspzip"; break;
		case SourceTool_t::SOURCE_TOOL_VBSP: pValue = &m_General.m_szVbsp, pszKey = "tools:vbsp"; break;
		case SourceTool_t::SOURCE_TOOL_VVIS: pValue = &m_General.m_szVvis, pszKey = "tools:vvis"; break;
		case SourceTool_t::SOURCE_TOOL_VRAD: pValue = &m_General.m_szVrad, pszKey = "tools:vrad"; break;
		case SourceTool_t::SOURCE_TOOL_GAME: pValue = &m_General.m_szGameExe, pszKey = "tools:game"; break;
	}
	if ( pValue && *pValue != szTrimmed )
	{
		*pValue = szTrimmed;
		Store( pszKey, szTrimmed );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetBspDir( const QString &szPath )
{
	if ( szPath.trimmed() != m_General.m_szBspDir )
	{
		m_General.m_szBspDir = szPath.trimmed();
		Store( "tools:bsp_dir", m_General.m_szBspDir );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Writes the map preferences that changed (and the keys, when they did).
//-----------------------------------------------------------------------------
void CPreferences::SetMapPreferences( const MapPreferences_t &map )
{
	if ( map == m_Map )
	{
		return;
	}
	const MapPreferences_t old = m_Map;
	m_Map = map;
	m_nMapRevision++;
	bool bChanged = false;
	ForEachMapPreference(
		[ & ]( const char *pszLegacy, const auto &value, const auto &before ) {
			if ( pszLegacy != KEY_PRESET_LEGACY && pszLegacy != CUSTOM_KEYS_LEGACY && !( value == before ) )
			{
				bChanged |= m_Config.Set( MapConfKey( pszLegacy ), ToText( value ) );
			}
		},
		m_Map, old );
	if ( bChanged )
	{
		SaveConfig();
	}
	if ( old.m_nKeyPreset != map.m_nKeyPreset || old.m_CustomKeys != map.m_CustomKeys )
	{
		StoreKeys();
	}
	emit MapPreferencesChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetGridAnimatingAll( bool bAll )
{
	if ( bAll == m_General.m_bGridAnimatingAll )
	{
		return;
	}
	m_General.m_bGridAnimatingAll = bAll;
	Store( "particles:grid_animates_all", bAll );
	emit GridAnimationChanged( bAll );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetGraphicsApi( GraphicsApi_t nApi )
{
	m_General.m_nGraphicsApi = nApi;
	Store( "general:graphics", nApi );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetSingleWindow( bool bSingle )
{
	m_General.m_bSingleWindow = bSingle;
	Store( "general:single_window", bSingle );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetMaterialVisual( bool bVisual )
{
	if ( bVisual == m_General.m_bMaterialVisual )
	{
		return;
	}
	m_General.m_bMaterialVisual = bVisual;
	Store( "material:visual", bVisual );
	emit MaterialEditingChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetMaterialLive( bool bLive )
{
	if ( bLive == m_General.m_bMaterialLive )
	{
		return;
	}
	m_General.m_bMaterialLive = bLive;
	Store( "material:live_preview", bLive );
	emit MaterialEditingChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetManifestReminder( bool bRemind )
{
	m_General.m_bManifestReminder = bRemind;
	Store( "general:manifest_reminder", bRemind );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetReopenSession( bool bReopen )
{
	m_General.m_bReopenSession = bReopen;
	Store( "general:reopen_session", bReopen );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetTabGroups( bool bGroups )
{
	m_General.m_bTabGroups = bGroups;
	Store( "general:tab_groups", bGroups );
	emit TabGroupsChanged( bGroups );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetDesktopFileChooser( bool bDesktop )
{
	m_General.m_bDesktopFileChooser = bDesktop;
	Store( "general:desktop_file_chooser", bDesktop );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetDebugLog( bool bOn )
{
	m_General.m_bDebugLog = bOn;
	Store( "general:debug_log", bOn );
	SetDebugLogging( bOn );
	DebugLog() << "debug log on";
}

//-----------------------------------------------------------------------------
// Purpose: The text editors' colours; an editor's own block comes first.
//-----------------------------------------------------------------------------
CodeScheme_t CPreferences::GetCodeScheme( const QString &szEditor ) const
{
	const auto found = m_CodeOverrides.find( szEditor );
	return found != m_CodeOverrides.end() && found->m_nScheme ? *found->m_nScheme : m_General.m_nCodeScheme;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetCodeScheme( CodeScheme_t nScheme )
{
	if ( nScheme == m_General.m_nCodeScheme )
	{
		return;
	}
	m_General.m_nCodeScheme = nScheme;
	Store( "code:scheme", nScheme );
	emit CodeEditorChanged();
}

//-----------------------------------------------------------------------------
// Purpose: The chosen font, falling back on the system's fixed one for what isn't chosen; an
//          editor's own block comes first.
//-----------------------------------------------------------------------------
QFont CPreferences::GetCodeFont( const QString &szEditor ) const
{
	QString szFamily = m_General.m_szCodeFontFamily;
	int nSize = m_General.m_nCodeFontSize;
	if ( const auto found = m_CodeOverrides.find( szEditor ); found != m_CodeOverrides.end() )
	{
		szFamily = found->m_szFontFamily.value_or( szFamily );
		nSize = found->m_nFontSize.value_or( nSize );
	}
	QFont font = QFontDatabase::systemFont( QFontDatabase::FixedFont );
	if ( !szFamily.isEmpty() )
	{
		font.setFamily( szFamily );
	}
	if ( nSize > 0 )
	{
		font.setPointSize( std::min( nSize, 72 ) );
	}
	font.setStyleHint( QFont::Monospace );
	return font;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetCodeFont( const QString &szFamily, int nSize )
{
	if ( szFamily == m_General.m_szCodeFontFamily && nSize == m_General.m_nCodeFontSize )
	{
		return;
	}
	m_General.m_szCodeFontFamily = szFamily;
	m_General.m_nCodeFontSize = nSize;
	const bool bFamily = m_Config.Set( "code:font", ToText( szFamily ) );
	const bool bSize = m_Config.Set( "code:font_size", ToText( nSize ) );
	if ( bFamily || bSize )
	{
		SaveConfig();
	}
	emit CodeEditorChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CPreferences::IsCodeCompletion( const QString &szEditor ) const
{
	const auto found = m_CodeOverrides.find( szEditor );
	return found != m_CodeOverrides.end() && found->m_bCompletion ? *found->m_bCompletion : m_General.m_bCodeCompletion;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetCodeCompletion( bool bOn )
{
	if ( bOn == m_General.m_bCodeCompletion )
	{
		return;
	}
	m_General.m_bCodeCompletion = bOn;
	Store( "code:completion", bOn );
	emit CodeEditorChanged();
}
