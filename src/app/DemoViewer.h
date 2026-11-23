//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The demo viewer (a first look): a Source demo (.dem) in a tab. What its header
//          says, a timeline of how much the server sent over the demo's length (click or drag
//          to move through it, play to run it at the demo's own speed), the recorder's camera
//          path seen from above, the console commands recorded, and its frames by kind;
//          and its world (DemoWorld.h): the map in 3D with the players where the demo has
//          them at the playhead, beside the scoreboard and logs of what the demo put on
//          screens (chat, the kill feed, HUD messages, hints, menus), and through a player's
//          eyes its HUD over the picture (DemoHud.h). Nothing is edited.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include <QElapsedTimer>
#include <QImage>
#include <QPolygonF>
#include <QTransform>
#include <QWidget>

#include "DemoHud.h"
#include "DemoSound.h"
#include "EditorTab.h"
#include "demo/Demo.h"
#include "demo/Contents.h"
#include "demo/Feed.h"
#include "demo/Logic.h"
#include "demo/Players.h"
#include "map/Geometry.h"

class QCheckBox;
class QFormLayout;
class QComboBox;
class QLabel;
class QPushButton;
class QSplitter;
class QTabWidget;
class QTableWidget;
class QTimer;
class QToolButton;
class QWheelEvent;
class CDemoLog;
class CDemoViewer;
class CDemoVoice;
class CDemoWorld;

// The demo's length left to right: the packets' bytes as bars, the console commands as marks,
// and the playhead.
class CDemoTimeline : public QWidget
{
	Q_OBJECT
public:
	explicit CDemoTimeline( CDemoViewer &viewer, QWidget *pParent = nullptr );
	// Widget pixels across to ticks and back.
	[[nodiscard]] int TickAt( int x ) const;
	[[nodiscard]] int XAt( int nTick ) const;
	[[nodiscard]] QSize sizeHint() const override;

protected:
	void paintEvent( QPaintEvent *pEvent ) override;
	void resizeEvent( QResizeEvent *pEvent ) override;
	void mousePressEvent( QMouseEvent *pEvent ) override;
	void mouseMoveEvent( QMouseEvent *pEvent ) override;

private:
	CDemoViewer &m_Viewer;
	std::vector<std::uint64_t> m_Bars; // one for each pixel across
};

// Where things went, from above, fitted to the widget: the recorder's camera path, every
// player's path (in its team's colour, what has played strong, the rest faint, the players
// where they are now), or a heatmap of where players were or died, over the map's floors
// (the compiled faces that face up, lighter the higher) once the map is in.
enum class DemoPathMode_t
{
	DEMO_PATH_CAMERA,  // the recorder's camera
	DEMO_PATH_PLAYERS, // every player's
	DEMO_PATH_PRESENCE, // heatmap: where living players spent their time
	DEMO_PATH_DEATHS,   // heatmap: where players died
};

// How the paths page looks at the map: straight down (Hammer's top view), or from above at a
// slant (isometric, turned by hand), both without perspective.
enum class DemoPathView_t
{
	DEMO_PATH_VIEW_TOP,
	DEMO_PATH_VIEW_ISOMETRIC,
};

// The paths page's cut: on each axis (x, y, z) that is on, only what lies between its two
// values is drawn (the floors cut there, the paths and heat inside).
struct PathCut_t
{
	std::array<bool, 3> m_bOn{};
	std::array<double, 3> m_flMin{};
	std::array<double, 3> m_flMax{};
};

// What the paths view's picture (Save Image…) holds.
struct DemoPathPicture_t
{
	double m_flScale = 2.0;     // times the view's size
	bool m_bMap = true;         // the radar or floors under the paths
	bool m_bTransparent = false; // without the map: no background
	bool m_bWholeDemo = false;  // every path strong, not only what played by the playhead
	bool m_bPlayers = true;     // the players where they are at the playhead
};

