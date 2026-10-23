//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MenuPicker.h.
//
//===========================================================================//
#include "MenuPicker.h"

#include <algorithm>
#include <memory>

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>

//-----------------------------------------------------------------------------
// Purpose: Fills a menu with a search box over a list, no taller than a quarter of the screen.
//-----------------------------------------------------------------------------
void FillPicker( QMenu *pSub, std::vector<PickerItem_t> items, const QString &szEmpty )
{
	if ( items.empty() )
	{
		pSub->addAction( szEmpty )->setEnabled( false );
		return;
	}
	auto pShared = std::make_shared<std::vector<PickerItem_t>>( std::move( items ) );
	auto *pBox = new QWidget;
	auto *pLayout = new QVBoxLayout( pBox );
	pLayout->setContentsMargins( 4, 4, 4, 4 );
	pLayout->setSpacing( 4 );
	auto *pFilter = new QLineEdit;
	pFilter->setPlaceholderText( QObject::tr( "Search…" ) );
	pFilter->setClearButtonEnabled( true );
	auto *pList = new QListWidget;
	pList->setObjectName( QStringLiteral( "picker" ) );
	for ( std::size_t i = 0; i < pShared->size(); i++ )
	{
		auto *pItem = new QListWidgetItem( ( *pShared )[ i ].m_szText, pList );
		pItem->setToolTip( ( *pShared )[ i ].m_szTip );
		pItem->setData( Qt::UserRole, static_cast<int>( i ) );
	}
	const QScreen *pScreen = pSub->screen();
	const int nQuarter = pScreen ? pScreen->availableGeometry().height() / 4 : 240;
	const int nRows = pList->sizeHintForRow( 0 ) * pList->count() + 2 * pList->frameWidth() + 2;
	pList->setFixedHeight( std::min( nQuarter, nRows ) );
	pList->setMinimumWidth( std::max( 240, pList->sizeHintForColumn( 0 ) + 32 ) );
	pLayout->addWidget( pFilter );
	pLayout->addWidget( pList );

	// A click can come as both clicked and activated; it runs once.
	auto pChosen = std::make_shared<bool>( false );
	const auto choose = [ pShared, pChosen ]( QListWidgetItem *pItem ) {
		if ( !pItem || *pChosen )
		{
			return;
		}
		*pChosen = true;
		const auto pfnRun = ( *pShared )[ static_cast<std::size_t>( pItem->data( Qt::UserRole ).toInt() ) ].m_pfnRun;
		// Nothing happens until the click is over. Closing the menus moves the hover, which on
		// some platforms shows this menu again at once and refills it; what runs may show a dialog.
		QTimer::singleShot( 0, qApp, [ pfnRun, pChosen ] {
			for ( int nTries = 0; nTries < 16 && QApplication::activePopupWidget(); nTries++ )
			{
				QApplication::activePopupWidget()->close();
			}
			*pChosen = false;
			pfnRun();
		} );
	};
	QObject::connect( pList, &QListWidget::itemClicked, pList, choose );
	QObject::connect( pList, &QListWidget::itemActivated, pList, choose );
	QObject::connect( pFilter, &QLineEdit::textChanged, pList, [ pList ]( const QString &szText ) {
		QListWidgetItem *pFirst = nullptr;
		for ( int i = 0; i < pList->count(); i++ )
		{
			QListWidgetItem *pItem = pList->item( i );
			// The tooltip counts too: a plain name finds the item by its real name and the other way round.
			pItem->setHidden( !pItem->text().contains( szText, Qt::CaseInsensitive ) && !pItem->toolTip().contains( szText, Qt::CaseInsensitive ) );
			pFirst = pFirst || pItem->isHidden() ? pFirst : pItem;
		}
		pList->setCurrentItem( pFirst );
	} );
	QObject::connect( pFilter, &QLineEdit::returnPressed, pList, [ pList, choose ] {
		choose( pList->currentItem() && !pList->currentItem()->isHidden() ? pList->currentItem() : nullptr );
	} );
	auto *pAction = new QWidgetAction( pSub );
	pAction->setDefaultWidget( pBox );
	pSub->addAction( pAction );
	QObject::connect( pSub, &QMenu::aboutToShow, pFilter, [ pFilter ] { pFilter->setFocus(); } );
	// Filled while the menu is about to show, the signal above comes too late.
	QTimer::singleShot( 0, pFilter, [ pFilter ] { pFilter->setFocus(); } );
}

//-----------------------------------------------------------------------------
// Purpose: Empties a menu that is filled again each time it shows, deleting what it held later.
//-----------------------------------------------------------------------------
void ClearMenu( QMenu *pMenu )
{
	for ( QAction *pAction : pMenu->actions() )
	{
		pMenu->removeAction( pAction );
		if ( QMenu *pSub = pAction->menu(); pSub && pSub->parent() == pMenu )
		{
			pSub->deleteLater();
		}
		else if ( pAction->parent() == pMenu )
		{
			pAction->deleteLater();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: A submenu holding the picker.
//-----------------------------------------------------------------------------
QMenu *AddPicker( QMenu *pParent, const QString &szTitle, std::vector<PickerItem_t> items, const QString &szEmpty )
{
	QMenu *pSub = pParent->addMenu( szTitle );
	FillPicker( pSub, std::move( items ), szEmpty );
	return pSub;
}
