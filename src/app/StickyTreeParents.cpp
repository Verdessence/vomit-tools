//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See StickyTreeParents.h.
//
//===========================================================================//
#include "StickyTreeParents.h"

#include <QAbstractItemModel>
#include <QEvent>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QTreeView>

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CStickyTreeParents::CStickyTreeParents( QTreeView *pView )
	: QWidget( pView->viewport() ),
	  m_pView( pView )
{
	setObjectName( QStringLiteral( "stickyTreeParents" ) );
	setMouseTracking( true );
	setAutoFillBackground( false );
	hide();
	// Anything that moves the rows: done once the view has laid them out.
	const auto later = [ this ] {
		if ( m_bPending )
			return;
		m_bPending = true;
		QTimer::singleShot( 0, this, [ this ] {
			m_bPending = false;
			Update();
		} );
	};
	connect( pView->verticalScrollBar(), &QScrollBar::valueChanged, this, [ this ] { Update(); } );
	connect( pView->horizontalScrollBar(), &QScrollBar::valueChanged, this, [ this ] { update(); } );
	connect( pView, &QTreeView::expanded, this, later );
	connect( pView, &QTreeView::collapsed, this, later );
	pView->viewport()->installEventFilter( this );
	const auto watch = [ this, later ] {
		if ( QAbstractItemModel *pModel = m_pView->model() )
		{
			connect( pModel, &QAbstractItemModel::modelReset, this, later, Qt::UniqueConnection );
			connect( pModel, &QAbstractItemModel::layoutChanged, this, later, Qt::UniqueConnection );
			connect( pModel, &QAbstractItemModel::rowsInserted, this, later, Qt::UniqueConnection );
			connect( pModel, &QAbstractItemModel::rowsRemoved, this, later, Qt::UniqueConnection );
		}
	};
	watch();
	// A model set later (the views make theirs after the tree).
	QTimer::singleShot( 0, this, watch );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<QModelIndex> CStickyTreeParents::GetPinned() const
{
	std::vector<QModelIndex> pinned;
	for ( const QPersistentModelIndex &index : m_Pinned )
		if ( index.isValid() )
			pinned.push_back( index );
	return pinned;
}

//-----------------------------------------------------------------------------
// Purpose: The viewport resized: the strip as wide as it.
//-----------------------------------------------------------------------------
bool CStickyTreeParents::eventFilter( QObject *pObject, QEvent *pEvent )
{
	if ( pObject == m_pView->viewport() && pEvent->type() == QEvent::Resize )
		QTimer::singleShot( 0, this, [ this ] { Update(); } );
	return QWidget::eventFilter( pObject, pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: Row by row from the top: the row under the strip so far, its folder at the next depth
//          pinned when that folder's own row is already above the strip's bottom.
//-----------------------------------------------------------------------------
void CStickyTreeParents::Update()
{
	std::vector<QPersistentModelIndex> pinned;
	const QModelIndex first = m_pView->indexAt( QPoint( 1, 1 ) );
	m_nRowHeight = first.isValid() ? m_pView->visualRect( first ).height() : 0;
	if ( m_nRowHeight > 0 && m_pView->verticalScrollBar()->value() > 0 )
	{
		while ( static_cast<int>( pinned.size() ) < MOST_PINNED )
		{
			const int nTop = static_cast<int>( pinned.size() ) * m_nRowHeight;
			const QModelIndex at = m_pView->indexAt( QPoint( 1, nTop + 1 ) );
			if ( !at.isValid() )
				break;
			// Its folders, outermost first.
			std::vector<QModelIndex> chain;
			for ( QModelIndex parent = at.parent(); parent.isValid(); parent = parent.parent() )
				chain.insert( chain.begin(), parent );
			if ( chain.size() <= pinned.size() )
				break;
			const QModelIndex next = chain[ pinned.size() ];
			if ( m_pView->visualRect( next ).top() >= nTop )
				break; // its own row shows below the strip
			pinned.emplace_back( next );
		}
	}
	m_Pinned = std::move( pinned );
	m_nHover = -1;
	const int nHeight = static_cast<int>( m_Pinned.size() ) * m_nRowHeight;
	setGeometry( 0, 0, m_pView->viewport()->width(), nHeight );
	setVisible( nHeight > 0 );
	raise();
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Each pinned folder as the tree draws its row (indented, its branch open), over the
//          view's own background, a line under the last.
//-----------------------------------------------------------------------------
void CStickyTreeParents::paintEvent( QPaintEvent * )
{
	QPainter painter( this );
	const QPalette &colours = m_pView->palette();
	// Opaque, whatever the style's base is (some styles make it see-through over the window's
	// colour): the rows under the strip mustn't show through it.
	QColor behind = colours.window().color();
	behind.setAlpha( 255 );
	painter.fillRect( rect(), behind );
	painter.fillRect( rect(), colours.base() );
	const int nScrollX = m_pView->horizontalScrollBar()->value();
	for ( std::size_t i = 0; i < m_Pinned.size(); i++ )
	{
		const QModelIndex index = m_Pinned[ i ];
		if ( !index.isValid() )
			continue;
		int nDepth = 0;
		for ( QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent() )
			nDepth++;
		const int nTop = static_cast<int>( i ) * m_nRowHeight;
		const QRect row( 0, nTop, width(), m_nRowHeight );
		if ( static_cast<int>( i ) == m_nHover )
		{
			QColor hover = colours.highlight().color();
			hover.setAlphaF( 0.25 );
			painter.fillRect( row, hover );
		}
		const int nIndent = m_pView->indentation();
		const int nBranchLeft = ( m_pView->rootIsDecorated() ? nDepth : nDepth - 1 ) * nIndent - nScrollX;
		if ( m_pView->rootIsDecorated() || nDepth > 0 )
		{
			QStyleOption branch;
			branch.initFrom( m_pView );
			branch.rect = QRect( nBranchLeft, nTop, nIndent, m_nRowHeight );
			branch.state = QStyle::State_Children | QStyle::State_Open | QStyle::State_Item | QStyle::State_Enabled;
			m_pView->style()->drawPrimitive( QStyle::PE_IndicatorBranch, &branch, &painter, m_pView );
		}
		QStyleOptionViewItem option;
		option.initFrom( m_pView );
		option.rect = QRect( nBranchLeft + nIndent, nTop, std::max( 0, m_pView->header()->sectionSize( 0 ) - nBranchLeft - nIndent ), m_nRowHeight );
		option.decorationSize = m_pView->iconSize().isValid() ? m_pView->iconSize() : QSize( 16, 16 );
		option.state |= QStyle::State_Enabled;
		option.state &= ~QStyle::State_Selected;
		option.state &= ~QStyle::State_HasFocus;
		option.state &= ~QStyle::State_MouseOver;
		option.showDecorationSelected = false;
		m_pView->itemDelegateForIndex( index )->paint( &painter, option, index );
	}
	QColor line = colours.mid().color();
	painter.setPen( line );
	painter.drawLine( 0, height() - 1, width(), height() - 1 );
}

//-----------------------------------------------------------------------------
// Purpose: Folded, and scrolled to.
//-----------------------------------------------------------------------------
void CStickyTreeParents::mousePressEvent( QMouseEvent *pEvent )
{
	const int nRow = m_nRowHeight > 0 ? static_cast<int>( pEvent->position().y() ) / m_nRowHeight : -1;
	if ( nRow < 0 || nRow >= static_cast<int>( m_Pinned.size() ) || !m_Pinned[ static_cast<std::size_t>( nRow ) ].isValid() )
		return;
	const QModelIndex index = m_Pinned[ static_cast<std::size_t>( nRow ) ];
	m_pView->collapse( index );
	m_pView->setCurrentIndex( index );
	m_pView->scrollTo( index, QAbstractItemView::PositionAtTop );
	pEvent->accept();
	Update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CStickyTreeParents::mouseMoveEvent( QMouseEvent *pEvent )
{
	const int nRow = m_nRowHeight > 0 ? static_cast<int>( pEvent->position().y() ) / m_nRowHeight : -1;
	if ( nRow != m_nHover )
	{
		m_nHover = nRow;
		setToolTip( nRow >= 0 && nRow < static_cast<int>( m_Pinned.size() ) ? tr( "Fold %1" ).arg( m_Pinned[ static_cast<std::size_t>( nRow ) ].data().toString() ) : QString{} );
		update();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CStickyTreeParents::leaveEvent( QEvent * )
{
	m_nHover = -1;
	update();
}
