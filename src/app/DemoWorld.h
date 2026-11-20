//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The demo viewer's 3D view: the demo's map as the game draws it (the .bsp's own faces
//          and the light the compiler baked; its entities, props and overlays read back as the
//          map viewer reads a .bsp), with the players standing where the demo has them at the
//          playhead, each a figure in its team's colour with its name over it. The map's
//          moving parts (brush entities, props) are drawn from the demo's entities, where the
//          demo has them and only while it draws them. The camera flies free, is the
//          recorder's own, or follows a player (through its eyes, or in third person: around it,
//          turned by dragging, nearer or further by the wheel).
//
//===========================================================================//
#pragma once

class CLoadingView;

#include <array>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include "bsp/Faces.h"
#include "demo/Contents.h"
#include "demo/Logic.h"
#include "demo/Players.h"
#include "demo/Sky.h"
#include "demo/Sprays.h"
#include "DemoFigures.h"
#include "PointerLock.h"
#include "map/MapCompiled.h"
#include "map/MapParticles.h"
#include "map/MapRenderData.h"
#include "map/ViewCamera.h"
#include "vfs/Vfs.h"

class CDemoViewer;
class CDemoWorld;
class CGpuView;
class CMapDocument;
class CMapResources;
class QTemporaryDir;
class QTimer;

enum class DemoCamera_t
{
	DEMO_CAMERA_FREE,         // flown by hand
	DEMO_CAMERA_RECORDER,     // what the recorder saw (a player's own demo)
	DEMO_CAMERA_FIRST_PERSON, // through the followed player's eyes
	DEMO_CAMERA_CHASE,        // third person: around the followed player, turned by hand
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
	// The map's fog (its env_fog_controller): off sees across a big map.
	[[nodiscard]] bool IsShowingFog() const { return m_bFog; }
	void SetShowingFog( bool bFog );
	// The 3D skybox (off: the 2D sky alone behind the map).
	[[nodiscard]] bool IsShowingSkybox3D() const { return m_bSkybox3D; }
	void SetShowingSkybox3D( bool bSkybox );
	// Every face at full light, without the map's lightmaps (for dark maps).
	[[nodiscard]] bool IsFullbright() const { return m_bFullbright; }
	void SetFullbright( bool bFullbright );
	// The demo's particle effects (info_particle_system, dispatched effects).
	[[nodiscard]] bool IsShowingParticles() const { return m_bParticlesOn; }
	void SetShowingParticles( bool bParticles );

