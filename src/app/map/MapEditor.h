//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The map editor's tab, laid out as Hammer is: the tool bar down the left, the four
//          view panes, the object panels on the right and Hammer's status bar along the
//          bottom.
//
//===========================================================================//
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <QDialog>
#include <QPointer>
#include <QWidget>

#include "EditorTab.h"
#include "map/Clip.h"
#include "map/DispEdit.h"
#include "map/Faces.h"
#include "map/Vertex.h"
#include "map/MapDocument.h"
#include "map/MapGizmo.h"
#include "map/MapRenderData.h"
#include "map/MapDialogs.h"
#include "map/MapView.h"

class CMapCheckDialog;
class CMapEntityReport;
class CMapFaceEdit;
class CMapFilterControl;
class CMapPane;
class CMapProperties;
class CMapResources;
class CMapQuadSplitter;
class QAction;
class QActionGroup;
class QComboBox;
class QSpinBox;
class QLabel;
class QStackedWidget;
class QLineEdit;
class QSplitter;
class QToolBar;
class QVBoxLayout;

class CMapEditor : public QWidget, public IEditorTab
{
	Q_OBJECT

public:
	// szPath is the file on disk, or its path in the game when bFromGame; empty for a new map.
	CMapEditor( const QString &szPath, bool bFromGame, QWidget *pParent = nullptr );
	~CMapEditor() override;

	void Load( CMapDocument::Source_t source );
	[[nodiscard]] CMapDocument &GetDocument() { return *m_pDocument; }
	[[nodiscard]] const CMapDocument &GetDocument() const { return *m_pDocument; }
	[[nodiscard]] const QString &GetPath() const { return m_szPath; }
	[[nodiscard]] const MapViewSettings_t &GetSettings() const { return m_Settings; }
	[[nodiscard]] MapTool_t GetTool() const { return m_nTool; }
	void SetTool( MapTool_t nTool );
	[[nodiscard]] MapEditMode_t GetEditMode() const { return m_nEditMode; }
	void SetEditMode( MapEditMode_t nMode );
	// Hammer++'s gizmo in the 3D views: shown (X), what it does (G, R, E) and whose axes (J).
	[[nodiscard]] bool IsGizmoShown() const { return m_bGizmo; }
	void SetGizmoShown( bool bShown );
	[[nodiscard]] MapGizmoMode_t GetGizmoMode() const { return m_nGizmoMode; }
	void SetGizmoMode( MapGizmoMode_t nMode );
	[[nodiscard]] bool IsGizmoLocal() const { return m_bGizmoLocal; }
	void SetGizmoLocal( bool bLocal );
	[[nodiscard]] CMapPane *GetPane( int nIndex ) const;
	[[nodiscard]] std::vector<CMapView *> GetViews() const;
	[[nodiscard]] CMapView *GetActiveView() const { return m_pActiveView; }
	[[nodiscard]] CMapRenderData &GetRenderData() { return *m_pRenderData; }
	[[nodiscard]] CMapResources &GetResources() { return *m_pResources; }
	[[nodiscard]] CMapFilterControl &GetFilterControl() { return *m_pFilter; }
	[[nodiscard]] CMapProperties &GetProperties() { return *m_pProperties; }
	// Where the 3D camera is, for the 2D views to draw it (the first 3D pane's).
	[[nodiscard]] std::optional<xv::map::CViewCamera> GetCamera() const;
	// Where the camera was when this map was last closed, unless the file was saved since
	// (Hammer then wrote its own camera into it); the 3D views start there.
	[[nodiscard]] const std::optional<xv::map::CViewCamera> &GetRememberedCamera() const { return m_RememberedCamera; }

