//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The settings Edit > Preferences changes, remembered between runs. The simple interface
//          is CInterfaceMode's; the rest live here.
//
//===========================================================================//
#pragma once

#include <cstdint>

#include <QColor>
#include <QObject>
#include <QStringList>

#include "Graphics.h"
#include "map/MapConstants.h"

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

// How many samples an anisotropic filter takes at most; 1 for the others.
[[nodiscard]] int MapAnisotropy( MapTextureFilter_t nFilter );

// The colours the map views draw with (Preferences > Maps > Colours); Hammer++'s by default.
struct MapColors_t
{
	QColor m_Background = mapstyle::BACKGROUND;
	QColor m_Grid = mapstyle::GRID;
	QColor m_GridHighlight = mapstyle::GRID_10;
	QColor m_Grid1024 = mapstyle::GRID_1024;
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
	bool m_bSkybox3D = true;
	bool m_bSprites = true;          // iconsprites and env_sprites drawn as such (else as boxes)
	bool m_bWireframeModels = true;  // props' edges in the wireframe view
	MapSelectedModels_t m_nSelectedModels = MapSelectedModels_t::MAP_SELECTED_MODELS_PROPS;

	// Editing.
	bool m_bArrowsNudge = true;      // arrow keys move the selection (Hammer++'s Nudge)
	bool m_bTextureLock = true;      // what a map opens with (Hammer++'s LockTextures)
	bool m_bScaleLock = false;       // and its ScaleLockTextures
	double m_flRotationSnap = mapstyle::ROTATION_SNAP; // degrees a handle's turn snaps to

	// The 2D views.
	bool m_bHideSmallGrid = true;
	double m_flHideGridBelow = mapstyle::HIDE_GRID_BELOW_PIXELS;
	int m_nHighlightEvery = mapstyle::GRID_HIGHLIGHT_EVERY;
	bool m_bDrawVertices = true;
	int m_nVertexSize = mapstyle::VERTEX_SIZE;
	int m_nVerticesFromPixels = mapstyle::VERTICES_FROM_PIXELS;
	bool m_bCenterHandles = true;
	int m_nHandleSize = mapstyle::HANDLE_SIZE;
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

	// Whether every system in the particle editor's grid plays, rather than only the selected one.
	[[nodiscard]] bool IsGridAnimatingAll() const { return m_bGridAnimatingAll; }
	void SetGridAnimatingAll( bool bAll );

	// What the previews draw with. CGraphics reads it as the app starts, so a change takes effect
	// the next time.
	[[nodiscard]] GraphicsApi_t GetGraphicsApi() const { return m_nGraphicsApi; }
	void SetGraphicsApi( GraphicsApi_t nApi );

	// Whether files opened from outside (a file manager, the command line) join the window
	// already open rather than starting another.
	[[nodiscard]] bool IsSingleWindow() const { return m_bSingleWindow; }
	void SetSingleWindow( bool bSingle );

	// Whether saving a file the game's manifest doesn't list offers to add it.
	[[nodiscard]] bool IsManifestReminder() const { return m_bManifestReminder; }
	void SetManifestReminder( bool bRemind );

	// Whether the files open at closing open again at the next start.
	[[nodiscard]] bool IsReopenSession() const { return m_bReopenSession; }
	void SetReopenSession( bool bReopen );

	// Whether open and save windows are the desktop's own file chooser (the XDG portal's, on
	// Linux) rather than Qt's.
	[[nodiscard]] bool IsDesktopFileChooser() const { return m_bDesktopFileChooser; }
	void SetDesktopFileChooser( bool bDesktop );

	// Whether the debug log prints what goes on in the background (Advanced).
	[[nodiscard]] bool IsDebugLog() const { return m_bDebugLog; }
	void SetDebugLog( bool bOn );

	// The map viewer's: open maps take a change at once (their own toggles start over).
	[[nodiscard]] const MapPreferences_t &GetMapPreferences() const { return m_Map; }
	void SetMapPreferences( const MapPreferences_t &map );
	// Bumped at every change to the map preferences, for views that cache what they draw.
	[[nodiscard]] std::uint64_t GetMapRevision() const { return m_nMapRevision; }

	// Whether materials are edited as a list of parameters (else as their text).
	[[nodiscard]] bool IsMaterialVisual() const { return m_bMaterialVisual; }
	void SetMaterialVisual( bool bVisual );
	// Whether the material preview redraws at each edit made in the list (the text's always does).
	[[nodiscard]] bool IsMaterialLive() const { return m_bMaterialLive; }
	void SetMaterialLive( bool bLive );

signals:
	void GridAnimationChanged( bool bAll );
	void MaterialEditingChanged();
	void MapPreferencesChanged();

private:
	CPreferences();

	bool m_bGridAnimatingAll = false;
	GraphicsApi_t m_nGraphicsApi = GraphicsApi_t::GRAPHICS_API_AUTO;
	bool m_bSingleWindow = true;
	bool m_bMaterialVisual = true;
	bool m_bMaterialLive = true;
	bool m_bManifestReminder = true;
	bool m_bReopenSession = true;
	bool m_bDesktopFileChooser = true;
	bool m_bDebugLog = false;
	MapPreferences_t m_Map;
	std::uint64_t m_nMapRevision = 0;
};