class CDemoPathView : public QWidget
{
	Q_OBJECT
public:
	explicit CDemoPathView( CDemoViewer &viewer, QWidget *pParent = nullptr );
	// Waits for paths being drawn on a worker (they read the viewer's players).
	void StopWork();
	// For tests: waits for the paths being drawn on a worker, and takes them.
	void WaitForPaths();
	// The view as a picture, the paths drawn again at its size.
	[[nodiscard]] QImage RenderPicture( const DemoPathPicture_t &options );
	// A place in the map to widget pixels (top: x right, y up, as Hammer's top view);
	// pDepth gets how far into the picture it is (larger: further).
	[[nodiscard]] QPointF ToWidget( const xv::CVec3 &vecAt, double *pDepth = nullptr ) const;
	[[nodiscard]] DemoPathView_t GetView() const { return m_nView; }
	void SetView( DemoPathView_t nView );
	// The isometric view's turn about the vertical, in degrees.
	[[nodiscard]] double GetYaw() const { return m_flYaw; }
	void SetYaw( double flYaw );
	// Back to the whole of the paths, fitted.
	void ResetView();
	[[nodiscard]] const PathCut_t &GetCut() const { return m_Cut; }
	// The heat follows at once; the map's floors a moment later, at most every CUT_INTERVAL
	// while the values keep changing (a spin box held, the wheel), so dragging stays smooth.
	void SetCut( const PathCut_t &cut );
	void SetCutGrid( int nAxis, bool bShow );
	// Whether the map is drawn under the paths.
	[[nodiscard]] bool IsShowingMap() const { return m_bShowMap; }
	void SetShowingMap( bool bShow );
	void SetPreferRadar( bool bPrefer );
	// The places the paths cover, for a cut's first values.
	[[nodiscard]] const xv::CVec3 &GetMins() const { return m_vecMins; }
	[[nodiscard]] const xv::CVec3 &GetMaxs() const { return m_vecMaxs; }
	[[nodiscard]] bool IsInCut( const xv::CVec3 &vecAt ) const;
	[[nodiscard]] static std::vector<xv::map::Point_t> ClipPolygon( std::vector<xv::map::Point_t> corners, int nAxis, double flAt, bool bKeepAbove );
	[[nodiscard]] DemoPathMode_t GetMode() const { return m_nMode; }
	void SetMode( DemoPathMode_t nMode );
	// Heatmaps of the demo up to the playhead only (else the whole of it).
	void SetUpToPlayhead( bool bUpTo );
	// Only one team's players (0: all).
	void SetTeam( int nTeam );
	// The heatmap's cells as last counted (for tests): seconds or deaths in each.
	[[nodiscard]] const std::vector<float> &GetHeat() const { return m_Heat; }
	// How far a heatmap's spots spread, in world units (the blur's width): small for fine
	// detail, large to see the broad picture.
	[[nodiscard]] float GetHeatSpread() const { return m_flHeatSpread; }
	void SetHeatSpread( float flUnits );
	// The map's floors are in: drawn under the paths.
	void MapArrived();
	void LoadRadar();
	[[nodiscard]] bool IsShowingRadar() const { return m_bShowMap && HasRadarBackground(); }

signals:
	void CutHandleMoved( int nAxis, bool bUpper, int nValue );
	void ViewModeChanged( DemoPathView_t nView );

protected:
	void paintEvent( QPaintEvent *pEvent ) override;
	void resizeEvent( QResizeEvent *pEvent ) override;
	void mousePressEvent( QMouseEvent *pEvent ) override;
	void mouseMoveEvent( QMouseEvent *pEvent ) override;
	void mouseReleaseEvent( QMouseEvent *pEvent ) override;
	void mouseDoubleClickEvent( QMouseEvent *pEvent ) override;
	void wheelEvent( QWheelEvent *pEvent ) override;

private:
	// The projection: the map turned (yaw, then tilted to the pitch), scaled and moved.
	struct Projection_t
	{
		double m_flYaw = 0.0;
		double m_flPitch = 90.0; // the view's slant above the horizon: 90 straight down
		double m_flScale = 1.0;  // pixels a unit
		double m_flCosYaw = 1.0, m_flSinYaw = 0.0;
		double m_flCosPitch = 0.0, m_flSinPitch = 1.0;
		QPointF m_Offset;        // where the bounds' middle lands
		[[nodiscard]] bool SameTurn( const Projection_t &other ) const { return m_flYaw == other.m_flYaw && m_flPitch == other.m_flPitch; }
	};
	// The fit for the widget's size and the view now (kept until either changes).
	void UpdateProjection() const;
	// The heat, paths and floors are drawn again on their own timers; until then the last
	// pictures are warped to the new view (Reprojection).
	void ViewChanged();
	// Takes what was drawn for another projection to where the view puts it now: exact for a
	// pan or zoom, and for a turn exact at the middle height (near enough for a moment).
	[[nodiscard]] QTransform Reprojection( const Projection_t &from ) const;
	[[nodiscard]] QPointF Project( const Projection_t &projection, double x, double y, double z, double *pDepth = nullptr ) const;
	[[nodiscard]] QPointF Facing( const xv::CVec3 &vecAt, double flYaw, double flLength ) const;
	// One stretch of a path without a jump in it (a respawn or a teleport breaks it), in
	// widget pixels, thinned to what the widget's size can show.
	struct Stretch_t
	{
		QPolygonF m_Points;
		std::vector<int> m_Ticks; // each point's
		QColor m_Color;
	};
	void FitBounds();
	// What drawing the paths needs, copied, so a worker can draw them while the view moves on.
	struct PathInputs_t
	{
		DemoPathMode_t m_nMode = DemoPathMode_t::DEMO_PATH_PLAYERS;
		int m_nTeam = 0;
		Projection_t m_Projection;
		xv::CVec3 m_vecCentre;
		const xv::demo::CPlayers *m_pPlayers = nullptr;
		const std::vector<xv::demo::View_t> *m_pViews = nullptr;
		double m_flTickInterval = 0.015;
		QColor m_Highlight, m_Text;
		QSize m_Pixels;
		double m_flDpr = 1.0;
	};
	// The paths drawn for one projection: faint, strong once played, and each pixel's tick.
	struct PathPictures_t
	{
		std::vector<Stretch_t> m_Stretches;
		QImage m_Faint, m_Strong, m_Ticks;
		Projection_t m_Projection;
		int m_nGeneration = 0;
	};
	[[nodiscard]] static QPointF ProjectAround( const Projection_t &projection, const xv::CVec3 &vecCentre, double x, double y, double z, double *pDepth = nullptr );
	[[nodiscard]] static std::vector<Stretch_t> BuildStretches( const PathInputs_t &inputs );
	[[nodiscard]] static PathPictures_t DrawPaths( const PathInputs_t &inputs );
	// Draws the paths for the view now: at once (bNow), else on a worker while the last
	// pictures are warped to the view.
	void StartPaths( bool bNow );
	void TakePaths( PathPictures_t pictures );
	// The paths are drawn again; bKeepShown leaves the last ones up (warped) until they are,
	// for a change of view that doesn't change what they show.
	void InvalidatePaths( bool bKeepShown = false );
	void PaintPaths( QPainter &painter, int nTick );
	void UpdatePlayedPath( int nTick );
	struct CutPlane_t
	{
		int m_nAxis;
		bool m_bUpper;
		double m_flValue;
		QPolygonF m_Outline;
		QPointF m_Handle;
		QPointF m_Direction; // pixels for one world unit along the cut axis
	};
	[[nodiscard]] std::vector<CutPlane_t> CutPlanes() const;
	void PaintCutGrid( QPainter &painter ) const;
	[[nodiscard]] bool HasRadarBackground() const { return m_bPreferRadar && m_nView == DemoPathView_t::DEMO_PATH_VIEW_TOP && !m_Radar.isNull(); }
	void PaintRadar( QPainter &painter ) const;
	void PaintUnder( QPainter &painter, bool bMap );
	void PaintPlayers( QPainter &painter, int nTick ) const;
	void BuildFloor();
	// The heat in the world (where, how much), counted again when what it counts changes.
	void GatherHeat();
	// Gathered again, then drawn.
	void InvalidateHeat();
	// The gathered heat as the view sees it.
	void BuildHeat();

