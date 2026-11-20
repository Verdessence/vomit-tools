//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See LoadingView.h.
//
//===========================================================================//
#include "LoadingView.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QTimer>

//-----------------------------------------------------------------------------
// Purpose: Cover the loading surface while leaving its controls and layout in place.
//-----------------------------------------------------------------------------
CLoadingView::CLoadingView( QWidget *pHost, LoadingLayout_t nLayout )
	: QWidget( pHost ),
	  m_nLayout( nLayout )
{
	setObjectName( QStringLiteral( "loadingSkeleton" ) );
	setFocusPolicy( Qt::NoFocus );
	m_pTimer = new QTimer( this );
	m_pTimer->setInterval( 80 );
	connect( m_pTimer, &QTimer::timeout, this, qOverload<>( &QWidget::update ) );
	pHost->installEventFilter( this );
	hide();
}

//-----------------------------------------------------------------------------
// Purpose: Start a new load with a wait cursor over the affected view.
//-----------------------------------------------------------------------------
void CLoadingView::Begin( const QString &szStatus )
{
	m_bBusy = true;
	m_Clock.restart();
	SetProgress( szStatus );
	setCursor( Qt::WaitCursor );
	setGeometry( parentWidget()->rect() );
	show();
	raise();
	if ( isVisible() )
		m_pTimer->start();
}

