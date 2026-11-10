//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Brush part selection, transforms and mesh operations shared by the map views.
//===========================================================================//
#pragma once

#include <QWidget>
#include <QPointF>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include "map/BrushMesh.h"
#include "map/BrushModeling.h"
#include "map/BrushSelect.h"
#include "map/MapModalTransform.h"

class CMapEditor;
class CMapView;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QPainter;
class QFormLayout;
class QSpinBox;
class QGroupBox;
class QUndoCommand;
class QPushButton;
class QLabel;
class QMenu;

// What parts turn and scale about.
enum class BrushPivot_t
{
	BRUSH_PIVOT_CENTRE,     // the middle of the selection's bounds
	BRUSH_PIVOT_ACTIVE,     // the part picked last
	BRUSH_PIVOT_INDIVIDUAL, // each part its own centre
	BRUSH_PIVOT_CURSOR,     // the 3D cursor
};

class CMapBrushEdit : public QWidget
{
	Q_OBJECT
public:
	explicit CMapBrushEdit( CMapEditor &editor );
	[[nodiscard]] const std::vector<xv::map::BrushPart_t> &GetParts() const { return m_Parts; }
	void SetParts( std::vector<xv::map::BrushPart_t> parts );
	// After an edit the tool didn't make: the parts on brushes it left alone stay as they were;
	// those on brushes it changed are found again by their corners (faces by their side), and
	// dropped where they are gone.
	void FindPartsAgain( const std::vector<std::uint32_t> &changed );
	void SetMode( int nMode );
	void SyncLegacyParts();
	void MergeVertices( bool bAtCenter, double flDistance = 1 );
	void Paint( CMapView &view, QPainter &painter ) const;
	bool HandleEvent( CMapView &view, QEvent *pEvent );
	void Cancel( bool bKeepTool = false );
	[[nodiscard]] bool IsTransformArmed( MapGizmoMode_t nMode ) const { return m_ArmedTransform == nMode; }
	[[nodiscard]] int GetOperation() const { return m_nOperation; }
	[[nodiscard]] int GetMode() const;
	void Transform( const xv::map::Transform_t &transform );
	void BeginTransform( MapGizmoMode_t nMode );
	void ApplyOperation( int nOperation );
	bool CommitPendingOperation();
	void SetModelParameters( const xv::map::BrushModelParams_t &params );
	[[nodiscard]] xv::map::BrushModelParams_t GetModelParameters() const;
	[[nodiscard]] static QString OperationName( int nOperation );

