//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See TabGroups.h.
//
//===========================================================================//
#include "TabGroups.h"

#include "FileIcons.h"
#include "Preferences.h"
#include "map/MapToolBars.h"

#include <algorithm>
#include <iterator>

#include <QApplication>
#include <QCloseEvent>
#include <QPainter>
#include <QRegion>
#include <QStyleOptionTab>
#include <QToolButton>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QStyle>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QWindow>

namespace
{

class CWorkspaceTabs final : public QTabBar
{
public:
	QSize tabSizeHint( int nIndex ) const override;
	QSize minimumTabSizeHint( int nIndex ) const override;
	QSize minimumSizeHint() const override;
	void paintEvent( QPaintEvent *pEvent ) override;
};

//-----------------------------------------------------------------------------
// Purpose: Give each circle room for its count badge, including multi-digit counts.
//-----------------------------------------------------------------------------
QSize CWorkspaceTabs::tabSizeHint( int nIndex ) const
{
	const QFontMetrics metrics = fontMetrics();
	const int nBadgeSize = std::max( metrics.height(), metrics.horizontalAdvance( tabText( nIndex ) ) + 4 );
	const int nDiameter = std::max( iconSize().height() + 9, metrics.height() + 8 );
	return { nDiameter + std::max( 4, nBadgeSize / 2 ), nDiameter };
}

//-----------------------------------------------------------------------------
// Purpose: Use the scroll buttons when the icons and counts no longer fit.
//-----------------------------------------------------------------------------
QSize CWorkspaceTabs::minimumTabSizeHint( int nIndex ) const
{
	return tabSizeHint( nIndex );
}

//-----------------------------------------------------------------------------
// Purpose: A short row needs less space than Qt reserves for a scrollable tab bar.
//-----------------------------------------------------------------------------
QSize CWorkspaceTabs::minimumSizeHint() const
{
	QSize minimum = QTabBar::minimumSizeHint();
	minimum.setWidth( std::min( minimum.width(), sizeHint().width() ) );
	return minimum;
}

//-----------------------------------------------------------------------------
// Purpose: Draw circular workspace buttons with tab selection and a smaller count circle.
//-----------------------------------------------------------------------------
void CWorkspaceTabs::paintEvent( QPaintEvent * )
{
	QRegion available( rect() );
	for ( const auto *pButton : findChildren<QToolButton *>( QString{}, Qt::FindDirectChildrenOnly ) )
	{
		if ( pButton->isVisible() )
			available -= pButton->geometry();
	}
	QPainter painter( this );
	painter.setClipRegion( available );
	painter.setRenderHint( QPainter::Antialiasing );
	const QPalette colors = palette();
	const QFontMetrics metrics = fontMetrics();
	for ( int i = 0; i < count(); i++ )
	{
		const QRect tab = tabRect( i );
		if ( !available.intersects( tab ) )
			continue;

		QStyleOptionTab option;
		initStyleOption( &option, i );
		const bool bSelected = option.state.testFlag( QStyle::State_Selected );
		const bool bHovered = option.state.testFlag( QStyle::State_MouseOver );
		const QRect circle = QStyle::visualRect( layoutDirection(), tab, QRect( tab.topLeft() + QPoint( 1, 1 ), QSize( tab.height() - 2, tab.height() - 2 ) ) );
		painter.setPen( colors.color( bSelected ? QPalette::Highlight : QPalette::Mid ) );
		painter.setBrush( colors.brush( bHovered || bSelected ? QPalette::AlternateBase : QPalette::Button ) );
		painter.drawEllipse( QRectF( circle ) );
		if ( bSelected && option.state.testFlag( QStyle::State_HasFocus ) )
		{
			painter.setPen( QPen( colors.color( QPalette::Highlight ), 1, Qt::DotLine ) );
			painter.setBrush( Qt::NoBrush );
			painter.drawEllipse( QRectF( circle.adjusted( 2, 2, -2, -2 ) ) );
		}

		QRect icon( QPoint{}, iconSize() );
		icon.moveCenter( circle.center() );
		tabIcon( i ).paint( &painter, icon, Qt::AlignCenter, isTabEnabled( i ) ? QIcon::Normal : QIcon::Disabled );
		const QString szCount = tabText( i );
		const int nBadgeSize = std::max( metrics.height(), metrics.horizontalAdvance( szCount ) + 4 );
		const QRect badge = QStyle::visualRect( layoutDirection(), tab, QRect( tab.right() - nBadgeSize, tab.bottom() - nBadgeSize, nBadgeSize, nBadgeSize ) );
		painter.setPen( colors.color( bSelected ? QPalette::Base : QPalette::Mid ) );
		painter.setBrush( colors.brush( bSelected ? QPalette::Highlight : QPalette::Button ) );
		painter.drawEllipse( QRectF( badge ) );
		painter.setPen( colors.color( bSelected ? QPalette::HighlightedText : QPalette::ButtonText ) );
		painter.drawText( badge, Qt::AlignCenter, szCount );
	}
}

// The user's groups: tab key to group.
constexpr char USER_GROUPS_SETTING[] = "tabGroups/user";
// Where each kind's editor window was: editorWindows/<kind>.
constexpr char WINDOW_GEOMETRY_SETTING[] = "editorWindows/";

// A window's tab widget, saying when tabs come and go.
class CWindowTabs : public QTabWidget
{
public:
	explicit CWindowTabs( std::function<void()> pfnChanged )
		: m_pfnChanged( std::move( pfnChanged ) )
	{
	}

protected:
	//-----------------------------------------------------------------------------
	// Purpose:
	//-----------------------------------------------------------------------------
	void tabInserted( int nIndex ) override
	{
		QTabWidget::tabInserted( nIndex );
		m_pfnChanged();
	}

