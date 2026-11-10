//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The settings Edit > Preferences changes, kept in vomit-tools.nut (Squirrel tables read
//          as data, src/core/conf): written back as they change without touching the rest of
//          the file, and read again whenever the file (or one it includes) changes on disk. The
//          file also holds the map keys (binds / unbinds), file and tab rules (rules),
//          per-editor overrides (editor <- { sound = { ... } }) and the theme.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <QColor>
#include <QFont>
#include <QObject>
#include <QHash>
#include <QStringList>

class QFileSystemWatcher;
class QTimer;

#include "Graphics.h"
#include "SourceTools.h"
#include "conf/Conf.h"
#include "map/MapConstants.h"
#include "map/MapKeys.h"

// The text editors' colours (a config's or material's text): the desktop's, or a scheme of
// their own whatever the desktop's colours are.
enum class CodeScheme_t
{
	CODE_SCHEME_SYSTEM,
	CODE_SCHEME_GRUVBOX_DARK,
	CODE_SCHEME_GRUVBOX_DARK_HARD,
	CODE_SCHEME_GRUVBOX_LIGHT,
	CODE_SCHEME_COUNT
};

// The application's light or dark look: the desktop's, or one forced.
enum class ThemeScheme_t
{
	THEME_SCHEME_SYSTEM,
	THEME_SCHEME_LIGHT,
	THEME_SCHEME_DARK,
};

// Settings that aren't the map viewer's. The defaults are what a new install starts with.
struct GeneralPreferences_t
{
	GraphicsApi_t m_nGraphicsApi = GraphicsApi_t::GRAPHICS_API_AUTO;
	bool m_bSingleWindow = true;
	bool m_bReopenSession = true;
	bool m_bTabGroups = true;
	bool m_bManifestReminder = true;
	bool m_bDesktopFileChooser = true;
	bool m_bDebugLog = false;
	bool m_bSimpleInterface = false;
	ThemeScheme_t m_nThemeScheme = ThemeScheme_t::THEME_SCHEME_SYSTEM;
	QColor m_ThemeAccent;  // invalid: the desktop's
	QString m_szThemeStyle; // a Qt style ("Fusion"); empty: the desktop's
	CodeScheme_t m_nCodeScheme = CodeScheme_t::CODE_SCHEME_SYSTEM;
	QString m_szCodeFontFamily; // empty: the system's fixed font
	int m_nCodeFontSize = 0;    // points; 0: the system's
	bool m_bCodeCompletion = true;
	bool m_bMaterialVisual = true;
	bool m_bMaterialLive = true;
	bool m_bGridAnimatingAll = false;
	// Source tools (empty: found beside the game and in PATH) and Hammer's $bspdir.
	QString m_szStudiomdl;
	QString m_szBspzip;
	QString m_szVbsp;
	QString m_szVvis;
	QString m_szVrad;
	QString m_szGameExe;
	QString m_szBspDir;

	friend bool operator==( const GeneralPreferences_t &, const GeneralPreferences_t & ) = default;
};

// What an editor's own table can change (editor <- { sound = { code = { font_size = 12 } } }); unset parts
// follow the general setting.
struct CodeOverrides_t
{
	std::optional<CodeScheme_t> m_nScheme;
	std::optional<QString> m_szFontFamily;
	std::optional<int> m_nFontSize;
	std::optional<bool> m_bCompletion;

	friend bool operator==( const CodeOverrides_t &, const CodeOverrides_t & ) = default;
};

// A rule, [ "<effect>:<argument>", glob ]: a file whose path matches gets the effect. A glob with a '/' is
// matched against the whole path ('*' crossing folders), one without against the file name.
struct FileRule_t
{
	QString m_szEffect;   // "group", "editor"
	QString m_szArgument; // the group's or editor's name
	QString m_szGlob;

	friend bool operator==( const FileRule_t &, const FileRule_t & ) = default;
};

// How 2D views colour grouped objects (Hammer++ gives each group a colour of its own).
enum class MapGroupColors_t
{
	MAP_GROUP_COLORS_RANDOM, // a colour picked for each group, the same every time
	MAP_GROUP_COLORS_SAVED,  // the colour the map's file gives the group
	MAP_GROUP_COLORS_NONE,
};

