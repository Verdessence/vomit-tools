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
#include <vector>

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>

#include "PointerLock.h"
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
	void ApplyPreferences();
	// The map was loaded: the camera starts where it was when the map was last closed, or at the
	// map's active camera, as in Hammer.
	void ResetCamera();
	// What a click at a widget point would hit, nearest first.
	[[nodiscard]] std::vector<xv::map::TraceHit_t> HitsAt( QPointF at );
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
	[[nodiscard]] xv::map::RasterMode_t GetMode() const;

	MapViewType_t m_nType;
	xv::map::CViewCamera m_Camera;
	const void *m_pCameraFor = nullptr; // the scene the camera was placed for
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
	QElapsedTimer m_LogClock; // the statistics go to the debug log once a second
};