	// What the views report and ask for.
	bool RunAction( MapAction_t nAction, CMapView *pFrom );
	void SetActiveView( CMapView *pView );
	void ViewTypeChanged( CMapView *pView );
	void ShowCursor( const QString &szAt );
	void ShowZoom( double flZoom );
	void CameraMoved( CMapView *pView );
	// The selection moved by offset (units), or copied there (bClone); one step to undo.
	void MoveSelection( xv::map::Point_t offset, bool bClone );
	// The selection transformed (turned, stretched, sheared), with the locks; one step to undo,
	// named szName in Edit > Undo.
	void TransformSelection( const xv::map::Transform_t &transform, const QString &szName );
	// Hammer++'s pivot (Ctrl+P): the point the selection turns about, its centre until dragged
	// somewhere in a 2D view; it stays where it was put as the selection moves (Verd: Hammer++'s
	// is independent of the brushes), and goes back to the centre when something else is
	// selected. None while it is off or nothing is selected.
	[[nodiscard]] std::optional<xv::map::Point_t> GetPivot() const;
	void SetPivot( const xv::map::Point_t &at );

	// The block tool's box, dragged in a 2D view and shown in every view; Enter makes it a block.
	[[nodiscard]] const std::optional<xv::map::Box_t> &GetNewBox() const { return m_NewBox; }
	void SetNewBox( std::optional<xv::map::Box_t> box );
	// How far a new box reaches along an axis its 2D view doesn't show: as far as the box there
	// already, else the selection, else the last block made.
	[[nodiscard]] std::pair<double, double> GetNewBoxDepth( int nAxis ) const;
	// The box made a block with the current texture, and selected.
	void CreateBlock();
	// The entity tool's marker, placed in a 2D view; Enter puts an entity there.
	[[nodiscard]] const std::optional<xv::map::Point_t> &GetEntityMarker() const { return m_EntityMarker; }
	void SetEntityMarker( std::optional<xv::map::Point_t> at );
	// A new entity of the entity tool's class at origin, selected.
	void PlaceDecal( const xv::map::Point_t &origin );
	void PlaceEntity( const xv::map::Point_t &origin );
	// The same, standing on a surface (a click in 3D): its box touches the surface at point, on
	// the side normal faces.
	void PlaceEntityOn( const xv::map::Point_t &point, const xv::map::Point_t &normal );
	// What new blocks' faces get, and the class the entity tool places.
	// Map > Map Properties: the world's keys (the sky) in a window of their own.
	void ShowMapProperties();
	// Map > Run Map (F9): Hammer's compile window, one per map.
	void RunMap();
	// Map > Load Pointfile / Unload / Go to Leak: a leak's line (the .lin beside the map).
	void LoadPointfile();
	void UnloadPointfile();
	void GoToLeak();
	// The clipping tool's line: two points in a 2D view's plane (the axis the view doesn't show
	// is nAxis); Hammer's three modes; Enter cuts the selection.
	struct ClipLine_t
	{
		xv::map::Point_t m_From;
		xv::map::Point_t m_To;
		int m_nAxis = 2;
	};
	[[nodiscard]] const std::optional<ClipLine_t> &GetClipLine() const { return m_ClipLine; }
	void SetClipLine( std::optional<ClipLine_t> line );
	[[nodiscard]] xv::map::ClipKeep_t GetClipMode() const { return m_nClipMode; }
	void SetClipMode( xv::map::ClipKeep_t nMode );
	// The plane the line means, and the selection as the cut would leave it (for the views).
	[[nodiscard]] std::optional<xv::map::Plane_t> GetClipPlane() const;
	[[nodiscard]] std::shared_ptr<const xv::map::CScene> GetClipPreview() const { return m_pClipPreview; }
	void ApplyClip();
	// Carve (Ctrl+Shift+C) with the selection; Make Hollow (the Map menu) with a wall width.
	void Carve();
	void MakeHollow();
	void MakeHollow( double flWallWidth );
	// Hammer's Transform dialog (Ctrl+M) and what it applies: turns about the selection's centre,
	// scales about it, or moves. Flip mirrors across the centre along one axis (Ctrl+L: the active
	// 2D view's horizontal one, Ctrl+I its vertical one; x and z from the 3D view).
	void ShowTransformDialog();
	void ApplyTransformValues( const TransformValues_t &values );
	void FlipSelection( std::size_t nAxis );
	// Paste Special (Ctrl+Shift+V) and what it applies; bStartAtView puts the first copy's centre
	// at the active 2D view's centre.
	void ShowPasteSpecialDialog();
	void PasteSpecial( xv::map::PasteSpecial_t how, bool bStartAtView );
	// Replace Textures (Ctrl+Shift+R) and what it does: the faces found replaced, or marked
	// (selected, with the texture tool) when bMarkOnly; among the selection or everything shown
	// (and hidden too). Returns how many faces.
	void ShowReplaceTextures();
	std::size_t ReplaceTextures( const xv::map::ReplaceTextures_t &how, bool bMarkOnly, bool bSelectedOnly, bool bHiddenToo );
	// The Entity Report and Check for Problems (Alt+P) windows, one of each, beside the views.
	CMapEntityReport *ShowEntityReport();
	CMapCheckDialog *ShowCheckForProblems();
	// Find Entities (Ctrl+Shift+F): the entities named exactly so selected and centred on.
	void ShowFindEntities();
	bool FindEntitiesByName( const QString &szName );
	// The vertex tool: which handles it shows, the handles picked (on the selected solids), and
	// a move of them as one undo step.
	[[nodiscard]] MapVertexMode_t GetVertexMode() const { return m_nVertexMode; }
	void SetVertexMode( MapVertexMode_t nMode );
	[[nodiscard]] const std::vector<xv::map::VertexRef_t> &GetVertexHandles() const { return m_VertexHandles; }
	void SetVertexHandles( std::vector<xv::map::VertexRef_t> handles );
	void ToggleVertexHandle( const xv::map::VertexRef_t &handle );
	void MoveVertexHandles( const xv::map::Point_t &offset );
	// The selected solids' corners and edges, by object.
	[[nodiscard]] std::vector<std::pair<std::uint32_t, xv::map::SolidVertices_t>> GetSelectedVertices() const;
	// Hammer++'s cordons: the box picked in the Filter Control's Cordons tab (the one Edit Cordon
	// Bounds drags), whether the cordon is on, the tool's box committed into the picked cordon (a
	// first cordon made when there is none), and Toggle Cordon State.
	[[nodiscard]] std::optional<xv::map::Box_t> GetCordonBox() const;
	[[nodiscard]] bool IsCordonActive() const;
	void CommitCordon();
	void ToggleCordon();
	// Edit > Tie to Entity (Ctrl+T), Move to World (Ctrl+Shift+W), Group (Ctrl+G), Ungroup (Ctrl+U).
	void TieToEntity();
	void TieToEntity( const QString &szClass );
	void MoveToWorld();
	void GroupSelection( bool bGroup );
	// Selects the object with Hammer's id and centres the views on it; centres them on a point.
	bool GoToObjectId( int nId );
	void CenterViewsOn( const xv::map::Point_t &point );
	[[nodiscard]] QString GetCurrentMaterial() const;
	void SetCurrentMaterial( const QString &szMaterial );
	// Picks the current texture from the game's materials (the texture browser).
	void BrowseMaterial();
	[[nodiscard]] QString GetEntityClass() const;

