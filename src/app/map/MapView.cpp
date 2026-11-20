//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapView.h.
//
//===========================================================================//
#include "MapView.h"

#include <algorithm>

#include <QAbstractSpinBox>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QTextEdit>

#include "map/MapEditor.h"
#include "map/MapPhysics.h"
#include "map/MapSplineTool.h"

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapView::CMapView( CMapEditor &editor, QWidget *pParent )
	: QWidget( pParent ),
	  m_Editor( editor )
{
	setFocusPolicy( Qt::StrongFocus );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapDocument &CMapView::GetDocument() const
{
	return m_Editor.GetDocument();
}

//-----------------------------------------------------------------------------
// Purpose: Claims Hammer's keys before the window's shortcuts see them, and keeps Tab from
//          moving the focus away (it cycles a 2D view's axis).
//-----------------------------------------------------------------------------
bool CMapView::event( QEvent *pEvent )
{
	if ( pEvent->type() == QEvent::ShortcutOverride )
	{
		auto *pKey = static_cast<QKeyEvent *>( pEvent );
		const MapTool_t nTool = m_Editor.GetTool();
		const bool bBrush = nTool == MapTool_t::MAP_TOOL_BRUSH_EDIT || nTool == MapTool_t::MAP_TOOL_VERTEX;
		// The spline tool's keys (Ctrl+Z on a path not made yet, Ctrl+X, its transforms).
		if ( ( nTool == MapTool_t::MAP_TOOL_SPLINE && m_Editor.GetSplineTool().WantsKey( *pKey ) ) || ( nTool == MapTool_t::MAP_TOOL_PHYSICS && m_Editor.GetPhysicsTool().WantsKey( *pKey ) ) )
		{
			pEvent->accept();
			return true;
		}
		if ( MapActionFor( *pKey, GetKeyContext() ) || ( bBrush && MapActionFor( *pKey, MapKeyContext_t::MAP_KEY_CONTEXT_BRUSH ) ) || ( pKey->key() == Qt::Key_Space && pKey->modifiers() == Qt::NoModifier ) )
		{
			pEvent->accept();
			return true;
		}
	}
	if ( pEvent->type() == QEvent::KeyPress && static_cast<QKeyEvent *>( pEvent )->key() == Qt::Key_Tab && static_cast<QKeyEvent *>( pEvent )->modifiers() == Qt::NoModifier )
	{
		keyPressEvent( static_cast<QKeyEvent *>( pEvent ) );
		return true;
	}
	return QWidget::event( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: The view under the mouse takes the keys, as in Hammer, unless something is being
//          typed elsewhere.
//-----------------------------------------------------------------------------
void CMapView::enterEvent( QEnterEvent *pEvent )
{
	QWidget *pFocus = QApplication::focusWidget();
	const bool bTyping = qobject_cast<QLineEdit *>( pFocus ) || qobject_cast<QTextEdit *>( pFocus ) || qobject_cast<QPlainTextEdit *>( pFocus ) || qobject_cast<QAbstractSpinBox *>( pFocus );
	if ( isActiveWindow() && !bTyping )
	{
		setFocus( Qt::MouseFocusReason );
	}
	QWidget::enterEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView::keyPressEvent( QKeyEvent *pEvent )
{
	if ( const auto nAction = MapActionFor( *pEvent, GetKeyContext() ) )
	{
		if ( pEvent->isAutoRepeat() && IsHeldMapAction( *nAction ) )
		{
			return;
		}
		// The view's own meaning first (Enter on a dragged box), then the editor's.
		if ( Run( *nAction ) || m_Editor.RunAction( *nAction, this ) )
		{
			return;
		}
	}
	QWidget::keyPressEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: A double-click with the selection tool opens Object Properties on what the first
//          click picked, as in Hammer. The second press is still a press to the tools.
//-----------------------------------------------------------------------------
void CMapView::mouseDoubleClickEvent( QMouseEvent *pEvent )
{
	if ( pEvent->button() == Qt::LeftButton && pEvent->modifiers() == Qt::NoModifier && m_Editor.GetTool() == MapTool_t::MAP_TOOL_SELECT && PickPortal( pEvent->position() ) )
	{
		pEvent->accept();
		return;
	}
	QWidget::mouseDoubleClickEvent( pEvent );
	if ( pEvent->button() == Qt::LeftButton && pEvent->modifiers() == Qt::NoModifier && m_Editor.GetTool() == MapTool_t::MAP_TOOL_SELECT && !GetDocument().GetSelection().empty() )
		m_Editor.RunAction( MapAction_t::MAP_ACTION_PROPERTIES, this );
}

//-----------------------------------------------------------------------------
// Purpose: The portals are inspection guides, drawn above the map in both renderers.
//-----------------------------------------------------------------------------
void CMapView::PaintPortals( QPainter &painter ) const
{
	const auto &portals = GetDocument().GetPortals();
	if ( portals.empty() )
		return;
	const auto pfnProject = PortalProjection();
	const auto selected = GetDocument().GetSelectedPortal();
	std::vector<QLineF> lines, highlighted;
	for ( std::size_t n = 0; n < portals.size(); ++n )
	{
		const auto &points = portals[ n ].m_Points;
		auto &edges = selected == n ? highlighted : lines;
		for ( std::size_t i = 0; i < points.size(); ++i )
			if ( const auto line = pfnProject( points[ i ], points[ ( i + 1 ) % points.size() ] ) )
				edges.push_back( *line );
	}
	painter.save();
	painter.setPen( QPen( QColor( 65, 150, 255 ), 1.0 ) );
	painter.drawLines( lines.data(), static_cast<int>( lines.size() ) );
	painter.setPen( QPen( QColor( 135, 225, 255 ), 2.0 ) );
	painter.drawLines( highlighted.data(), static_cast<int>( highlighted.size() ) );
	painter.restore();
}

//-----------------------------------------------------------------------------
// Purpose: Inspect the leaves of the closest portal outline within a few pixels.
//-----------------------------------------------------------------------------
bool CMapView::PickPortal( QPointF at )
{
	auto &document = GetDocument();
	const auto &portals = document.GetPortals();
	if ( portals.empty() )
		return false;
	const auto pfnProject = PortalProjection();
	std::optional<std::size_t> picked;
	double flBest = 5.0 * 5.0;
	for ( std::size_t n = 0; n < portals.size(); ++n )
	{
		const auto &points = portals[ n ].m_Points;
		for ( std::size_t i = 0; i < points.size(); ++i )
		{
			const auto line = pfnProject( points[ i ], points[ ( i + 1 ) % points.size() ] );
			if ( !line )
				continue;
			const QPointF delta = line->p2() - line->p1();
			const QPointF offset = at - line->p1();
			const double flLength = QPointF::dotProduct( delta, delta );
			const double flAlong = flLength > 0.0 ? std::clamp( QPointF::dotProduct( offset, delta ) / flLength, 0.0, 1.0 ) : 0.0;
			const QPointF gap = offset - delta * flAlong;
			if ( const double flDistance = QPointF::dotProduct( gap, gap ); flDistance < flBest )
			{
				flBest = flDistance;
				picked = n;
			}
		}
	}
	if ( !picked )
		return false;
	document.SelectPortal( *picked );
	const auto &portal = portals[ *picked ];
	emit m_Editor.StatusMessage( tr( "Portal %1 of %2 connects leaves %3 and %4." ).arg( *picked + 1 ).arg( portals.size() ).arg( portal.m_nLeafA ).arg( portal.m_nLeafB ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView::focusInEvent( QFocusEvent *pEvent )
{
	m_Editor.SetActiveView( this );
	update();
	QWidget::focusInEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView::focusOutEvent( QFocusEvent *pEvent )
{
	update();
	QWidget::focusOutEvent( pEvent );
}
