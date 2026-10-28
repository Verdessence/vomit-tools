//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's four view panes: each with a caption that switches what it shows, held in
//          a splitter whose two bars cross, so the panes stay lined up, with one pane
//          maximised on Shift+Z.
//
//===========================================================================//
#pragma once

#include <array>

#include <QWidget>

#include "map/MapView.h"

class CMapEditor;
class QToolButton;

class CMapPane : public QWidget
{
	Q_OBJECT

public:
	CMapPane( CMapEditor &editor, MapViewType_t nType, QWidget *pParent = nullptr );

	[[nodiscard]] CMapView *GetView() const { return m_pView; }
	[[nodiscard]] MapViewType_t GetType() const;
	// Switches the pane, making a new view when it changes between 2D and 3D.
	void SetType( MapViewType_t nType );
	void UpdateCaption();
	[[nodiscard]] static QString TypeName( MapViewType_t nType );

protected:
	void resizeEvent( QResizeEvent *pEvent ) override;

private:
	void ShowMenu();

	CMapEditor &m_Editor;
	CMapView *m_pView = nullptr;
	QToolButton *m_pCaption = nullptr;
};

class CMapQuadSplitter : public QWidget
{
	Q_OBJECT

public:
	CMapQuadSplitter( std::array<CMapPane *, 4> panes, QWidget *pParent = nullptr );

	[[nodiscard]] CMapPane *GetPane( int nIndex ) const { return m_Panes[ static_cast<std::size_t>( nIndex ) ]; }
	[[nodiscard]] int IndexOf( const QWidget *pChild ) const;
	// One pane filling the splitter (Shift+Z), or -1 for all four.
	void SetMaximized( int nPane );
	[[nodiscard]] int GetMaximized() const { return m_nMaximized; }
	// Back to four equal panes (Ctrl+A).
	void Autosize();
	[[nodiscard]] double GetSplitX() const { return m_flX; }
	[[nodiscard]] double GetSplitY() const { return m_flY; }

protected:
	void resizeEvent( QResizeEvent *pEvent ) override;
	void mousePressEvent( QMouseEvent *pEvent ) override;
	void mouseMoveEvent( QMouseEvent *pEvent ) override;
	void mouseReleaseEvent( QMouseEvent *pEvent ) override;
	void leaveEvent( QEvent *pEvent ) override;
	void paintEvent( QPaintEvent *pEvent ) override;

private:
	void Lay();
	[[nodiscard]] int BarsAt( QPoint at ) const; // 1: vertical bar, 2: horizontal, 3: both

	std::array<CMapPane *, 4> m_Panes{};
	double m_flX = 0.5;
	double m_flY = 0.5;
	int m_nMaximized = -1;
	int m_nDragging = 0;
};