	// The Texture Application tool's faces.
	[[nodiscard]] const std::vector<xv::map::FaceRef_t> &GetSelectedFaces() const { return m_SelectedFaces; }
	void SetSelectedFaces( std::vector<xv::map::FaceRef_t> faces, bool bLiftFirst = false );
	void ToggleFace( const xv::map::FaceRef_t &face );
	// One step to undo: pfnEdit gets each selected face's side, its plane's normal and its points.
	void EditSelectedFaces( const QString &szName, const std::function<void( xv::map::Side_t &, const xv::map::Point_t &, const std::vector<xv::map::Point_t> & )> &pfnEdit );
	// Shift+T: the current texture (and, bValues, the sheet's values) onto the selected faces, or
	// every face of the selected objects when no face is.
	void ApplyCurrentTexture( bool bValues );
	// A click on a face with the texture tool, in the sheet's mode (Lift, Select, Apply...).
	void ClickFace( const xv::map::FaceRef_t &face, Qt::KeyboardModifiers modifiers );
	void ApplyToFace( const xv::map::FaceRef_t &face, bool bValues );
	[[nodiscard]] CMapFaceEdit &GetFaceEdit() { return *m_pFaceEdit; }
	// The Displacement tab's edits, each one undo step, on the selected faces (else every face
	// of the selected objects): displacements made at a power on four-sided faces, taken off,
	// sewn to each other and to plain faces among them, subdivided together, given noise along
	// their faces' normals, their alpha inverted, their power and elevation set.
	void CreateDisplacements( int nPower );
	void DestroyDisplacements();
	void SewDisplacements();
	void SubdivideDisplacements();
	void NoiseDisplacements( double flMin, double flMax );
	void InvertDisplacementAlpha();
	void SetDisplacementAttributes( int nPower, double flElevation, double flScale = 1.0, std::optional<int> nFlags = std::nullopt );
	// Hammer's Face Edit Sheet as a window of its own (Preferences), else nothing.
	[[nodiscard]] QDialog *GetFaceEditWindow() const { return m_pFaceEditWindow; }
	// The displacements a brush paints: the selected faces', else the one under the mouse.
	[[nodiscard]] std::vector<xv::map::FaceRef_t> GetPaintFaces( const std::optional<xv::map::FaceRef_t> &under ) const;
	// A stroke's result (the painted copies, from pScene) made one undo step.
	void CommitDisplacements( const xv::map::CScene &scene, const std::vector<std::pair<xv::map::FaceRef_t, xv::map::DispInfo_t>> &painted, const QString &szName );
	// A material's texture size in texels (128 by 128 until it is read, as for a missing one).
	[[nodiscard]] std::array<int, 2> GetMaterialSize( std::uint32_t nMaterial ) const;
	// A 3D view changed how it draws (the card's drawing failed): software needs the textures decoded.
	void RendererChanged();