	// Selection extras (stage 4). Loops: Alt+click (Ctrl+Alt: a ring; Shift adds), through the
	// edge nearest the pointer; from the panel, through each selected edge.
	bool SelectLoopAt( CMapView &view, QPointF at, bool bRing, bool bAdd );
	void SelectLoops( bool bRing );
	void GrowSelection();
	void ShrinkSelection();
	// The parts of the current kind of an object (none: of every object a part is on); bRemove
	// takes them away.
	void SelectLinked( std::optional<std::uint32_t> nObject = std::nullopt, bool bRemove = false );
	void SelectSimilar( xv::map::BrushSimilar_t nSimilar );
	// Every face of the map in the selected faces' materials, their brushes selected too.
	void SelectByMaterial();
	// Proportional editing: nearby corners of the selected brushes follow moves by a falloff.
	void SetProportional( bool bOn );
	[[nodiscard]] bool IsProportional() const;
	void SetProportionalRadius( double flRadius );
	[[nodiscard]] double GetProportionalRadius() const;
	void SetFalloff( xv::map::BrushFalloff_t nFalloff );
	void SetPivot( BrushPivot_t nPivot );
	[[nodiscard]] BrushPivot_t GetPivot() const;
	// The 3D cursor (Shift+right click places it), a pivot and a point for operations.
	void SetCursor( xv::map::Point_t cursor );
	[[nodiscard]] xv::map::Point_t GetCursor() const { return m_Cursor; }
	// The point parts turn and scale about.
	[[nodiscard]] std::optional<xv::map::Point_t> GetPivotPoint() const;
	// G pressed again during a move: the selected vertices or edges slide along their edges.
	void BeginSlide( CMapView &view, QPointF at );
	[[nodiscard]] bool IsSliding() const { return m_bSlide; }

signals:
	void ToolChanged();

private:
	void CreateModelControls( QFormLayout &layout );
	bool HandleModelEvent( CMapView &view, QEvent *pEvent );
	void BeginModelPreview( CMapView &view, QPointF at );
	void UpdateModelPreview();
	void MoveModelPointer( CMapView &view, QPointF at, Qt::KeyboardModifiers modifiers );
	void PaintModel( CMapView &view, QPainter &painter ) const;
	[[nodiscard]] std::optional<QPointF> ModelHandleCenter( CMapView &view ) const;
	void PaintModelHandle( CMapView &view, QPainter &painter ) const;
	bool PickModelRing( CMapView &view, QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier );
	void ModelParametersChanged();
	void RefreshModelControls();
	bool CommitOperationEdit( xv::map::MapEdit_t edit, const std::vector<int> &sides );
	bool HandlePolygon( CMapView &view, QEvent *pEvent );
	bool HandleClip( CMapView &view, QEvent *pEvent );
	void PaintConstruction( CMapView &view, QPainter &painter ) const;
	void StartTransform( CMapView &view, QPointF at );
	struct Handle_t
	{
		xv::map::BrushPart_t m_Part;
		xv::map::Point_t m_Position;
		QPointF m_Screen;
	};
	[[nodiscard]] std::vector<Handle_t> Handles( CMapView &view ) const;
	void Refresh();
	[[nodiscard]] std::optional<CMapGizmo> Gizmo() const;
	[[nodiscard]] xv::map::CViewCamera Camera( CMapView &view ) const;
	void PreviewTransform( CMapView &view, Qt::KeyboardModifiers modifiers );
	void Commit( xv::map::MapEdit_t edit, const QString &szName );
	[[nodiscard]] xv::map::MapEdit_t BuildOperation( int nOperation );
	void PreviewOperation( CMapView &view, QPointF at, Qt::KeyboardModifiers modifiers );
	void FinishOperation( bool bCommit );
	void AdjustLastOperation( bool bRestoreInvalid = false );
	[[nodiscard]] std::optional<QPointF> Project( CMapView &view, xv::map::Point_t point ) const;
	[[nodiscard]] std::optional<xv::map::Point_t> PointerPoint( CMapView &view, QPointF at, xv::map::Point_t anchor ) const;
	[[nodiscard]] std::optional<QPointF> InsetCenter( CMapView &view ) const;
	[[nodiscard]] xv::map::BrushConcave_t GetConcave() const;
	// The corner moves a transform makes of the selection, with the pivot and proportional
	// editing's settings (the legacy Vertex tool: the transform alone).
	[[nodiscard]] std::vector<xv::map::BrushCornerMove_t> MovesFor( const xv::map::CScene &scene, const xv::map::Transform_t &transform ) const;
	[[nodiscard]] xv::map::MapEdit_t MoveEdit( const xv::map::CScene &scene, const xv::map::Transform_t &transform, std::string *pProblem ) const;
	// Commit corner moves as one step, the selection found again where it went.
	void CommitMoves( const std::vector<xv::map::BrushCornerMove_t> &moves, const xv::map::Transform_t *pLock, const QString &szName );
	void PreviewSlide( CMapView &view, QPointF at );
	// The edge of a selected brush nearest a point of a view (in 3D, of the face under it), and
	// the face beside it the click was on.
	struct EdgePick_t
	{
		std::uint32_t m_nObject = 0;
		std::uint32_t m_nEdge = 0;
		std::optional<std::uint32_t> m_nFace;
	};
	[[nodiscard]] std::optional<EdgePick_t> EdgeAt( CMapView &view, QPointF at ) const;
	[[nodiscard]] std::optional<std::uint32_t> ObjectAt( CMapView &view, QPointF at ) const;
	[[nodiscard]] std::vector<std::uint32_t> SelectedSolidObjects() const;
	void PlaceCursor( CMapView &view, QPointF at, bool bFree );
	void PaintSelectionExtras( CMapView &view, QPainter &painter ) const;
	void ShowSimilarMenu( const QPoint &globalAt );
	void FillSelectMenu( QMenu &menu );
	void RefreshCursorLabel();
	// A solid's mesh (by its index in the scene), built once per scene snapshot: painting and
	// hovering ask for the selected solids' meshes many times a frame, and a fine sphere's
	// takes milliseconds to build.
	[[nodiscard]] std::shared_ptr<const xv::map::BrushMeshParts_t> MeshOf( const xv::map::CScene &scene, std::uint32_t nSolid ) const;