	CDemoViewer &m_Viewer;
	DemoPathMode_t m_nMode = DemoPathMode_t::DEMO_PATH_CAMERA;
	bool m_bUpToPlayhead = false;
	int m_nTeam = 0;
	xv::CVec3 m_vecMins;
	xv::CVec3 m_vecMaxs;
	std::vector<Stretch_t> m_Stretches;
	QImage m_FaintPaths;
	QImage m_PlayedPaths; // m_StrongPaths where m_PathTicks is at or before the playhead
	QImage m_StrongPaths; // every path as it shows once played
	QImage m_PathTicks;   // each pixel's earliest tick a path reaches it, plus one, in its rgb; 0: none
	Projection_t m_PathProjection;
	int m_nPathTick = -1;
	int m_nPathGeneration = 0;  // raised by each change the paths must follow
	bool m_bPathsStale = false; // the pictures shown are for an older view
	std::future<PathPictures_t> m_PathBuild;
	QTimer *m_pPathPoll = nullptr; // looks for the worker's pictures
	qint64 m_nPathPalette = 0;
	QTimer *m_pPathTimer = nullptr;
	QImage m_Radar;
	xv::map::Point_t m_RadarOrigin{};
	double m_flRadarScale = 0.0;
	QImage m_Floor;     // the map from above, at the widget's size
	Projection_t m_FloorProjection; // what m_Floor was drawn with
	QImage m_HeatImage; // the heatmap, a pixel a cell
	Projection_t m_HeatProjection; // what m_HeatImage was drawn with
	double m_flHeatCell = 2.0;      // its cells' size, in widget pixels
	float m_flHeatSpread = 48.0f;
	QTimer *m_pHeatTimer = nullptr;
	DemoPathView_t m_nView = DemoPathView_t::DEMO_PATH_VIEW_TOP;
	double m_flYaw = 45.0; // the isometric view's
	double m_flZoom = 1.0; // times the fitted scale
	PathCut_t m_Cut;
	std::array<bool, 3> m_CutGrids{};
	std::optional<CutPlane_t> m_CutDrag;
	QPointF m_CutDragStart;
	QPointF m_Pan;         // pixels from the fitted place
	mutable Projection_t m_Projection;
	mutable QSize m_ProjectedFor; // the size m_Projection fits
	QTimer *m_pFloorTimer = nullptr;
	QTimer *m_pCutTimer = nullptr;
	bool m_bShowMap = true;
	bool m_bPreferRadar = true;
	// A drag: pans (left, middle), turns the isometric view (right).
	QPoint m_DragFrom;
	Qt::MouseButton m_nDragButton = Qt::NoButton;
	std::vector<float> m_Heat;
	// The heat in world space: each bin's weighted middle and its seconds (or deaths).
	struct HeatPoint_t
	{
		xv::CVec3 m_vecAt;
		float m_flAmount = 0.0f;
	};
	std::vector<HeatPoint_t> m_HeatPoints;
	bool m_bHeatGathered = false;
	int m_nHeatTick = -1; // the playhead the heat was counted to (up to the playhead)
};

