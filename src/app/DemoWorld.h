//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The demo viewer's 3D view: the demo's map (its .bsp read back as the map viewer
//          reads one, drawn by the map's own renderers) with the players standing where the
//          demo has them at the playhead, each a figure in its team's colour with its name
//          over it. The camera flies free, is the recorder's own, or follows a player (through
//          its eyes, or from behind).
//
//===========================================================================//
#pragma once

#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include "demo/Players.h"
#include "map/MapRenderData.h"
#include "map/ViewCamera.h"
#include "vfs/Vfs.h"

class CDemoViewer;
class CDemoWorld;
class CGpuView;
class CMapDocument;
class CMapResources;
class QTimer;

enum class DemoCamera_t
{
	DEMO_CAMERA_FREE,         // flown by hand
	DEMO_CAMERA_RECORDER,     // what the recorder saw (a player's own demo)
	DEMO_CAMERA_FIRST_PERSON, // through the followed player's eyes
	DEMO_CAMERA_CHASE,        // behind the followed player
};

// A team's colour for its players' figures and names.
[[nodiscard]] QColor DemoTeamColor( int nTeam );

// The names, over the graphics card's picture (which a widget can't paint on itself).
class CDemoWorldOverlay : public QWidget
{
	Q_OBJECT

public:
	explicit CDemoWorldOverlay( CDemoWorld &world );

protected:
	void paintEvent( QPaintEvent *pEvent ) override;

private:
	CDemoWorld &m_World;
};

class CDemoWorld : public QWidget
{
	Q_OBJECT

public:
	explicit CDemoWorld( CDemoViewer &viewer, QWidget *pParent = nullptr );
	~CDemoWorld() override;

	// A compiled map's bytes, read back and loaded in the background. game is where its
	// textures and models come from; disk, the .bsp's own path when it is a file, mounts what is
	// packed into it ahead of the game's.
	void LoadCompiledMap( std::vector<std::byte> bsp, std::optional<xv::vfs::CGameInfo> game, const QString &szName, const std::filesystem::path &disk );
	// The demo's map isn't to be had: said in the view, which shows the players alone.
	void SetNoMap( const QString &szWhy );
	// Stops the reading under way.
	void Stop();
	[[nodiscard]] bool HasMap() const;
	// What the view says of its map while it is read, or why there is none; empty once it is in.
	[[nodiscard]] const QString &GetStatus() const { return m_szStatus; }

	[[nodiscard]] DemoCamera_t GetCameraMode() const { return m_nMode; }
	void SetCameraMode( DemoCamera_t nMode );
	// The player the following cameras follow (its entity); 0 for none.
	[[nodiscard]] int GetFollowed() const { return m_nFollowed; }
	void SetFollowed( int nEntity );
	// Names over the players, and the players shown through walls.
	[[nodiscard]] bool IsShowingNames() const { return m_bNames; }
	void SetShowingNames( bool bNames );
	[[nodiscard]] bool IsSeeingThroughWalls() const { return m_bThroughWalls; }
	void SetSeeingThroughWalls( bool bThrough );

	// The camera as it is at the playhead, in whatever mode.
	[[nodiscard]] xv::map::CViewCamera GetCamera();
	void SetCamera( const xv::map::CViewCamera &camera );
	// The free camera put where it sees the players there at the playhead.
	void LookAtPlayers();
	// The players drawn at the playhead: the living ones on a team.
	[[nodiscard]] std::vector<xv::demo::PlayerState_t> GetPlayersShown() const;
	// The player whose figure a widget point is on, nearest first; 0 for none.
	[[nodiscard]] int PlayerAt( QPointF at );
	// The playhead moved, or something else drawn changed.
	void Redraw();
	// The picture as it would be drawn now, in software (tests, and comparisons).
	[[nodiscard]] QImage RenderNow( QSize size );
	[[nodiscard]] bool IsUsingGpu() const { return m_pGpu != nullptr; }
	[[nodiscard]] MapFrame_t MakeFrame( QSize size );
	void PaintLabels( QPainter &painter );

signals:
	void StatusChanged();
	void CameraModeChanged();
	void FollowedChanged( int nEntity );

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
	void SetStatus( const QString &szStatus );
	void MapLoaded();
	void ResourcesArrived();
	void WorkerLoop();
	// The free camera moved by the keys held, over the time since the last step.
	void Fly();
	// The camera becomes the hand's, from where it is now.
	void TakeCamera();
	// Somewhere to look at the players from, when nothing else places the camera.
	void PlaceCamera();
	[[nodiscard]] std::shared_ptr<const xv::map::CMapMesh> BuildPlayerMesh( const std::vector<xv::demo::PlayerState_t> &players, std::uint32_t nMaterial ) const;
	[[nodiscard]] xv::map::CViewCamera SizedCamera();

	CDemoViewer &m_Viewer;
	CMapDocument *m_pDocument = nullptr;
	std::unique_ptr<CMapRenderData> m_pRenderData;
	CMapResources *m_pResources = nullptr;
	CGpuView *m_pGpu = nullptr;
	CDemoWorldOverlay *m_pOverlay = nullptr;
	QString m_szStatus;
	std::optional<xv::vfs::CGameInfo> m_Game;

	// The map read back on a thread of its own; a newer load, or Stop, leaves its result unused.
	std::thread m_Decompiler;
	std::shared_ptr<int> m_pLoad;

	xv::map::CViewCamera m_Camera;
	DemoCamera_t m_nMode = DemoCamera_t::DEMO_CAMERA_FREE;
	int m_nFollowed = 0;
	double m_flChaseDistance = 160.0;
	bool m_bPlaced = false; // the free camera has been put somewhere
	bool m_bNames = true;
	bool m_bThroughWalls = false;

	// The players' material after the map's own, made again when those change.
	std::shared_ptr<const std::vector<xv::map::RasterMaterial_t>> m_pMaterials;
	const void *m_pMaterialsFor = nullptr;

	// Software drawing: one frame at a time on a thread, always the newest asked for.
	std::thread m_Worker;
	std::mutex m_Mutex;
	std::condition_variable m_Wake;
	std::optional<MapFrame_t> m_Pending;
	bool m_bQuit = false;
	QImage m_Image;

	// Flying.
	QTimer *m_pFly = nullptr;
	QElapsedTimer m_FlyClock;
	std::vector<int> m_Held; // keys
	QPoint m_LastMouse;
	QPoint m_PressAt;
	bool m_bDragged = false;
};