	CMapEditor &m_Editor;
	QComboBox *m_pMode = nullptr;
	QComboBox *m_pConcave = nullptr;
	QCheckBox *m_pXray = nullptr;
	QDoubleSpinBox *m_pDistance = nullptr;
	std::vector<xv::map::BrushPart_t> m_Parts;
	// The scene the parts were picked on, to find them again after an edit made elsewhere.
	std::shared_ptr<const xv::map::CScene> m_pPartsScene;
	std::shared_ptr<const xv::map::CScene> m_pBase;
	std::optional<QPointF> m_From;
	QPointF m_At;
	xv::map::Point_t m_Anchor{};
	xv::map::Transform_t m_Transform;
	std::optional<CMapModalTransform> m_Modal;
	std::optional<CMapGizmo> m_DragGizmo;
	std::optional<MapGizmoMode_t> m_ArmedTransform;
	CMapView *m_pOperationView = nullptr;
	int m_nOperation = -1;
	bool m_bOperationValid = false;
	bool m_bWaitForClick = false;
	std::weak_ptr<const xv::map::CScene> m_pLastScene;
	struct MeshCache_t
	{
		std::weak_ptr<const xv::map::CScene> m_pScene;
		std::unordered_map<std::uint32_t, std::shared_ptr<const xv::map::BrushMeshParts_t>> m_Meshes;
	};
	mutable std::vector<MeshCache_t> m_MeshCaches; // the last few scenes asked about
	std::vector<xv::map::BrushPart_t> m_LastParts;
	int m_nLastOperation = -1;
	int m_nLastUndoIndex = -1;
	int m_nGizmoPart = -1;
	bool m_bBox = false;
	bool m_bApplying = false;
	std::vector<xv::map::Point_t> m_PolygonPoints;
	std::optional<xv::map::Point_t> m_PolygonHover;
	int m_nPolygonAxis = -1;
	double m_flPolygonDepth = 64;
	bool m_bClipThree = false;
	xv::map::BrushModelParams_t m_ModelParams;
	xv::map::BrushModelParams_t m_LastModelParams;
	std::vector<int> m_ModelSelectedSides;
	std::vector<std::uint32_t> m_LastObjects;
	QPointF m_LastModelPointer;
	xv::map::Point_t m_ModelKnifeNormal{};
	QString m_szModelNumber;
	bool m_bUpdatingModelControls = false;
	bool m_bAdjustingLast = false;
	double m_flLastDistance = 0;
	double m_flModelPointerAmount = 0;
	xv::map::Point_t m_ModelEnd{};
	std::shared_ptr<const xv::map::CScene> m_pLastOperationBase;
	std::vector<std::uint32_t> m_OperationObjects;
	bool m_bReplacingOperation = false;
	bool m_bModelHandleDrag = false;
	const QUndoCommand *m_pLastOperationCommand = nullptr;
	std::vector<int> m_LastSelectedSides;
	QGroupBox *m_pModelControls = nullptr;
	QWidget *m_pModelCommitControls = nullptr;
	QPushButton *m_pModelApply = nullptr;
	QDoubleSpinBox *m_pModelDepth = nullptr;
	QDoubleSpinBox *m_pModelAngle = nullptr;
	QDoubleSpinBox *m_pModelSlide = nullptr;
	QDoubleSpinBox *m_pModelOffsets[ 3 ]{};
	QSpinBox *m_pModelCount = nullptr;
	QComboBox *m_pModelAxis = nullptr;
	QCheckBox *m_pModelNewBrush = nullptr;
	QCheckBox *m_pModelRegion = nullptr;
	QCheckBox *m_pModelSquare = nullptr;
	QCheckBox *m_pModelBestPlane = nullptr;
	QCheckBox *m_pModelWholeBrush = nullptr;
	QCheckBox *m_pProportional = nullptr;
	QDoubleSpinBox *m_pRadius = nullptr;
	QComboBox *m_pFalloff = nullptr;
	QComboBox *m_pPivot = nullptr;
	QLabel *m_pCursorLabel = nullptr;
	xv::map::Point_t m_Cursor{};
	bool m_bCursorPlaced = false;
	std::optional<xv::map::BrushPart_t> m_Active; // the part picked last (the Active pivot)
	bool m_bSlide = false;
	QPointF m_SlideFrom;
	std::vector<xv::map::BrushCornerMove_t> m_SlideMoves;
};