class CDemoViewer : public QWidget, public IEditorTab
{
	Q_OBJECT
public:
	// szName: the game path or file name shown; disk: the file on disk, empty for a game file.
	// players: the demo's players, followed through its packets (empty when they weren't).
	CDemoViewer( const QString &szName, xv::demo::CDemo demo, const std::filesystem::path &disk, bool bFromGame, xv::demo::CPlayers players = {}, std::shared_ptr<const xv::demo::CContents> pContents = {}, QWidget *pParent = nullptr );
	~CDemoViewer() override;

	[[nodiscard]] const xv::demo::CDemo &GetDemo() const { return m_Demo; }
	[[nodiscard]] const xv::demo::CPlayers &GetPlayers() const { return m_Players; }
	// Everything else the packets hold (the entities, events, messages); null when not read.
	[[nodiscard]] const xv::demo::CContents *GetContents() const { return m_pContents.get(); }
	[[nodiscard]] std::shared_ptr<const xv::demo::CContents> GetContentsShared() const { return m_pContents; }
	// The Voice tab: every stretch of talking, to hear, save and put in words.
	[[nodiscard]] CDemoVoice *GetVoice() const { return m_pVoice; }
	// The side's pages ("summary", "players", "chat", "kills", "messages", "commands", "voice",
	// "logic"): which part each is in (0 top, 1 middle, 2 bottom), moving one (remembered), and
	// bringing one to the front.
	[[nodiscard]] int GetSideGroup( const QString &szKey ) const;
	void MoveSidePage( const QString &szKey, int nGroup );
	void ShowSidePage( const QString &szKey );
	// The 3D view of the demo's map and players.
	[[nodiscard]] CDemoWorld &GetWorld() { return *m_pWorld; }
	// What it was opened as: the path on disk, or the game's.
	[[nodiscard]] const QString &GetName() const { return m_szName; }

