//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Listening from positions: a top-down plan with the listener and env_soundscape's eight
//          position targets, each draggable. Sounds a soundscape plays at a position are heard
//          by their distance (the engine's falloff for their sound level) and direction (a pan).
//
//===========================================================================//
#pragma once

#include <array>
#include <optional>
#include <set>

#include <QElapsedTimer>
#include <QWidget>

class CSoundPlayer;
class QTimer;

class CListenCanvas : public QWidget
{
	Q_OBJECT

public:
	explicit CListenCanvas( CSoundPlayer *pPlayer, QWidget *pParent = nullptr );

	// The positions the picked soundscape plays sounds at, drawn brighter than the rest.
	void SetUsed( std::set<int> used );
	// Turns the simulation on or off in the player.
	void SetListening( bool bOn );

	// For tests: the widget point a world point lands on, and the other way.
	[[nodiscard]] QPointF ToWidget( const QPointF &world ) const;
	[[nodiscard]] QPointF ToWorld( const QPointF &at ) const;

protected:
	void paintEvent( QPaintEvent *pEvent ) override;
	void mousePressEvent( QMouseEvent *pEvent ) override;
	void mouseMoveEvent( QMouseEvent *pEvent ) override;
	void mouseReleaseEvent( QMouseEvent *pEvent ) override;
	void wheelEvent( QWheelEvent *pEvent ) override;

private:
	// What a point is over: a position (0-7), the listener (8), or nothing.
	[[nodiscard]] std::optional<int> HitAt( const QPointF &at ) const;
	void SaveScene() const;

	CSoundPlayer *m_pPlayer;
	std::set<int> m_Used;
	std::optional<int> m_Dragging;
	double m_flUnitsPerPixel = 4.0;
	// When each position last started a sound, for its ring to fade.
	std::array<std::optional<qint64>, 8> m_Heard;
	QElapsedTimer m_Clock;
	QTimer *m_pFade = nullptr;
};