	//-----------------------------------------------------------------------------
	// Purpose:
	//-----------------------------------------------------------------------------
	void tabRemoved( int nIndex ) override
	{
		QTabWidget::tabRemoved( nIndex );
		m_pfnChanged();
	}

private:
	std::function<void()> m_pfnChanged;
};

struct Kind_t
{
	const char *m_pszClass;
	const char *m_pszKind;
	MapIcon_t m_nIcon;
};

// Editors by class, and the kinds in the strip's order.
constexpr Kind_t KINDS[] = {
	{ "CMapEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Maps" ), MapIcon_t::MAP_ICON_MAP_PROPERTIES },
	{ "CDemoViewer", QT_TRANSLATE_NOOP( "CTabGroups", "Demos" ), MapIcon_t::MAP_ICON_RUN_MAP },
	{ "CModelEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Models" ), MapIcon_t::MAP_ICON_BLOCK },
	{ "CModelBrowser", QT_TRANSLATE_NOOP( "CTabGroups", "Models" ), MapIcon_t::MAP_ICON_BLOCK },
	{ "CTextureEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Textures" ), MapIcon_t::MAP_ICON_MATERIAL },
	{ "CMaterialEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Textures" ), MapIcon_t::MAP_ICON_MATERIAL },
	{ "CParticleEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Particles" ), MapIcon_t::MAP_ICON_PARTICLES },
	{ "CSoundEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Sounds" ), MapIcon_t::MAP_ICON_SOUND },
	{ "CSkyEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Skies" ), MapIcon_t::MAP_ICON_SKY_2D },
	{ "CDetailEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Details" ), MapIcon_t::MAP_ICON_DETAIL_PROPS },
	{ "CVpkEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Archives" ), MapIcon_t::MAP_ICON_ARCHIVE },
	{ "CStripperEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Stripper" ), MapIcon_t::MAP_ICON_STRIPPER },
	{ "CHudEditor", QT_TRANSLATE_NOOP( "CTabGroups", "HUD" ), MapIcon_t::MAP_ICON_SCREEN_EFFECT },
	{ "CSchemeEditor", QT_TRANSLATE_NOOP( "CTabGroups", "HUD" ), MapIcon_t::MAP_ICON_SCREEN_EFFECT },
};
constexpr char OTHER[] = QT_TRANSLATE_NOOP( "CTabGroups", "Other" );