	// IEditorTab
	[[nodiscard]] QString GetTabTitle() const override;
	[[nodiscard]] QString GetSessionEntry() const override;
	[[nodiscard]] bool IsFromGame() const override { return m_bFromGame; }
	void StopWork() override;
	void GameChanged( const xv::vfs::CGameInfo *pGame ) override;
	[[nodiscard]] QUndoStack *GetUndoStack() const override;
	bool ConfirmClose() override;
	bool Save() override;
	bool SaveAs() override;

protected:
	// The Face Edit window goes with the tab: hidden while another tab shows.
	void showEvent( QShowEvent *pEvent ) override;
	void hideEvent( QHideEvent *pEvent ) override;

signals:
	void StatusMessage( const QString &szText );
	// The tab's title changed (saved under another name, or modified).
	void TitleChanged();
	void CurrentMaterialChanged( const QString &szMaterial );

private:
	void BuildToolBars( QVBoxLayout *pLayout );
	[[nodiscard]] QWidget *BuildNewObjects();
	// The entity classes and materials to pick from, once the map's game data is read.
	void FillNewObjects();
	void RepaintViews();
	void RepaintTools();
	void Loaded();
	void SettingsChanged();
	void UpdateStatus();
	void UpdateGameStatus();
	void ResourcesArrived();
	// An edit made a new scene: the views follow, and materials it brought in are read.
	void SceneEdited( const std::vector<std::uint32_t> &changed );
	void GoToBrush();
	void Paste( CMapView *pFrom );
	void SnapSelection();
	void ShowProperties();
	void TakePreferences();
	// The faces the Displacement tab works on, and one of its edits applied under a name.
	[[nodiscard]] std::vector<xv::map::FaceRef_t> GetFaceTargets() const;
	void EditDisplacementFaces( const QString &szName, const std::function<bool( std::vector<xv::map::DispFace_t> & )> &pfnEdit, const QString &szNothing );
	[[nodiscard]] bool NeedsDecodedTextures() const;
	void StartResources();
	// The camera kept between runs, per map file.
	void RememberCamera() const;
	[[nodiscard]] std::optional<xv::map::CViewCamera> RecallCamera() const;
	[[nodiscard]] QString GetCameraKey() const;
	[[nodiscard]] QString GetFileName() const;
	void PreferencesChanged();
	[[nodiscard]] CMapPane *PaneOf( const CMapView *pView ) const;

