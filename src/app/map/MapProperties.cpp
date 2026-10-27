//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The Object Properties panel: the selection's sheet (map::DescribeSelection) laid
//          out as Hammer's dialog lays it out, in tabs.
//
//===========================================================================//
#include "map/MapProperties.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "map/MapDocument.h"
#include "map/Scene.h"

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString FromStd( const std::string &sz )
{
	return QString::fromUtf8( sz.data(), static_cast<qsizetype>( sz.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose: A read-only list with a header row.
//-----------------------------------------------------------------------------
QTreeWidget *MakeList( const QString &szName, const QStringList &columns )
{
	auto *pList = new QTreeWidget;
	pList->setObjectName( szName );
	pList->setColumnCount( static_cast<int>( columns.size() ) );
	pList->setHeaderLabels( columns );
	pList->setHeaderHidden( columns.size() < 2 );
	pList->setRootIsDecorated( false );
	pList->setUniformRowHeights( true );
	pList->setAlternatingRowColors( true );
	pList->header()->setStretchLastSection( true );
	return pList;
}

//-----------------------------------------------------------------------------
// Purpose: A list with a Mark button under it, for the Outputs and Inputs tabs.
//-----------------------------------------------------------------------------
QWidget *WithMark( QTreeWidget *pList, QPushButton *pMark )
{
	auto *pPage = new QWidget;
	auto *pLayout = new QVBoxLayout( pPage );
	pLayout->setContentsMargins( 2, 2, 2, 2 );
	pLayout->addWidget( pList, 1 );
	auto *pRow = new QHBoxLayout;
	pRow->addStretch( 1 );
	pRow->addWidget( pMark );
	pLayout->addLayout( pRow );
	return pPage;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapProperties::CMapProperties( CMapDocument &document, QWidget *pParent )
	: QWidget( pParent ),
	  m_Document( document )
{
	setObjectName( QStringLiteral( "mapProperties" ) );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );

	auto *pTop = new QHBoxLayout;
	pTop->addWidget( new QLabel( tr( "Class:" ) ) );
	m_pClass = new QLineEdit;
	m_pClass->setObjectName( QStringLiteral( "mapPropertiesClass" ) );
	m_pClass->setReadOnly( true );
	m_pClass->setPlaceholderText( tr( "Nothing selected" ) );
	pTop->addWidget( m_pClass, 1 );
	m_pSmartEdit = new QToolButton;
	m_pSmartEdit->setObjectName( QStringLiteral( "mapPropertiesSmartEdit" ) );
	m_pSmartEdit->setText( tr( "SmartEdit" ) );
	m_pSmartEdit->setToolTip( tr( "Show keys and choices by the names the game data gives them" ) );
	m_pSmartEdit->setCheckable( true );
	m_pSmartEdit->setChecked( true );
	pTop->addWidget( m_pSmartEdit );
	pLayout->addLayout( pTop );

	m_pTabs = new QTabWidget;
	m_pTabs->setObjectName( QStringLiteral( "mapPropertiesTabs" ) );
	auto *pInfo = new QWidget;
	auto *pInfoLayout = new QVBoxLayout( pInfo );
	pInfoLayout->setContentsMargins( 2, 2, 2, 2 );
	m_pKeys = MakeList( QStringLiteral( "mapPropertiesKeys" ), { tr( "Property Name" ), tr( "Value" ) } );
	pInfoLayout->addWidget( m_pKeys, 1 );
	m_pHelp = new QLabel;
	m_pHelp->setObjectName( QStringLiteral( "mapPropertiesHelp" ) );
	m_pHelp->setWordWrap( true );
	m_pHelp->setTextFormat( Qt::PlainText );
	m_pHelp->setTextInteractionFlags( Qt::TextSelectableByMouse );
	pInfoLayout->addWidget( m_pHelp );
	pInfoLayout->addWidget( new QLabel( tr( "Comments:" ) ) );
	m_pComments = new QPlainTextEdit;
	m_pComments->setObjectName( QStringLiteral( "mapPropertiesComments" ) );
	m_pComments->setReadOnly( true );
	m_pComments->setMaximumHeight( 60 );
	pInfoLayout->addWidget( m_pComments );
	m_pTabs->addTab( pInfo, tr( "Class Info" ) );

	m_pFlags = MakeList( QStringLiteral( "mapPropertiesFlags" ), { tr( "Flags" ) } );
	m_pTabs->addTab( m_pFlags, tr( "Flags" ) );

	m_pOutputs = MakeList( QStringLiteral( "mapPropertiesOutputs" ), { tr( "My Output" ), tr( "Target Entity" ), tr( "Target Input" ), tr( "Parameter" ), tr( "Delay" ), tr( "Only Once" ) } );
	auto *pMarkTargets = new QPushButton( tr( "Mark" ) );
	pMarkTargets->setObjectName( QStringLiteral( "mapPropertiesMarkTarget" ) );
	pMarkTargets->setToolTip( tr( "Select the entities the output is sent to" ) );
	m_pTabs->addTab( WithMark( m_pOutputs, pMarkTargets ), tr( "Outputs" ) );
	m_pInputs = MakeList( QStringLiteral( "mapPropertiesInputs" ), { tr( "Source Entity" ), tr( "Output" ), tr( "My Input" ), tr( "Parameter" ), tr( "Delay" ), tr( "Only Once" ) } );
	auto *pMarkSources = new QPushButton( tr( "Mark" ) );
	pMarkSources->setObjectName( QStringLiteral( "mapPropertiesMarkSource" ) );
	pMarkSources->setToolTip( tr( "Select the entity the output comes from" ) );
	m_pTabs->addTab( WithMark( m_pInputs, pMarkSources ), tr( "Inputs" ) );
	pLayout->addWidget( m_pTabs, 1 );

	connect( m_pSmartEdit, &QToolButton::toggled, this, &CMapProperties::Refresh );
	connect( m_pKeys, &QTreeWidget::currentItemChanged, this, &CMapProperties::ShowHelp );
	connect( pMarkTargets, &QPushButton::clicked, this, [ this ] { Mark( m_pOutputs ); } );
	connect( pMarkSources, &QPushButton::clicked, this, [ this ] { Mark( m_pInputs ); } );
	connect( m_pOutputs, &QTreeWidget::itemDoubleClicked, this, [ this ] { Mark( m_pOutputs ); } );
	connect( m_pInputs, &QTreeWidget::itemDoubleClicked, this, [ this ] { Mark( m_pInputs ); } );
	connect( &m_Document, &CMapDocument::SelectionChanged, this, &CMapProperties::Refresh );
	connect( &m_Document, &CMapDocument::Loaded, this, &CMapProperties::Refresh );
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapProperties::IsSmartEdit() const
{
	return m_pSmartEdit->isChecked();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapProperties::SetSmartEdit( bool bSmartEdit )
{
	m_pSmartEdit->setChecked( bSmartEdit );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapProperties::Refresh()
{
	const xv::map::CScene *pScene = m_Document.GetScene();
	m_Sheet = pScene ? xv::map::DescribeSelection( *pScene, m_Document.GetSelection(), IsSmartEdit() ) : xv::map::PropertySheet_t{};
	if ( m_Sheet.m_nEntities == 0 )
	{
		const std::size_t nBrushes = m_Sheet.m_nWorldSolids;
		m_pClass->setText( nBrushes == 0 ? QString{} : nBrushes == 1 ? tr( "1 world brush" )
																	 : tr( "%1 world brushes" ).arg( nBrushes ) );
	}
	else
	{
		m_pClass->setText( m_Sheet.m_szClass.empty() ? tr( "(several classes)" ) : FromStd( m_Sheet.m_szClass ) );
	}

	m_pKeys->clear();
	m_pKeys->setHeaderLabels( IsSmartEdit() ? QStringList{ tr( "Property Name" ), tr( "Value" ) } : QStringList{ tr( "Key" ), tr( "Value" ) } );
	for ( std::size_t i = 0; i < m_Sheet.m_Rows.size(); i++ )
	{
		const xv::map::PropertyRow_t &row = m_Sheet.m_Rows[ i ];
		auto *pItem = new QTreeWidgetItem( QStringList{ FromStd( row.m_szName ), FromStd( row.m_szShown ) } );
		pItem->setData( 0, Qt::UserRole, static_cast<qulonglong>( i ) );
		if ( row.m_bDefault )
		{
			// Not in the map: what the game data says it defaults to.
			QFont font = pItem->font( 1 );
			font.setItalic( true );
			pItem->setFont( 1, font );
			pItem->setToolTip( 1, tr( "Not set in the map; the game data's default" ) );
		}
		if ( row.m_bDiffers )
		{
			pItem->setToolTip( 1, tr( "The selected entities differ" ) );
		}
		m_pKeys->addTopLevelItem( pItem );
	}
	m_pKeys->resizeColumnToContents( 0 );

	m_pFlags->clear();
	for ( const xv::map::PropertyFlag_t &flag : m_Sheet.m_Flags )
	{
		auto *pItem = new QTreeWidgetItem( QStringList{ FromStd( flag.m_szName ) } );
		pItem->setFlags( Qt::ItemIsEnabled );
		pItem->setCheckState( 0, flag.m_nState == xv::map::PropertyFlagState_t::PROPERTY_FLAG_STATE_ON ? Qt::Checked : flag.m_nState == xv::map::PropertyFlagState_t::PROPERTY_FLAG_STATE_MIXED ? Qt::PartiallyChecked
																																																: Qt::Unchecked );
		pItem->setToolTip( 0, QString::number( flag.m_nBit ) );
		m_pFlags->addTopLevelItem( pItem );
	}
	m_pTabs->setTabEnabled( 1, !m_Sheet.m_Flags.empty() );

	auto fill = [ this ]( QTreeWidget *pList, const std::vector<xv::map::PropertyConnection_t> &rows, bool bInputs ) {
		pList->clear();
		for ( std::size_t i = 0; i < rows.size(); i++ )
		{
			const xv::map::PropertyConnection_t &row = rows[ i ];
			const QString szDelay = QString::number( row.m_flDelay, 'f', 2 );
			const QString szOnce = row.m_bOnce ? tr( "Yes" ) : tr( "No" );
			QStringList columns = bInputs ? QStringList{ FromStd( row.m_szEntity ), FromStd( row.m_szOutput ), FromStd( row.m_szInput ), FromStd( row.m_szParameter ), szDelay, szOnce }
										  : QStringList{ FromStd( row.m_szOutput ), FromStd( row.m_szEntity ), FromStd( row.m_szInput ), FromStd( row.m_szParameter ), szDelay, szOnce };
			auto *pItem = new QTreeWidgetItem( columns );
			pItem->setData( 0, Qt::UserRole, static_cast<qulonglong>( i ) );
			pList->addTopLevelItem( pItem );
		}
	};
	fill( m_pOutputs, m_Sheet.m_Outputs, false );
	fill( m_pInputs, m_Sheet.m_Inputs, true );
	m_pComments->setPlainText( FromStd( m_Sheet.m_szComments ) );
	ShowHelp();
}

//-----------------------------------------------------------------------------
// Purpose: The selected key's help, or the class's when no key is picked.
//-----------------------------------------------------------------------------
void CMapProperties::ShowHelp()
{
	const QTreeWidgetItem *pItem = m_pKeys->currentItem();
	const std::size_t i = pItem ? pItem->data( 0, Qt::UserRole ).toULongLong() : m_Sheet.m_Rows.size();
	if ( i < m_Sheet.m_Rows.size() )
	{
		const xv::map::PropertyRow_t &row = m_Sheet.m_Rows[ i ];
		m_pHelp->setText( row.m_szHelp.empty() ? FromStd( row.m_szKey ) : QStringLiteral( "%1: %2" ).arg( FromStd( row.m_szKey ), FromStd( row.m_szHelp ) ) );
		return;
	}
	m_pHelp->setText( FromStd( m_Sheet.m_szClassHelp ) );
}

//-----------------------------------------------------------------------------
// Purpose: Selects the other end of the current output: the entities it is sent to, or the
//          one it comes from.
//-----------------------------------------------------------------------------
void CMapProperties::Mark( const QTreeWidget *pList )
{
	const xv::map::CScene *pScene = m_Document.GetScene();
	const QTreeWidgetItem *pItem = pList->currentItem();
	if ( !pScene || !pItem )
	{
		return;
	}
	const std::size_t i = pItem->data( 0, Qt::UserRole ).toULongLong();
	std::vector<std::uint32_t> objects;
	if ( pList == m_pInputs && i < m_Sheet.m_Inputs.size() )
	{
		objects = m_Document.Expand( m_Sheet.m_Inputs[ i ].m_nObject );
	}
	else if ( pList == m_pOutputs && i < m_Sheet.m_Outputs.size() )
	{
		const std::string &szTarget = m_Sheet.m_Outputs[ i ].m_szEntity;
		for ( std::size_t e = 0; e < pScene->m_Map.m_Entities.size(); e++ )
		{
			const std::string_view szName = pScene->m_Map.m_Entities[ e ].GetValue( "targetname" );
			if ( !szName.empty() && xv::map::TargetMatches( szTarget, szName ) )
			{
				const std::vector<std::uint32_t> more = m_Document.Expand( pScene->EntityObject( e ) );
				objects.insert( objects.end(), more.begin(), more.end() );
			}
		}
	}
	if ( !objects.empty() )
	{
		m_Document.SetSelection( std::move( objects ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapProperties::FocusKeys()
{
	m_pTabs->setCurrentIndex( 0 );
	m_pKeys->setFocus( Qt::ShortcutFocusReason );
	if ( !m_pKeys->currentItem() && m_pKeys->topLevelItemCount() > 0 )
	{
		m_pKeys->setCurrentItem( m_pKeys->topLevelItem( 0 ) );
	}
}