	// The camera as it is at the playhead, in whatever mode.
	[[nodiscard]] xv::map::CViewCamera GetCamera();
	void SetCamera( const xv::map::CViewCamera &camera );
	// The free camera put where it sees the players there at the playhead.
	void LookAtPlayers();
	// The players drawn at the playhead: the living ones on a team.
	[[nodiscard]] std::vector<xv::demo::PlayerState_t> GetPlayersShown() const;
	// The player whose figure a widget point is on, nearest first; 0 for none.
	[[nodiscard]] int PlayerAt( QPointF at );
	// Third person: the camera put behind the player, looking where it looks now.
	void ResetOrbit();
	// The player whose HUD shows over the picture in the current camera: the one seen
	// through or followed, else (free) the followed one or the recorder; 0 for none.
	[[nodiscard]] int GetHudPlayer() const;
	// The player whose ears the sound is heard with (seen through: its own sounds are its
	// own); 0 for none.
	[[nodiscard]] int GetEarPlayer() const;
	// The playhead moved, or something else drawn changed.
	void Redraw();
	// The picture as it would be drawn now, in software (tests, and comparisons).
	[[nodiscard]] QImage RenderNow( QSize size );
	[[nodiscard]] bool IsUsingGpu() const { return m_pGpu != nullptr; }
	[[nodiscard]] MapFrame_t MakeFrame( QSize size );
	// The demo's fog and sky camera over the map's, in a frame made for camera.
	void ApplyDemoSky( MapFrame_t &frame, const xv::map::CViewCamera &camera );
	// The demo's particle systems at the playhead, stepped to it; their mesh for camera (its
	// materials from nFirstMaterial on: the map's come first), null when none draws.
	[[nodiscard]] std::shared_ptr<const xv::map::CMapMesh> StepParticles( const xv::map::CViewCamera &camera, std::uint32_t nFirstMaterial );
	// For tests: the particle systems playing.
	[[nodiscard]] const CMapParticles &GetParticles() const { return *m_pParticles; }
	void PaintLabels( QPainter &painter );
	// Whether the map is drawn from its own faces and light (else from its brushes read back).
	[[nodiscard]] bool IsDrawingCompiled() const { return m_pCompiled && m_pCompiled->IsBuilt(); }
	[[nodiscard]] const xv::bsp::CompiledMap_t *GetCompiled() const { return m_pCompiled ? &m_pCompiled->GetMap() : nullptr; }
	// The map's brush models and props drawn at the playhead, as (entity, model name), the
	// demo's entity number or, without a demo's entities, 0.
	[[nodiscard]] std::vector<std::pair<int, std::string>> GetPlacedModels();
	// The players drawn as their models in the last frame: what each wore, held and did.
	// What the guessing of the map's logic reads of the map (its entity lump, its brush
	// models' boxes); empty until the map is read.
	[[nodiscard]] const xv::demo::LogicMap_t &GetLogicMap() const { return m_LogicMap; }
	// The game's files as the map's tab reads them: its packed files first (a ZE map's music
	// is often only there); null until the map is read, or without a game.
	[[nodiscard]] std::shared_ptr<const xv::vfs::CFileSystem> GetMapFiles() const { return m_pMapFiles; }
	// Things marked in the view: a demo entity (followed as it moves; its brush model's box,
	// else a small box at its place) or a box of the map's, each in a colour with a label.
	struct Highlight_t
	{
		int m_nEntity = 0;
		std::optional<xv::map::Box_t> m_Box;
		QColor m_Color;
		QString m_szLabel;
	};
	void SetHighlights( std::vector<Highlight_t> highlights );
	[[nodiscard]] const std::vector<Highlight_t> &GetHighlights() const { return m_Highlights; }
	struct DrawnFigure_t
	{
		int m_nEntity = 0;
		std::string m_szModel, m_szWeapon, m_szLegs, m_szUpper;
	};
	[[nodiscard]] const std::vector<DrawnFigure_t> &GetLastFigures() const { return m_LastFigures; }

signals:
	void StatusChanged();
	void CameraModeChanged();
	void FollowedChanged( int nEntity );
	// The map's entities and models are read: the logic can be guessed at.
	void LogicMapRead();
	// GetMapFiles() has the map's files.
	void MapFilesMounted();

public:
	// Only the world the camera's cluster may see is drawn (the map's visibility, as the game
	// does); off, all of it.
	[[nodiscard]] bool IsUsingVisibility() const { return m_bVisibility; }
	void SetUsingVisibility( bool bUse );
	// Players' sprays on the walls.
	[[nodiscard]] bool IsShowingSprays() const { return m_bSprays; }
	void SetShowingSprays( bool bShow );
	[[nodiscard]] int GetSpraysDrawn() const { return m_nSpraysDrawn; }
	// The map's env_sprites (glows) as the demo has them.
	[[nodiscard]] bool IsShowingEntitySprites() const { return m_bEntitySprites; }
	void SetShowingEntitySprites( bool bShow );
	[[nodiscard]] bool IsShowingViewModel() const { return m_bViewModel; }
	void SetShowingViewModel( bool bShow );
	[[nodiscard]] QImage RenderViewModel( const xv::map::CViewCamera &worldCamera );
	[[nodiscard]] int GetEntitySpritesDrawn() const { return m_nEntitySpritesDrawn; }
	// The props posed in their sequences last frame (for tests).
	[[nodiscard]] int GetPropsPosed() const { return m_nPropsPosed; }
	// The world's triangles the last frame drew (for tests).
	[[nodiscard]] std::size_t GetWorldTrianglesDrawn() const { return m_pCompiled ? m_pCompiled->GetWorldTriangles() : 0; }
	// The scoreboard over the picture, as Tab held in the game shows it.
	[[nodiscard]] bool IsShowingScoreboard() const { return m_bScoreboard; }
	void SetShowingScoreboard( bool bShow );

protected:
	bool event( QEvent *pEvent ) override;
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
	// A drag's turn of the camera, by the pointer's movement in widget pixels.
	void Turn( QPointF delta );
	void CapturePointer();
	void ReleasePointer();
	// Somewhere to look at the players from, when nothing else places the camera.
	void PlaceCamera();
	[[nodiscard]] std::shared_ptr<const xv::map::CMapMesh> BuildPlayerMesh( const std::vector<xv::demo::PlayerState_t> &players, std::uint32_t nMaterial ) const;
	// The compiled faces on the scene's materials, and the map's own entities the demo draws
	// instead (with the props' templates) to hide.
	void BuildCompiledMeshes();
	// The brush models and props at the playhead, with their light.
	void PlaceEntities( std::vector<CMapCompiled::Drawn_t> &drawn );
	// The players' sprays up at the playhead as quads on the surfaces sprayed, each with its
	// image (a material after the particles'); null when there are none.
	[[nodiscard]] std::shared_ptr<const xv::map::CMapMesh> BuildSprayMesh( std::uint32_t nFirstMaterial );
	// The demo's env_sprites (glows, a point_spotlight's halo) at the playhead, each a square
	// facing the camera in its colour and brightness; null when none are up.
	[[nodiscard]] std::shared_ptr<const xv::map::CMapMesh> BuildEntitySpriteMesh( const xv::map::CViewCamera &camera, std::uint32_t nFirstMaterial );
	// A sprite material ("sprites/glow01") among the effects' materials, loaded the first time.
	[[nodiscard]] std::optional<std::uint32_t> SpriteMaterial( const std::string &szMaterial, bool bAdditive );
	// A logo's material among the sprays', loaded the first time; nullopt when its file isn't there.
	[[nodiscard]] std::optional<std::uint32_t> SprayMaterial( std::uint32_t nLogo );
	// An animating prop's cycle in its sequence at a tick.
	[[nodiscard]] double PropCycle( const xv::demo::EntityLife_t &life, const std::string &szModel, int nSequence, double flAt ) const;
	// The players as their models, posed and lit; the players drawn so.
	[[nodiscard]] std::set<int> PlaceFigures( std::vector<CMapCompiled::Drawn_t> &drawn );
	[[nodiscard]] const xv::demo::EntityLife_t *HandleLife( std::uint32_t nHandle, int nTick ) const;
	// The shots of the last moment as tracers, and where players died lately (C2).
	void AddShots( MapFrame_t &frame );
	// Where an entity of the demo is at a tick: its place and angles, after its parents'.
	[[nodiscard]] std::optional<xv::map::Transform_t> EntityTransform( const xv::demo::EntityLife_t &life, double flTick, int nDepth = 0 ) const;
	void ExposureMeasured( float flExposure );
	void PaintHighlights( QPainter &painter, const xv::map::CViewCamera &camera );
	[[nodiscard]] xv::map::CViewCamera SizedCamera();

