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
#include <vector>

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>

#include "PointerLock.h"
#include "map/Faces.h"
#include "map/MapGizmo.h"
#include "map/MapRenderData.h"
#include "map/MapRenderer.h"
#include "map/MapView.h"
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
	void SetCamera( const xv::map::CViewCamera &camera );
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
	// The statistics and focus frame, over the picture.
	void PaintLabels( QPainter &painter );
	// The gizmo at the selection, if there is one to show, and the camera at this view's size.
	[[nodiscard]] std::optional<CMapGizmo> GetGizmo() const;
	// A cross while the Displacement tab's brush is on, else the arrow (not during mouselook).
	void UpdateCursor();
	// The brush face under a widget point (the nearest hit's triangle, when it is a brush's).
	[[nodiscard]] std::optional<xv::map::FaceRef_t> FaceAt( QPointF at );
	[[nodiscard]] xv::map::CViewCamera GetSizedCamera() const;

protected:
	void paintEvent( QPaintEvent *pEvent ) override;
	void resizeEvent( QResizeEvent *pEvent ) override;
	void mousePressEvent( QMouseEvent *pEvent ) override;
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
	void Select( QPointF at, bool bToggle );
	// The block tool's box, the entity tool's marker and the gizmo, over the picture.
	void PaintTools( QPainter &painter ) const;
	void PreviewGizmo( QPointF at, Qt::KeyboardModifiers modifiers );
	void EndGizmo( bool bCommit, QPointF at, Qt::KeyboardModifiers modifiers );
	// The Displacement tab's brush: whether a press paints, a stroke's sample at a point
	// (previewed), and the stroke made one undo step (or dropped).
	[[nodiscard]] bool IsPaintingDisplacements() const;
	void PaintStroke( QPointF at );
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
	Qt::MouseButtons m_Dragging;
	QPoint m_LastMouse;
	QPoint m_PressAt;
	bool m_bMoved = false;

	// Page Up and Page Down step through what was under the last click.
	std::vector<xv::map::TraceHit_t> m_Hits;
	std::size_t m_nHit = 0;
	std::size_t m_nCamera = 0; // the map's camera Page Up and Page Down went to
	bool m_bStatistics = false;
	// The gizmo: the axis under the mouse, and a drag on one (from where, on which scene).
	int m_nGizmoHover = -1;
	int m_nGizmoDrag = -1;
	QPointF m_GizmoFrom;
	std::optional<CMapGizmo> m_DragGizmo;
	std::shared_ptr<const xv::map::CScene> m_pGizmoScene;
	std::vector<std::uint32_t> m_GizmoSelection;
	// A paint stroke: the scene it started from, the displacements as the stroke leaves them,
	// whether the right button (lowering) began it, where it last painted, and the brush's
	// place under the mouse (its ring).
	std::shared_ptr<const xv::map::CScene> m_pPaintScene;
	std::vector<std::pair<xv::map::FaceRef_t, xv::map::DispInfo_t>> m_Painted;
	bool m_bPaintLower = false;
	QPoint m_PaintLast;
	std::optional<std::pair<xv::map::Point_t, xv::map::Point_t>> m_BrushAt;
	// Alt+drag sizing the brush: where it began and the radius then.
	std::optional<QPoint> m_RadiusFrom;
	double m_flRadiusFrom = 0.0;
	QElapsedTimer m_LogClock; // the statistics go to the debug log once a second
};