// How textures are filtered in the 3D views (Hammer++'s Options: bilinear, trilinear, or
// anisotropic at 2 to 16 times).
enum class MapTextureFilter_t
{
	MAP_TEXTURE_FILTER_BILINEAR,
	MAP_TEXTURE_FILTER_TRILINEAR,
	MAP_TEXTURE_FILTER_ANISOTROPIC_2X,
	MAP_TEXTURE_FILTER_ANISOTROPIC_4X,
	MAP_TEXTURE_FILTER_ANISOTROPIC_8X,
	MAP_TEXTURE_FILTER_ANISOTROPIC_16X,
};

// What a selected model shows over everything in the 3D views, and in the 2D views instead of its
// picture: its edges, or its box. Hammer++ shows every model's edges.
enum class MapSelectedModels_t
{
	MAP_SELECTED_MODELS_PROPS, // the edges of props (prop_ entities), the box of other models
	MAP_SELECTED_MODELS_EDGES,
	MAP_SELECTED_MODELS_BOX,
};

// How Carve cuts the brushes it meets (map/Clip.h's CarveStyle_t, kept as a number).
enum class MapCarve_t
{
	MAP_CARVE_AUTO,      // slabs for a block, a fan for anything else
	MAP_CARVE_HAMMER,    // by the carver's faces in turn, as Hammer does
	MAP_CARVE_BOX_FIRST, // by the carver's box first: no cut runs past it
	MAP_CARVE_FAN,       // pieces fanning out from the carver's centre (arch segments round a cylinder)
};

// How many samples an anisotropic filter takes at most; 1 for the others.
[[nodiscard]] int MapAnisotropy( MapTextureFilter_t nFilter );

// The colours the map views draw with (Preferences > Maps > Colours); Hammer++'s by default.
struct MapColors_t
{
	QColor m_Background = mapstyle::BACKGROUND;
	QColor m_Grid = mapstyle::GRID;
	QColor m_GridHighlight = mapstyle::GRID_10;
	QColor m_Grid1024 = mapstyle::GRID_1024;
	QColor m_GridBeyond = mapstyle::GRID_BEYOND;  // the grid past the map's size
	QColor m_Axis = mapstyle::AXIS;
	QColor m_Brush = mapstyle::BRUSH;
	QColor m_Entity = mapstyle::ENTITY;            // entities whose game data gives no colour
	QColor m_Invalid = mapstyle::INVALID;          // broken solids, in 2D and 3D
	QColor m_Vertex = mapstyle::VERTEX;
	QColor m_CenterHandle = mapstyle::CENTER_HANDLE;
	QColor m_Selection = mapstyle::SELECTION;      // 2D
	QColor m_SelectionFill = mapstyle::FILL_SELECTION;
	QColor m_ToolSelection = mapstyle::TOOL_SELECTION;
	QColor m_ToolHandle = mapstyle::TOOL_HANDLE;
	QColor m_ToolBlock = mapstyle::TOOL_BLOCK;
	QColor m_EntityMarker = mapstyle::ENTITY_MARKER;
	QColor m_Pivot = mapstyle::PIVOT;
	QColor m_Pointfile = mapstyle::POINTFILE;
	QColor m_Cordon = mapstyle::CORDON;
	QColor m_Camera = mapstyle::CAMERA;
	QColor m_Instance2D = mapstyle::INSTANCE_TINT; // instances' contents in 2D, when chosen
	QColor m_Background3D = mapstyle::BACKGROUND_3D;
	QColor m_Selection3D = mapstyle::SELECTION_3D;
	QColor m_SelectionEdge3D = mapstyle::SELECTION_EDGE_3D;
	QColor m_Instance = mapstyle::INSTANCE_TINT;   // the 3D views' tint of instances' contents

	friend bool operator==( const MapColors_t &, const MapColors_t & ) = default;
};

// The map viewer's settings (Preferences > Maps). The defaults are Hammer++'s own.
struct MapPreferences_t
{
	QStringList m_Fgds; // game data, in order; empty: the game's Hammer configuration's
	int m_nTextureSize = 1024; // a texture's largest side, as read

