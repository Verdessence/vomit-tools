//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapRecent.h.
//
//===========================================================================//
#include "MapRecent.h"

#include <QLabel>
#include <QListWidget>
#include <QSettings>
#include <QVBoxLayout>

namespace
{

constexpr char CLASSES_SETTING[] = "maps/recentClasses";
constexpr char MODELS_SETTING[] = "maps/recentModels";

} // namespace

//-----------------------------------------------------------------------------
// Purpose: A heading and a list for each; the lists share the panel's height.
//-----------------------------------------------------------------------------
CMapRecentPanel::CMapRecentPanel( QWidget *pParent )
	: QWidget( pParent )
{
	setObjectName( QStringLiteral( "mapRecentPanel" ) );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	const auto add = [ & ]( const QString &szTitle, const char *pszName, const QString &szTip ) {
		pLayout->addWidget( new QLabel( szTitle ) );
		auto *pList = new QListWidget;
		pList->setObjectName( QLatin1String( pszName ) );
		pList->setToolTip( szTip );
		pList->setMinimumHeight( 3 * pList->fontMetrics().height() + 8 );
		pList->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Expanding );
		pList->setTextElideMode( Qt::ElideMiddle );
		pLayout->addWidget( pList, 1 );
		return pList;
	};
	m_pTextures = add( tr( "Textures" ), "mapRecentTextures", tr( "Click to make it the current texture" ) );
	m_pClasses = add( tr( "Entity classes" ), "mapRecentClasses", tr( "Click to have the entity tool place it" ) );
	m_pModels = add( tr( "Models" ), "mapRecentModels", tr( "Click to give the selected entities this model" ) );
	connect( m_pTextures, &QListWidget::itemClicked, this, [ this ]( QListWidgetItem *pItem ) { emit TexturePicked( pItem->text() ); } );
	connect( m_pClasses, &QListWidget::itemClicked, this, [ this ]( QListWidgetItem *pItem ) { emit ClassPicked( pItem->text() ); } );
	connect( m_pModels, &QListWidget::itemClicked, this, [ this ]( QListWidgetItem *pItem ) { emit ModelPicked( pItem->text() ); } );
	Fill( m_pClasses, QSettings{}.value( QLatin1String( CLASSES_SETTING ) ).toStringList() );
	Fill( m_pModels, QSettings{}.value( QLatin1String( MODELS_SETTING ) ).toStringList() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapRecentPanel::SetTextures( const QStringList &textures )
{
	Fill( m_pTextures, textures );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapRecentPanel::AddClass( const QString &szClass )
{
	Raise( m_pClasses, szClass, CLASSES_SETTING );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapRecentPanel::AddModel( const QString &szModel )
{
	Raise( m_pModels, szModel, MODELS_SETTING );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapRecentPanel::Fill( QListWidget *pList, const QStringList &items )
{
	pList->clear();
	pList->addItems( items.mid( 0, MAX_RECENT ) );
}

//-----------------------------------------------------------------------------
// Purpose: The newest first, without repeats; kept for the next map.
//-----------------------------------------------------------------------------
void CMapRecentPanel::Raise( QListWidget *pList, const QString &szItem, const char *pszSetting )
{
	if ( szItem.trimmed().isEmpty() )
		return;
	QStringList items{ szItem };
	for ( int i = 0; i < pList->count() && items.size() < MAX_RECENT; i++ )
	{
		if ( pList->item( i )->text().compare( szItem, Qt::CaseInsensitive ) != 0 )
			items << pList->item( i )->text();
	}
	Fill( pList, items );
	QSettings{}.setValue( QLatin1String( pszSetting ), items );
}
