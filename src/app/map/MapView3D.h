//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The map's 3D view (Hammer's camera view): the map drawn wireframe, flat, textured,
//          textured and shaded or with its lightmap grid, flown through with Hammer's camera
//          controls, and clicked to select. It draws on the graphics card when there is one,
//          and in software otherwise (on a thread of its own, the newest picture wanted wins).
//
//===========================================================================//
#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <span>
#include <vector>

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>

#include "PointerLock.h"
#include "map/Faces.h"
#include "map/MapGizmo.h"
#include "map/MapModalTransform.h"
#include "map/MapRenderData.h"
#include "map/MapRenderer.h"
#include "map/MapView.h"
#include "map/Sprinkle.h"
#include "map/Trace.h"
#include "map/ViewCamera.h"

class CGpuView;
class CMapView3D;
class QPainter;
class QTimer;

class CMapOverlay3D : public QWidget
{
	Q_OBJECT

public:
	explicit CMapOverlay3D( CMapView3D &view );

protected:
	void paintEvent( QPaintEvent *pEvent ) override;

private:
	CMapView3D &m_View;
};

class CMapView3D : public CMapView
{
	Q_OBJECT

public:
	CMapView3D( CMapEditor &editor, MapViewType_t nType, QWidget *pParent = nullptr );
	~CMapView3D() override;

	[[nodiscard]] MapViewType_t GetType() const override { return m_nType; }
	void SetType( MapViewType_t nType ) override;
	bool Run( MapAction_t nAction ) override;
	void CenterOn( const xv::map::Box_t &box ) override;