	CDemoViewer &m_Viewer;
	CMapDocument *m_pDocument = nullptr;
	std::unique_ptr<CMapRenderData> m_pRenderData;
	CMapResources *m_pResources = nullptr;
	CGpuView *m_pGpu = nullptr;
	CDemoWorldOverlay *m_pOverlay = nullptr;
	CLoadingView *m_pLoading = nullptr;
	QString m_szStatus;
	std::optional<xv::vfs::CGameInfo> m_Game;

	// The map read back on a thread of its own; a newer load, or Stop, leaves its result unused.
	std::thread m_Decompiler;
	std::shared_ptr<int> m_pLoad;

	xv::map::CViewCamera m_Camera;
	DemoCamera_t m_nMode = DemoCamera_t::DEMO_CAMERA_FREE;
	int m_nFollowed = 0;
	double m_flChaseDistance = 160.0;
	// Third person: the way the camera looks at the player, turned by hand only.
	double m_flOrbitYaw = 0.0;
	double m_flOrbitPitch = 15.0;
	bool m_bPlaced = false; // the free camera has been put somewhere
	bool m_bNames = true;
	bool m_bThroughWalls = false;
	bool m_bFog = true;
	bool m_bSkybox3D = true;
	bool m_bFullbright = false;
	bool m_bParticlesOn = true;
	bool m_bVisibility = true;
	bool m_bSprays = true;
	std::unique_ptr<xv::demo::CSprays> m_pSprays;
	std::map<std::uint32_t, std::optional<std::uint32_t>> m_SprayMaterialOf; // by logo
	std::vector<std::shared_ptr<xv::map::RasterTexture_t>> m_SprayTextures;
	// The sprays' and the env_sprites' materials (after the particles' in the frame's table).
	std::shared_ptr<std::vector<xv::map::RasterMaterial_t>> m_pSprayMaterials = std::make_shared<std::vector<xv::map::RasterMaterial_t>>();
	std::map<std::pair<std::string, bool>, std::optional<std::uint32_t>> m_SpriteMaterialOf;
	bool m_bEntitySprites = false;
	bool m_bViewModel = true;
	int m_nEntitySpritesDrawn = 0;
	const void *m_pSprayMaterialsFor = nullptr;
	int m_nSpraysDrawn = 0;
	int m_nPropsPosed = 0;
	xv::demo::LogicMap_t m_LogicMap;
	// Keeps a BSP opened from another archive available for its pakfile mount.
	std::shared_ptr<QTemporaryDir> m_pMapSnapshot;
	std::shared_ptr<const xv::vfs::CFileSystem> m_pMapFiles;
	std::vector<Highlight_t> m_Highlights;

