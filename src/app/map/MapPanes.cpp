//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapPanes.h.
//
//===========================================================================//
#include "MapPanes.h"

#include <algorithm>

#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

#include "map/MapEditor.h"
#include "map/MapView2D.h"
#include "map/MapView3D.h"

namespace
{

constexpr int BAR_WIDTH = 4;
constexpr double SMALLEST_SPLIT = 0.05;

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapPane::CMapPane( CMapEditor &editor, MapViewType_t nType, QWidget *pParent )
	: QWidget( pParent ),
	  m_Editor( editor )
{
	m_pCaption = new QToolButton( this );
	m_pCaption->setObjectName( QStringLiteral( "mapPaneCaption" ) );
	m_pCaption->setAutoRaise( true );
	m_pCaption->setFocusPolicy( Qt::NoFocus );
	m_pCaption->setToolTip( tr( "What this pane shows; click to change it" ) );
	m_pCaption->setStyleSheet( QStringLiteral( "QToolButton { color: white; background: rgba(0, 0, 0, 110); border: none; padding: 1px 4px; }"
											   "QToolButton:hover { color: yellow; }" ) );
	connect( m_pCaption, &QToolButton::clicked, this, &CMapPane::ShowMenu );
	SetType( nType );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
MapViewType_t CMapPane::GetType() const
{
	return m_pView ? m_pView->GetType() : MapViewType_t::MAP_VIEW_TYPE_TOP;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMapPane::TypeName( MapViewType_t nType )
{
	switch ( nType )
	{
		case MapViewType_t::MAP_VIEW_TYPE_3D_WIREFRAME: return tr( "3D Wireframe" );
		case MapViewType_t::MAP_VIEW_TYPE_3D_FLAT: return tr( "3D Flat" );
		case MapViewType_t::MAP_VIEW_TYPE_3D_TEXTURED: return tr( "3D Textured" );
		case MapViewType_t::MAP_VIEW_TYPE_3D_TEXTURED_SHADED: return tr( "3D Textured Shaded" );
		case MapViewType_t::MAP_VIEW_TYPE_3D_LIGHTMAP_GRID: return tr( "3D Lightmap Grid" );
		case MapViewType_t::MAP_VIEW_TYPE_3D_LIGHTING: return tr( "3D Lighting Preview" );
		case MapViewType_t::MAP_VIEW_TYPE_TOP: return tr( "Top (x/y)" );
		case MapViewType_t::MAP_VIEW_TYPE_FRONT: return tr( "Front (y/z)" );
		case MapViewType_t::MAP_VIEW_TYPE_SIDE: return tr( "Side (x/z)" );
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapPane::SetType( MapViewType_t nType )
{
	const bool bHadFocus = m_pView && m_pView->hasFocus();
	if ( m_pView && Is2DView( m_pView->GetType() ) == Is2DView( nType ) )
	{
		m_pView->SetType( nType );
	}
	else
	{
		// Cleared first: a new view's constructor already asks the editor for the panes' views.
		delete m_pView;
		m_pView = nullptr;
		m_pView = Is2DView( nType ) ? static_cast<CMapView *>( new CMapView2D( m_Editor, nType, this ) ) : static_cast<CMapView *>( new CMapView3D( m_Editor, nType, this ) );
		m_pView->setGeometry( rect() );
		m_pView->show();
		m_pCaption->raise();
	}
	if ( bHadFocus )
	{
		m_pView->setFocus();
	}
	UpdateCaption();
}

//-----------------------------------------------------------------------------
// Purpose: Hammer labels the 3D pane "camera" and the 2D ones by their axes.
//-----------------------------------------------------------------------------
void CMapPane::UpdateCaption()
{
	const MapViewType_t nType = GetType();
	m_pCaption->setText( Is2DView( nType ) ? TypeName( nType ) : tr( "Camera" ) );
	m_pCaption->setToolTip( tr( "%1; click to change what this pane shows" ).arg( TypeName( nType ) ) );
	m_pCaption->adjustSize();
	m_pCaption->move( 2, 2 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapPane::ShowMenu()
{
	QMenu menu( this );
	for ( const MapViewType_t nType : { MapViewType_t::MAP_VIEW_TYPE_3D_WIREFRAME, MapViewType_t::MAP_VIEW_TYPE_3D_FLAT, MapViewType_t::MAP_VIEW_TYPE_3D_TEXTURED, MapViewType_t::MAP_VIEW_TYPE_3D_TEXTURED_SHADED, MapViewType_t::MAP_VIEW_TYPE_3D_LIGHTMAP_GRID, MapViewType_t::MAP_VIEW_TYPE_3D_LIGHTING, MapViewType_t::MAP_VIEW_TYPE_TOP, MapViewType_t::MAP_VIEW_TYPE_FRONT, MapViewType_t::MAP_VIEW_TYPE_SIDE } )
	{
		if ( nType == MapViewType_t::MAP_VIEW_TYPE_TOP )
		{
			menu.addSeparator();
		}
		QAction *pAction = menu.addAction( TypeName( nType ), [ this, nType ] { SetType( nType ); } );
		pAction->setCheckable( true );
		pAction->setChecked( nType == GetType() );
	}
	menu.exec( m_pCaption->mapToGlobal( QPoint{ 0, m_pCaption->height() } ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapPane::resizeEvent( QResizeEvent *pEvent )
{
	QWidget::resizeEvent( pEvent );
	if ( m_pView )
	{
		m_pView->setGeometry( rect() );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapQuadSplitter::CMapQuadSplitter( std::array<CMapPane *, 4> panes, QWidget *pParent )
	: QWidget( pParent ),
	  m_Panes( panes )
{
	setMouseTracking( true );
	for ( CMapPane *pPane : m_Panes )
	{
		pPane->setParent( this );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CMapQuadSplitter::IndexOf( const QWidget *pChild ) const
{
	for ( int i = 0; i < 4; i++ )
	{
		if ( m_Panes[ static_cast<std::size_t>( i ) ]->isAncestorOf( pChild ) || m_Panes[ static_cast<std::size_t>( i ) ] == pChild )
		{
			return i;
		}
	}
	return -1;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapQuadSplitter::SetMaximized( int nPane )
{
	m_nMaximized = nPane;
	Lay();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapQuadSplitter::Autosize()
{
	m_flX = 0.5;
	m_flY = 0.5;
	m_nMaximized = -1;
	Lay();
}

//-----------------------------------------------------------------------------
// Purpose: Panes in Hammer's order: top left, top right, bottom left, bottom right.
//-----------------------------------------------------------------------------
void CMapQuadSplitter::Lay()
{
	if ( m_nMaximized >= 0 )
	{
		for ( int i = 0; i < 4; i++ )
		{
			m_Panes[ static_cast<std::size_t>( i ) ]->setVisible( i == m_nMaximized );
		}
		m_Panes[ static_cast<std::size_t>( m_nMaximized ) ]->setGeometry( rect() );
		update();
		return;
	}
	const int nX = static_cast<int>( ( width() - BAR_WIDTH ) * m_flX );
	const int nY = static_cast<int>( ( height() - BAR_WIDTH ) * m_flY );
	const std::array<QRect, 4> areas = {
		QRect{ 0, 0, nX, nY },
		QRect{ nX + BAR_WIDTH, 0, width() - nX - BAR_WIDTH, nY },
		QRect{ 0, nY + BAR_WIDTH, nX, height() - nY - BAR_WIDTH },
		QRect{ nX + BAR_WIDTH, nY + BAR_WIDTH, width() - nX - BAR_WIDTH, height() - nY - BAR_WIDTH },
	};
	for ( std::size_t i = 0; i < 4; i++ )
	{
		m_Panes[ i ]->setGeometry( areas[ i ] );
		m_Panes[ i ]->setVisible( true );
	}
	update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CMapQuadSplitter::BarsAt( QPoint at ) const
{
	if ( m_nMaximized >= 0 )
	{
		return 0;
	}
	const int nX = static_cast<int>( ( width() - BAR_WIDTH ) * m_flX );
	const int nY = static_cast<int>( ( height() - BAR_WIDTH ) * m_flY );
	int nBars = 0;
	if ( at.x() >= nX - 1 && at.x() <= nX + BAR_WIDTH + 1 )
	{
		nBars |= 1;
	}
	if ( at.y() >= nY - 1 && at.y() <= nY + BAR_WIDTH + 1 )
	{
		nBars |= 2;
	}
	return nBars;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapQuadSplitter::resizeEvent( QResizeEvent *pEvent )
{
	QWidget::resizeEvent( pEvent );
	Lay();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapQuadSplitter::mousePressEvent( QMouseEvent *pEvent )
{
	m_nDragging = pEvent->button() == Qt::LeftButton ? BarsAt( pEvent->position().toPoint() ) : 0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapQuadSplitter::mouseMoveEvent( QMouseEvent *pEvent )
{
	const QPoint at = pEvent->position().toPoint();
	if ( m_nDragging == 0 )
	{
		const int nBars = BarsAt( at );
		setCursor( nBars == 3 ? Qt::SizeAllCursor : nBars == 1 ? Qt::SplitHCursor
				: nBars == 2                                   ? Qt::SplitVCursor
															   : Qt::ArrowCursor );
		return;
	}
	if ( m_nDragging & 1 )
	{
		m_flX = std::clamp( static_cast<double>( at.x() ) / std::max( 1, width() - BAR_WIDTH ), SMALLEST_SPLIT, 1.0 - SMALLEST_SPLIT );
	}
	if ( m_nDragging & 2 )
	{
		m_flY = std::clamp( static_cast<double>( at.y() ) / std::max( 1, height() - BAR_WIDTH ), SMALLEST_SPLIT, 1.0 - SMALLEST_SPLIT );
	}
	Lay();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapQuadSplitter::mouseReleaseEvent( QMouseEvent * )
{
	m_nDragging = 0;
}

//-----------------------------------------------------------------------------
// Purpose: The bar's cursor goes when the mouse leaves for a pane.
//-----------------------------------------------------------------------------
void CMapQuadSplitter::leaveEvent( QEvent * )
{
	setCursor( Qt::ArrowCursor );
}

//-----------------------------------------------------------------------------
// Purpose: The bars between the panes.
//-----------------------------------------------------------------------------
void CMapQuadSplitter::paintEvent( QPaintEvent * )
{
	QPainter painter( this );
	painter.fillRect( rect(), palette().window() );
}