//-----------------------------------------------------------------------------
// Purpose: Use the map toolbar's glyphs for built-in groups and a folder for custom groups.
//-----------------------------------------------------------------------------
QIcon GroupIcon( const QString &szGroup )
{
	if ( szGroup == QLatin1String( CTabGroups::ALL ) )
		return ForegroundIcon( MakeMapIcon( MapIcon_t::MAP_ICON_AUTOSIZE, Qt::white ) );
	for ( const Kind_t &kind : KINDS )
		if ( szGroup == QCoreApplication::translate( "CTabGroups", kind.m_pszKind ) )
			return ForegroundIcon( MakeMapIcon( kind.m_nIcon, Qt::white ) );
	return FolderIcon();
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEditorTabWidget::tabInserted( int nIndex )
{
	QTabWidget::tabInserted( nIndex );
	if ( !m_bMoving )
		emit TabsChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEditorTabWidget::tabRemoved( int nIndex )
{
	QTabWidget::tabRemoved( nIndex );
	if ( !m_bMoving )
		emit TabsChanged();
}

//-----------------------------------------------------------------------------
// Purpose: The windows go with the tabs' owner.
//-----------------------------------------------------------------------------
CEditorTabWidget::~CEditorTabWidget()
{
	for ( const QPointer<CEditorWindow> &pWindow : m_Windows )
		delete pWindow.data();
}

//-----------------------------------------------------------------------------
// Purpose: Which tab widget holds tab nIndex of the whole list, and where in it.
//-----------------------------------------------------------------------------
CEditorTabWidget::Place_t CEditorTabWidget::PlaceOf( int nIndex ) const
{
	if ( nIndex < 0 )
		return {};
	const int nOwn = Own()->count();
	if ( nIndex < nOwn )
		return { const_cast<QTabWidget *>( Own() ), nIndex };
	nIndex -= nOwn;
	for ( const QPointer<CEditorWindow> &pWindow : m_Windows )
	{
		if ( !pWindow )
			continue;
		const int nCount = pWindow->GetTabs()->count();
		if ( nIndex < nCount )
			return { pWindow->GetTabs(), nIndex };
		nIndex -= nCount;
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CEditorTabWidget::count() const
{
	int nCount = Own()->count();
	for ( const QPointer<CEditorWindow> &pWindow : m_Windows )
		if ( pWindow )
			nCount += pWindow->GetTabs()->count();
	return nCount;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QWidget *CEditorTabWidget::widget( int nIndex ) const
{
	const Place_t place = PlaceOf( nIndex );
	return place.m_pTabs ? place.m_pTabs->widget( place.m_nIndex ) : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CEditorTabWidget::indexOf( const QWidget *pWidget ) const
{
	if ( !pWidget )
		return -1;
	int nBase = Own()->count();
	if ( const int n = Own()->indexOf( pWidget ); n >= 0 )
		return n;
	for ( const QPointer<CEditorWindow> &pWindow : m_Windows )
	{
		if ( !pWindow )
			continue;
		if ( const int n = pWindow->GetTabs()->indexOf( pWidget ); n >= 0 )
			return nBase + n;
		nBase += pWindow->GetTabs()->count();
	}
	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: The current tab of the window last used.
//-----------------------------------------------------------------------------
QWidget *CEditorTabWidget::currentWidget() const
{
	const QTabWidget *pTabs = m_pActive ? m_pActive.data() : Own();
	return pTabs->currentWidget();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CEditorTabWidget::currentIndex() const
{
	return indexOf( currentWidget() );
}

//-----------------------------------------------------------------------------
// Purpose: Also brings its window forward.
//-----------------------------------------------------------------------------
void CEditorTabWidget::setCurrentWidget( QWidget *pWidget )
{
	setCurrentIndex( indexOf( pWidget ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEditorTabWidget::setCurrentIndex( int nIndex )
{
	const Place_t place = PlaceOf( nIndex );
	if ( !place.m_pTabs )
		return;
	if ( place.m_pTabs == Own() )
	{
		const bool bWasActive = !m_pActive;
		m_pActive = nullptr;
		// From another window (a file previewed from an editor's): the main window comes forward.
		if ( !bWasActive && window()->isVisible() )
		{
			window()->raise();
			window()->activateWindow();
		}
		const int nWas = Own()->currentIndex();
		QTabWidget::setCurrentIndex( place.m_nIndex );
		// Coming back to this window with its tab already current: the current tab still changed.
		if ( !bWasActive || nWas == place.m_nIndex )
			emit currentChanged( currentIndex() );
		return;
	}
	auto *pWindow = static_cast<CEditorWindow *>( place.m_pTabs->window() );
	const bool bChanged = m_pActive != place.m_pTabs || place.m_pTabs->currentIndex() != place.m_nIndex;
	m_pActive = place.m_pTabs;
	{
		const QSignalBlocker blocker( place.m_pTabs );
		place.m_pTabs->setCurrentIndex( place.m_nIndex );
	}
	if ( window()->isVisible() )
	{
		pWindow->show();
		pWindow->raise();
		pWindow->activateWindow();
	}
	if ( bChanged )
		emit currentChanged( currentIndex() );
}

//-----------------------------------------------------------------------------
// Purpose: Windowed, an editor goes to its kind's window (the first tab stays here).
//-----------------------------------------------------------------------------
int CEditorTabWidget::addTab( QWidget *pWidget, const QString &szText )
{
	if ( !m_bWindowed || Own()->count() == 0 )
		return QTabWidget::addTab( pWidget, szText );
	CEditorWindow *pWindow = WindowFor( CTabGroups::KindOf( pWidget ) );
	pWindow->GetTabs()->addTab( pWidget, szText );
	if ( window()->isVisible() && !pWindow->isVisible() )
		pWindow->show();
	return indexOf( pWidget );
}

//-----------------------------------------------------------------------------
// Purpose: A window left without tabs goes away.
//-----------------------------------------------------------------------------
void CEditorTabWidget::removeTab( int nIndex )
{
	const Place_t place = PlaceOf( nIndex );
	if ( !place.m_pTabs )
		return;
	if ( place.m_pTabs == Own() )
	{
		QTabWidget::removeTab( place.m_nIndex );
		return;
	}
	place.m_pTabs->removeTab( place.m_nIndex );
	if ( place.m_pTabs->count() == 0 )
	{
		place.m_pTabs->window()->hide();
		if ( m_pActive == place.m_pTabs )
		{
			m_pActive = nullptr;
			emit currentChanged( currentIndex() );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CEditorTabWidget::tabText( int nIndex ) const
{
	const Place_t place = PlaceOf( nIndex );
	return place.m_pTabs ? place.m_pTabs->tabText( place.m_nIndex ) : QString{};
}

//-----------------------------------------------------------------------------
// Purpose: A window's title follows its current tab's.
//-----------------------------------------------------------------------------
void CEditorTabWidget::setTabText( int nIndex, const QString &szText )
{
	const Place_t place = PlaceOf( nIndex );
	if ( !place.m_pTabs )
		return;
	place.m_pTabs->setTabText( place.m_nIndex, szText );
	if ( place.m_pTabs != Own() )
		WindowCurrentChanged( static_cast<CEditorWindow *>( place.m_pTabs->window() ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CEditorTabWidget::tabToolTip( int nIndex ) const
{
	const Place_t place = PlaceOf( nIndex );
	return place.m_pTabs ? place.m_pTabs->tabToolTip( place.m_nIndex ) : QString{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEditorTabWidget::setTabToolTip( int nIndex, const QString &szTip )
{
	const Place_t place = PlaceOf( nIndex );
	if ( place.m_pTabs )
		place.m_pTabs->setTabToolTip( place.m_nIndex, szTip );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CEditorTabWidget::isTabVisible( int nIndex ) const
{
	const Place_t place = PlaceOf( nIndex );
	return place.m_pTabs && place.m_pTabs->isTabVisible( place.m_nIndex );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEditorTabWidget::setTabVisible( int nIndex, bool bVisible )
{
	const Place_t place = PlaceOf( nIndex );
	if ( place.m_pTabs )
		place.m_pTabs->setTabVisible( place.m_nIndex, bVisible );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEditorTabWidget::setTabEnabled( int nIndex, bool bEnabled )
{
	const Place_t place = PlaceOf( nIndex );
	if ( place.m_pTabs )
		place.m_pTabs->setTabEnabled( place.m_nIndex, bEnabled );
}

//-----------------------------------------------------------------------------
// Purpose: The tabs move, each as it was; the one current stays current.
//-----------------------------------------------------------------------------
void CEditorTabWidget::SetWindowed( bool bWindowed )
{
	if ( bWindowed == m_bWindowed )
		return;
	QWidget *pCurrent = currentWidget();
	m_bMoving = true;
	m_bWindowed = bWindowed;
	if ( bWindowed )
	{
		// The main window coming up shows the windows with it; using it makes its tab current.
		window()->installEventFilter( this );
		connect( qApp, &QGuiApplication::focusWindowChanged, this, [ this ]( QWindow *pFocus ) {
			if ( !pFocus )
				return;
			if ( pFocus == window()->windowHandle() )
				Activated( nullptr );
			for ( const QPointer<CEditorWindow> &pWindow : m_Windows )
				if ( pWindow && pFocus == pWindow->windowHandle() )
					Activated( pWindow->GetTabs() );
		} );
		const QSignalBlocker blocker( this );
		while ( Own()->count() > 1 )
		{
			QWidget *pWidget = Own()->widget( 1 );
			MoveTab( this, 1, WindowFor( CTabGroups::KindOf( pWidget ) )->GetTabs() );
		}
	}
	else
	{
		window()->removeEventFilter( this );
		disconnect( qApp, &QGuiApplication::focusWindowChanged, this, nullptr );
		const QSignalBlocker blocker( this );
		for ( const QPointer<CEditorWindow> &pWindow : m_Windows )
		{
			if ( !pWindow )
				continue;
			while ( pWindow->GetTabs()->count() > 0 )
				MoveTab( pWindow->GetTabs(), 0, this );
			pWindow->SetClosingQuietly( true );
			pWindow->close();
			pWindow->deleteLater();
		}
		m_Windows.clear();
		m_pActive = nullptr;
	}
	m_bMoving = false;
	if ( window()->isVisible() )
		for ( CEditorWindow *pWindow : GetWindows() )
			pWindow->setVisible( pWindow->GetTabs()->count() > 0 );
	if ( pCurrent && indexOf( pCurrent ) >= 0 )
		setCurrentWidget( pCurrent );
	emit TabsChanged();
	emit currentChanged( currentIndex() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEditorTabWidget::MoveTab( QTabWidget *pFrom, int nIndex, QTabWidget *pTo )
{
	QWidget *pWidget = pFrom->widget( nIndex );
	const QString szText = pFrom->tabText( nIndex );
	const QString szTip = pFrom->tabToolTip( nIndex );
	const QIcon icon = pFrom->tabIcon( nIndex );
	const bool bVisible = pFrom->isTabVisible( nIndex );
	pFrom->removeTab( nIndex );
	const int nAt = pTo->addTab( pWidget, icon, szText );
	pTo->setTabToolTip( nAt, szTip );
	pTo->setTabVisible( nAt, bVisible );
}

//-----------------------------------------------------------------------------
// Purpose: Where it was last, the main window's menus, and its tabs' signals as this one's.
//-----------------------------------------------------------------------------
CEditorWindow *CEditorTabWidget::WindowFor( const QString &szKind )
{
	for ( const QPointer<CEditorWindow> &pWindow : m_Windows )
		if ( pWindow && pWindow->GetKind() == szKind )
			return pWindow;
	auto *pWindow = new CEditorWindow( szKind, window() );
	pWindow->TakeMenus( m_pMenuSource );
	if ( auto *pMain = qobject_cast<QMainWindow *>( window() ) )
		connect( pMain->statusBar(), &QStatusBar::messageChanged, pWindow->statusBar(), [ pWindow ]( const QString &szMessage ) { pWindow->statusBar()->showMessage( szMessage ); } );
	if ( const QByteArray geometry = QSettings{}.value( QLatin1String( WINDOW_GEOMETRY_SETTING ) + szKind ).toByteArray(); !geometry.isEmpty() )
		pWindow->restoreGeometry( geometry );
	else
		pWindow->resize( window()->size() );
	connect( pWindow, &CEditorWindow::Activated, this, [ this, pWindow ] { Activated( pWindow->GetTabs() ); } );
	connect( pWindow, &CEditorWindow::TabCloseRequested, this, [ this ]( QWidget *pWidget ) {
		if ( const int nIndex = indexOf( pWidget ); nIndex >= 0 )
			emit tabCloseRequested( nIndex );
	} );
	connect( pWindow, &CEditorWindow::TabsChanged, this, [ this, pWindow ] {
		if ( m_bMoving )
			return;
		WindowCurrentChanged( pWindow );
		emit TabsChanged();
	} );
	connect( pWindow->GetTabs(), &QTabWidget::currentChanged, this, [ this, pWindow ] {
		WindowCurrentChanged( pWindow );
		if ( !m_bMoving && m_pActive == pWindow->GetTabs() )
			emit currentChanged( currentIndex() );
	} );
	m_Windows.push_back( pWindow );
	return pWindow;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEditorTabWidget::Activated( QTabWidget *pTabs )
{
	if ( pTabs == m_pActive.data() || ( pTabs && pTabs->count() == 0 ) )
		return;
	m_pActive = pTabs;
	emit currentChanged( currentIndex() );
}

//-----------------------------------------------------------------------------
// Purpose: The window's title: its current tab's, then its kind.
//-----------------------------------------------------------------------------
void CEditorTabWidget::WindowCurrentChanged( CEditorWindow *pWindow )
{
	QTabWidget *pTabs = pWindow->GetTabs();
	const QString szTab = pTabs->currentIndex() >= 0 ? pTabs->tabText( pTabs->currentIndex() ) : QString{};
	pWindow->setWindowTitle( ( szTab.isEmpty() ? QString{} : szTab + QStringLiteral( " — " ) ) + pWindow->GetKind() + QStringLiteral( " — vomit-tools" ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEditorTabWidget::SetMenuSource( QMenuBar *pMenus )
{
	m_pMenuSource = pMenus;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QWidget *CEditorTabWidget::GetWindowOf( const QWidget *pWidget ) const
{
	const Place_t place = PlaceOf( indexOf( pWidget ) );
	return place.m_pTabs ? place.m_pTabs->window() : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<CEditorWindow *> CEditorTabWidget::GetWindows() const
{
	std::vector<CEditorWindow *> windows;
	for ( const QPointer<CEditorWindow> &pWindow : m_Windows )
		if ( pWindow )
			windows.push_back( pWindow );
	return windows;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEditorTabWidget::CloseWindows()
{
	for ( CEditorWindow *pWindow : GetWindows() )
	{
		pWindow->SetClosingQuietly( true );
		pWindow->close();
		pWindow->SetClosingQuietly( false );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The main window shown (the windows with tabs come up with it) or hidden.
//-----------------------------------------------------------------------------
bool CEditorTabWidget::eventFilter( QObject *pObject, QEvent *pEvent )
{
	if ( pObject == window() && pEvent->type() == QEvent::Show )
	{
		for ( CEditorWindow *pWindow : GetWindows() )
			if ( pWindow->GetTabs()->count() > 0 )
				pWindow->show();
	}
	return QTabWidget::eventFilter( pObject, pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: A top-level window of the main window's (it goes when that does), its tabs in the
//          middle.
//-----------------------------------------------------------------------------
CEditorWindow::CEditorWindow( const QString &szKind, QWidget *pParent )
	: QMainWindow( pParent, Qt::Window ),
	  m_szKind( szKind )
{
	setObjectName( QStringLiteral( "editorWindow_" ) + szKind );
	setAttribute( Qt::WA_DeleteOnClose, false );
	m_pTabs = new CWindowTabs( [ this ] { emit TabsChanged(); } );
	m_pTabs->setObjectName( QStringLiteral( "editorWindowTabs" ) );
	m_pTabs->setDocumentMode( true );
	m_pTabs->setTabsClosable( true );
	m_pTabs->setMovable( true );
	connect( m_pTabs, &QTabWidget::tabCloseRequested, this, [ this ]( int nIndex ) { emit TabCloseRequested( m_pTabs->widget( nIndex ) ); } );
	setCentralWidget( m_pTabs );
}

//-----------------------------------------------------------------------------
// Purpose: The same menus (the actions are shared, so they follow the main window's state:
//          the Map menu shows while a map is current, and so on).
//-----------------------------------------------------------------------------
void CEditorWindow::TakeMenus( QMenuBar *pMenus )
{
	if ( !pMenus )
		return;
	menuBar()->setStyleSheet( pMenus->styleSheet() );
	for ( QAction *pAction : pMenus->actions() )
		menuBar()->addAction( pAction );
}

//-----------------------------------------------------------------------------
// Purpose: Using the window makes its current tab the current one.
//-----------------------------------------------------------------------------
void CEditorWindow::changeEvent( QEvent *pEvent )
{
	QMainWindow::changeEvent( pEvent );
	if ( pEvent->type() == QEvent::ActivationChange && isActiveWindow() )
		emit Activated();
}

//-----------------------------------------------------------------------------
// Purpose: Each tab asks about unsaved changes; the window stays while any is kept. Its place
//          is kept for the next time.
//-----------------------------------------------------------------------------
void CEditorWindow::closeEvent( QCloseEvent *pEvent )
{
	QSettings{}.setValue( QLatin1String( WINDOW_GEOMETRY_SETTING ) + m_szKind, saveGeometry() );
	if ( !m_bQuiet )
	{
		std::vector<QPointer<QWidget>> tabs;
		for ( int i = 0; i < m_pTabs->count(); i++ )
			tabs.emplace_back( m_pTabs->widget( i ) );
		for ( const QPointer<QWidget> &pTab : tabs )
			if ( pTab && m_pTabs->indexOf( pTab ) >= 0 )
				emit TabCloseRequested( pTab );
		if ( m_pTabs->count() > 0 )
		{
			pEvent->ignore();
			return;
		}
	}
	pEvent->accept();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CTabGroups::CTabGroups( CEditorTabWidget *pTabs )
	: QObject( pTabs ),
	  m_pTabs( pTabs )
{
	m_pBar = new CWorkspaceTabs;
	m_pBar->setObjectName( QStringLiteral( "tabGroups" ) );
	m_pBar->setIconSize( QSize( 17, 17 ) );
	connect( &CPreferences::Instance(), &CPreferences::RulesChanged, this, &CTabGroups::Refresh );
	m_pBar->setDocumentMode( true );
	m_pBar->setDrawBase( false );
	m_pBar->setExpanding( false );
	m_pBar->setUsesScrollButtons( true );
	QFont font = m_pBar->font();
	if ( font.pointSizeF() > 0 )
		font.setPointSizeF( font.pointSizeF() * 0.85 );
	m_pBar->setFont( font );
	m_pBar->setContextMenuPolicy( Qt::CustomContextMenu );
	m_pBar->hide();
	connect( m_pBar, &QTabBar::currentChanged, this, [ this ]( int nIndex ) {
		if ( !m_bRefreshing && nIndex >= 0 )
			ShowGroup( m_pBar->tabData( nIndex ).toString() );
	} );
	connect( m_pBar, &QTabBar::customContextMenuRequested, this, [ this ]( const QPoint &at ) {
		const int nIndex = m_pBar->tabAt( at );
		const QString szGroup = nIndex >= 0 ? m_pBar->tabData( nIndex ).toString() : QString{};
		if ( !GetUserGroups().contains( szGroup ) )
			return;
		QMenu menu;
		menu.addAction( tr( "Rename Group…" ), this, [ this, szGroup ] {
			bool bOk = false;
			const QString szName = QInputDialog::getText( m_pTabs, tr( "Rename Group" ), tr( "Name:" ), QLineEdit::Normal, szGroup, &bOk ).trimmed();
			if ( !bOk || szName.isEmpty() || szName == szGroup )
				return;
			for ( int i = 0; i < m_pTabs->count(); i++ )
				if ( m_UserGroups.value( m_pTabs->widget( i ) ) == szGroup )
					SetUserGroup( m_pTabs->widget( i ), szName );
			ShowGroup( szName );
		} );
		menu.addAction( tr( "Ungroup" ), this, [ this, szGroup ] {
			for ( int i = 0; i < m_pTabs->count(); i++ )
				if ( m_UserGroups.value( m_pTabs->widget( i ) ) == szGroup )
					SetUserGroup( m_pTabs->widget( i ), {} );
		} );
		menu.exec( m_pBar->mapToGlobal( at ) );
	} );
	m_pTabs->tabBar()->setContextMenuPolicy( Qt::CustomContextMenu );
	connect( m_pTabs->tabBar(), &QTabBar::customContextMenuRequested, this, [ this ]( const QPoint &at ) {
		ShowTabMenu( m_pTabs->tabBar()->tabAt( at ), m_pTabs->tabBar()->mapToGlobal( at ) );
	} );
	connect( m_pTabs, &CEditorTabWidget::TabsChanged, this, [ this ] {
		for ( auto it = m_UserGroups.begin(); it != m_UserGroups.end(); )
			it = m_pTabs->indexOf( const_cast<QWidget *>( it.key() ) ) < 0 ? m_UserGroups.erase( it ) : std::next( it );
		// A tab coming back from the last start joins the group it was in.
		for ( int i = 1; i < m_pTabs->count(); i++ )
		{
			QWidget *pTab = m_pTabs->widget( i );
			if ( !m_UserGroups.contains( pTab ) )
				if ( const QString szGroup = Remembered( pTab ); !szGroup.isEmpty() )
				{
					m_UserGroups.insert( pTab, szGroup );
					if ( !m_UserGroupOrder.contains( szGroup ) )
						m_UserGroupOrder.append( szGroup );
				}
		}
		Refresh();
	} );
	connect( m_pTabs, &QTabWidget::currentChanged, this, &CTabGroups::CurrentChanged );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CTabGroups::KindOf( const QWidget *pTab )
{
	for ( const Kind_t &kind : KINDS )
	{
		if ( pTab && pTab->inherits( kind.m_pszClass ) )
			return tr( kind.m_pszKind );
	}
	return tr( OTHER );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CTabGroups::GroupOf( const QWidget *pTab ) const
{
	const QString szUser = m_UserGroups.value( pTab );
	if ( !szUser.isEmpty() )
		return szUser;
	// A "rule = group:<name>, <pattern>" for the tab's file comes before its kind.
	const QString szKey = m_pfnKey ? m_pfnKey( const_cast<QWidget *>( pTab ) ) : QString{};
	if ( const QString szPath = szKey.section( u'\t', 2 ); !szPath.isEmpty() )
	{
		if ( const QString szRule = CPreferences::Instance().RuleFor( QStringLiteral( "group" ), szPath ); !szRule.isEmpty() )
			return szRule;
	}
	return KindOf( pTab );
}

//-----------------------------------------------------------------------------
// Purpose: The editor around a widget, by its class.
//-----------------------------------------------------------------------------
QString CTabGroups::EditorNameOf( const QWidget *pWidget )
{
	static constexpr std::pair<const char *, const char *> EDITORS[] = {
		{ "CMapEditor", "map" },
		{ "CModelEditor", "model" },
		{ "CModelBrowser", "model" },
		{ "CTextureEditor", "texture" },
		{ "CMaterialEditor", "material" },
		{ "CParticleEditor", "particle" },
		{ "CSoundEditor", "sound" },
		{ "CSkyEditor", "sky" },
		{ "CDetailEditor", "detail" },
		{ "CVpkEditor", "archive" },
		{ "CStripperEditor", "stripper" },
	};
	for ( const QWidget *p = pWidget; p; p = p->parentWidget() )
	{
		for ( const auto &[ pszClass, pszName ] : EDITORS )
		{
			if ( p->inherits( pszClass ) )
				return QLatin1String( pszName );
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QStringList CTabGroups::GetUserGroups() const
{
	QStringList groups;
	for ( const QString &szGroup : m_UserGroupOrder )
	{
		for ( auto it = m_UserGroups.begin(); it != m_UserGroups.end(); ++it )
		{
			if ( it.value() == szGroup && m_pTabs->indexOf( const_cast<QWidget *>( it.key() ) ) >= 0 )
			{
				groups.append( szGroup );
				break;
			}
		}
	}
	return groups;
}

//-----------------------------------------------------------------------------
// Purpose: The kinds in their order, then the user's groups as they were made.
//-----------------------------------------------------------------------------
QStringList CTabGroups::GetGroups() const
{
	QStringList groups;
	QStringList kinds;
	for ( const Kind_t &kind : KINDS )
	{
		if ( !kinds.contains( tr( kind.m_pszKind ) ) )
			kinds.append( tr( kind.m_pszKind ) );
	}
	kinds.append( tr( OTHER ) );
	for ( const QString &szKind : kinds )
	{
		for ( int i = 1; i < m_pTabs->count(); i++ )
		{
			if ( GroupOf( m_pTabs->widget( i ) ) == szKind )
			{
				groups.append( szKind );
				break;
			}
		}
	}
	for ( const QString &szGroup : GetUserGroups() )
	{
		if ( !groups.contains( szGroup ) )
			groups.append( szGroup );
	}
	// Groups that rules made, in the order their tabs are.
	for ( int i = 1; i < m_pTabs->count(); i++ )
	{
		if ( const QString szGroup = GroupOf( m_pTabs->widget( i ) ); !groups.contains( szGroup ) )
			groups.append( szGroup );
	}
	return groups;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTabGroups::SetUserGroup( QWidget *pTab, const QString &szGroup )
{
	if ( szGroup.isEmpty() || szGroup == KindOf( pTab ) )
	{
		m_UserGroups.remove( pTab );
	}
	else
	{
		m_UserGroups.insert( pTab, szGroup );
		if ( !m_UserGroupOrder.contains( szGroup ) )
			m_UserGroupOrder.append( szGroup );
	}
	Remember( pTab, m_UserGroups.value( pTab ) );
	// The tab moves with the strip: its new group shows.
	if ( m_pTabs->currentWidget() == pTab && m_szCurrent != QLatin1String( ALL ) )
		m_szCurrent = GroupOf( pTab );
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTabGroups::Remember( QWidget *pTab, const QString &szGroup )
{
	const QString szKey = m_pfnKey ? m_pfnKey( pTab ) : QString{};
	if ( szKey.isEmpty() )
		return;
	QVariantMap groups = QSettings{}.value( QLatin1String( USER_GROUPS_SETTING ) ).toMap();
	if ( szGroup.isEmpty() )
		groups.remove( szKey );
	else
		groups.insert( szKey, szGroup );
	QSettings{}.setValue( QLatin1String( USER_GROUPS_SETTING ), groups );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CTabGroups::Remembered( QWidget *pTab ) const
{
	const QString szKey = m_pfnKey ? m_pfnKey( pTab ) : QString{};
	return szKey.isEmpty() ? QString{} : QSettings{}.value( QLatin1String( USER_GROUPS_SETTING ) ).toMap().value( szKey ).toString();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTabGroups::SetEnabled( bool bEnabled )
{
	m_bEnabled = bEnabled;
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTabGroups::ShowGroup( const QString &szGroup )
{
	// Taken before Refresh(): hiding the current tab makes Qt pick the nearest one still shown,
	// which isn't the group's last tab.
	QWidget *pCurrent = m_pTabs->currentWidget();
	m_szCurrent = szGroup;
	Refresh();
	const bool bAll = szGroup == QLatin1String( ALL );
	if ( pCurrent && m_pTabs->indexOf( pCurrent ) > 0 && ( bAll || GroupOf( pCurrent ) == szGroup ) )
		return;
	QWidget *pLast = m_LastTab.value( szGroup );
	if ( pLast && m_pTabs->indexOf( pLast ) > 0 && ( bAll || GroupOf( pLast ) == szGroup ) )
	{
		m_pTabs->setCurrentWidget( pLast );
		return;
	}
	for ( int i = 1; i < m_pTabs->count(); i++ )
	{
		if ( m_pTabs->isTabVisible( i ) )
		{
			m_pTabs->setCurrentIndex( i );
			return;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: A tab made current from outside (opened, picked from a menu) brings its group with
//          it unless "All" shows.
//-----------------------------------------------------------------------------
void CTabGroups::CurrentChanged()
{
	if ( m_bRefreshing )
		return;
	QWidget *pCurrent = m_pTabs->currentWidget();
	const int nIndex = m_pTabs->indexOf( pCurrent );
	if ( nIndex <= 0 )
		return;
	const QString szGroup = GroupOf( pCurrent );
	m_LastTab.insert( szGroup, pCurrent );
	m_LastTab.insert( QString::fromLatin1( ALL ), pCurrent );
	if ( m_bEnabled && m_szCurrent != QLatin1String( ALL ) && m_szCurrent != szGroup )
	{
		m_szCurrent = szGroup;
		Refresh();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Update the header counts and apply the group filter to the editor tabs.
//-----------------------------------------------------------------------------
void CTabGroups::Refresh()
{
	m_bRefreshing = true;
	const QStringList groups = GetGroups();
	const bool bStrip = m_bEnabled;
	if ( !bStrip || ( m_szCurrent != QLatin1String( ALL ) && !groups.contains( m_szCurrent ) ) )
	{
		// A group emptied (or grouping off): the current tab's, else all.
		const int nCurrent = m_pTabs->currentIndex();
		m_szCurrent = bStrip && nCurrent > 0 ? GroupOf( m_pTabs->currentWidget() ) : QString::fromLatin1( ALL );
	}
	QStringList wanted{ QString::fromLatin1( ALL ) };
	wanted.append( groups );
	while ( m_pBar->count() > wanted.size() )
		m_pBar->removeTab( m_pBar->count() - 1 );
	while ( m_pBar->count() < wanted.size() )
		m_pBar->addTab( QString{} );
	QHash<QString, int> counts;
	for ( int i = 1; i < m_pTabs->count(); i++ )
		counts[ GroupOf( m_pTabs->widget( i ) ) ]++;
	for ( int i = 0; i < wanted.size(); i++ )
	{
		const int nCount = i == 0 ? m_pTabs->count() - 1 : counts.value( wanted[ i ] );
		const QString szName = i == 0 ? tr( ALL ) : wanted[ i ];
		const QString szLabel = tr( "%1 (%2)" ).arg( szName ).arg( nCount );
		m_pBar->setTabText( i, QString::number( nCount ) );
		m_pBar->setTabIcon( i, GroupIcon( wanted[ i ] ) );
		m_pBar->setTabToolTip( i, szLabel );
		m_pBar->setAccessibleTabName( i, szLabel );
		m_pBar->setTabData( i, wanted[ i ] );
		if ( wanted[ i ] == m_szCurrent )
			m_pBar->setCurrentIndex( i );
	}
	m_pBar->setVisible( bStrip );
	const bool bAll = !bStrip || m_szCurrent == QLatin1String( ALL );
	for ( int i = 1; i < m_pTabs->count(); i++ )
	{
		m_pTabs->setTabVisible( i, bAll || GroupOf( m_pTabs->widget( i ) ) == m_szCurrent );
	}
	m_bRefreshing = false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTabGroups::ShowTabMenu( int nIndex, const QPoint &globalAt )
{
	if ( nIndex <= 0 )
		return;
	QWidget *pTab = m_pTabs->widget( nIndex );
	QMenu menu;
	QMenu *pMove = menu.addMenu( tr( "Move to Group" ) );
	pMove->setObjectName( QStringLiteral( "tabGroupMenu" ) );
	const QString szGroup = GroupOf( pTab );
	for ( const QString &szUser : GetUserGroups() )
	{
		QAction *pAction = pMove->addAction( szUser, this, [ this, pTab, szUser ] { SetUserGroup( pTab, szUser ); } );
		pAction->setEnabled( szUser != szGroup );
	}
	if ( !pMove->isEmpty() )
		pMove->addSeparator();
	pMove->addAction( tr( "New Group…" ), this, [ this, pTab ] {
		bool bOk = false;
		const QString szName = QInputDialog::getText( m_pTabs, tr( "New Group" ), tr( "Name:" ), QLineEdit::Normal, QString{}, &bOk ).trimmed();
		if ( bOk && !szName.isEmpty() && szName != QLatin1String( ALL ) )
			SetUserGroup( pTab, szName );
	} );
	if ( m_UserGroups.contains( pTab ) )
		pMove->addAction( tr( "Back to %1" ).arg( KindOf( pTab ) ), this, [ this, pTab ] { SetUserGroup( pTab, {} ); } );
	menu.exec( globalAt );
}
