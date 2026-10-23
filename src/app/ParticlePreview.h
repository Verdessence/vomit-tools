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
// handles: drag one to move it across the view, or with Shift held, up and down. Below the view:
// play/pause, restart, speed, looping, the grid and the bounding boxes.
class CParticlePreview : public QWidget
{
	Q_OBJECT

public:
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
	// Puts a control point somewhere else, without starting over.
	void MoveControlPoint( int nIndex, xv::CVec3 vecPosition );
	void ResetControlPoints();
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

private:
	friend class CParticleCanvas;
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
	void PaintCounts( QPainter &painter );
	// The camera as it maps onto the canvas (the picture itself may be drawn smaller).
	[[nodiscard]] xv::particles::CCamera GetCanvasCamera() const;

	xv::particles::FileReader_t m_pfnReader;
	std::shared_ptr<const xv::particles::CLibrary> m_pLibrary;
	std::unique_ptr<xv::particles::CSimulation> m_pSimulation;
	std::map<std::string, std::shared_ptr<const xv::particles::Sprite_t>> m_SpriteCache; // by material
	std::vector<const xv::particles::Sprite_t *> m_Sprites;                                // by definition
	std::string m_szSystem;
	std::array<xv::CVec3, xv::particles::CSimulation::CONTROL_POINTS> m_ControlPoints{};
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
