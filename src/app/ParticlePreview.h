//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A live view of one particle system and the systems it spawns: drag to turn around it,
//          right-drag to move, scroll to zoom, double-click to frame it again. The control points
//          the system uses are handles: drag one to move it across the view, or with Shift held,
//          up and down. Below the view:…
//
//===========================================================================//
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include "particles/Render.h"
#include "particles/Simulation.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QTimer;
class QToolButton;

// A live view of one particle system and the systems it spawns: drag to turn around it, right-drag
// to move, scroll to zoom, double-click to frame it again. The control points the system uses are
// handles: drag one to move it across the view, or with Shift held, up and down; while the grid
// shows, it snaps to it. Control point 0 is where the effect is, so moving it only changes the
// view. Moving any other point writes it into the file, through a "Set Control Point Positions"
// operator (one is added when needed); with Ctrl held, a point the file doesn't place moves in the
// view only. Below the view: play/pause, restart, speed, looping, the grid and the bounding boxes.
class CParticlePreview : public QWidget
{
	Q_OBJECT

public:
	using ControlPoints_t = std::array<xv::CVec3, xv::particles::CSimulation::CONTROL_POINTS>;

	explicit CParticlePreview( QWidget *pParent = nullptr );
	~CParticlePreview() override;

	// Where materials and textures are read from.
	void SetReader( xv::particles::FileReader_t pfnReader );
	// The systems of the file being edited. The current system keeps playing, from about where it
	// was, so edits show at once.
	void SetDocument( const xv::dmx::Document_t &document );
	// Shows a system by name, from the start, framed.
	void SetSystem( const std::string &szName );
	[[nodiscard]] const std::string &GetSystem() const { return m_szSystem; }

	void SetPlaying( bool bPlaying );
	void Restart();
	// Moves the camera to take in what the system shows.
	void FrameView();
	// Advances the simulation by seconds (the timer does this while playing).
	void Advance( float flSeconds );
	// Puts a control point somewhere else in the view, without starting over.
	void MoveControlPoint( int nIndex, xv::CVec3 vecPosition );
	// Puts the control points the file doesn't place back where they start.
	void ResetControlPoints();
	[[nodiscard]] const ControlPoints_t &GetControlPoints() const { return m_ControlPoints; }
	void SetControlPoints( const ControlPoints_t &points );
	// Dragging a control point, as the mouse does it: begin on its handle, drag to places in the
	// world (snapped to the grid while it shows), end to keep it there. In the view only for point
	// 0, or with bViewOnly for a point no operator places; otherwise ControlPointPlaced asks for the
	// file to be changed when it ends.
	void BeginDrag( int nIndex, bool bViewOnly = false );
	void DragTo( xv::CVec3 vecPosition );
	void EndDrag();
	[[nodiscard]] int GetDragged() const { return m_Drag.m_nIndex; }
	// The operator selected in the editor: its emission shape shows even without Bounds.
	void SetSelectedElement( std::int32_t nElement );
	// Whether a "Set Control Point Positions" operator of the shown systems places a point.
	[[nodiscard]] bool IsPlacedByFile( int nIndex ) const;
	// Puts a dragged point into the file at location, measured the way the operator measures it:
	// with operator nOperator (its slots for the point, and slot nSlot turned to it when not -1), or
	// a new operator in the shown system when nOperator is -1 (see particles::PlaceControlPoint).
	// Returns whether the file changed; until it comes back through SetDocument() the point stays
	// held where it was put.
	using Placer_t = std::function<bool( int nIndex, xv::CVec3 vecLocation, std::int32_t nOperator, int nSlot )>;
	void SetPlacer( Placer_t pfnPlace ) { m_pfnPlace = std::move( pfnPlace ); }
	// The control points the shown systems use (always 0).
	[[nodiscard]] std::vector<int> GetUsedControlPoints() const;

