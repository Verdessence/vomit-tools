//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What the map's 2D and 3D views share: Hammer's keys taken before the window's
//          shortcuts, focus following the mouse between views as in Hammer, and the way back to
//          the editor for commands that act on every view.
//
//===========================================================================//
#pragma once

#include <QWidget>

#include "map/Geometry.h"
#include "map/MapKeys.h"

class CMapDocument;
class CMapEditor;

// Which kind each pane shows; any pane can show any of them.
enum class MapViewType_t
{
	MAP_VIEW_TYPE_3D_WIREFRAME,
	MAP_VIEW_TYPE_3D_FLAT,
	MAP_VIEW_TYPE_3D_TEXTURED,
	MAP_VIEW_TYPE_3D_TEXTURED_SHADED,
	MAP_VIEW_TYPE_3D_LIGHTMAP_GRID,
	MAP_VIEW_TYPE_TOP,   // 2D x/y
	MAP_VIEW_TYPE_FRONT, // 2D y/z
	MAP_VIEW_TYPE_SIDE,  // 2D x/z
};

[[nodiscard]] inline bool Is2DView( MapViewType_t nType )
{
	return nType == MapViewType_t::MAP_VIEW_TYPE_TOP || nType == MapViewType_t::MAP_VIEW_TYPE_FRONT || nType == MapViewType_t::MAP_VIEW_TYPE_SIDE;
}

// The tools of Hammer's tool bar that the editor has so far.
enum class MapTool_t
{
	MAP_TOOL_SELECT,
	MAP_TOOL_MAGNIFY,
	MAP_TOOL_CAMERA,
	MAP_TOOL_ENTITY,
	MAP_TOOL_BLOCK,
	MAP_TOOL_TEXTURE, // Hammer's Texture Application: faces picked in 3D, the Face Edit sheet
	MAP_TOOL_CLIP,    // Hammer's clipping tool: a line dragged in 2D, Enter cuts the selection
	MAP_TOOL_CORDON,  // Hammer's Edit Cordon Bounds: the cordon's box dragged like a new block's
	MAP_TOOL_OVERLAY, // Shift+O: place and edit overlays
	MAP_TOOL_DECAL,   // Shift+D: place an infodecal on a picked surface
	MAP_TOOL_VERTEX,  // Hammer's Vertex Tool: the selected solids' corners and edges dragged in 2D
};

// What the vertex tool shows as handles: Hammer's cycle (Shift+V again).
enum class MapVertexMode_t
{
	MAP_VERTEX_MODE_BOTH,
	MAP_VERTEX_MODE_VERTICES,
	MAP_VERTEX_MODE_EDGES,
};

// What the selection's handles do in the 2D views: Hammer's cycle, a click inside the selection
// (or X) going to the next.
enum class MapEditMode_t
{
	MAP_EDIT_MODE_RESIZE,
	MAP_EDIT_MODE_ROTATE,
	MAP_EDIT_MODE_SHEAR,
};

// How the views draw, shared by all of a map's views (Hammer's View menu toggles).
struct MapViewSettings_t
{
	int m_nGridSize = 64;
	bool m_bShowGrid = true;
	bool m_bSnapToGrid = true;    // Shift+W
	bool m_bTextureLock = true;   // textures move with the faces
	bool m_bScaleLock = false;    // and stretch with them (Hammer's Texture Scale Lock)
	bool m_bPivot = true;         // Ctrl+P: turns go about Hammer++'s pivot
	bool m_bModels2D = true;      // Shift+M
	bool m_bNames = true;         // Ctrl+Shift+N
	bool m_bHelpers = true;       // Ctrl+Shift+H
	bool m_bToolTextures = true;  // Ctrl+Shift+F2
	bool m_bSkybox3D = true;      // Alt+V
	bool m_bWalkable = false;     // the selected displacements' steep triangles tinted (Hammer's Display walkable area)
	bool m_bUseGroupColors = true;
	bool m_bDrawVertices = true;
	bool m_bCenterHandles = true;
	bool m_bHideSmallGrid = true;  // grid lines closer than a few pixels left out
	int m_nHighlightEvery = 8;     // every this many lines brighter (GridHighSpec)

	friend bool operator==( const MapViewSettings_t &, const MapViewSettings_t & ) = default;
};

class CMapView : public QWidget
{
	Q_OBJECT

public:
	CMapView( CMapEditor &editor, QWidget *pParent = nullptr );

	[[nodiscard]] virtual MapViewType_t GetType() const = 0;
	// Shows the view as another type of the same kind (another 2D axis, another 3D look).
	virtual void SetType( MapViewType_t nType ) = 0;
	// Commands for this view alone; true if handled. The editor gets the ones it doesn't take.
	virtual bool Run( MapAction_t nAction ) = 0;
	// Brings box into view (Ctrl+E, Ctrl+Shift+E).
	virtual void CenterOn( const xv::map::Box_t &box ) = 0;

protected:
	bool event( QEvent *pEvent ) override;
	void enterEvent( QEnterEvent *pEvent ) override;
	void keyPressEvent( QKeyEvent *pEvent ) override;
	void focusInEvent( QFocusEvent *pEvent ) override;
	void focusOutEvent( QFocusEvent *pEvent ) override;
	[[nodiscard]] MapKeyContext_t GetKeyContext() const { return Is2DView( GetType() ) ? MapKeyContext_t::MAP_KEY_CONTEXT_2D : MapKeyContext_t::MAP_KEY_CONTEXT_3D; }
	[[nodiscard]] CMapDocument &GetDocument() const;

	CMapEditor &m_Editor;
};
