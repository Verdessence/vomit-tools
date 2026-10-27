//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapView.h.
//
//===========================================================================//
#include "MapView.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>

#include "map/MapEditor.h"

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
		if ( MapActionFor( *pKey, GetKeyContext() ) || ( pKey->key() == Qt::Key_Space && pKey->modifiers() == Qt::NoModifier ) )
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