	// The 3D views.
	double m_flFov = mapstyle::FOV; // horizontal, in degrees
	double m_flSensitivity = mapstyle::MOUSE_SENSITIVITY; // mouse look
	double m_flForwardSpeed = mapstyle::MAX_FORWARD_SPEED;
	double m_flRenderDistance = mapstyle::FAR_PLANE;
	double m_flWheelStep = mapstyle::WHEEL_STEP; // units a wheel notch moves the camera
	double m_flPanScale = mapstyle::PAN_SCALE;   // units a pixel of right-drag moves it
	double m_flFarStep = mapstyle::FAR_PLANE_STEP; // keys 1 and 2
	MapTextureFilter_t m_nTextureFilter = MapTextureFilter_t::MAP_TEXTURE_FILTER_ANISOTROPIC_4X;
	double m_flSelectionTint = mapstyle::SELECTION_TINT; // how far selected faces go toward the selection colour
	double m_flInvalidTint = mapstyle::INVALID_TINT;     // and broken solids' toward the invalid one
	double m_flInstanceTint = mapstyle::INSTANCE_TINT_AMOUNT;
	double m_flLightmapGridShade = mapstyle::LIGHTMAP_GRID_SHADE; // 1: no checkerboard
	double m_flLightBrightness = 1.0;  // the lighting preview: light times this before it is shown
	int m_nLightBounces = 3;           // passes of light bounced between faces
	int m_nLightLuxels = 4;            // millions of luxels at most, else every other one (and so on)
	bool m_bLightHdr = true;           // as the game with HDR: the lights' HDR values, no overbright, auto exposure
	bool m_bFog = true;                // env_fog_controller's fog in the textured 3D views, as the game draws it (the Fog toggle's start)
	bool m_bLightOnly = false;         // the light alone, textures a plain grey (mat_fullbright 2)
	bool m_bLightPropShadows = true;   // prop_static's models cast shadows, as vrad's do
	bool m_bLightSmoothShadows = true; // shadow edges smoothed across their luxels, as vrad's -final
	bool m_bLightAutoBake = true;      // the preview bakes again by itself after edits (else on Bake Lighting, as Hammer++)
	bool m_bLightInRadius = false;     // bake only around the 3D camera (Hammer++'s Bake in Radius)
	double m_flLightRadius = 1024.0;   // and how far
	int m_nLightDelay = 250;           // milliseconds after an edit before baking (Hammer++'s update delay)
	int m_nLightSkySamples = 48;       // rays to the sky a sample, for its ambient (Hammer++'s visibility samples)
	double m_flLightMonitorGamma = 2.2; // the lighting preview's mat_monitorgamma (the game's brightness: 1.6 brightest, 2.6 darkest)
	bool m_bSkybox3D = true;
	bool m_bSky2D = true;            // the map's 2D sky behind everything in the textured 3D views
	bool m_bSprites = true;          // iconsprites and env_sprites drawn as such (else as boxes)
	bool m_bWireframeModels = true;  // props' edges in the wireframe view
	MapSelectedModels_t m_nSelectedModels = MapSelectedModels_t::MAP_SELECTED_MODELS_PROPS;
	double m_flGizmoPixels = mapstyle::GIZMO_PIXELS; // the gizmo's axes, on screen
	double m_flGizmoThickness = mapstyle::GIZMO_THICKNESS;
	int m_nGizmoAlpha = mapstyle::GIZMO_ALPHA;
	bool m_bGizmoFollowsHandles = true;
	bool m_bGizmoAll = false;

	// Editing.
	bool m_bArrowsNudge = true;      // arrow keys move the selection (Hammer++'s Nudge)
	bool m_bTextureLock = true;      // what a map opens with (Hammer++'s LockTextures)
	bool m_bScaleLock = false;       // and its ScaleLockTextures
	double m_flRotationSnap = mapstyle::ROTATION_SNAP; // degrees a handle's turn snaps to
	bool m_bPivot = true;            // what a map opens with (Hammer++'s Pivot): turns go about a point that can be moved
	MapCarve_t m_nCarve = MapCarve_t::MAP_CARVE_AUTO;
	bool m_bGroupCarve = true;       // a carved brush's pieces grouped (Hammer++'s GroupCarve)
	bool m_bShowDisplacementBrush = false; // show its ring in Select while the Displacement tab is open
	bool m_bFaceEditWindow = true;   // Face Edit in a window of its own, as Hammer's (else in the side panel)
	bool m_bOnePanel = false;        // the side shows the panel whose tab was picked alone, as Blender's tabs do
	bool m_bAutosave = true;         // a modified map is written to <map>.vmf.autosave now and then
	int m_nAutosaveMinutes = 5;      // how often
	// The keys (Preferences > Keys): a preset, or the user's own (map/MapKeys.h's settings list).
	MapKeyPreset_t m_nKeyPreset = MapKeyPreset_t::MAP_KEY_PRESET_HAMMER_PLUS_PLUS;
	QStringList m_CustomKeys;