	// The picture as it is drawn now, without the overlays.
	[[nodiscard]] QImage Frame();
	// A small square picture of a system of the current file, a moment after it starts, framed.
	[[nodiscard]] QImage Thumbnail( const std::string &szName, int nSize );
	[[nodiscard]] const xv::particles::CSimulation *GetSimulation() const { return m_pSimulation.get(); }
	[[nodiscard]] const xv::particles::CCamera &GetCamera() const { return m_Camera; }
	// Functions in the shown systems that the preview leaves out, and materials it couldn't load.
	[[nodiscard]] QStringList GetNotes() const;

	struct Count_t
	{
		std::string m_szSystem;
		int m_nParticles = 0;
		int m_nLimit = 0; // max_particles
	};
	// How full each shown system is.
	[[nodiscard]] std::vector<Count_t> GetCounts() const;

signals:
	// Points moved in the view only (dragged or reset), for undo.
	void ControlPointsMoved( const ControlPoints_t &before, const ControlPoints_t &after, const QString &szDescription );

private:
	friend class CParticleCanvas;

	struct Drag_t
	{
		int m_nIndex = -1;
		bool m_bInFile = false;
		std::int32_t m_nOperator = -1;
		int m_nSlot = -1;
		xv::CVec3 m_vecOrigin;   // where the operator measures from
		xv::CVec3 m_vecLocation; // from the origin
		bool m_bMoved = false;
		bool m_bEnded = false; // let go, waiting for the file
		ControlPoints_t m_Before{};
	};

	friend class CParticleOverlay;

	void Rebuild( bool bKeepTime );
	void Tick();
	void UpdateInfo();
	[[nodiscard]] const xv::particles::Sprite_t *Sprite( std::size_t nDefinition );
	void PaintCanvas( QPainter &painter, const QRect &area );
	void PaintOverlays( QPainter &painter );
	// Draws on the graphics card, or in software.
	void SetUseGpu( bool bOn );
	// Asks for the picture to be drawn again.
	void Redraw();
	void PaintBounds( QPainter &painter, const xv::particles::CCamera &camera );
	// Where the position initializers put particles: spheres, boxes and offset ranges, around
	// their control points.
	void PaintShapes( QPainter &painter, const xv::particles::CCamera &camera );
	void PaintCounts( QPainter &painter );
	// The camera as it maps onto the canvas (the picture itself may be drawn smaller).
	[[nodiscard]] xv::particles::CCamera GetCanvasCamera() const;

	xv::particles::FileReader_t m_pfnReader;
	std::shared_ptr<const xv::particles::CLibrary> m_pLibrary;
	std::unique_ptr<xv::particles::CSimulation> m_pSimulation;
	std::map<std::string, std::shared_ptr<const xv::particles::Sprite_t>> m_SpriteCache; // by material
	std::vector<const xv::particles::Sprite_t *> m_Sprites;                                // by definition
	std::string m_szSystem;
	ControlPoints_t m_ControlPoints{};
	Drag_t m_Drag;
	Placer_t m_pfnPlace;
	std::int32_t m_nSelectedElement = -1;
	xv::particles::CCamera m_Camera;
	bool m_bCameraSet = false;
	QImage m_Image;
	bool m_bDirty = true;
	float m_flSpeed = 1.0f;
	float m_flIdle = 0.0f; // seconds since a looping system finished

	QWidget *m_pCanvas = nullptr;
	QTimer *m_pTimer = nullptr;
	QElapsedTimer m_Clock;
	QToolButton *m_pPlay = nullptr;
	QComboBox *m_pSpeedBox = nullptr;
	QCheckBox *m_pLoop = nullptr;
	QCheckBox *m_pGrid = nullptr;
	QCheckBox *m_pBounds = nullptr;
	QCheckBox *m_pGpuBox = nullptr;
	QWidget *m_pGpu = nullptr;     // the GPU view, when drawing on the graphics card
	QWidget *m_pOverlay = nullptr; // what is painted over it
	QLabel *m_pInfo = nullptr;
};