	[[nodiscard]] const xv::map::CViewCamera &GetCamera() const { return m_Camera; }
	// The HDR lighting preview's auto exposure, as this view last measured it.
	[[nodiscard]] float GetExposure() const { return m_flExposure; }
	void SetCamera( const xv::map::CViewCamera &camera );
	// A parallel projection (Blender's orthographic view), scaled so what is ahead keeps its size
	// on screen; the wheel then zooms rather than moving.
	void SetOrthographic( bool bOrthographic );
	[[nodiscard]] bool IsOrthographic() const { return m_Camera.IsOrthographic(); }
	// The map's state changed (selection, visibility, settings): draw it again.
	void Redraw() { RequestRender(); }
	// Only what is drawn over the picture changed (the tools' box or marker).
	void RedrawLabels();
	void ApplyPreferences();
	// The map was loaded: the camera starts where it was when the map was last closed, or at the
	// map's active camera, as in Hammer.
	void ResetCamera();
	// What a click at a widget point would hit, nearest first.
	[[nodiscard]] std::vector<xv::map::TraceHit_t> HitsAt( QPointF at );
	// Where a click at a widget point meets the nearest thing drawn, and the way that surface
	// faces (towards the camera).
	[[nodiscard]] std::optional<std::pair<xv::map::Point_t, xv::map::Point_t>> SurfaceAt( QPointF at );
	// The picture as it would be drawn now, in software, synchronously (for tests and the
	// comparison with the graphics card).
	[[nodiscard]] QImage RenderNow( QSize size );
	[[nodiscard]] bool IsUsingGpu() const { return m_pGpu != nullptr; }
	// The last picture's cost (the card's renderer's, or the software one's time in m_flFrameMs).
	[[nodiscard]] MapRenderStats_t GetStats() const;
	[[nodiscard]] double GetLastRenderTime() const { return m_flRenderMs; }
	// Moves the camera as held keys would over flSeconds (the fly logic, without the clock).
	void Fly( double flSeconds );
	[[nodiscard]] double GetSpeed() const { return m_flSpeed; }
	// The frame the view draws next: the document's state with this view's camera and look.
	[[nodiscard]] MapFrame_t MakeFrame( QSize size );
	void ExposureMeasured( float flExposure );
	// The statistics and focus frame, over the picture.
	void PaintLabels( QPainter &painter );
	// The gizmo at the selection, if there is one to show, and the camera at this view's size.
	[[nodiscard]] std::optional<CMapGizmo> GetGizmo() const;
	// The block tool's box in a 3D view. An arrow stands out of the middle of each face (axis
	// * 2 + 0 for its low side, + 1 for its high one) and drags that face along its axis; a drag
	// on the box itself slides it across the face pressed; G, R and E move, turn and size it
	// with the keys (X, Y, Z for an axis), as they do the selection.
	struct BoxArrow_t
	{
		QPointF m_From; // on screen
		QPointF m_To;
		double m_flDepth = 0.0;
	};
	[[nodiscard]] MapGridPlane_t BoxFrame() const;
	[[nodiscard]] std::pair<xv::map::Point_t, xv::map::Point_t> BoxRay( QPointF at ) const;
	[[nodiscard]] std::array<xv::map::Point_t, 6> BoxHandles( const xv::map::Box_t &box ) const;
	[[nodiscard]] std::optional<BoxArrow_t> BoxArrow( const xv::map::Box_t &box, int nFace ) const;
	[[nodiscard]] int BoxHandleAt( QPointF at ) const;
	[[nodiscard]] std::optional<std::pair<int, xv::map::Point_t>> BoxSurfaceAt( QPointF at ) const;
	void DragBox( QPointF at, Qt::KeyboardModifiers modifiers );
	void EndBoxDrag( bool bCommit );
	// A new box dragged out in the view: on the world grid's plane while it shows, else on the
	// surface pressed (along the world's axes). On a plane turned off the world's axes the block
	// is made when the drag ends; else the box is left to size, as one drawn in a 2D view.
	bool BeginBlockDrag( QPointF at );
	void DragBlock( QPointF at, Qt::KeyboardModifiers modifiers );
	void EndBlockDrag( bool bCommit );
	[[nodiscard]] bool IsDraggingBlock() const { return m_BlockPlane.has_value(); }
	// The world grid's plane moved and turned by a transform (its axes kept square).
	[[nodiscard]] static MapGridPlane_t MovedPlane( const MapGridPlane_t &plane, const xv::map::Transform_t &transform );
	// A cross while the Displacement tab's brush is on, else the arrow (not during mouselook).
	void UpdateCursor();
	[[nodiscard]] bool HasPaintBrushRing() const;
	[[nodiscard]] QString GetPaintCosts() const;
	// The brush face under a widget point (the nearest hit's triangle, when it is a brush's).
	[[nodiscard]] std::optional<xv::map::FaceRef_t> FaceAt( QPointF at, bool bThroughOverlays = false );
	// FaceAt for many points, with the frame made once (a fine brush's handles each ask).
	[[nodiscard]] std::vector<std::optional<xv::map::FaceRef_t>> FacesAt( std::span<const QPointF> points, bool bThroughOverlays = false );
	// For each point, whether a brush other than its own object hides it (the brush tools'
	// handles: a part facing the view can't be behind its own convex brush).
	[[nodiscard]] std::vector<bool> BrushesHide( std::span<const xv::map::Point_t> points, std::span<const std::uint32_t> objects );
	[[nodiscard]] xv::map::CViewCamera GetSizedCamera() const;

protected:
	void paintEvent( QPaintEvent *pEvent ) override;
	void resizeEvent( QResizeEvent *pEvent ) override;
	void mousePressEvent( QMouseEvent *pEvent ) override;
	void mouseDoubleClickEvent( QMouseEvent *pEvent ) override;
	void mouseMoveEvent( QMouseEvent *pEvent ) override;
	void mouseReleaseEvent( QMouseEvent *pEvent ) override;
	void wheelEvent( QWheelEvent *pEvent ) override;
	void keyPressEvent( QKeyEvent *pEvent ) override;
	void keyReleaseEvent( QKeyEvent *pEvent ) override;
	void focusOutEvent( QFocusEvent *pEvent ) override;

private:
	void SetUseGpu( bool bOn );
	// Puts the camera where the map starts it (see ResetCamera), without drawing.
	void PlaceCamera();
	void Changed();
	void RequestRender();
	void WorkerLoop();
	void Tick();
	// The card's frames move the camera while keys are held.
	void StepFlight();
	void SetMouselook( bool bOn );
	// Turns the camera by a mouse movement, at the preferences' speed.
	void Look( QPointF delta );
	void DragCamera( Qt::MouseButtons buttons, QPointF delta );
	void EndSpaceDrag();
	// Hides the cursor and holds it in the view while the mouse moves the camera.
	void CapturePointer( QPoint at, std::function<void( QPointF )> pfnMoved );
	void ReleasePointer();
	void WarpToMiddle();
	void Select( QPointF at, bool bToggle );
	// The block tool's box, the entity tool's marker and the gizmo, over the picture.
	void PaintTools( QPainter &painter );
	void PaintOriginAxes( QPainter &painter ) const;
	void UpdateCollapsibleVertices();
	bool BeginOverlayDrag( QPointF at );
	// A selected overlay's corner (0 to 3) or centre (4) under the mouse, with the Overlay tool.
	[[nodiscard]] std::optional<std::pair<std::uint32_t, int>> OverlayHandleAt( QPointF at ) const;
	void PreviewOverlayDrag( QPointF at );
	void EndOverlayDrag( bool bCommit );
	std::shared_ptr<const xv::map::CScene> m_pOverlayScene;
	xv::map::MapEdit_t m_OverlayEdit;
	xv::map::Point_t m_OverlayFrom{};
	std::uint32_t m_nOverlayEntity = 0;
	int m_nOverlayCorner = -1;
	void BeginModalTransform( MapGizmoMode_t nMode );
	void PreviewModalTransform( Qt::KeyboardModifiers modifiers );
	void EndModalTransform( bool bCommit );
	std::optional<CMapModalTransform> m_ModalTransform;
	xv::map::Transform_t m_ModalResult;
	bool m_bConsumeModalRelease = false;
	void PreviewGizmo( QPointF at, Qt::KeyboardModifiers modifiers );
	void EndGizmo( bool bCommit, QPointF at, Qt::KeyboardModifiers modifiers );
	// The Displacement tab's brush: whether a press paints, a stroke's sample at a point
	// (previewed), and the stroke made one undo step (or dropped).
	[[nodiscard]] bool IsPaintingDisplacements() const;
	[[nodiscard]] bool IsDisplacementBrushShown() const;
	// The ring under the mouse: the displacement brush's or the sprinkle tool's, at its radius.
	[[nodiscard]] bool IsBrushShown() const;
	[[nodiscard]] double GetBrushRadius() const;
	void SetBrushRadius( double flRadius );
	// The sprinkle tool: a stroke from the press to the release, previewed as it goes, one undo
	// step at the end (or dropped).
	[[nodiscard]] bool IsSprinkling() const;
	void BeginSprinkle( QPointF at, Qt::KeyboardModifiers modifiers );
	void SprinkleAt( QPointF at, Qt::KeyboardModifiers modifiers );
	void EndSprinkle( bool bCommit );
	std::unique_ptr<xv::map::CSprinkleStroke> m_pSprinkleStroke;
	std::optional<MapFrame_t> m_SprinkleFrame; // what a dab's rays trace, made once a dab
	xv::map::RasterScene_t m_SprinkleRaster;
	QPoint m_SprinkleLast;
	std::uint32_t m_nSprinkleStrokes = 0;
	void PaintStroke( QPointF at );
	void PreviewPaint();
	void PaintFrameSubmitted();
	void EndPaint( bool bCommit );
	[[nodiscard]] xv::map::RasterMode_t GetMode() const;