	// The 2D views.
	bool m_bHideSmallGrid = true;
	bool m_bGridBeyond = false;      // the grid also past the map's size, in a colour of its own
	double m_flHideGridBelow = mapstyle::HIDE_GRID_BELOW_PIXELS;
	int m_nHighlightEvery = mapstyle::GRID_HIGHLIGHT_EVERY;
	bool m_bDrawVertices = false;
	bool m_bBrushKeepConcave = false;
	int m_nVertexSize = mapstyle::VERTEX_SIZE;
	int m_nVerticesFromPixels = mapstyle::VERTICES_FROM_PIXELS;
	bool m_bCenterHandles = true;
	int m_nHandleSize = mapstyle::HANDLE_SIZE;
	int m_nHandleReach = mapstyle::HANDLE_REACH;
	int m_nCenterHandlesFromPixels = mapstyle::CENTER_HANDLES_FROM_PIXELS;
	int m_nNamesFromPixels = mapstyle::NAMES_FROM_PIXELS;
	bool m_bModels2D = true;
	bool m_bTexturedModels2D = true; // models as their textures, else as their edges
	int m_nModelsFromPixels = mapstyle::MODELS_FROM_PIXELS;
	bool m_bGroupColors = true;      // objects in their visgroup's (and group's) colour
	MapGroupColors_t m_nGroupColors = MapGroupColors_t::MAP_GROUP_COLORS_RANDOM;
	bool m_bInstanceColor2D = false; // instances' contents in a colour of their own, not the func_instance's
	int m_nCameraPixels = mapstyle::CAMERA_PIXELS;
	double m_flWheelZoom = mapstyle::WHEEL_ZOOM_STEP;
	double m_flKeyZoom = mapstyle::KEY_ZOOM_STEP;
	int m_nScrollStep = mapstyle::SCROLL_STEP_PIXELS;
	double m_flZoomSmallest = mapstyle::ZOOM_SMALLEST;
	double m_flZoomLargest = mapstyle::ZOOM_LARGEST;

	MapColors_t m_Colors;

	friend bool operator==( const MapPreferences_t &, const MapPreferences_t & ) = default;
};

class CPreferences : public QObject
{
	Q_OBJECT

public:
	[[nodiscard]] static CPreferences &Instance();

	// vomit-tools.nut: beside Qt's own settings file (VOMIT_TOOLS_CONFIG names another).
	[[nodiscard]] QString GetConfigPath() const;
	// Problems with the file as last read: "file:line: message".
	[[nodiscard]] const QStringList &GetConfigProblems() const { return m_ConfigProblems; }
	// Reads the file (and what it sources) again, as a change on disk does.
	void ReloadConfig();
	[[nodiscard]] const GeneralPreferences_t &GetGeneral() const { return m_General; }

	// The light or dark look, accent colour and Qt style, put onto the application.
	void ApplyTheme() const;

	// The rule for a file, if any: the group or editor a "rule" line gives its path.
	[[nodiscard]] QString RuleFor( const QString &szEffect, const QString &szPath ) const;
	// The editors "editor:<name>" blocks and editor rules can name.
	[[nodiscard]] static const QStringList &EditorNames();

	// The simple interface (CInterfaceMode shows it; it is kept here).
	[[nodiscard]] bool IsSimpleInterface() const { return m_General.m_bSimpleInterface; }
	void SetSimpleInterface( bool bSimple );

	// A Source tool's path as set (empty: found automatically), and Hammer's $bspdir.
	[[nodiscard]] QString GetSourceToolPath( SourceTool_t nTool ) const;
	void SetSourceToolPath( SourceTool_t nTool, const QString &szPath );
	[[nodiscard]] QString GetBspDir() const { return m_General.m_szBspDir; }
	void SetBspDir( const QString &szPath );

	// Whether every system in the particle editor's grid plays, rather than only the selected one.
	[[nodiscard]] bool IsGridAnimatingAll() const { return m_General.m_bGridAnimatingAll; }
	void SetGridAnimatingAll( bool bAll );

	// What the previews draw with. CGraphics reads it as the app starts, so a change takes effect
	// the next time.
	[[nodiscard]] GraphicsApi_t GetGraphicsApi() const { return m_General.m_nGraphicsApi; }
	void SetGraphicsApi( GraphicsApi_t nApi );

	// Whether files opened from outside (a file manager, the command line) join the window
	// already open rather than starting another.
	[[nodiscard]] bool IsSingleWindow() const { return m_General.m_bSingleWindow; }
	void SetSingleWindow( bool bSingle );

