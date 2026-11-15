//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A column of foldable panels that can be dragged into another order, each shown or
//          hidden by a tab down the column's side.
//
//===========================================================================//
#include "PanelColumn.h"

#include <algorithm>

#include <QApplication>
#include <QDrag>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QMimeData>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{

// What a dragged panel carries: its id, and only to its own column.
constexpr char PANEL_MIME[] = "application/x-vomit-tools-panel";
// How near the column's top or bottom a drag scrolls it, and by how much a move.
constexpr int DRAG_SCROLL_EDGE = 32;
constexpr int DRAG_SCROLL_STEP = 16;

} // namespace

namespace
{
// A group of tabs on a rounded ground of its own, a shade lighter than the strip.
class CTabGroup : public QWidget
{
public:
	CTabGroup()
	{
		setAttribute( Qt::WA_StyledBackground, false );
	}

protected:
	//-----------------------------------------------------------------------------
	// Purpose:
	//-----------------------------------------------------------------------------
	void paintEvent( QPaintEvent * ) override
	{
		QPainter painter( this );
		painter.setRenderHint( QPainter::Antialiasing, true );
		painter.setPen( Qt::NoPen );
		painter.setBrush( palette().color( QPalette::Window ).lighter( 118 ) );
		painter.drawRoundedRect( QRectF( rect() ).adjusted( 0.5, 0.5, -0.5, -0.5 ), 5, 5 );
	}
};

// A panel's own window: a tool window over the editor's, telling the column when it is
// closed (the panel goes back) and where it was put.
class CPanelWindow : public QWidget
{
public:
	CPanelWindow( QWidget *pParent, std::function<void()> pfnClosed, std::function<void( QRect )> pfnPlaced )
		: QWidget( pParent, Qt::Tool ),
		  m_pfnClosed( std::move( pfnClosed ) ),
		  m_pfnPlaced( std::move( pfnPlaced ) )
	{
		auto *pLayout = new QVBoxLayout( this );
		pLayout->setContentsMargins( 0, 0, 0, 0 );
	}

protected:
	//-----------------------------------------------------------------------------
	// Purpose:
	//-----------------------------------------------------------------------------
	void closeEvent( QCloseEvent *pEvent ) override
	{
		pEvent->ignore(); // the column takes the panel back and deletes the window
		m_pfnPlaced( geometry() );
		m_pfnClosed();
	}

	//-----------------------------------------------------------------------------
	// Purpose:
	//-----------------------------------------------------------------------------
	void moveEvent( QMoveEvent *pEvent ) override
	{
		QWidget::moveEvent( pEvent );
		if ( isVisible() )
			m_pfnPlaced( geometry() );
	}

