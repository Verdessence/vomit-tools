//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The demo viewer (a first look): a Source demo (.dem) in a tab. What its header
//          says, a timeline of how much the server sent over the demo's length (click or drag
//          to move through it, play to run it at the demo's own speed), the recorder's camera
//          path seen from above, the console commands recorded, and its frames by kind;
//          and its world (DemoWorld.h): the map in 3D with the players where the demo has
//          them at the playhead, beside a list of them. Nothing is edited.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <QElapsedTimer>
#include <QPolygonF>
#include <QWidget>

#include "EditorTab.h"
#include "demo/Demo.h"
#include "demo/Players.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QTimer;
class CDemoViewer;
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

// The camera's path from above, fitted to the widget: what has played so far drawn strong,
// the rest faint, the camera as a dot with the way it looks.
class CDemoPathView : public QWidget
{
	Q_OBJECT
public:
	explicit CDemoPathView( CDemoViewer &viewer, QWidget *pParent = nullptr );
	// A place in the map to widget pixels (x right, y up, as Hammer's top view).
	[[nodiscard]] QPointF ToWidget( const xv::CVec3 &vecAt ) const;

protected:
	void paintEvent( QPaintEvent *pEvent ) override;
	void resizeEvent( QResizeEvent *pEvent ) override;

private:
	// One stretch of the path without a jump in it (a respawn or a teleport breaks it), in
	// widget pixels, thinned to what the widget's size can show.
	struct Stretch_t
	{
		QPolygonF m_Points;
		std::vector<int> m_Ticks; // each point's
	};
	void BuildStretches();

	CDemoViewer &m_Viewer;
	xv::CVec3 m_vecMins;
	xv::CVec3 m_vecMaxs;
	std::vector<Stretch_t> m_Stretches;
};

class CDemoViewer : public QWidget, public IEditorTab
{
	Q_OBJECT
public:
	// szName: the game path or file name shown; disk: the file on disk, empty for a game file.
	// players: the demo's players, followed through its packets (empty when they weren't).
	CDemoViewer( const QString &szName, xv::demo::CDemo demo, const std::filesystem::path &disk, bool bFromGame, xv::demo::CPlayers players = {}, QWidget *pParent = nullptr );

	[[nodiscard]] const xv::demo::CDemo &GetDemo() const { return m_Demo; }
	[[nodiscard]] const xv::demo::CPlayers &GetPlayers() const { return m_Players; }
	// The 3D view of the demo's map and players.
	[[nodiscard]] CDemoWorld &GetWorld() { return *m_pWorld; }
	// What it was opened as: the path on disk, or the game's.
	[[nodiscard]] const QString &GetName() const { return m_szName; }

	// The playhead, kept within the demo's ticks.
	[[nodiscard]] int GetTick() const { return m_nTick; }
	void SetTick( int nTick );
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

	[[nodiscard]] QString GetTabTitle() const override;
	[[nodiscard]] QString GetSessionEntry() const override;
	[[nodiscard]] bool IsFromGame() const override { return m_bFromGame; }
	void StopWork() override;

signals:
	void TickChanged( int nTick );

protected:
	void keyPressEvent( QKeyEvent *pEvent ) override;

private:
	[[nodiscard]] QWidget *BuildSummary();
	[[nodiscard]] QWidget *BuildCommands();
	[[nodiscard]] QWidget *BuildFrameKinds();
	[[nodiscard]] QWidget *BuildWorld();
	// The player list's rows as the players are at the playhead.
	void UpdatePlayerList();
	void UpdateCameraControls();
	void Advance();
	void UpdateTimeLabel();

	xv::demo::CDemo m_Demo;
	xv::demo::CPlayers m_Players;
	QString m_szName;
	std::filesystem::path m_Disk;
	bool m_bFromGame = false;
	int m_nTick = 0;
	// Playing: where the playhead was, in ticks and fractions, when the clock last read.
	double m_flPlayTick = 0.0;
	QElapsedTimer m_PlayClock;
	QTimer *m_pPlayTimer = nullptr;
	CDemoTimeline *m_pTimeline = nullptr;
	CDemoPathView *m_pPath = nullptr;
	QTabWidget *m_pPages = nullptr;
	QTableWidget *m_pCommands = nullptr;
	CDemoWorld *m_pWorld = nullptr;
	QTableWidget *m_pPlayerList = nullptr; // a row for each of m_Players' tracks, in their order
	QComboBox *m_pCamera = nullptr;
	QElapsedTimer m_ListClock; // the list follows a playing demo a few times a second
	QPushButton *m_pPlay = nullptr;
	QComboBox *m_pSpeed = nullptr;
	QLabel *m_pTime = nullptr;
};