	// The playhead, kept within the demo's ticks.
	[[nodiscard]] int GetTick() const { return m_nTick; }
	void SetTick( int nTick );
	bool StepKey( QKeyEvent *pEvent );
	[[nodiscard]] bool GoTo( const QString &szInput );
	[[nodiscard]] QString ScoreboardConnections() const;
	// The playhead with the fraction of a tick it is through while playing: what moves
	// smoothly between ticks is drawn there.
	[[nodiscard]] double GetPlayTick() const;
	// Running from the playhead at the demo's speed times GetSpeed(); stops at the end.
	[[nodiscard]] bool IsPlaying() const;
	void SetPlaying( bool bPlaying );
	[[nodiscard]] double GetSpeed() const;
	void SetSpeed( double flSpeed );
	// "12:34.5" for a tick; ticks alone when the header gives no tick length.
	[[nodiscard]] QString FormatTime( int nTick ) const;

	// A warning under the summary (the map shown isn't the demo's).
	void AddWarning( const QString &szText );
	// Takes the map warnings away (another map is being put under the demo).
	void ClearWarnings();
	// The game's files: the HUD's layout, scheme and icons, and the language the demo's texts
	// are put in words with. Null for none.
	// nAudio: where the demo's sound goes (tests: offline).
	void SetGameFiles( CHudEditor::ReadFile_t pfnRead, xv::audio::CEngine::Output_t nAudio = xv::audio::CEngine::Output_t::OUTPUT_DEVICE );
	// The sounds and the HUD read the map's files (its packed ones first) once they are in.
	void UseMapFiles();
	// The demo's sounds and the map's soundscape, heard from the view's camera while playing.
	[[nodiscard]] bool IsPlayerMuted( int nEntity, int nTick ) const;
	void SetPlayerMuted( int nEntity, int nTick, bool bMuted );
	[[nodiscard]] CDemoSound &GetSound() { return *m_pSound; }
	// What the demo put on screens.
	[[nodiscard]] const xv::demo::CFeed &GetFeed() const { return m_Feed; }
	// The followed player's HUD over the picture (else the panels alone say it).
	[[nodiscard]] bool IsShowingHud() const { return m_bHud; }
	void SetShowingHud( bool bShow );
	[[nodiscard]] bool HasGameHud() const { return m_Hud.HasGameHud(); }
	// The HUD of a player (an entity; 0 for none) at the playhead, over rect; bEyes: seen
	// through its eyes (the crosshair shows).
	void PaintHud( QPainter &painter, const QRect &rect, int nPlayer, bool bEyes = true );
	[[nodiscard]] DemoHudMoment_t HudMoment( int nPlayer, double flTick ) const;
	// The scoreboard (Tab in the view) at the playhead over rect; nFollowed: the player the
	// view is with, marked.
	void PaintScoreboard( QPainter &painter, const QRect &rect, int nFollowed );
	[[nodiscard]] DemoScoreboard_t ScoreboardAt( int nTick, int nFollowed ) const;
	// Who is talking at the playhead.
	[[nodiscard]] std::vector<int> GetSpeakers() const;
	// A player's numbers on the scoreboard at a tick; each nullopt when the demo hasn't it.
	struct Score_t
	{
		std::optional<int> m_nScore, m_nDeaths, m_nPing, m_nArmor, m_nMoney;
	};
	[[nodiscard]] Score_t ScoreOf( int nEntity, int nTick ) const;
	// The round, its clock and the teams' scores at a tick, in words.
	[[nodiscard]] QString RoundText( int nTick ) const;
	// A player's name at a tick, and its team.
	[[nodiscard]] QString PlayerName( int nEntity, int nTick ) const;
	[[nodiscard]] int PlayerTeam( int nEntity, int nTick ) const;
	// The map's logic guessed at (E): each change of a named entity with the outputs that may
	// have made it, once the map is read.
	[[nodiscard]] const std::vector<xv::demo::LogicGuess_t> &GetLogicGuesses() const { return m_Guesses; }
	// Puts the playhead at a guess and marks it in the view: the entity that changed, and
	// what may have set it off.
	void ShowGuess( std::size_t nGuess );
	[[nodiscard]] CDemoLog *GetLogicLog() const { return m_pLogicLog; }
	// The log pages beside the world (for tests): chat, kills, messages.
	[[nodiscard]] CDemoLog *GetChatLog() const { return m_pChatLog; }
	[[nodiscard]] CDemoLog *GetKillLog() const { return m_pKillLog; }
	[[nodiscard]] CDemoLog *GetMessageLog() const { return m_pMessageLog; }