//-----------------------------------------------------------------------------
// Purpose: Keep the same placeholder while the operation reports its current stage.
//-----------------------------------------------------------------------------
void CLoadingView::SetProgress( const QString &szStatus, int nPercent )
{
	m_szStatus = szStatus;
	m_nPercent = std::clamp( nPercent, -1, 100 );
	setAccessibleName( m_szStatus );
	setToolTip( m_szStatus );
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Leave an error in the space the result would have occupied.
//-----------------------------------------------------------------------------
void CLoadingView::Fail( const QString &szStatus )
{
	m_bBusy = false;
	m_pTimer->stop();
	unsetCursor();
	SetProgress( szStatus );
	setGeometry( parentWidget()->rect() );
	show();
	raise();
}

//-----------------------------------------------------------------------------
// Purpose: Uncover the loaded content or clear a cancelled request.
//-----------------------------------------------------------------------------
void CLoadingView::End()
{
	m_bBusy = false;
	m_pTimer->stop();
	unsetCursor();
	hide();
}

//-----------------------------------------------------------------------------
// Purpose: Keep the overlay aligned with its host as the window and panes move.
//-----------------------------------------------------------------------------
bool CLoadingView::eventFilter( QObject *pObject, QEvent *pEvent )
{
	if ( pObject == parent() && pEvent->type() == QEvent::Resize )
		setGeometry( parentWidget()->rect() );
	return QWidget::eventFilter( pObject, pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: Animate only while the placeholder is on screen.
//-----------------------------------------------------------------------------
void CLoadingView::showEvent( QShowEvent *pEvent )
{
	QWidget::showEvent( pEvent );
	if ( m_bBusy )
		m_pTimer->start();
}

//-----------------------------------------------------------------------------
// Purpose: A background tab has no visible animation to update.
//-----------------------------------------------------------------------------
void CLoadingView::hideEvent( QHideEvent *pEvent )
{
	m_pTimer->stop();
	QWidget::hideEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: Suggest the shape of the content and keep the stage readable at small sizes.
//-----------------------------------------------------------------------------
void CLoadingView::paintEvent( QPaintEvent * )
{
	QPainter painter( this );
	const QPalette colors = palette();
	painter.fillRect( rect(), colors.brush( QPalette::Base ) );
	painter.setPen( colors.color( QPalette::Text ) );
	if ( !m_bBusy )
	{
		painter.drawText( rect().adjusted( 12, 12, -12, -12 ), Qt::AlignCenter | Qt::TextWordWrap, m_szStatus );
		return;
	}

	const int nMargin = 12;
	const int nTextHeight = fontMetrics().height();
	const QRect status( nMargin, nMargin, width() - 2 * nMargin, nTextHeight );
	painter.drawText( status, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText( m_szStatus, Qt::ElideMiddle, std::max( 0, status.width() ) ) );
	const int nTop = status.bottom() + 12;
	const QRect content = rect().adjusted( nMargin, nTop, -nMargin, -nMargin );
	if ( content.isEmpty() )
		return;

	painter.setRenderHint( QPainter::Antialiasing );
	painter.setPen( Qt::NoPen );
	if ( m_nPercent >= 0 )
	{
		painter.setBrush( colors.brush( QPalette::Mid ) );
		painter.drawRoundedRect( QRectF( content.x(), nTop - 6, content.width(), 3 ), 1.5, 1.5 );
		painter.setBrush( colors.brush( QPalette::Highlight ) );
		painter.drawRoundedRect( QRectF( content.x(), nTop - 6, content.width() * ( m_nPercent / 100.0 ), 3 ), 1.5, 1.5 );
	}
	QColor fill = colors.color( QPalette::Text );
	fill.setAlphaF( 0.12 + 0.04 * std::sin( m_Clock.elapsed() / 260.0 ) );
	painter.setBrush( fill );

	switch ( m_nLayout )
	{
		case LoadingLayout_t::LOADING_LAYOUT_ROWS:
			for ( int i = 0; i < std::min( 18, content.height() / 24 ); ++i )
			{
				const int y = content.y() + i * 24;
				painter.drawRoundedRect( QRectF( content.x(), y, 13, 13 ), 3, 3 );
				painter.drawRoundedRect( QRectF( content.x() + 22, y + 2, content.width() * ( 0.65 - ( i % 4 ) * 0.10 ), 9 ), 4, 4 );
			}
			break;
		case LoadingLayout_t::LOADING_LAYOUT_GRID:
			for ( int y = content.top(); y + 56 <= content.bottom(); y += 84 )
				for ( int x = content.left(); x + 64 <= content.right(); x += 84 )
				{
					painter.drawRoundedRect( QRectF( x, y, 64, 54 ), 5, 5 );
					painter.drawRoundedRect( QRectF( x + 7, y + 60, 50, 7 ), 3, 3 );
				}
			break;
		case LoadingLayout_t::LOADING_LAYOUT_PREVIEW:
			painter.drawRoundedRect( QRectF( content ), 8, 8 );
			break;
		case LoadingLayout_t::LOADING_LAYOUT_PANES:
		{
			const QSizeF size( ( content.width() - 8 ) * 0.5, ( content.height() - 8 ) * 0.5 );
			for ( int y = 0; y < 2; ++y )
				for ( int x = 0; x < 2; ++x )
					painter.drawRoundedRect( QRectF( content.x() + x * ( size.width() + 8 ), content.y() + y * ( size.height() + 8 ), size.width(), size.height() ), 6, 6 );
			break;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Keep a place for a thumbnail while the real picture is drawn off-thread.
//-----------------------------------------------------------------------------
QIcon LoadingThumbnail()
{
	QColor color = QApplication::palette().color( QPalette::Text );
	color.setAlpha( 35 );
	static QColor previous;
	static QIcon icon;
	if ( color != previous )
	{
		previous = color;
		QPixmap picture( 96, 96 );
		picture.fill( Qt::transparent );
		{
			QPainter painter( &picture );
			painter.setRenderHint( QPainter::Antialiasing );
			painter.setPen( Qt::NoPen );
			painter.setBrush( color );
			painter.drawRoundedRect( QRectF( 3, 3, 90, 90 ), 8, 8 );
		}
		icon = QIcon( picture );
	}
	return icon;
}

//-----------------------------------------------------------------------------
// Purpose: Show a busy cursor until the synchronous operation returns.
//-----------------------------------------------------------------------------
CWaitCursor::CWaitCursor( CLoadingView *pLoading, const QString &szStatus )
	: m_pLoading( pLoading && pLoading->isHidden() ? pLoading : nullptr )
{
	QApplication::setOverrideCursor( Qt::WaitCursor );
	if ( m_pLoading )
	{
		m_pLoading->Begin( szStatus );
		m_pLoading->repaint();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Restore the previous cursor even if loading failed.
//-----------------------------------------------------------------------------
CWaitCursor::~CWaitCursor()
{
	Finish();
}

//-----------------------------------------------------------------------------
// Purpose: Drop the busy state before asking for input or showing a modal error.
//-----------------------------------------------------------------------------
void CWaitCursor::Finish()
{
	if ( !m_bWaiting )
		return;
	m_bWaiting = false;
	if ( m_pLoading )
		m_pLoading->End();
	QApplication::restoreOverrideCursor();
}