	CMapDocument *m_pDocument = nullptr;
	std::unique_ptr<CMapRenderData> m_pRenderData;
	CMapResources *m_pResources = nullptr;
	QString m_szPath;
	bool m_bFromGame = false;
	MapViewSettings_t m_Settings;
	MapTool_t m_nTool = MapTool_t::MAP_TOOL_SELECT;
	MapEditMode_t m_nEditMode = MapEditMode_t::MAP_EDIT_MODE_RESIZE;
	bool m_bGizmo = true;
	MapGizmoMode_t m_nGizmoMode = MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE;
	bool m_bGizmoLocal = false;
	std::optional<xv::map::Point_t> m_Pivot;        // where it was dragged to
	std::vector<std::uint32_t> m_PivotSelection;  // for this selection
	std::optional<xv::map::CViewCamera> m_RememberedCamera;
	CMapView *m_pActiveView = nullptr;
	std::optional<xv::map::Box_t> m_NewBox;
	xv::map::Box_t m_LastBox{ { 0, 0, 0 }, { 64, 64, 64 } };
	std::optional<xv::map::Point_t> m_EntityMarker;
	std::optional<ClipLine_t> m_ClipLine;
	xv::map::ClipKeep_t m_nClipMode = xv::map::ClipKeep_t::CLIP_KEEP_FRONT;
	std::shared_ptr<const xv::map::CScene> m_pClipPreview; // the selection cut, drawn in white
	QLineEdit *m_pMaterial = nullptr;
	QComboBox *m_pEntityClass = nullptr;
	MapVertexMode_t m_nVertexMode = MapVertexMode_t::MAP_VERTEX_MODE_BOTH;
	std::vector<xv::map::VertexRef_t> m_VertexHandles;
	QPointer<CMapEntityReport> m_pEntityReport;
	QPointer<CMapCheckDialog> m_pCheck;
	QComboBox *m_pPrimitive = nullptr; // the block tool's shape (Hammer's Objects box)
	QSpinBox *m_pSides = nullptr;      // its Faces box
	QPointer<QDialog> m_pCompileDialog;
	QDialog *m_pFaceEditWindow = nullptr; // holds Face Edit when it is a window of its own
	// Face Edit where Preferences wants it: its window, or the side panel under Object Properties.
	void PlaceFaceEdit();
	// The cordon and box the cordon tool's box belongs to (the Cordons tab's pick when the tool
	// took it up).
	std::size_t m_nCordonPick = 0;
	std::size_t m_nCordonBoxPick = 0;
	// The map's cordons with the picked box set to box.
	[[nodiscard]] std::vector<xv::map::Cordon_t> CordonsWithBox( const xv::map::Box_t &box ) const;

	std::array<CMapPane *, 4> m_Panes{};
	CMapQuadSplitter *m_pSplitter = nullptr;
	std::vector<std::pair<QAction *, const bool *>> m_SettingActions; // toggles and what they show
	int m_nTextureSize = 1024;
	std::uint32_t m_nMaterialsRead = 0; // the scene's materials the resources were given
	std::string m_szSkyRead;            // the skyname the resources were started with
	QSplitter *m_pSidePanel = nullptr;
	CMapFilterControl *m_pFilter = nullptr;
	CMapProperties *m_pProperties = nullptr;
	CMapFaceEdit *m_pFaceEdit = nullptr;
	QStackedWidget *m_pPanelStack = nullptr; // Object Properties, or Face Edit with the texture tool
	QLabel *m_pPanelTitle = nullptr;
	std::vector<xv::map::FaceRef_t> m_SelectedFaces;
	QLabel *m_pLoading = nullptr;
	QActionGroup *m_pTools = nullptr;
	QActionGroup *m_pModes = nullptr;
	QLabel *m_pStatusSelection = nullptr;
	QLabel *m_pStatusSize = nullptr;
	QLabel *m_pStatusCursor = nullptr;
	QLabel *m_pStatusZoom = nullptr;
	QLabel *m_pStatusGrid = nullptr;
	QLabel *m_pStatusGame = nullptr;
};