	[[nodiscard]] QString GetTabTitle() const override;
	[[nodiscard]] QString GetSessionEntry() const override;
	[[nodiscard]] bool IsFromGame() const override { return m_bFromGame; }
	void StopWork() override;

signals:
	void TickChanged( int nTick );
	// Map File… was clicked: the window asks which .bsp to show the demo on.
	void MapFileWanted();

protected:
	void keyPressEvent( QKeyEvent *pEvent ) override;
	bool eventFilter( QObject *pWatched, QEvent *pEvent ) override;
	void changeEvent( QEvent *pEvent ) override;

private:
	[[nodiscard]] QWidget *BuildSummary();
	[[nodiscard]] QWidget *BuildCommands();
	[[nodiscard]] QWidget *BuildFrameKinds();
	[[nodiscard]] QWidget *BuildPaths();
	[[nodiscard]] QWidget *BuildGraphs();
	[[nodiscard]] QWidget *BuildPathCut();
	[[nodiscard]] QWidget *BuildMix();
	void ShowMuteSounds();
	[[nodiscard]] QWidget *BuildWorld();
	[[nodiscard]] QWidget *BuildPlayers();
	[[nodiscard]] QWidget *BuildSide();
	[[nodiscard]] QWidget *BuildVoice();
	// The player list's rows as the players are at the playhead.
	void UpdatePlayerList();
	void UpdatePlayerMutes();
	void ShowPlayerMenu( const QPoint &point );
	// The list's rows and the players' tracks, either way round (the list sorts).
	[[nodiscard]] std::size_t TrackOfRow( int nRow ) const;
	[[nodiscard]] int RowOfTrack( std::size_t nTrack ) const;
	// The feed read again (with the language, once the files come) and the logs filled.
	void BuildFeed();
	void BuildLogs();
	void BuildLogic();
	void UpdateLogs();
	void UpdateSound();
	// Seconds left on the round's clock at a tick; nullopt when the demo hasn't the clock.
	[[nodiscard]] std::optional<double> RoundLeft( double flTick ) const;
	void UpdateCameraControls();
	void Advance();
	void UpdateTimeLabel();
	void AskGoTo();
	void IndexConnections();
	[[nodiscard]] std::optional<std::pair<int, bool>> ConnectionTime( const xv::demo::PlayerTrack_t &track, int nTick ) const;
	[[nodiscard]] QString FormatConnection( const std::pair<int, bool> &time ) const;

