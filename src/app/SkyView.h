//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The sky as a player sees it from inside: drag to look around, wheel to zoom. Angles and
//          field of view follow Source's, so "setang <pitch> <yaw> 0" in game shows the same view.
//
//===========================================================================//
#pragma once

#include <QPoint>
#include <QWidget>

#include "sky/Sky.h"
#include "texture/Image.h"

class CSkyView : public QWidget
{
	Q_OBJECT

public:
	explicit CSkyView( QWidget *pParent = nullptr );

	// The cube is not owned; call Refresh() after it changes. nullptr shows nothing.
	void SetCube( const xv::sky::CCube *pCube );
	void Refresh();

	[[nodiscard]] xv::sky::Angles_t GetAngles() const { return m_Angles; }
	void SetAngles( xv::sky::Angles_t angles );
	[[nodiscard]] float GetFov() const { return m_flFov; }
	void SetFov( float flFov );
	void SetShowLabels( bool bShow );
	void SetShowEdges( bool bShow );
	// "setang 0 90 0": the console command (sv_cheats 1) for the same view in game.
	[[nodiscard]] QString GetSetangCommand() const;

signals:
	void ViewChanged();

protected:
	void paintEvent( QPaintEvent *pEvent ) override;
	void mousePressEvent( QMouseEvent *pEvent ) override;
	void mouseMoveEvent( QMouseEvent *pEvent ) override;
	void mouseDoubleClickEvent( QMouseEvent *pEvent ) override;
	void wheelEvent( QWheelEvent *pEvent ) override;
	void keyPressEvent( QKeyEvent *pEvent ) override;
	void contextMenuEvent( QContextMenuEvent *pEvent ) override;

private:
	void Look( float flYawDelta, float flPitchDelta );

	const xv::sky::CCube *m_pCube = nullptr;
	xv::sky::Angles_t m_Angles;
	float m_flFov = 90.0f;
	bool m_bLabels = true;
	bool m_bEdges = false;
	QPoint m_LastMouse;
	xv::texture::CImage m_Frame;
};