	// The map's own faces and light, drawn in place of the brushes read back from it.
	std::unique_ptr<CMapCompiled> m_pCompiled;
	// The weapon in the first-person pass, as recorded or reconstructed.
	enum class ViewModelAction_t
	{
		VIEW_MODEL_ACTION_IDLE,
		VIEW_MODEL_ACTION_DRAW,
		VIEW_MODEL_ACTION_FIRE,
		VIEW_MODEL_ACTION_RELOAD
	};
	struct ViewModelPose_t
	{
		std::string m_szModel;
		int m_nSkin = 0;
		int m_nSequence = -1;
		double m_flCycle = 0.0;
	};
	void IndexViewModels( std::set<std::pair<std::string, int>> &templates, std::set<std::string> &studios );
	[[nodiscard]] ViewModelPose_t ViewModelAt( int nPlayer, double flTick ) const;
	[[nodiscard]] const CMapCompiled::ModelLight_t &FigureLightAt( int nPlayer, xv::CVec3 vecOrigin );
	std::vector<const xv::demo::EntityLife_t *> m_ViewModels;
	std::map<int, std::vector<std::pair<int, ViewModelAction_t>>> m_WeaponActions; // per user ID
	std::map<xv::map::PropKey_t, std::shared_ptr<const xv::map::CMapMesh>> m_ViewModelDefaults;
	CDemoFigures m_Figures; // the players' models, posed
	std::vector<DrawnFigure_t> m_LastFigures;
	// Each player's light, and the cell where it was looked up.
	std::map<int, std::pair<std::array<int, 3>, CMapCompiled::ModelLight_t>> m_FigureLight;
	// Each player's mesh last frame and its light (and the cell that light was for).
	std::map<int, std::pair<std::shared_ptr<const xv::map::CMapMesh>, std::shared_ptr<const std::vector<xv::map::LightVertex_t>>>> m_FigureLit;
	std::map<int, std::array<int, 3>> m_FigureLitCell;
	// Where each shot (by its index among the temp entities) met the map.
	std::map<std::size_t, xv::map::Point_t> m_ShotEnds;
	static constexpr int SHOT_CLASS_UNKNOWN = -2; // not looked for yet
	static constexpr int SHOT_CLASS_NONE = -1;    // the demo has no shots
	int m_nShotClass = SHOT_CLASS_UNKNOWN;
	// Each model the demo's props use, by lower-case name and skin: the object of the entity
	// put at the origin for it, whose mesh the props are placed copies of.
	std::map<std::pair<std::string, int>, std::uint32_t> m_PropTemplates;
	std::vector<std::uint32_t> m_HiddenObjects; // the map's own entities the demo draws, its solids, and the templates
	// What was placed last frame, kept while it stays put (the card keeps its upload).
	struct Placed_t
	{
		xv::map::Transform_t m_Transform;
		std::shared_ptr<const xv::map::CMapMesh> m_pMesh;
		std::shared_ptr<const std::vector<xv::map::LightVertex_t>> m_pLights;
		std::shared_ptr<const xv::map::CMapMesh> m_pSource; // what was placed (a prop's pose)
	};
	std::map<std::pair<const void *, int>, Placed_t> m_Placed;
	float m_flExposure = 1.0f; // HDR's auto exposure, as last measured
	std::vector<std::pair<int, std::string>> m_LastPlaced; // GetPlacedModels

	// The players' material after the map's own, made again when those change.
	std::shared_ptr<const std::vector<xv::map::RasterMaterial_t>> m_pMaterials; // the map's, the particles', the figures'
	const void *m_pMaterialsFor = nullptr;
	const void *m_pParticleMaterialsFor = nullptr;
	// The demo's particle systems (H7), and the playhead they were last stepped to.
	CMapParticles *m_pParticles = nullptr;
	double m_flParticlesTick = -1.0;

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
	bool m_bLooking = false;    // a drag has the pointer
	bool m_bScoreboard = false; // Tab is held
	CPointerLock m_PointerLock; // Wayland: pins the pointer meanwhile
};
