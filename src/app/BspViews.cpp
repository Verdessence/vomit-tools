//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See BspViews.h.
//
//===========================================================================//
#include "BspViews.h"

#include <algorithm>

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "bsp/Bsp.h"

namespace
{

enum EntityColumn_t
{
	ENTITY_COLUMN_INDEX,
	ENTITY_COLUMN_CLASS,
	ENTITY_COLUMN_NAME,
	ENTITY_COLUMN_ORIGIN,
	ENTITY_COLUMN_COUNT
};

enum LumpColumn_t
{
	LUMP_COLUMN_INDEX,
	LUMP_COLUMN_NAME,
	LUMP_COLUMN_SIZE,
	LUMP_COLUMN_OFFSET,
	LUMP_COLUMN_VERSION,
	LUMP_COLUMN_COMPRESSED,
	LUMP_COLUMN_COUNT
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Qs( std::string_view sz )
{
	return QString::fromUtf8( sz.data(), static_cast<qsizetype>( sz.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose: A tree item that sorts its number columns as numbers.
//-----------------------------------------------------------------------------
class CNumberItem : public QTreeWidgetItem
{
public:
	using QTreeWidgetItem::QTreeWidgetItem;

	bool operator<( const QTreeWidgetItem &other ) const override
	{
		const int nColumn = treeWidget() ? treeWidget()->sortColumn() : 0;
		const QVariant left = data( nColumn, Qt::UserRole );
		const QVariant right = other.data( nColumn, Qt::UserRole );
		if ( left.isValid() && right.isValid() )
		{
			return left.toLongLong() < right.toLongLong();
		}
		return text( nColumn ).compare( other.text( nColumn ), Qt::CaseInsensitive ) < 0;
	}
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CEntityView::CEntityView( const xv::bsp::CBsp &bsp, QWidget *pParent )
	: QWidget( pParent ),
	  m_Bsp( bsp )
{
	m_pSearch = new QLineEdit;
	m_pSearch->setPlaceholderText( tr( "Search entities… (class, name, or any key or value)" ) );
	m_pSearch->setClearButtonEnabled( true );
	connect( m_pSearch, &QLineEdit::textChanged, this, &CEntityView::SetSearch );

	m_pList = new QTreeWidget;
	m_pList->setColumnCount( ENTITY_COLUMN_COUNT );
	m_pList->setHeaderLabels( { tr( "#" ), tr( "Class" ), tr( "Name" ), tr( "Origin" ) } );
	m_pList->setRootIsDecorated( false );
	m_pList->setUniformRowHeights( true );
	m_pList->setSortingEnabled( true );
	m_pList->header()->setStretchLastSection( true );
	const auto &entities = bsp.GetEntities();
	QList<QTreeWidgetItem *> items;
	items.reserve( static_cast<qsizetype>( entities.size() ) );
	for ( std::size_t i = 0; i < entities.size(); i++ )
	{
		const auto &entity = entities[ i ];
		auto *pItem = new CNumberItem;
		pItem->setText( ENTITY_COLUMN_INDEX, QString::number( i ) );
		pItem->setData( ENTITY_COLUMN_INDEX, Qt::UserRole, static_cast<qlonglong>( i ) );
		pItem->setText( ENTITY_COLUMN_CLASS, Qs( entity.Get( "classname" ) ) );
		pItem->setText( ENTITY_COLUMN_NAME, Qs( entity.Get( "targetname" ) ) );
		pItem->setText( ENTITY_COLUMN_ORIGIN, Qs( entity.Get( "origin" ) ) );
		items.push_back( pItem );
	}
	m_pList->addTopLevelItems( items );
	m_pList->sortByColumn( ENTITY_COLUMN_INDEX, Qt::AscendingOrder );
	m_pList->setColumnWidth( ENTITY_COLUMN_INDEX, 60 );
	m_pList->setColumnWidth( ENTITY_COLUMN_CLASS, 200 );
	m_pList->setColumnWidth( ENTITY_COLUMN_NAME, 160 );
	connect( m_pList, &QTreeWidget::currentItemChanged, this, [ this ]( QTreeWidgetItem *pCurrent ) {
		ShowKeyValues( pCurrent ? pCurrent->data( ENTITY_COLUMN_INDEX, Qt::UserRole ).toInt() : -1 );
	} );

	m_pKeyValues = new QTableWidget( 0, 2 );
	m_pKeyValues->setHorizontalHeaderLabels( { tr( "Key" ), tr( "Value" ) } );
	m_pKeyValues->horizontalHeader()->setStretchLastSection( true );
	m_pKeyValues->verticalHeader()->setVisible( false );
	m_pKeyValues->setEditTriggers( QAbstractItemView::NoEditTriggers );
	m_pKeyValues->setColumnWidth( 0, 180 );

	auto *pSplitter = new QSplitter;
	pSplitter->addWidget( m_pList );
	pSplitter->addWidget( m_pKeyValues );
	pSplitter->setSizes( { 560, 540 } );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 6, 6, 6, 0 );
	if ( !bsp.HasEntities() )
	{
		pLayout->addWidget( new QLabel( tr( "This map's entity lump is compressed and wouldn't unpack; it may be damaged." ) ) );
	}
	pLayout->addWidget( m_pSearch );
	pLayout->addWidget( pSplitter, 1 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEntityView::SetSearch( const QString &szSearch )
{
	if ( m_pSearch->text() != szSearch )
	{
		m_pSearch->setText( szSearch );
		return; // textChanged comes back here
	}
	const QString szText = szSearch.trimmed();
	const auto &entities = m_Bsp.GetEntities();
	const auto contains = [ &szText ]( std::string_view sz ) { return Qs( sz ).contains( szText, Qt::CaseInsensitive ); };
	for ( int i = 0; i < m_pList->topLevelItemCount(); i++ )
	{
		QTreeWidgetItem *pItem = m_pList->topLevelItem( i );
		const auto &entity = entities[ static_cast<std::size_t>( pItem->data( ENTITY_COLUMN_INDEX, Qt::UserRole ).toLongLong() ) ];
		const bool bShow = szText.isEmpty() || std::ranges::any_of( entity.m_KeyValues, [ & ]( const auto &pair ) { return contains( pair.first ) || contains( pair.second ); } );
		pItem->setHidden( !bShow );
	}
	// The keyvalues shown always belong to an entity in the list.
	if ( m_pList->currentItem() && m_pList->currentItem()->isHidden() )
	{
		m_pList->setCurrentItem( nullptr );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CEntityView::GetShownCount() const
{
	int nShown = 0;
	for ( int i = 0; i < m_pList->topLevelItemCount(); i++ )
	{
		nShown += !m_pList->topLevelItem( i )->isHidden();
	}
	return nShown;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEntityView::SelectEntity( int nIndex )
{
	for ( int i = 0; i < m_pList->topLevelItemCount(); i++ )
	{
		if ( m_pList->topLevelItem( i )->data( ENTITY_COLUMN_INDEX, Qt::UserRole ).toInt() == nIndex )
		{
			m_pList->setCurrentItem( m_pList->topLevelItem( i ) );
			return;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEntityView::ShowKeyValues( int nIndex )
{
	const auto &entities = m_Bsp.GetEntities();
	if ( nIndex < 0 || nIndex >= static_cast<int>( entities.size() ) )
	{
		m_pKeyValues->setRowCount( 0 );
		return;
	}
	const auto &keyValues = entities[ static_cast<std::size_t>( nIndex ) ].m_KeyValues;
	m_pKeyValues->setRowCount( static_cast<int>( keyValues.size() ) );
	for ( int nRow = 0; nRow < static_cast<int>( keyValues.size() ); nRow++ )
	{
		const auto &[ key, value ] = keyValues[ static_cast<std::size_t>( nRow ) ];
		// Outputs separate their parts with an escape character in newer games; shown as commas.
		QString szValue = Qs( value );
		szValue.replace( QChar( 0x1b ), u',' );
		m_pKeyValues->setItem( nRow, 0, new QTableWidgetItem( Qs( key ) ) );
		m_pKeyValues->setItem( nRow, 1, new QTableWidgetItem( szValue ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CLumpView::CLumpView( const xv::bsp::CBsp &bsp, QWidget *pParent )
	: QWidget( pParent )
{
	auto *pTree = new QTreeWidget;
	pTree->setColumnCount( LUMP_COLUMN_COUNT );
	pTree->setHeaderLabels( { tr( "#" ), tr( "Lump" ), tr( "Size" ), tr( "Offset" ), tr( "Version" ), tr( "Compressed" ) } );
	pTree->setRootIsDecorated( false );
	pTree->setUniformRowHeights( true );
	pTree->setSortingEnabled( true );
	int nUsed = 0;
	for ( const auto &lump : bsp.GetLumps() )
	{
		auto *pItem = new CNumberItem( pTree );
		pItem->setText( LUMP_COLUMN_INDEX, QString::number( lump.m_nIndex ) );
		pItem->setData( LUMP_COLUMN_INDEX, Qt::UserRole, lump.m_nIndex );
		pItem->setText( LUMP_COLUMN_NAME, QString::fromStdString( xv::bsp::LumpName( lump.m_nIndex ) ) );
		pItem->setText( LUMP_COLUMN_SIZE, QLocale{}.formattedDataSize( lump.m_nLength ) );
		pItem->setData( LUMP_COLUMN_SIZE, Qt::UserRole, static_cast<qlonglong>( lump.m_nLength ) );
		pItem->setText( LUMP_COLUMN_OFFSET, lump.m_nLength ? QString::number( lump.m_nOffset ) : QString{} );
		pItem->setData( LUMP_COLUMN_OFFSET, Qt::UserRole, static_cast<qlonglong>( lump.m_nOffset ) );
		pItem->setText( LUMP_COLUMN_VERSION, QString::number( lump.m_nVersion ) );
		pItem->setData( LUMP_COLUMN_VERSION, Qt::UserRole, static_cast<qlonglong>( lump.m_nVersion ) );
		pItem->setText( LUMP_COLUMN_COMPRESSED, lump.m_nUncompressed ? tr( "yes, %1 unpacked" ).arg( QLocale{}.formattedDataSize( lump.m_nUncompressed ) ) : QString{} );
		for ( int nColumn : { LUMP_COLUMN_INDEX, LUMP_COLUMN_SIZE, LUMP_COLUMN_OFFSET, LUMP_COLUMN_VERSION } )
		{
			pItem->setTextAlignment( nColumn, Qt::AlignRight | Qt::AlignVCenter );
		}
		if ( lump.m_nLength == 0 )
		{
			// Empty lumps stay listed (their slot exists) but greyed.
			for ( int nColumn = 0; nColumn < LUMP_COLUMN_COUNT; nColumn++ )
			{
				pItem->setForeground( nColumn, pTree->palette().brush( QPalette::Disabled, QPalette::Text ) );
			}
		}
		nUsed += lump.m_nLength != 0;
	}
	pTree->sortByColumn( LUMP_COLUMN_INDEX, Qt::AscendingOrder );
	pTree->setColumnWidth( LUMP_COLUMN_INDEX, 50 );
	pTree->setColumnWidth( LUMP_COLUMN_NAME, 260 );
	pTree->setColumnWidth( LUMP_COLUMN_SIZE, 100 );
	pTree->setColumnWidth( LUMP_COLUMN_OFFSET, 100 );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 6, 6, 6, 0 );
	pLayout->addWidget( new QLabel( tr( "BSP version %1, map revision %2. %3 of %4 lumps hold data." ).arg( bsp.GetVersion() ).arg( bsp.GetRevision() ).arg( nUsed ).arg( xv::bsp::LUMP_COUNT ) ) );
	pLayout->addWidget( pTree, 1 );
}
