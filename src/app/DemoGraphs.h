//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The demo viewer's graphs over the demo's time: each team's players (and how many
//          of them are alive), every player's score, and the damage each player took.
//
//===========================================================================//
#pragma once

#include <vector>

#include <QPoint>
#include <QString>
#include <QWidget>

class CDemoViewer;

enum class DemoGraph_t
{
	DEMO_GRAPH_TEAMS,  // players on each team, and alive, over time
	DEMO_GRAPH_SCORE,  // each player's score (the scoreboard's) over time
	DEMO_GRAPH_DAMAGE, // the damage each player took, added up, over time
};

// One line of a graph: a value that holds from each point's tick to the next point's (a step),
// NaN where it breaks (the player wasn't in the game).
struct DemoGraphSeries_t
{
	QString m_szName;
	std::vector<int> m_Ticks;
	std::vector<double> m_Values;
	std::vector<int> m_Teams; // each point's team: a player's line takes its team's colour as it goes
	bool m_bDashed = false;   // the teams graph: alive, beside on the team
	bool m_bPlayer = false;   // one player's (shown faint until hovered or picked)

	// The value at a tick; NaN before the first point or in a break.
	[[nodiscard]] double ValueAt( int nTick ) const;
	[[nodiscard]] int TeamAt( int nTick ) const;
	[[nodiscard]] double GetLast() const;
};

class CDemoGraphView : public QWidget
{
	Q_OBJECT
public:
	explicit CDemoGraphView( CDemoViewer &viewer, QWidget *pParent = nullptr );

	[[nodiscard]] DemoGraph_t GetGraph() const { return m_nGraph; }
	void SetGraph( DemoGraph_t nGraph );
	// The per-player graphs show one team's players only (0: all).
	void SetTeam( int nTeam );
	// The graph's lines, counted when first shown (for tests and the CSV copy).
	[[nodiscard]] const std::vector<DemoGraphSeries_t> &GetSeries();
	// The lines as comma-separated values: a row a time a line changed, a column a line.
	[[nodiscard]] QString ToCsv();
	[[nodiscard]] QSize sizeHint() const override { return { 640, 360 }; }

protected:
	void paintEvent( QPaintEvent *pEvent ) override;
	void mouseMoveEvent( QMouseEvent *pEvent ) override;
	void mousePressEvent( QMouseEvent *pEvent ) override;
	void leaveEvent( QEvent *pEvent ) override;
	void contextMenuEvent( QContextMenuEvent *pEvent ) override;
	void changeEvent( QEvent *pEvent ) override;

private:
	void Build();
	void BuildTeams();
	void BuildScores();
	void BuildDamage();
	[[nodiscard]] bool IsShown( const DemoGraphSeries_t &series ) const;
	[[nodiscard]] QRectF PlotRect() const;
	[[nodiscard]] double XAt( int nTick ) const;
	[[nodiscard]] int TickAt( double x ) const;
	[[nodiscard]] double YAt( double flValue ) const;
	// The shown line nearest the pointer at its time, within a few pixels; -1 for none.
	[[nodiscard]] int SeriesNear( QPointF at ) const;
	[[nodiscard]] QColor TeamColor( int nTeam ) const;
	[[nodiscard]] bool IsDark() const;

	CDemoViewer &m_Viewer;
	DemoGraph_t m_nGraph = DemoGraph_t::DEMO_GRAPH_TEAMS;
	int m_nTeam = 0;
	bool m_bBuilt = false;
	std::vector<DemoGraphSeries_t> m_Series;
	double m_flTop = 1.0;  // the value at the plot's top
	double m_flStep = 1.0; // between grid lines
	QString m_szEmpty;     // why there is nothing to draw
	int m_nHover = -1;     // the line under the pointer
	int m_nPicked = -1;    // the line clicked, kept strong
	QPointF m_Mouse;
	bool m_bMouseIn = false;
};
