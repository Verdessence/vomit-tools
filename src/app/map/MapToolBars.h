//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The map editor's tool bars: our own icons for its commands, and the bars themselves
//          as Qt's tool bars round the views, which the user moves, hides and fills (Customize:
//          which buttons each bar has, in what order). The layout is saved and is every map
//          tab's.
//
//===========================================================================//
#pragma once

#include <utility>
#include <vector>

#include <QDialog>
#include <QIcon>
#include <QMainWindow>
#include <QStringList>

class QTreeWidget;

enum class MapIcon_t
{
	MAP_ICON_SELECT,
	MAP_ICON_MAGNIFY,
	MAP_ICON_CAMERA,
	MAP_ICON_ENTITY,
	MAP_ICON_BLOCK,
	MAP_ICON_TEXTURE,
	MAP_ICON_DECAL,
	MAP_ICON_OVERLAY,
	MAP_ICON_CLIP,
	MAP_ICON_CORDON,
	MAP_ICON_VERTEX,
	MAP_ICON_SPRINKLE,
	MAP_ICON_PROPERTIES,
	MAP_ICON_MATERIAL,
	MAP_ICON_LIGHT,
	// The bars' commands.
	MAP_ICON_SELECT_GROUPS,
	MAP_ICON_SELECT_OBJECTS,
	MAP_ICON_SELECT_SOLIDS,
	MAP_ICON_GIZMO_MOVE,
	MAP_ICON_GIZMO_ROTATE,
	MAP_ICON_GIZMO_SCALE,
	MAP_ICON_GIZMO_SHEAR,
	MAP_ICON_GIZMO_ALL,
	MAP_ICON_GRID_SMALLER,
	MAP_ICON_GRID_LARGER,
	MAP_ICON_GRID,
	MAP_ICON_SNAP,
	MAP_ICON_TEXTURE_LOCK,
	MAP_ICON_SCALE_LOCK,
	MAP_ICON_PIVOT,
	MAP_ICON_MODELS_2D,
	MAP_ICON_NAMES,
	MAP_ICON_HELPERS,
	MAP_ICON_TOOL_TEXTURES,
	MAP_ICON_NODRAW,
	MAP_ICON_SKYBOX,
	MAP_ICON_WALKABLE,
	MAP_ICON_CENTER,
	MAP_ICON_GO_TO_BRUSH,
	MAP_ICON_AUTOSIZE,
	MAP_ICON_RUN_MAP,
	MAP_ICON_CARVE,
	MAP_ICON_HOLLOW,
	MAP_ICON_GROUP,
	MAP_ICON_UNGROUP,
	MAP_ICON_TIE,
	MAP_ICON_MOVE_TO_WORLD,
	MAP_ICON_HIDE,
	MAP_ICON_HIDE_OTHERS,
	MAP_ICON_UNHIDE,
	MAP_ICON_STRIPPER,
	MAP_ICON_PARTICLES,
	MAP_ICON_PARTICLES_SELECTED,
};

// Our own pictures (Hammer's are Valve's to keep), drawn in color.
[[nodiscard]] QIcon MakeMapIcon( MapIcon_t nIcon, const QColor &color );

// A bar as it comes: its name (Qt's object name, which the saved layout goes by), title, edge
// and its buttons by their actions' object names ("-" for a gap).
struct MapToolBarSpec_t
{
	QString m_szName;
	QString m_szTitle;
	Qt::ToolBarArea m_nArea = Qt::TopToolBarArea;
	QStringList m_Actions;
};

// One bar's buttons as laid out: each action's name, "-" for a gap; hidden ones start with '!'.
using MapToolBarLayout_t = std::vector<std::pair<QString, QStringList>>;

// What the views sit in: a main window of its own inside the tab, so the bars move to any edge
// and float. Its context menu shows and hides bars, picks icons or words, customizes and resets.
class CMapToolHost : public QMainWindow
{
	Q_OBJECT

public:
	// actions are every command a bar can hold (named), specs the bars as they first come.
	CMapToolHost( std::vector<MapToolBarSpec_t> specs, QList<QAction *> actions, QWidget *pParent = nullptr );
	~CMapToolHost() override;

	[[nodiscard]] QMenu *createPopupMenu() override;
	[[nodiscard]] QToolBar *GetBar( const QString &szName ) const;
	// The bars' buttons as they are now.
	[[nodiscard]] MapToolBarLayout_t GetLayout() const;
	// Fills the bars (an action no bar names goes back to the one it came in, so new commands
	// show up), and saves it as every map tab's when bSave.
	void SetLayout( const MapToolBarLayout_t &layout, bool bSave );
	void SetButtonStyle( Qt::ToolButtonStyle nStyle, bool bSave );
	// The bars as they first came, where they first were.
	void ResetLayout();
	// Where the bars are (edges, order, floating, shown), kept for the next map tab.
	void SaveState() const;
	[[nodiscard]] const std::vector<MapToolBarSpec_t> &GetSpecs() const { return m_Specs; }
	[[nodiscard]] QAction *FindAction( const QString &szName ) const;
	// Customize: opens the dialog; the layout it ends with is every tab's.
	void Customize();

private:
	[[nodiscard]] MapToolBarLayout_t DefaultLayout() const;
	[[nodiscard]] static MapToolBarLayout_t SavedLayout();
	// Every open map tab's host.
	static QList<CMapToolHost *> &Hosts();

	std::vector<MapToolBarSpec_t> m_Specs;
	QList<QAction *> m_Actions;
	std::vector<QToolBar *> m_Bars;
	MapToolBarLayout_t m_Layout;
	QByteArray m_StartState; // where the bars were when the tab opened (or was reset)
};

// Customize Tool Bars: the bars and their buttons in a tree, dragged into order and between
// bars, ticked to show; Gap adds a gap after the picked button.
class CMapToolBarDialog : public QDialog
{
	Q_OBJECT

public:
	CMapToolBarDialog( const CMapToolHost &host, QWidget *pParent = nullptr );
	[[nodiscard]] MapToolBarLayout_t GetLayout() const;
	[[nodiscard]] QTreeWidget *GetTree() const { return m_pTree; }

private:
	void Fill( const MapToolBarLayout_t &layout );

	const CMapToolHost &m_Host;
	QTreeWidget *m_pTree = nullptr;
};
