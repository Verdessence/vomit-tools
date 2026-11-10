//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The physics tool (Hammer++'s Physics Simulation): Space starts the selected props and
//          brushes falling against the rest of the map (map/Physics), the views showing them
//          move; Space again leaves them where they are, one undo step (Escape puts them back).
//          While it runs, in a 3D view the left button drags an object, the right shoots it and
//          the middle sets off an explosion. The side panel holds the mouse's strengths, the
//          environment (gravity, air density, time scale), the options and the debug drawing,
//          kept in vomit-tools.nut.
//
//===========================================================================//
#pragma once

#include <map>
#include <memory>
#include <string>

#include <QElapsedTimer>
#include <QWidget>

#include "map/Physics.h"

class CMapEditor;
class CMapView;
class QCheckBox;
class QDoubleSpinBox;
class QEvent;
class QKeyEvent;
class QLabel;
class QPainter;
class QPushButton;
class QTimer;

namespace xv::vfs
{
class CFileSystem;
}

class CMapPhysicsTool : public QWidget
{
	Q_OBJECT

public:
	explicit CMapPhysicsTool( CMapEditor &editor );
	~CMapPhysicsTool() override;

	// The tool chosen (or left: a running simulation stops, keeping where things are).
	void SetActive( bool bActive );
	bool HandleEvent( CMapView &view, QEvent *pEvent );
	void Paint( CMapView &view, QPainter &painter ) const;
	// Space (start and stop) and, while it runs, Escape.
	[[nodiscard]] bool WantsKey( const QKeyEvent &key ) const;

	[[nodiscard]] bool IsRunning() const { return m_pScene != nullptr; }
	// Starts on the selection; false, with why in the panel, when nothing of it can move.
	bool Start();
	// Stops: bKeep leaves everything where it is now (one undo step), else puts it back.
	void Stop( bool bKeep );
	void Toggle();
	// The simulation moved on by simulated seconds at once (the timer's own steps aside).
	void Advance( double flSeconds );
	[[nodiscard]] const xv::map::CPhysicsScene *GetScene() const { return m_pScene.get(); }
	// The panel's settings.
	[[nodiscard]] xv::map::PhysicsSettings_t GetSettings() const;

private:
	void Tick();
	void Preview();
	void SettingsChanged();
	void WritePanel();
	void UpdateStatus();
	void Refresh();
	// A model's collision, read once (null when it has none).
	[[nodiscard]] std::shared_ptr<const xv::map::PropCollision_t> CollisionOf( std::string_view szModel );

	CMapEditor &m_Editor;
	bool m_bActive = false;
	bool m_bPanelUpdating = false;
	bool m_bPreviewing = false; // the document's signals from our own previews aren't another edit
	std::unique_ptr<xv::map::CPhysicsScene> m_pScene;
	std::shared_ptr<const xv::map::CScene> m_pBase; // the map as it was when it started
	QTimer *m_pTimer = nullptr;
	QElapsedTimer m_Clock;
	double m_flGrabDepth = 0.0;
	std::string m_szProblem;
	// The game's files for the models' collision, and what was read of them.
	std::unique_ptr<xv::vfs::CFileSystem> m_pFileSystem;
	std::map<std::string, std::shared_ptr<const xv::map::PropCollision_t>> m_Collisions;

	QPushButton *m_pToggle = nullptr;
	QLabel *m_pStatus = nullptr;
	QDoubleSpinBox *m_pDrag = nullptr;
	QDoubleSpinBox *m_pBullet = nullptr;
	QDoubleSpinBox *m_pExplode = nullptr;
	QDoubleSpinBox *m_pGravity[ 3 ] = {};
	QDoubleSpinBox *m_pAirDensity = nullptr;
	QDoubleSpinBox *m_pTimeScale = nullptr;
	QCheckBox *m_pSleep = nullptr;
	QCheckBox *m_pAutoMass = nullptr;
	QCheckBox *m_pPropsOnly = nullptr;
	QCheckBox *m_pDrawMoving = nullptr;
	QCheckBox *m_pDrawStatic = nullptr;
};