	// Whether saving a file the game's manifest doesn't list offers to add it.
	[[nodiscard]] bool IsManifestReminder() const { return m_General.m_bManifestReminder; }
	void SetManifestReminder( bool bRemind );

	// Whether the files open at closing open again at the next start.
	[[nodiscard]] bool IsReopenSession() const { return m_General.m_bReopenSession; }
	void SetReopenSession( bool bReopen );

	// Whether the main window's tabs are grouped (by kind, or the user's groups) under a strip.
	[[nodiscard]] bool IsTabGroups() const { return m_General.m_bTabGroups; }
	void SetTabGroups( bool bGroups );

	// Whether open and save windows are the desktop's own file chooser (the XDG portal's, on
	// Linux) rather than Qt's.
	[[nodiscard]] bool IsDesktopFileChooser() const { return m_General.m_bDesktopFileChooser; }
	void SetDesktopFileChooser( bool bDesktop );

	// Whether the debug log prints what goes on in the background (Advanced).
	[[nodiscard]] bool IsDebugLog() const { return m_General.m_bDebugLog; }
	void SetDebugLog( bool bOn );

	// The map viewer's: open maps take a change at once (their own toggles start over).
	[[nodiscard]] const MapPreferences_t &GetMapPreferences() const { return m_Map; }
	void SetMapPreferences( const MapPreferences_t &map );
	// Bumped at every change to the map preferences, for views that cache what they draw.
	[[nodiscard]] std::uint64_t GetMapRevision() const { return m_nMapRevision; }

	// Whether materials are edited as a list of parameters (else as their text).
	[[nodiscard]] bool IsMaterialVisual() const { return m_General.m_bMaterialVisual; }
	void SetMaterialVisual( bool bVisual );
	// Whether the material preview redraws at each edit made in the list (the text's always does).
	[[nodiscard]] bool IsMaterialLive() const { return m_General.m_bMaterialLive; }
	void SetMaterialLive( bool bLive );

	// The text editors': their colours, their font (the system's fixed one when its family is
	// empty) and whether they suggest words as they're typed. With an editor's name (as
	// EditorNames()), its own block's overrides come first.
	[[nodiscard]] CodeScheme_t GetCodeScheme( const QString &szEditor = {} ) const;
	void SetCodeScheme( CodeScheme_t nScheme );
	[[nodiscard]] QFont GetCodeFont( const QString &szEditor = {} ) const;
	[[nodiscard]] const QString &GetCodeFontFamily() const { return m_General.m_szCodeFontFamily; }
	[[nodiscard]] int GetCodeFontSize() const { return m_General.m_nCodeFontSize; } // points; 0: the system's
	void SetCodeFont( const QString &szFamily, int nSize );
	[[nodiscard]] bool IsCodeCompletion( const QString &szEditor = {} ) const;
	void SetCodeCompletion( bool bOn );

	// Says that the Discord settings changed.
	void NotifyDiscordChanged() { emit DiscordChanged(); }

signals:
	void GridAnimationChanged( bool bAll );
	void MaterialEditingChanged();
	void MapPreferencesChanged();
	void TabGroupsChanged( bool bGroups );
	// The Discord settings changed (they live in QSettings under discord/).
	void DiscordChanged();
	void CodeEditorChanged();
	void ThemeChanged();
	void SimpleInterfaceChanged( bool bSimple );
	void RulesChanged();
	void ConfigProblemsChanged();

private:
	CPreferences();

	// Reads the settings out of the config as it is, noting what can't be read.
	void ApplyConfig();
	// Writes one setting (by its key in the file) and saves.
	template <class Value_t>
	void Store( const char *pszKey, const Value_t &value );
	void StoreKeys();
	void SaveConfig();
	void WatchConfig();
	// The first run: the old settings (QSettings) written out as the file.
	void Migrate();

	xv::conf::CConfig m_Config;
	QString m_szConfigPath;
	QStringList m_ConfigProblems;
	QFileSystemWatcher *m_pWatcher = nullptr;
	QTimer *m_pReloadTimer = nullptr;
	GeneralPreferences_t m_General;
	QHash<QString, CodeOverrides_t> m_CodeOverrides; // by editor name
	std::vector<FileRule_t> m_Rules;
	MapPreferences_t m_Map;
	std::uint64_t m_nMapRevision = 0;
};