	MapViewType_t m_nType;
	xv::map::CViewCamera m_Camera;
	int m_nCameraLoad = -1; // the map (by the document's load count) the camera was placed for
	CGpuView *m_pGpu = nullptr;
	CMapOverlay3D *m_pOverlay = nullptr;

	// Software drawing.
	std::thread m_Worker;
	std::mutex m_Mutex;
	std::condition_variable m_Wake;
	std::optional<MapFrame_t> m_Pending;
	bool m_bQuit = false;
	QImage m_Image;
	double m_flRenderMs = 0.0;
	float m_flExposure = 1.0f; // the HDR lighting preview's auto exposure, as last measured
	bool m_bReduced = false;
	QTimer *m_pSettle = nullptr; // full resolution once the camera stops

	// Flying.
	QTimer *m_pTick = nullptr;
	QElapsedTimer m_Clock;
	std::vector<MapAction_t> m_Held;
	double m_flSpeed = 0.0;
	bool m_bMouselook = false;  // Z
	QPoint m_LookFrom;          // where the cursor was when mouselook began: it goes back there
	CPointerLock m_PointerLock; // Wayland: pins the pointer meanwhile
	bool m_bSpaceHeld = false;
	bool m_bSpaceDrag = false; // a drag begun with Space held: the camera's, whatever the tool
	Qt::MouseButtons m_Dragging;
	QPoint m_LastMouse;
	QPoint m_PressAt;
	bool m_bMoved = false;

