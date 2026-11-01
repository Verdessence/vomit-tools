//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See TabGroups.h.
//
//===========================================================================//
#include "TabGroups.h"

#include <iterator>

#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QTabBar>

namespace
{

// The user's groups: tab key to group.
constexpr char USER_GROUPS_SETTING[] = "tabGroups/user";

struct Kind_t
{
	const char *m_pszClass;
	const char *m_pszKind;
};

// Editors by class, and the kinds in the strip's order.
constexpr Kind_t KINDS[] = {
	{ "CMapEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Maps" ) },
	{ "CModelEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Models" ) },
	{ "CModelBrowser", QT_TRANSLATE_NOOP( "CTabGroups", "Models" ) },
	{ "CTextureEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Textures" ) },
	{ "CMaterialEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Textures" ) },
	{ "CParticleEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Particles" ) },
	{ "CSoundEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Sounds" ) },
	{ "CSkyEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Skies" ) },
	{ "CDetailEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Details" ) },
	{ "CVpkEditor", QT_TRANSLATE_NOOP( "CTabGroups", "Archives" ) },
};
constexpr char OTHER[] = QT_TRANSLATE_NOOP( "CTabGroups", "Other" );

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEditorTabWidget::tabInserted( int nIndex )
{
	QTabWidget::tabInserted( nIndex );
	emit TabsChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEditorTabWidget::tabRemoved( int nIndex )
{
	QTabWidget::tabRemoved( nIndex );
	emit TabsChanged();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CTabGroups::CTabGroups( CEditorTabWidget *pTabs )
	: QObject( pTabs ),
	  m_pTabs( pTabs )
{
	m_pBar = new QTabBar;
	m_pBar->setObjectName( QStringLiteral( "tabGroups" ) );
	m_pBar->setDocumentMode( true );
	m_pBar->setDrawBase( false );
	m_pBar->setExpanding( false );
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
	return szUser.isEmpty() ? KindOf( pTab ) : szUser;
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
	m_szCurrent = szGroup;
	Refresh();
	QWidget *pCurrent = m_pTabs->currentWidget();
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
// Purpose: The strip's tabs ("All", then each group with its count) and which tabs show.
//-----------------------------------------------------------------------------
void CTabGroups::Refresh()
{
	m_bRefreshing = true;
	const QStringList groups = GetGroups();
	const bool bStrip = m_bEnabled && groups.size() > 1;
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
	for ( int i = 0; i < wanted.size(); i++ )
	{
		int nCount = 0;
		for ( int j = 1; j < m_pTabs->count(); j++ )
			nCount += i == 0 || GroupOf( m_pTabs->widget( j ) ) == wanted[ i ] ? 1 : 0;
		m_pBar->setTabText( i, QStringLiteral( "%1 (%2)" ).arg( i == 0 ? tr( ALL ) : wanted[ i ] ).arg( nCount ) );
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