	xv::demo::CDemo m_Demo;
	xv::demo::CPlayers m_Players;
	std::set<std::pair<int, int>> m_MutedPlayers; // slot and user ID for players without a Steam ID
	std::shared_ptr<const xv::demo::CContents> m_pContents;
	QString m_szName;
	std::filesystem::path m_Disk;
	bool m_bFromGame = false;
	std::vector<int> m_PacketTicks;
	std::optional<std::size_t> m_PacketStep;
	std::map<int, std::vector<std::pair<int, bool>>> m_Connections; // by user ID, then tick and connected state
	int m_nTick = 0;
	// Playing: where the playhead was, in ticks and fractions, when the clock last read.
	double m_flPlayTick = 0.0;
	QElapsedTimer m_PlayClock;
	QTimer *m_pPlayTimer = nullptr;
	CDemoTimeline *m_pTimeline = nullptr;
	CDemoPathView *m_pPath = nullptr;
	QTabWidget *m_pPages = nullptr;
	// The side: three parts (top, middle, bottom), each a tab widget of the pages put there.
	enum
	{
		SIDE_TOP,
		SIDE_MIDDLE,
		SIDE_BOTTOM,
		SIDE_GROUPS
	};
	struct SidePage_t
	{
		const char *m_pszKey;
		QString m_szTitle;
		QWidget *m_pWidget;
		int m_nGroup;
	};
	void FillSideGroups();
	void SetSideCollapsed( int nGroup, bool bCollapsed );
	// Every group's height from what is folded: a folded group its tab bar, the open ones the
	// rest, in their proportions (nOpened, just unfolded, gets back its old height first).
	// The splitter alone would leave a folded group's old room empty under its tab bar.
	void ApplySideSizes( int nOpened = -1 );
	void QueueSideSizes();
	bool m_bSideSizesQueued = false;
	void ShowSideMenu( QTabWidget *pGroup, const QPoint &point );
	QSplitter *m_pSide = nullptr;
	std::array<QTabWidget *, SIDE_GROUPS> m_SideGroups{};
	std::array<int, SIDE_GROUPS> m_SideExpandedHeights{};
	std::array<QToolButton *, SIDE_GROUPS> m_SideButtons{};
	std::vector<SidePage_t> m_SidePages;
	CDemoVoice *m_pVoice = nullptr;
	QTableWidget *m_pCommands = nullptr;
	CDemoWorld *m_pWorld = nullptr;
	QTableWidget *m_pPlayerList = nullptr; // a row for each of m_Players' tracks, in their order
	QComboBox *m_pCamera = nullptr;
	QElapsedTimer m_ListClock; // the list follows a playing demo a few times a second
	QPushButton *m_pPlay = nullptr;
	QComboBox *m_pSpeed = nullptr;
	QPushButton *m_pTime = nullptr;
	xv::demo::CFeed m_Feed;
	CDemoHud m_Hud;
	std::unique_ptr<CDemoSound> m_pSound;
	bool m_bHud = true;
	// SetGameFiles was called: the game is mounted, and its audio output.
	bool m_bGameFiles = false;
	xv::audio::CEngine::Output_t m_nAudio = xv::audio::CEngine::Output_t::OUTPUT_DEVICE;
	CDemoLog *m_pChatLog = nullptr;
	CDemoLog *m_pKillLog = nullptr;
	CDemoLog *m_pMessageLog = nullptr;
	CDemoLog *m_pLogicLog = nullptr;
	std::vector<xv::demo::LogicGuess_t> m_Guesses;
	QLabel *m_pRound = nullptr;
	QFormLayout *m_pSummaryForm = nullptr;
};