	// Page Up and Page Down step through what was under the last click.
	std::vector<xv::map::TraceHit_t> m_Hits;
	std::size_t m_nHit = 0;
	bool m_bStatistics = false;
	// The gizmo: the axis under the mouse, and a drag on one (from where, on which scene).
	int m_nGizmoHover = -1;
	int m_nGizmoDrag = -1;
	QPointF m_GizmoFrom;
	std::optional<CMapGizmo> m_DragGizmo;
	std::shared_ptr<const xv::map::CScene> m_pGizmoScene;
	std::vector<std::uint32_t> m_GizmoSelection;
	// The block tool's box while it is dragged or moved by the keys: the box as it was.
	std::optional<xv::map::Box_t> m_BoxFrom;
	int m_nBoxFace = -1;      // the face dragged by its arrow
	int m_nBoxFaceHover = -1; // the face whose arrow is under the pointer
	int m_nBoxPlane = -1;     // the axis the box slides across, dragged by itself
	xv::map::Point_t m_BoxGrab{}; // where on the box that drag began
	bool m_bBoxHover = false; // the pointer is over the box itself
	// A new box being dragged out: the plane it is drawn on, where on it the drag began, which
	// side of the plane it stands on and how tall, the box in the plane's measures, and the
	// editor's box as it was (a drag given up puts it back).
	std::optional<MapGridPlane_t> m_BlockPlane;
	std::array<double, 2> m_BlockFrom{};
	double m_flBlockDepth = 0.0;
	std::optional<xv::map::Box_t> m_BlockLocal;
	std::optional<xv::map::Box_t> m_BlockBefore;
	std::optional<MapGridPlane_t> m_BlockPlaneBefore;
	// The world grid's plane as it was, while the gizmo or the keys move it.
	std::optional<MapGridPlane_t> m_GridFrom;
	// A paint stroke: the scene it started from, the displacements as the stroke leaves them,
	// whether the right button (lowering) began it, where it last painted, and the brush's
	// place under the mouse (its ring).
	std::shared_ptr<const xv::map::CScene> m_pCollapsibleScene;
	std::vector<xv::map::FaceRef_t> m_CollapsibleFaces;
	std::vector<std::vector<std::size_t>> m_CollapsibleVertices;
	std::vector<std::vector<double>> m_SculptApplied;
	Qt::KeyboardModifiers m_PaintModifiers;
	bool m_bPaintAlpha = false;
	bool m_bPaintSculpt = false;
	std::vector<xv::map::FaceRef_t> m_PaintChanged;
	QTimer *m_pPaintPreview = nullptr;
	bool m_bPaintPending = false;
	bool m_bPaintPreviewUsed = false;
	int m_nPaintSamples = 0;
	int m_nPaintPreviews = 0;
	double m_flPaintStartMs = 0;
	double m_flPaintCommitMs = 0;
	QElapsedTimer m_PaintClock;
	double m_flGridPaintMs = 0.0;
	double m_flDispEditMs = 0.0;
	std::shared_ptr<const xv::map::CScene> m_pPaintScene;
	std::vector<std::pair<xv::map::FaceRef_t, xv::map::DispInfo_t>> m_Painted;
	bool m_bPaintLower = false;
	QPoint m_PaintLast;
	std::optional<std::pair<xv::map::Point_t, xv::map::Point_t>> m_BrushAt;
	// Alt+drag sizing the brush: where it began and the radius then.
	std::optional<QPoint> m_RadiusFrom;
	double m_flRadiusFrom = 0.0;
	double m_flDensityFrom = 0.0; // the sprinkle tool's, as the right-drag began
	QElapsedTimer m_LogClock; // the statistics go to the debug log once a second
};