	//-----------------------------------------------------------------------------
	// Purpose:
	//-----------------------------------------------------------------------------
	void resizeEvent( QResizeEvent *pEvent ) override
	{
		QWidget::resizeEvent( pEvent );
		if ( isVisible() )
			m_pfnPlaced( geometry() );
	}

private:
	std::function<void()> m_pfnClosed;
	std::function<void( QRect )> m_pfnPlaced;
};

//-----------------------------------------------------------------------------
// Purpose: A window's place as the state keeps it.
//-----------------------------------------------------------------------------
QString GeometryText( const QRect &rect )
{
	return QStringLiteral( "%1 %2 %3 %4" ).arg( rect.x() ).arg( rect.y() ).arg( rect.width() ).arg( rect.height() );
}

//-----------------------------------------------------------------------------
// Purpose: The other way; an empty rectangle for text that isn't four numbers.
//-----------------------------------------------------------------------------
QRect GeometryFromText( const QString &szText )
{
	const QStringList parts = szText.split( QLatin1Char( ' ' ), Qt::SkipEmptyParts );
	if ( parts.size() != 4 )
		return {};
	std::array<int, 4> values{};
	for ( int i = 0; i < 4; i++ )
	{
		bool bOk = false;
		values[ static_cast<std::size_t>( i ) ] = parts[ i ].toInt( &bOk );
		if ( !bOk )
			return {};
	}
	return { values[ 0 ], values[ 1 ], values[ 2 ], values[ 3 ] };
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: A header (fold arrow, title) above pContent.
//-----------------------------------------------------------------------------
CPanel::CPanel( CPanelColumn &column, const QString &szId, const QString &szTitle, QWidget *pContent, bool bStretch )
	: QWidget( nullptr ),
	  m_Column( column ),
	  m_szId( szId ),
	  m_bStretch( bStretch )
{
	setObjectName( QStringLiteral( "panel_" ) + szId );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 2 );
	pLayout->setSpacing( 0 );

	m_pHeader = new QWidget;
	m_pHeader->setObjectName( QStringLiteral( "panelHeader" ) );
	// A shade off the window's, as Blender sets its headers apart from their content.
	m_pHeader->setAutoFillBackground( true );
	QPalette palette = m_pHeader->palette();
	const QColor window = palette.color( QPalette::Window );
	palette.setColor( QPalette::Window, window.lightness() < 128 ? window.lighter( 125 ) : window.darker( 108 ) );
	m_pHeader->setPalette( palette );
	m_pHeader->setToolTip( tr( "Click to fold, Ctrl+click to fold the others; drag to move" ) );
	auto *pHeaderLayout = new QHBoxLayout( m_pHeader );
	pHeaderLayout->setContentsMargins( 4, 3, 6, 3 );
	pHeaderLayout->setSpacing( 4 );
	m_pArrow = new QLabel;
	m_pArrow->setFixedWidth( 12 );
	m_pTitle = new QLabel( szTitle );
	QFont font = m_pTitle->font();
	font.setBold( true );
	m_pTitle->setFont( font );
	// Dots where a grip would be: the header is what moves the panel.
	auto *pGrip = new QLabel( QStringLiteral( "⋮⋮" ) );
	pGrip->setForegroundRole( QPalette::PlaceholderText );
	pHeaderLayout->addWidget( m_pArrow );
	pHeaderLayout->addWidget( m_pTitle, 1 );
	pHeaderLayout->addWidget( pGrip );
	m_pHeader->installEventFilter( this );
	pLayout->addWidget( m_pHeader );

	m_pContent = new QWidget;
	m_pContent->setObjectName( QStringLiteral( "panelBody" ) );
	auto *pBody = new QVBoxLayout( m_pContent );
	pBody->setContentsMargins( 6, 4, 4, 4 );
	pBody->addWidget( pContent, 1 );
	pLayout->addWidget( m_pContent, 1 );
	UpdateArrow();
}

//-----------------------------------------------------------------------------
// Purpose: Folded: only the header shows.
//-----------------------------------------------------------------------------
void CPanel::SetFolded( bool bFolded )
{
	if ( m_bFolded == bFolded )
	{
		return;
	}
	m_bFolded = bFolded;
	m_pContent->setVisible( !bFolded || !m_pHeader->isVisibleTo( this ) );
	UpdateArrow();
	emit FoldChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPanel::SetTitle( const QString &szTitle )
{
	m_pTitle->setText( szTitle );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CPanel::GetTitle() const
{
	return m_pTitle->text();
}

//-----------------------------------------------------------------------------
// Purpose: The fold is kept for when it comes back to the column.
//-----------------------------------------------------------------------------
void CPanel::SetFloating( bool bFloating )
{
	m_pHeader->setVisible( !bFloating );
	m_pContent->setVisible( bFloating || !m_bFolded );
}

//-----------------------------------------------------------------------------
// Purpose: The arrow points down while the panel is open, right while it is folded.
//-----------------------------------------------------------------------------
void CPanel::UpdateArrow()
{
	m_pArrow->setText( m_bFolded ? QStringLiteral( "▸" ) : QStringLiteral( "▾" ) );
}

//-----------------------------------------------------------------------------
// Purpose: The header: a click folds (Ctrl+click folds the others), a drag moves the panel.
//-----------------------------------------------------------------------------
bool CPanel::eventFilter( QObject *pObject, QEvent *pEvent )
{
	if ( pObject != m_pHeader )
	{
		return QWidget::eventFilter( pObject, pEvent );
	}
	switch ( pEvent->type() )
	{
		case QEvent::ContextMenu:
			m_Column.ShowMenu( m_szId, static_cast<QContextMenuEvent *>( pEvent )->globalPos() );
			return true;
		case QEvent::MouseButtonPress:
		{
			const auto *pMouse = static_cast<QMouseEvent *>( pEvent );
			if ( pMouse->button() == Qt::LeftButton )
			{
				m_bPressed = true;
				m_PressAt = pMouse->position().toPoint();
				return true;
			}
			break;
		}
		case QEvent::MouseMove:
		{
			const auto *pMouse = static_cast<QMouseEvent *>( pEvent );
			if ( m_bPressed && ( pMouse->position().toPoint() - m_PressAt ).manhattanLength() >= QApplication::startDragDistance() )
			{
				m_bPressed = false;
				m_Column.StartDrag( this );
				return true;
			}
			break;
		}
		case QEvent::MouseButtonRelease:
		{
			const auto *pMouse = static_cast<QMouseEvent *>( pEvent );
			if ( m_bPressed && pMouse->button() == Qt::LeftButton )
			{
				m_bPressed = false;
				if ( pMouse->modifiers() & Qt::ControlModifier )
				{
					m_Column.FoldOthers( this );
				}
				else
				{
					SetFolded( !m_bFolded );
				}
				return true;
			}
			break;
		}
		default:
			break;
	}
	return QWidget::eventFilter( pObject, pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: The tab strip on the left, the scrolling column of panels beside it.
//-----------------------------------------------------------------------------
CPanelColumn::CPanelColumn( QWidget *pParent )
	: QWidget( pParent )
{
	auto *pLayout = new QHBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->setSpacing( 0 );

	auto *pStrip = m_pStrip = new QWidget;
	pStrip->setObjectName( QStringLiteral( "panelTabs" ) );
	pStrip->setAutoFillBackground( true );
	QPalette palette = pStrip->palette();
	palette.setColor( QPalette::Window, palette.color( QPalette::Window ).darker( 112 ) );
	pStrip->setPalette( palette );
	m_pTabs = new QVBoxLayout( pStrip );
	m_pTabs->setContentsMargins( 3, 4, 3, 4 );
	m_pTabs->setSpacing( 4 );
	for ( std::size_t n = 0; n < m_TabGroups.size(); n++ )
	{
		auto *pGroup = m_TabGroups[ n ] = new CTabGroup;
		pGroup->setObjectName( QStringLiteral( "panelTabGroup%1" ).arg( n ) );
		auto *pGroupLayout = new QVBoxLayout( pGroup );
		pGroupLayout->setContentsMargins( 2, 3, 2, 3 );
		pGroupLayout->setSpacing( 2 );
		// The modes' group runs down the rest of the strip.
		m_pTabs->addWidget( pGroup, n + 1 == m_TabGroups.size() ? 1 : 0 );
	}
	pLayout->addWidget( pStrip );

	m_pScroll = new QScrollArea;
	m_pScroll->setObjectName( QStringLiteral( "panelScroll" ) );
	m_pScroll->setFrameShape( QFrame::NoFrame );
	m_pScroll->setWidgetResizable( true );
	// A mode's panel wider than the column scrolls across rather than widening it.
	m_pScroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
	m_pContent = new QWidget;
	m_pContent->setObjectName( QStringLiteral( "panelColumn" ) );
	m_pContent->setAcceptDrops( true );
	m_pContent->installEventFilter( this );
	m_pPanels = new QVBoxLayout( m_pContent );
	m_pPanels->setContentsMargins( 2, 2, 2, 2 );
	m_pPanels->setSpacing( 2 );
	m_pScroll->setWidget( m_pContent );
	pLayout->addWidget( m_pScroll, 1 );

	m_pDropLine = new QWidget( m_pContent );
	m_pDropLine->setAutoFillBackground( true );
	m_pDropLine->setBackgroundRole( QPalette::Highlight );
	m_pDropLine->hide();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPanel *CPanelColumn::AddPanel( const QString &szId, const QString &szTitle, const QIcon &icon, QWidget *pContent, bool bStretch )
{
	auto *pPanel = new CPanel( *this, szId, szTitle, pContent, bStretch );
	auto *pTab = new QToolButton;
	pTab->setObjectName( QStringLiteral( "panelTab_" ) + szId );
	pTab->setCheckable( true );
	pTab->setChecked( true );
	pTab->setAutoRaise( true );
	pTab->setIcon( icon );
	pTab->setIconSize( QSize{ 20, 20 } );
	pTab->setToolTip( szTitle );
	connect( pTab, &QToolButton::toggled, this, [ this, szId, pTab ]( bool bChecked ) {
		// A panel in a window has a tab of its own, whatever the column does.
		if ( m_bOnePanel && !IsFloating( szId ) )
		{
			// One tab down at a time: picking another lets this one up; this one can't be let up.
			if ( !bChecked )
			{
				const QSignalBlocker blocker( pTab );
				pTab->setChecked( true );
				return;
			}
			for ( const Entry_t &entry : m_Entries )
			{
				if ( entry.m_pTab != pTab && !entry.m_pWindow )
				{
					const QSignalBlocker blocker( entry.m_pTab );
					entry.m_pTab->setChecked( false );
				}
			}
			m_szCurrent = szId;
			Relayout();
			emit StateChanged();
			return;
		}
		Relayout();
		emit StateChanged();
	} );
	connect( pPanel, &CPanel::FoldChanged, this, [ this ] {
		Relayout();
		emit StateChanged();
	} );
	pTab->setContextMenuPolicy( Qt::CustomContextMenu );
	connect( pTab, &QWidget::customContextMenuRequested, this, [ this, szId, pTab ]( const QPoint &at ) { ShowMenu( szId, pTab->mapToGlobal( at ) ); } );
	Entry_t entry;
	entry.m_pPanel = pPanel;
	entry.m_pTab = pTab;
	m_Entries.push_back( entry );
	Relayout();
	return pPanel;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPanelColumn::SetHiddenByDefault( const QString &szId )
{
	const auto it = std::ranges::find_if( m_Entries, [ & ]( const Entry_t &entry ) { return entry.m_pPanel->GetId() == szId; } );
	if ( it == m_Entries.end() )
		return;
	it->m_bHiddenByDefault = true;
	SetShown( szId, false );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPanelColumn::SetPinned( const QString &szId )
{
	const auto it = std::ranges::find_if( m_Entries, [ & ]( const Entry_t &entry ) { return entry.m_pPanel->GetId() == szId; } );
	if ( it == m_Entries.end() )
		return;
	it->m_bPinned = true;
	Relayout();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPanel *CPanelColumn::GetPanel( const QString &szId ) const
{
	const auto it = std::ranges::find_if( m_Entries, [ & ]( const Entry_t &entry ) { return entry.m_pPanel->GetId() == szId; } );
	return it == m_Entries.end() ? nullptr : it->m_pPanel;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QToolButton *CPanelColumn::GetTab( const QString &szId ) const
{
	const auto it = std::ranges::find_if( m_Entries, [ & ]( const Entry_t &entry ) { return entry.m_pPanel->GetId() == szId; } );
	return it == m_Entries.end() ? nullptr : it->m_pTab;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<CPanel *> CPanelColumn::GetPanels() const
{
	std::vector<CPanel *> panels;
	for ( const Entry_t &entry : m_Entries )
	{
		panels.push_back( entry.m_pPanel );
	}
	return panels;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CPanelColumn::IsShown( const QString &szId ) const
{
	const QToolButton *pTab = GetTab( szId );
	return pTab && pTab->isChecked();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPanelColumn::SetShown( const QString &szId, bool bShown )
{
	if ( QToolButton *pTab = GetTab( szId ) )
	{
		pTab->setChecked( bShown );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CPanelColumn::IsAvailable( const QString &szId ) const
{
	const auto it = std::ranges::find_if( m_Entries, [ & ]( const Entry_t &entry ) { return entry.m_pPanel->GetId() == szId; } );
	return it != m_Entries.end() && it->m_bAvailable;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPanelColumn::SetAvailable( const QString &szId, bool bAvailable )
{
	const auto it = std::ranges::find_if( m_Entries, [ & ]( const Entry_t &entry ) { return entry.m_pPanel->GetId() == szId; } );
	if ( it == m_Entries.end() || ( it->m_bAvailable == bAvailable && it->m_bMode ) )
	{
		return;
	}
	it->m_bAvailable = bAvailable;
	it->m_bMode = true;
	if ( m_bOnePanel && !it->m_pWindow )
	{
		const QString szId = it->m_pPanel->GetId();
		if ( bAvailable )
		{
			const auto current = std::ranges::find_if( m_Entries, []( const Entry_t &entry ) { return entry.m_pTab->isChecked(); } );
			m_szBeforeMode = current != m_Entries.end() ? current->m_pPanel->GetId() : QString{};
			SetShown( szId, true );
		}
		else if ( it->m_pTab->isChecked() && !m_szBeforeMode.isEmpty() )
			SetShown( m_szBeforeMode, true );
	}
	Relayout();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPanelColumn::ScrollTo( const QString &szId )
{
	CPanel *pPanel = GetPanel( szId );
	if ( !pPanel || !IsAvailable( szId ) || !IsShown( szId ) )
	{
		return;
	}
	if ( QWidget *pWindow = GetWindow( szId ) )
	{
		pWindow->raise();
		return;
	}
	// Once the layout has placed it.
	QTimer::singleShot( 0, this, [ this, pPanel ] { m_pScroll->ensureWidgetVisible( pPanel, 0, 0 ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPanelColumn::Reveal( const QString &szId )
{
	CPanel *pPanel = GetPanel( szId );
	if ( !pPanel )
	{
		return;
	}
	SetShown( szId, true );
	pPanel->SetFolded( false );
	ScrollTo( szId );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPanelColumn::Entry_t *CPanelColumn::FindEntry( const QString &szId )
{
	const auto it = std::ranges::find_if( m_Entries, [ & ]( const Entry_t &entry ) { return entry.m_pPanel->GetId() == szId; } );
	return it == m_Entries.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CPanelColumn::IsFloating( const QString &szId ) const
{
	return GetWindow( szId ) != nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QWidget *CPanelColumn::GetWindow( const QString &szId ) const
{
	const auto it = std::ranges::find_if( m_Entries, [ & ]( const Entry_t &entry ) { return entry.m_pPanel->GetId() == szId; } );
	return it == m_Entries.end() ? nullptr : it->m_pWindow;
}

//-----------------------------------------------------------------------------
// Purpose: Out in a window of its own (shown), or back where it was in the column's order.
//-----------------------------------------------------------------------------
void CPanelColumn::SetFloating( const QString &szId, bool bFloating )
{
	Entry_t *pEntry = FindEntry( szId );
	if ( !pEntry || ( pEntry->m_pWindow != nullptr ) == bFloating )
		return;
	if ( bFloating )
	{
		MakeWindow( *pEntry );
		const QSignalBlocker blocker( pEntry->m_pTab );
		pEntry->m_pTab->setChecked( true );
	}
	else
	{
		DropWindow( *pEntry );
		const QSignalBlocker blocker( pEntry->m_pTab );
		// Back in the side, an on-demand panel shows again (in its window it was put away).
		if ( pEntry->m_bOnDemand )
			pEntry->m_pTab->setChecked( true );
		// With one panel at a time, the one coming back is the one shown.
		if ( m_bOnePanel )
		{
			for ( const Entry_t &entry : m_Entries )
			{
				const QSignalBlocker blockOther( entry.m_pTab );
				if ( !entry.m_pWindow )
					entry.m_pTab->setChecked( entry.m_pPanel->GetId() == szId );
			}
			m_szCurrent = szId;
		}
	}
	Relayout();
	emit StateChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPanelColumn::SetOnDemand( const QString &szId )
{
	if ( Entry_t *pEntry = FindEntry( szId ) )
		pEntry->m_bOnDemand = true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CPanelColumn::IsOnDemandWindow( const QString &szId ) const
{
	const auto it = std::ranges::find_if( m_Entries, [ & ]( const Entry_t &entry ) { return entry.m_pPanel->GetId() == szId; } );
	return it != m_Entries.end() && it->m_bOnDemand && it->m_pWindow;
}

//-----------------------------------------------------------------------------
// Purpose: The panel's window, beside the column the first time. Closing it puts the panel
//          back, or only puts it away when it is opened on demand.
//-----------------------------------------------------------------------------
void CPanelColumn::MakeWindow( Entry_t &entry )
{
	CPanel *pPanel = entry.m_pPanel;
	const QString szId = pPanel->GetId();
	auto *pWindow = new CPanelWindow(
		this, [ this, szId ] { QTimer::singleShot( 0, this, [ this, szId ] {
								   if ( IsOnDemandWindow( szId ) )
									   SetShown( szId, false );
								   else
									   SetFloating( szId, false );
							   } ); },
		[ this, szId ]( QRect rect ) {
			if ( Entry_t *p = FindEntry( szId ) )
			{
				p->m_WindowGeometry = rect;
				emit StateChanged();
			}
		} );
	pWindow->setObjectName( QStringLiteral( "panelWindow_" ) + szId );
	pWindow->setWindowTitle( pPanel->GetTitle() );
	m_pPanels->removeWidget( pPanel );
	pWindow->layout()->addWidget( pPanel );
	pPanel->SetFloating( true );
	pPanel->show();
	if ( entry.m_WindowGeometry.isValid() )
		pWindow->setGeometry( entry.m_WindowGeometry );
	else
	{
		// Beside the column, half as tall as it, each a little lower than the one before.
		const int nIndex = static_cast<int>( &entry - m_Entries.data() );
		pWindow->resize( std::max( std::max( width(), 280 ), pPanel->sizeHint().width() ), std::max( 240, height() / 2 ) );
		pWindow->move( mapToGlobal( QPoint{ -pWindow->width() - 8, 24 * nIndex } ) );
	}
	entry.m_pWindow = pWindow;
}

//-----------------------------------------------------------------------------
// Purpose: The panel back in the column, its window gone.
//-----------------------------------------------------------------------------
void CPanelColumn::DropWindow( Entry_t &entry )
{
	QWidget *pWindow = std::exchange( entry.m_pWindow, nullptr );
	if ( !pWindow )
		return;
	pWindow->layout()->removeWidget( entry.m_pPanel );
	entry.m_pPanel->setParent( m_pContent );
	entry.m_pPanel->SetFloating( false );
	pWindow->deleteLater();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPanelColumn::showEvent( QShowEvent *pEvent )
{
	QWidget::showEvent( pEvent );
	Relayout();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPanelColumn::hideEvent( QHideEvent *pEvent )
{
	QWidget::hideEvent( pEvent );
	for ( const Entry_t &entry : m_Entries )
		if ( entry.m_pWindow )
			entry.m_pWindow->hide();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPanelColumn::MovePanel( const QString &szId, int nIndex )
{
	const auto it = std::ranges::find_if( m_Entries, [ & ]( const Entry_t &entry ) { return entry.m_pPanel->GetId() == szId; } );
	if ( it == m_Entries.end() )
	{
		return;
	}
	const int nFrom = static_cast<int>( it - m_Entries.begin() );
	// The index counts the panel itself where it was; without it, later places move up one.
	nIndex = std::clamp( nIndex > nFrom ? nIndex - 1 : nIndex, 0, static_cast<int>( m_Entries.size() ) - 1 );
	if ( nIndex == nFrom )
	{
		return;
	}
	const Entry_t entry = *it;
	m_Entries.erase( it );
	m_Entries.insert( m_Entries.begin() + nIndex, entry );
	Relayout();
	emit StateChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPanelColumn::FoldOthers( CPanel *pKeep )
{
	bool bAllFolded = true;
	for ( const Entry_t &entry : m_Entries )
	{
		if ( entry.m_pPanel != pKeep && entry.m_bAvailable && entry.m_pTab->isChecked() && !entry.m_pPanel->IsFolded() )
		{
			bAllFolded = false;
		}
	}
	for ( const Entry_t &entry : m_Entries )
	{
		entry.m_pPanel->SetFolded( entry.m_pPanel != pKeep && !bAllFolded );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QVariantMap CPanelColumn::GetState() const
{
	QStringList order, hidden, folded, windows;
	QVariantMap geometry;
	for ( const Entry_t &entry : m_Entries )
	{
		order.append( entry.m_pPanel->GetId() );
		if ( entry.m_pWindow )
			windows.append( entry.m_pPanel->GetId() );
		if ( entry.m_WindowGeometry.isValid() )
			geometry.insert( entry.m_pPanel->GetId(), GeometryText( entry.m_WindowGeometry ) );
		// An on-demand panel coming and going in its window leaves the column's choice alone.
		const bool bShown = entry.m_bOnDemand && entry.m_pWindow ? true
			: m_bOnePanel && !entry.m_pWindow                    ? m_StackedShown.contains( entry.m_pPanel->GetId() )
																 : entry.m_pTab->isChecked();
		if ( !bShown )
		{
			hidden.append( entry.m_pPanel->GetId() );
		}
		if ( entry.m_pPanel->IsFolded() )
		{
			folded.append( entry.m_pPanel->GetId() );
		}
	}
	return { { QStringLiteral( "order" ), order }, { QStringLiteral( "hidden" ), hidden }, { QStringLiteral( "folded" ), folded }, { QStringLiteral( "current" ), m_szCurrent }, { QStringLiteral( "windows" ), windows }, { QStringLiteral( "windowGeometry" ), geometry } };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPanelColumn::SetState( const QVariantMap &state )
{
	const QStringList order = state.value( QStringLiteral( "order" ) ).toStringList();
	const QStringList hidden = state.value( QStringLiteral( "hidden" ) ).toStringList();
	const QStringList folded = state.value( QStringLiteral( "folded" ) ).toStringList();
	m_szCurrent = state.value( QStringLiteral( "current" ) ).toString();
	// Named panels first, in the order given; the rest after them as they were.
	std::ranges::stable_sort( m_Entries, std::less<>{}, [ & ]( const Entry_t &entry ) {
		const qsizetype n = order.indexOf( entry.m_pPanel->GetId() );
		return n < 0 ? order.size() : n;
	} );
	for ( const Entry_t &entry : m_Entries )
	{
		const QSignalBlocker blockTab( entry.m_pTab );
		const QSignalBlocker blockPanel( entry.m_pPanel );
		// A panel the state doesn't know (newer than it) starts as it does by default.
		const QString &szId = entry.m_pPanel->GetId();
		entry.m_pTab->setChecked( order.contains( szId ) || !entry.m_bHiddenByDefault ? !hidden.contains( szId ) : false );
		entry.m_pPanel->SetFolded( folded.contains( entry.m_pPanel->GetId() ) );
	}
	const QStringList windows = state.value( QStringLiteral( "windows" ) ).toStringList();
	const QVariantMap geometry = state.value( QStringLiteral( "windowGeometry" ) ).toMap();
	const QSignalBlocker blockColumn( this );
	for ( Entry_t &entry : m_Entries )
	{
		const QString szId = entry.m_pPanel->GetId();
		entry.m_WindowGeometry = GeometryFromText( geometry.value( szId ).toString() );
		const bool bShown = entry.m_pTab->isChecked();
		SetFloating( szId, windows.contains( szId ) );
		const QSignalBlocker blockTab( entry.m_pTab );
		// In its window, an on-demand panel waits to be asked for, as Hammer's dialogs do.
		entry.m_pTab->setChecked( entry.m_bOnDemand && entry.m_pWindow ? false : bShown );
	}
	Relayout();
}

//-----------------------------------------------------------------------------
// Purpose: The first panel shown (one that is always there) is the one kept going in.
//-----------------------------------------------------------------------------
void CPanelColumn::SetOnePanel( bool bOnePanel )
{
	if ( bOnePanel == m_bOnePanel )
		return;
	if ( bOnePanel )
	{
		m_StackedShown.clear();
		QString szKeep;
		for ( const Entry_t &entry : m_Entries )
		{
			if ( entry.m_pTab->isChecked() && !entry.m_pWindow )
			{
				m_StackedShown.append( entry.m_pPanel->GetId() );
				if ( szKeep.isEmpty() && entry.m_bAvailable && !entry.m_bMode )
					szKeep = entry.m_pPanel->GetId();
			}
		}
		// The panel shown alone last time, when it is still about.
		for ( const Entry_t &entry : m_Entries )
		{
			if ( entry.m_pPanel->GetId() == m_szCurrent && entry.m_bAvailable && !entry.m_bMode && !entry.m_pWindow )
				szKeep = m_szCurrent;
		}
		if ( szKeep.isEmpty() && !m_Entries.empty() )
			szKeep = m_Entries.front().m_pPanel->GetId();
		m_szCurrent = szKeep;
		// A mode's panel that is there now stays the one shown.
		for ( const Entry_t &entry : m_Entries )
			if ( entry.m_bMode && entry.m_bAvailable && entry.m_pTab->isChecked() && !entry.m_pWindow )
			{
				m_szBeforeMode = szKeep;
				szKeep = entry.m_pPanel->GetId();
			}
		m_bOnePanel = true;
		for ( const Entry_t &entry : m_Entries )
		{
			const QSignalBlocker blocker( entry.m_pTab );
			if ( !entry.m_pWindow )
				entry.m_pTab->setChecked( entry.m_pPanel->GetId() == szKeep );
		}
	}
	else
	{
		m_bOnePanel = false;
		for ( const Entry_t &entry : m_Entries )
		{
			const QSignalBlocker blocker( entry.m_pTab );
			if ( !entry.m_pWindow )
				entry.m_pTab->setChecked( m_StackedShown.contains( entry.m_pPanel->GetId() ) );
		}
	}
	Relayout();
}

//-----------------------------------------------------------------------------
// Purpose: A drag carrying the panel's id, with a picture of its header.
//-----------------------------------------------------------------------------
void CPanelColumn::StartDrag( CPanel *pPanel )
{
	auto *pDrag = new QDrag( pPanel );
	auto *pData = new QMimeData;
	pData->setData( QLatin1String( PANEL_MIME ), pPanel->GetId().toUtf8() );
	pDrag->setMimeData( pData );
	QWidget *pHeader = pPanel->findChild<QWidget *>( QStringLiteral( "panelHeader" ) );
	if ( pHeader )
	{
		pDrag->setPixmap( pHeader->grab() );
	}
	pDrag->exec( Qt::MoveAction );
	m_pDropLine->hide();
}

//-----------------------------------------------------------------------------
// Purpose: Drops on the column: a line where the panel would go, scrolling near the ends.
//-----------------------------------------------------------------------------
bool CPanelColumn::eventFilter( QObject *pObject, QEvent *pEvent )
{
	if ( pObject != m_pContent )
	{
		return QWidget::eventFilter( pObject, pEvent );
	}
	if ( pEvent->type() == QEvent::LayoutRequest )
	{
		// A panel's content wants another size: so may the column (minimumSizeHint).
		updateGeometry();
		return QWidget::eventFilter( pObject, pEvent );
	}
	const auto ours = [ this ]( const QDropEvent *pDrop ) {
		const auto *pPanel = qobject_cast<const CPanel *>( pDrop->source() );
		return pDrop->mimeData()->hasFormat( QLatin1String( PANEL_MIME ) ) && pPanel && GetPanel( pPanel->GetId() ) == pPanel;
	};
	switch ( pEvent->type() )
	{
		case QEvent::DragEnter:
		case QEvent::DragMove:
		{
			auto *pDrop = static_cast<QDragMoveEvent *>( pEvent );
			if ( !ours( pDrop ) )
			{
				return false;
			}
			pDrop->acceptProposedAction();
			const int y = pDrop->position().toPoint().y();
			const int nIndex = DropIndexAt( y );
			// Between the panel before and after the place, or at the column's end.
			int nLineY = m_pPanels->contentsMargins().top();
			for ( int i = nIndex - 1; i >= 0; i-- )
			{
				if ( !m_Entries[ static_cast<std::size_t>( i ) ].m_pWindow && m_Entries[ static_cast<std::size_t>( i ) ].m_pPanel->isVisible() )
				{
					nLineY = m_Entries[ static_cast<std::size_t>( i ) ].m_pPanel->geometry().bottom() + 1;
					break;
				}
			}
			m_pDropLine->setGeometry( 0, std::max( nLineY - 1, 0 ), m_pContent->width(), 3 );
			m_pDropLine->show();
			m_pDropLine->raise();
			// Near the viewport's top or bottom, the column scrolls on.
			const int nInView = y - m_pScroll->verticalScrollBar()->value();
			QScrollBar *pBar = m_pScroll->verticalScrollBar();
			if ( nInView < DRAG_SCROLL_EDGE )
			{
				pBar->setValue( pBar->value() - DRAG_SCROLL_STEP );
			}
			else if ( nInView > m_pScroll->viewport()->height() - DRAG_SCROLL_EDGE )
			{
				pBar->setValue( pBar->value() + DRAG_SCROLL_STEP );
			}
			return true;
		}
		case QEvent::DragLeave:
			m_pDropLine->hide();
			return true;
		case QEvent::Drop:
		{
			auto *pDrop = static_cast<QDropEvent *>( pEvent );
			m_pDropLine->hide();
			if ( !ours( pDrop ) )
			{
				return false;
			}
			pDrop->acceptProposedAction();
			MovePanel( QString::fromUtf8( pDrop->mimeData()->data( QLatin1String( PANEL_MIME ) ) ), DropIndexAt( pDrop->position().toPoint().y() ) );
			return true;
		}
		default:
			break;
	}
	return QWidget::eventFilter( pObject, pEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QSize CPanelColumn::minimumSizeHint() const
{
	// The panels that are always there set the width; a mode's coming and going (Face Edit with
	// its tool) leaves it as it is.
	int nContent = 0;
	for ( const Entry_t &entry : m_Entries )
	{
		if ( !entry.m_bMode && entry.m_bAvailable && entry.m_pTab->isChecked() && !entry.m_pWindow )
			nContent = std::max( nContent, entry.m_pPanel->minimumSizeHint().width() );
	}
	const QMargins margins = m_pPanels->contentsMargins();
	const int nScrollBar = m_pScroll->verticalScrollBar()->sizeHint().width();
	return { m_pStrip->sizeHint().width() + nContent + margins.left() + margins.right() + nScrollBar, m_pStrip->sizeHint().height() };
}

//-----------------------------------------------------------------------------
// Purpose: Before the first shown panel whose middle is below y.
//-----------------------------------------------------------------------------
int CPanelColumn::DropIndexAt( int y ) const
{
	for ( std::size_t i = 0; i < m_Entries.size(); i++ )
	{
		const CPanel *pPanel = m_Entries[ i ].m_pPanel;
		if ( !m_Entries[ i ].m_pWindow && pPanel->isVisible() && y < pPanel->geometry().center().y() )
		{
			return static_cast<int>( i );
		}
	}
	return static_cast<int>( m_Entries.size() );
}

//-----------------------------------------------------------------------------
// Purpose: The panels and tabs in m_Entries' order. Shown panels that stretch share the
//          spare height; with none, it goes below the last panel.
//-----------------------------------------------------------------------------
void CPanelColumn::Relayout()
{
	while ( QLayoutItem *pItem = m_pPanels->takeAt( 0 ) )
	{
		delete pItem;
	}
	for ( QWidget *pGroup : m_TabGroups )
	{
		while ( QLayoutItem *pItem = pGroup->layout()->takeAt( 0 ) )
		{
			delete pItem;
		}
	}
	bool bStretching = false;
	for ( const Entry_t &entry : m_Entries )
	{
		const bool bShown = entry.m_bAvailable && entry.m_pTab->isChecked();
		entry.m_pTab->setVisible( entry.m_bAvailable );
		if ( entry.m_pWindow )
		{
			// Its window shows while the column does (it is the editor's).
			if ( bShown && isVisible() )
			{
				if ( !entry.m_pWindow->isVisible() )
					entry.m_pWindow->show();
			}
			else
				entry.m_pWindow->hide();
			continue;
		}
		// A panel shown alone fills the column, its lists growing into the height.
		const bool bStretch = bShown && ( entry.m_pPanel->IsStretching() || ( m_bOnePanel && !entry.m_pPanel->IsFolded() ) );
		bStretching = bStretching || bStretch;
		m_pPanels->addWidget( entry.m_pPanel, bStretch ? 1 : 0 );
		entry.m_pPanel->setVisible( bShown );
		entry.m_pTab->setVisible( entry.m_bAvailable );
	}
	if ( !bStretching )
	{
		m_pPanels->addStretch( 1 );
	}
	// Each tab in its group; a group with no tab there is away, but the modes' (the strip's foot).
	std::array<bool, 3> used{};
	for ( const Entry_t &entry : m_Entries )
	{
		const std::size_t nGroup = entry.m_bPinned ? 0 : entry.m_bMode ? 2
																	   : 1;
		static_cast<QVBoxLayout *>( m_TabGroups[ nGroup ]->layout() )->addWidget( entry.m_pTab );
		used[ nGroup ] = used[ nGroup ] || entry.m_bAvailable;
	}
	static_cast<QVBoxLayout *>( m_TabGroups[ 2 ]->layout() )->addStretch( 1 );
	m_TabGroups[ 0 ]->setVisible( used[ 0 ] );
	m_TabGroups[ 1 ]->setVisible( used[ 1 ] );
}

//-----------------------------------------------------------------------------
// Purpose: A panel supplies commands for both its header and its tab.
//-----------------------------------------------------------------------------
void CPanelColumn::SetMenu( const QString &szId, std::function<void( QMenu & )> pfnMenu )
{
	if ( Entry_t *pEntry = FindEntry( szId ) )
		pEntry->m_pfnMenu = std::move( pfnMenu );
}

//-----------------------------------------------------------------------------
// Purpose: The panel's own commands at the pointer, and taking it out into a window or back.
//-----------------------------------------------------------------------------
void CPanelColumn::ShowMenu( const QString &szId, const QPoint &globalAt )
{
	const Entry_t *pEntry = FindEntry( szId );
	if ( !pEntry )
		return;
	auto *pMenu = new QMenu( this );
	pMenu->setAttribute( Qt::WA_DeleteOnClose );
	QAction *pOwn = pMenu->addAction( tr( "Own Window" ), this, [ this, szId ]( bool bOn ) { SetFloating( szId, bOn ); } );
	pOwn->setObjectName( QStringLiteral( "panelOwnWindow" ) );
	pOwn->setCheckable( true );
	pOwn->setChecked( pEntry->m_pWindow != nullptr );
	if ( pEntry->m_pfnMenu )
	{
		if ( !pMenu->isEmpty() )
			pMenu->addSeparator();
		pEntry->m_pfnMenu( *pMenu );
	}
	if ( pMenu->isEmpty() )
	{
		delete pMenu;
		return;
	}
	pMenu->popup( globalAt );
}
