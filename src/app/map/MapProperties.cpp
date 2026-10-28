//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapProperties.h.
//
//===========================================================================//
#include "MapProperties.h"

#include <algorithm>
#include <cmath>

#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "kv1/Kv1.h"
#include "util/Numbers.h"

namespace
{

constexpr int ROLE_ROW = Qt::UserRole;        // the sheet's row (or output) the item shows
constexpr int ROLE_VALUE = Qt::UserRole + 1;  // a key's value as stored, behind a choice's name
constexpr int ROLE_OBJECT = Qt::UserRole + 2; // an output's entity

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString FromStd( const std::string &sz )
{
	return QString::fromUtf8( sz.data(), static_cast<qsizetype>( sz.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString FromView( std::string_view sz )
{
	return QString::fromUtf8( sz.data(), static_cast<qsizetype>( sz.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose: A list with a header row.
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
	pList->setEditTriggers( QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed );
	return pList;
}

//-----------------------------------------------------------------------------
// Purpose: A list with buttons under it.
//-----------------------------------------------------------------------------
QWidget *WithButtons( QTreeWidget *pList, const std::vector<QPushButton *> &buttons )
{
	auto *pPage = new QWidget;
	auto *pLayout = new QVBoxLayout( pPage );
	pLayout->setContentsMargins( 2, 2, 2, 2 );
	pLayout->addWidget( pList, 1 );
	auto *pRow = new QHBoxLayout;
	pRow->addStretch( 1 );
	for ( QPushButton *pButton : buttons )
	{
		pRow->addWidget( pButton );
	}
	pLayout->addLayout( pRow );
	return pPage;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPropertyDelegate::CPropertyDelegate( CMapProperties &panel, QTreeWidget &list )
	: QStyledItemDelegate( &list ),
	  m_Panel( panel ),
	  m_List( list )
{
}

//-----------------------------------------------------------------------------
// Purpose: A combo for a choice, a line for anything else.
//-----------------------------------------------------------------------------
QWidget *CPropertyDelegate::createEditor( QWidget *pParent, const QStyleOptionViewItem &, const QModelIndex &index ) const
{
	const auto nRow = static_cast<std::size_t>( index.data( ROLE_ROW ).toULongLong() );
	const std::vector<std::pair<std::string, std::string>> choices = &m_List == m_Panel.GetKeys() ? m_Panel.GetKeyChoices( nRow ) : m_Panel.GetOutputChoices( nRow, index.column() );
	if ( choices.empty() )
	{
		return new QLineEdit( pParent );
	}
	auto *pCombo = new QComboBox( pParent );
	pCombo->setEditable( true );
	for ( const auto &[ szValue, szName ] : choices )
	{
		pCombo->addItem( FromStd( szName ), FromStd( szValue ) );
	}
	return pCombo;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPropertyDelegate::setEditorData( QWidget *pEditor, const QModelIndex &index ) const
{
	const QString szValue = index.data( ROLE_VALUE ).isValid() ? index.data( ROLE_VALUE ).toString() : index.data( Qt::DisplayRole ).toString();
	if ( auto *pCombo = qobject_cast<QComboBox *>( pEditor ) )
	{
		const int nChoice = pCombo->findData( szValue );
		if ( nChoice >= 0 )
		{
			pCombo->setCurrentIndex( nChoice );
		}
		else
		{
			pCombo->setCurrentText( szValue );
		}
	}
	else if ( auto *pLine = qobject_cast<QLineEdit *>( pEditor ) )
	{
		pLine->setText( szValue );
		pLine->selectAll();
	}
}

//-----------------------------------------------------------------------------
// Purpose: The value goes to the panel as an edit; the list is filled again from the map.
//-----------------------------------------------------------------------------
void CPropertyDelegate::setModelData( QWidget *pEditor, QAbstractItemModel *, const QModelIndex &index ) const
{
	QString szValue;
	if ( auto *pCombo = qobject_cast<QComboBox *>( pEditor ) )
	{
		// A name picked from the list means its value; anything typed is taken as it is.
		const int nChoice = pCombo->findText( pCombo->currentText() );
		szValue = nChoice >= 0 ? pCombo->itemData( nChoice ).toString() : pCombo->currentText();
	}
	else if ( auto *pLine = qobject_cast<QLineEdit *>( pEditor ) )
	{
		szValue = pLine->text();
	}
	const auto nRow = static_cast<std::size_t>( index.data( ROLE_ROW ).toULongLong() );
	if ( &m_List == m_Panel.GetKeys() )
	{
		if ( index.column() == 0 )
		{
			m_Panel.RenameKey( nRow, szValue.toStdString() );
		}
		else
		{
			m_Panel.CommitKey( nRow, szValue.toStdString() );
		}
	}
	else
	{
		m_Panel.CommitOutput( nRow, index.column(), szValue.toStdString() );
	}
}

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
	m_pClass->setPlaceholderText( tr( "Nothing selected" ) );
	m_pClass->setToolTip( tr( "The entities' class; type another to change it" ) );
	pTop->addWidget( m_pClass, 1 );
	m_pSmartEdit = new QToolButton;
	m_pSmartEdit->setObjectName( QStringLiteral( "mapPropertiesSmartEdit" ) );
	m_pSmartEdit->setText( tr( "SmartEdit" ) );
	m_pSmartEdit->setToolTip( tr( "Show keys and choices by the names the game data gives them; off, the keys as the file has them, to add and change" ) );
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
	m_pKeys->setItemDelegate( new CPropertyDelegate( *this, *m_pKeys ) );
	pInfoLayout->addWidget( m_pKeys, 1 );
	auto *pKeyButtons = new QHBoxLayout;
	pKeyButtons->addStretch( 1 );
	m_pAddKey = new QPushButton( tr( "Add Key" ) );
	m_pAddKey->setObjectName( QStringLiteral( "mapPropertiesAddKey" ) );
	m_pRemoveKey = new QPushButton( tr( "Remove Key" ) );
	m_pRemoveKey->setObjectName( QStringLiteral( "mapPropertiesRemoveKey" ) );
	pKeyButtons->addWidget( m_pAddKey );
	pKeyButtons->addWidget( m_pRemoveKey );
	pInfoLayout->addLayout( pKeyButtons );
	m_pHelp = new QLabel;
	m_pHelp->setObjectName( QStringLiteral( "mapPropertiesHelp" ) );
	m_pHelp->setWordWrap( true );
	m_pHelp->setTextFormat( Qt::PlainText );
	m_pHelp->setTextInteractionFlags( Qt::TextSelectableByMouse );
	pInfoLayout->addWidget( m_pHelp );
	pInfoLayout->addWidget( new QLabel( tr( "Comments:" ) ) );
	m_pComments = new QPlainTextEdit;
	m_pComments->setObjectName( QStringLiteral( "mapPropertiesComments" ) );
	m_pComments->setMaximumHeight( 60 );
	m_pComments->installEventFilter( this );
	pInfoLayout->addWidget( m_pComments );
	m_pTabs->addTab( pInfo, tr( "Class Info" ) );

	m_pFlags = MakeList( QStringLiteral( "mapPropertiesFlags" ), { tr( "Flags" ) } );
	m_pFlags->setEditTriggers( QAbstractItemView::NoEditTriggers );
	m_pTabs->addTab( m_pFlags, tr( "Flags" ) );

	m_pOutputs = MakeList( QStringLiteral( "mapPropertiesOutputs" ), { tr( "My Output" ), tr( "Target Entity" ), tr( "Target Input" ), tr( "Parameter" ), tr( "Delay" ), tr( "Only Once" ) } );
	m_pOutputs->setItemDelegate( new CPropertyDelegate( *this, *m_pOutputs ) );
	auto *pAddOutput = new QPushButton( tr( "Add" ) );
	pAddOutput->setObjectName( QStringLiteral( "mapPropertiesAddOutput" ) );
	auto *pRemoveOutput = new QPushButton( tr( "Delete" ) );
	pRemoveOutput->setObjectName( QStringLiteral( "mapPropertiesRemoveOutput" ) );
	auto *pMarkTargets = new QPushButton( tr( "Mark" ) );
	pMarkTargets->setObjectName( QStringLiteral( "mapPropertiesMarkTarget" ) );
	pMarkTargets->setToolTip( tr( "Select the entities the output is sent to" ) );
	m_pTabs->addTab( WithButtons( m_pOutputs, { pAddOutput, pRemoveOutput, pMarkTargets } ), tr( "Outputs" ) );
	m_pInputs = MakeList( QStringLiteral( "mapPropertiesInputs" ), { tr( "Source Entity" ), tr( "Output" ), tr( "My Input" ), tr( "Parameter" ), tr( "Delay" ), tr( "Only Once" ) } );
	m_pInputs->setEditTriggers( QAbstractItemView::NoEditTriggers );
	auto *pMarkSources = new QPushButton( tr( "Mark" ) );
	pMarkSources->setObjectName( QStringLiteral( "mapPropertiesMarkSource" ) );
	pMarkSources->setToolTip( tr( "Select the entity the output comes from" ) );
	m_pTabs->addTab( WithButtons( m_pInputs, { pMarkSources } ), tr( "Inputs" ) );
	pLayout->addWidget( m_pTabs, 1 );

	connect( m_pSmartEdit, &QToolButton::toggled, this, &CMapProperties::Refresh );
	connect( m_pKeys, &QTreeWidget::currentItemChanged, this, &CMapProperties::ShowHelp );
	connect( m_pClass, &QLineEdit::editingFinished, this, [ this ] { CommitClass( m_pClass->text().trimmed().toStdString() ); } );
	connect( m_pAddKey, &QPushButton::clicked, this, &CMapProperties::AddKey );
	connect( m_pRemoveKey, &QPushButton::clicked, this, &CMapProperties::RemoveKey );
	connect( m_pFlags, &QTreeWidget::itemChanged, this, &CMapProperties::FlagChanged );
	connect( pAddOutput, &QPushButton::clicked, this, &CMapProperties::AddOutput );
	connect( pRemoveOutput, &QPushButton::clicked, this, &CMapProperties::RemoveOutput );
	connect( pMarkTargets, &QPushButton::clicked, this, [ this ] { Mark( m_pOutputs ); } );
	connect( pMarkSources, &QPushButton::clicked, this, [ this ] { Mark( m_pInputs ); } );
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
void CMapProperties::SetWorld( bool bWorld )
{
	m_bWorld = bWorld;
	m_pClass->setEnabled( !bWorld );
	m_pTabs->setTabVisible( 3, !bWorld );
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapProperties::Refresh()
{
	const xv::map::CScene *pScene = m_Document.GetScene();
	m_Sheet = !pScene ? xv::map::PropertySheet_t{} : m_bWorld ? xv::map::DescribeWorld( *pScene, IsSmartEdit() )
															  : xv::map::DescribeSelection( *pScene, m_Document.GetSelection(), IsSmartEdit() );
	m_bFilling = true;
	const bool bEntities = m_Sheet.m_nEntities > 0;
	if ( !bEntities )
	{
		const std::size_t nBrushes = m_Sheet.m_nWorldSolids;
		m_pClass->setText( nBrushes == 0 ? QString{} : nBrushes == 1 ? tr( "1 world brush" )
																	 : tr( "%1 world brushes" ).arg( nBrushes ) );
	}
	else
	{
		m_pClass->setText( m_Sheet.m_szClass.empty() ? tr( "(several classes)" ) : FromStd( m_Sheet.m_szClass ) );
	}
	m_pClass->setReadOnly( !bEntities || m_bWorld );
	// Every class the game data knows, offered as one is typed.
	QStringList classes;
	for ( const auto &[ szLower, fgdClass ] : pScene && pScene->m_pFgd ? pScene->m_pFgd->GetClasses() : std::map<std::string, xv::map::FgdClass_t>{} )
	{
		if ( fgdClass.m_nKind != xv::map::FgdClassKind_t::FGD_CLASS_KIND_BASE )
		{
			classes << FromStd( fgdClass.m_szName );
		}
	}
	delete m_pClass->completer();
	auto *pCompleter = new QCompleter( classes, m_pClass );
	pCompleter->setCaseSensitivity( Qt::CaseInsensitive );
	pCompleter->setFilterMode( Qt::MatchContains );
	m_pClass->setCompleter( pCompleter );

	m_pKeys->clear();
	m_pKeys->setHeaderLabels( IsSmartEdit() ? QStringList{ tr( "Property Name" ), tr( "Value" ) } : QStringList{ tr( "Key" ), tr( "Value" ) } );
	for ( std::size_t i = 0; i < m_Sheet.m_Rows.size(); i++ )
	{
		const xv::map::PropertyRow_t &row = m_Sheet.m_Rows[ i ];
		auto *pItem = new QTreeWidgetItem( QStringList{ FromStd( row.m_szName ), FromStd( row.m_szShown ) } );
		pItem->setData( 0, ROLE_ROW, static_cast<qulonglong>( i ) );
		pItem->setData( 1, ROLE_ROW, static_cast<qulonglong>( i ) );
		pItem->setData( 1, ROLE_VALUE, FromStd( row.m_szValue ) );
		pItem->setFlags( pItem->flags() | Qt::ItemIsEditable );
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
			pItem->setToolTip( 1, tr( "The selected entities differ; a value typed here goes to all of them" ) );
		}
		m_pKeys->addTopLevelItem( pItem );
	}
	m_pKeys->resizeColumnToContents( 0 );
	m_pAddKey->setVisible( !IsSmartEdit() );
	m_pRemoveKey->setVisible( !IsSmartEdit() );
	m_pAddKey->setEnabled( bEntities );
	m_pRemoveKey->setEnabled( bEntities );

	m_pFlags->clear();
	for ( const xv::map::PropertyFlag_t &flag : m_Sheet.m_Flags )
	{
		auto *pItem = new QTreeWidgetItem( QStringList{ FromStd( flag.m_szName ) } );
		pItem->setFlags( Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable );
		pItem->setCheckState( 0, flag.m_nState == xv::map::PropertyFlagState_t::PROPERTY_FLAG_STATE_ON ? Qt::Checked : flag.m_nState == xv::map::PropertyFlagState_t::PROPERTY_FLAG_STATE_MIXED ? Qt::PartiallyChecked
																																																: Qt::Unchecked );
		pItem->setData( 0, ROLE_ROW, flag.m_nBit );
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
			for ( int c = 0; c < columns.size(); c++ )
			{
				pItem->setData( c, ROLE_ROW, static_cast<qulonglong>( i ) );
				pItem->setData( c, ROLE_OBJECT, row.m_nObject );
			}
			if ( !bInputs )
			{
				pItem->setFlags( pItem->flags() | Qt::ItemIsEditable );
				pItem->setData( 5, ROLE_VALUE, row.m_bOnce ? QStringLiteral( "1" ) : QStringLiteral( "0" ) );
				if ( !TargetExists( row.m_szEntity ) )
				{
					// Hammer's red: nothing in the map answers to the name.
					pItem->setForeground( 1, QBrush{ QColor{ 220, 0, 0 } } );
					pItem->setToolTip( 1, tr( "No entity in the map has this name" ) );
				}
			}
			pList->addTopLevelItem( pItem );
		}
	};
	fill( m_pOutputs, m_Sheet.m_Outputs, false );
	fill( m_pInputs, m_Sheet.m_Inputs, true );
	m_pComments->setPlainText( FromStd( m_Sheet.m_szComments ) );
	m_pComments->setReadOnly( m_Sheet.m_nEntities != 1 );
	m_bFilling = false;
	ShowHelp();
}

//-----------------------------------------------------------------------------
// Purpose: The selected key's help, or the class's when no key is picked.
//-----------------------------------------------------------------------------
void CMapProperties::ShowHelp()
{
	const QTreeWidgetItem *pItem = m_pKeys->currentItem();
	const std::size_t i = pItem ? pItem->data( 0, ROLE_ROW ).toULongLong() : m_Sheet.m_Rows.size();
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
	const std::size_t i = pItem->data( 0, ROLE_ROW ).toULongLong();
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
	m_pKeys->setFocus();
	if ( m_pKeys->topLevelItemCount() > 0 && !m_pKeys->currentItem() )
	{
		m_pKeys->setCurrentItem( m_pKeys->topLevelItem( 0 ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Made once the list is done with its editor: the list is filled again from the map
//          as the edit goes through, which mustn't happen under the editor's feet.
//-----------------------------------------------------------------------------
void CMapProperties::Apply( const QString &szName, std::function<xv::map::MapEdit_t( const xv::map::CScene & )> pfnEdit )
{
	QTimer::singleShot( 0, this, [ this, szName, pfnEdit = std::move( pfnEdit ) ] {
		const xv::map::CScene *pScene = m_Document.GetScene();
		if ( !pScene )
		{
			return;
		}
		xv::map::MapEdit_t edit = pfnEdit( *pScene );
		if ( !edit.IsEmpty() )
		{
			m_Document.Edit( std::move( edit ), szName );
		}
	} );
}

//-----------------------------------------------------------------------------
// Purpose: The selected entities changed by pfnEdit, or the world when the panel shows it.
//-----------------------------------------------------------------------------
xv::map::MapEdit_t CMapProperties::EditShown( const xv::map::CScene &scene, const std::vector<std::uint32_t> &selection, const std::function<void( xv::map::Entity_t & )> &pfnEdit ) const
{
	return m_bWorld ? xv::map::EditWorld( scene, pfnEdit ) : xv::map::EditEntities( scene, selection, pfnEdit );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapProperties::TargetExists( const std::string &szTarget ) const
{
	const xv::map::CScene *pScene = m_Document.GetScene();
	if ( !pScene || szTarget.empty() )
	{
		return true;
	}
	for ( const xv::map::Entity_t &entity : pScene->m_Map.m_Entities )
	{
		if ( entity.m_bDeleted )
		{
			continue;
		}
		const std::string_view szName = entity.GetValue( "targetname" );
		if ( ( !szName.empty() && xv::map::TargetMatches( szTarget, szName ) ) || xv::kv1::EqualsIgnoreCase( entity.GetClassname(), szTarget ) )
		{
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Every selected entity gets the value (an empty one on a key the game data knows
//          takes the key out, so its default holds again).
//-----------------------------------------------------------------------------
void CMapProperties::CommitKey( std::size_t nRow, const std::string &szValue )
{
	if ( nRow >= m_Sheet.m_Rows.size() )
	{
		return;
	}
	const xv::map::PropertyRow_t &row = m_Sheet.m_Rows[ nRow ];
	if ( !row.m_bDiffers && szValue == row.m_szValue )
	{
		return;
	}
	const std::string szKey = row.m_szKey;
	const bool bRemove = szValue.empty() && !row.m_szType.empty();
	Apply( tr( "Change %1" ).arg( FromStd( szKey ) ), [ this, szKey, szValue, bRemove, selection = m_Document.GetSelection() ]( const xv::map::CScene &scene ) {
		return EditShown( scene, selection, [ & ]( xv::map::Entity_t &entity ) {
			if ( bRemove )
			{
				xv::map::RemoveKey( entity, szKey );
			}
			else
			{
				xv::map::SetKeyValue( scene.m_Map, entity, szKey, szValue );
			}
		} );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: SmartEdit off: the key itself changed; its value goes with it.
//-----------------------------------------------------------------------------
void CMapProperties::RenameKey( std::size_t nRow, const std::string &szKey )
{
	if ( nRow >= m_Sheet.m_Rows.size() || szKey.empty() || szKey == m_Sheet.m_Rows[ nRow ].m_szKey )
	{
		return;
	}
	const std::string szOld = m_Sheet.m_Rows[ nRow ].m_szKey;
	const std::string szValue = m_Sheet.m_Rows[ nRow ].m_szValue;
	Apply( tr( "Rename %1" ).arg( FromStd( szOld ) ), [ this, szOld, szKey, szValue, selection = m_Document.GetSelection() ]( const xv::map::CScene &scene ) {
		return EditShown( scene, selection, [ & ]( xv::map::Entity_t &entity ) {
			xv::map::RemoveKey( entity, szOld );
			xv::map::SetKeyValue( scene.m_Map, entity, szKey, szValue );
		} );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapProperties::CommitClass( const std::string &szClass )
{
	if ( m_Sheet.m_nEntities == 0 || szClass.empty() || xv::kv1::EqualsIgnoreCase( szClass, m_Sheet.m_szClass ) )
	{
		return;
	}
	Apply( tr( "Change Class" ), [ this, szClass, selection = m_Document.GetSelection() ]( const xv::map::CScene &scene ) {
		return EditShown( scene, selection, [ & ]( xv::map::Entity_t &entity ) { xv::map::SetKeyValue( scene.m_Map, entity, "classname", szClass ); } );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: SmartEdit off: a key named newkey (a number on it when that is taken), to rename
//          and fill in.
//-----------------------------------------------------------------------------
void CMapProperties::AddKey()
{
	std::string szKey = "newkey";
	for ( int n = 2; std::ranges::any_of( m_Sheet.m_Rows, [ & ]( const xv::map::PropertyRow_t &row ) { return row.m_szKey == szKey; } ); n++ )
	{
		szKey = "newkey" + std::to_string( n );
	}
	Apply( tr( "Add Key" ), [ this, szKey, selection = m_Document.GetSelection() ]( const xv::map::CScene &scene ) {
		return EditShown( scene, selection, [ & ]( xv::map::Entity_t &entity ) { xv::map::SetKeyValue( scene.m_Map, entity, szKey, "" ); } );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapProperties::RemoveKey()
{
	const QTreeWidgetItem *pItem = m_pKeys->currentItem();
	const std::size_t nRow = pItem ? pItem->data( 0, ROLE_ROW ).toULongLong() : m_Sheet.m_Rows.size();
	if ( nRow >= m_Sheet.m_Rows.size() )
	{
		return;
	}
	const std::string szKey = m_Sheet.m_Rows[ nRow ].m_szKey;
	Apply( tr( "Remove %1" ).arg( FromStd( szKey ) ), [ this, szKey, selection = m_Document.GetSelection() ]( const xv::map::CScene &scene ) {
		return EditShown( scene, selection, [ & ]( xv::map::Entity_t &entity ) { xv::map::RemoveKey( entity, szKey ); } );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: A flag ticked or cleared on every selected entity.
//-----------------------------------------------------------------------------
void CMapProperties::FlagChanged( QTreeWidgetItem *pItem )
{
	if ( m_bFilling || !pItem )
	{
		return;
	}
	const auto nBit = pItem->data( 0, ROLE_ROW ).toUInt();
	const bool bOn = pItem->checkState( 0 ) == Qt::Checked;
	Apply( tr( "Change Flags" ), [ this, nBit, bOn, nDefault = m_Sheet.m_nFlagsDefault, selection = m_Document.GetSelection() ]( const xv::map::CScene &scene ) {
		return EditShown( scene, selection, [ & ]( xv::map::Entity_t &entity ) { xv::map::SetSpawnFlag( scene.m_Map, entity, nBit, bOn, nDefault ); } );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: The outputs of one entity, as the list shows them (a changed cell already in it).
//          Their text goes into the map's store, which every scene shares, so the views into
//          it hold until the edit is made.
//-----------------------------------------------------------------------------
std::vector<xv::map::Connection_t> CMapProperties::OutputsOf( std::uint32_t nObject ) const
{
	std::vector<xv::map::Connection_t> outputs;
	const xv::map::CScene *pScene = m_Document.GetScene();
	if ( !pScene )
	{
		return outputs;
	}
	auto store = [ & ]( const QString &sz ) { return pScene->m_Map.Store( sz.toStdString() ); };
	const int nEntity = nObject == xv::map::WORLD_OBJECT ? -1 : pScene->GetObjectEntity( nObject );
	const xv::map::Entity_t &owner = nEntity >= 0 ? pScene->m_Map.m_Entities[ static_cast<std::size_t>( nEntity ) ] : pScene->m_Map.m_World;
	const char chSeparator = owner.m_Connections.empty() ? '\x1b' : owner.m_Connections.front().m_chSeparator;
	for ( int i = 0; i < m_pOutputs->topLevelItemCount(); i++ )
	{
		const QTreeWidgetItem *pItem = m_pOutputs->topLevelItem( i );
		if ( pItem->data( 0, ROLE_OBJECT ).toUInt() != nObject )
		{
			continue;
		}
		xv::map::Connection_t out;
		out.m_szOutput = store( pItem->text( 0 ) );
		out.m_szTarget = store( pItem->text( 1 ) );
		out.m_szInput = store( pItem->text( 2 ) );
		out.m_szParameter = store( pItem->text( 3 ) );
		out.m_flDelay = pItem->text( 4 ).toFloat();
		out.m_nTimes = pItem->data( 5, ROLE_VALUE ).toString() == QStringLiteral( "1" ) ? 1 : -1;
		out.m_chSeparator = chSeparator;
		outputs.push_back( std::move( out ) );
	}
	return outputs;
}

//-----------------------------------------------------------------------------
// Purpose: A cell of an output changed: that entity's outputs are written again as the list
//          then has them. The strings live in the items until the edit copies them into the
//          map's store.
//-----------------------------------------------------------------------------
void CMapProperties::CommitOutput( std::size_t nRow, int nColumn, const std::string &szValue )
{
	if ( nRow >= m_Sheet.m_Outputs.size() || nRow >= static_cast<std::size_t>( m_pOutputs->topLevelItemCount() ) )
	{
		return;
	}
	QTreeWidgetItem *pItem = m_pOutputs->topLevelItem( static_cast<int>( nRow ) );
	m_bFilling = true;
	if ( nColumn == 5 )
	{
		const bool bOnce = szValue == "1" || xv::kv1::EqualsIgnoreCase( szValue, "yes" ) || xv::kv1::EqualsIgnoreCase( szValue, "true" );
		pItem->setData( 5, ROLE_VALUE, bOnce ? QStringLiteral( "1" ) : QStringLiteral( "0" ) );
		pItem->setText( 5, bOnce ? tr( "Yes" ) : tr( "No" ) );
	}
	else
	{
		pItem->setText( nColumn, FromStd( szValue ) );
	}
	m_bFilling = false;
	const std::uint32_t nObject = m_Sheet.m_Outputs[ nRow ].m_nObject;
	Apply( tr( "Change Output" ), [ this, nObject, outputs = OutputsOf( nObject ) ]( const xv::map::CScene &scene ) {
		std::vector<xv::map::Connection_t> copy = outputs;
		return EditShown( scene, { nObject }, [ & ]( xv::map::Entity_t &entity ) { xv::map::SetConnections( scene.m_Map, entity, copy ); } );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: A new output on the first selected entity: its class's first output, aimed at
//          nothing yet.
//-----------------------------------------------------------------------------
void CMapProperties::AddOutput()
{
	const xv::map::CScene *pScene = m_Document.GetScene();
	if ( !pScene || m_Sheet.m_nEntities == 0 )
	{
		return;
	}
	std::uint32_t nObject = xv::map::WORLD_OBJECT;
	bool bFound = m_bWorld;
	for ( const std::uint32_t n : m_bWorld ? std::vector<std::uint32_t>{} : m_Document.GetSelection() )
	{
		if ( pScene->GetObjectEntity( n ) >= 0 )
		{
			nObject = pScene->EntityObject( static_cast<std::size_t>( pScene->GetObjectEntity( n ) ) );
			bFound = true;
			break;
		}
	}
	if ( !bFound )
	{
		return;
	}
	std::vector<xv::map::Connection_t> outputs = OutputsOf( nObject );
	xv::map::Connection_t out;
	out.m_szOutput = m_Sheet.m_OutputNames.empty() ? std::string_view{} : std::string_view{ m_Sheet.m_OutputNames.front() };
	out.m_nTimes = -1;
	out.m_chSeparator = outputs.empty() ? '\x1b' : outputs.front().m_chSeparator;
	outputs.push_back( out );
	Apply( tr( "Add Output" ), [ this, nObject, outputs ]( const xv::map::CScene &scene ) {
		std::vector<xv::map::Connection_t> copy = outputs;
		return EditShown( scene, { nObject }, [ & ]( xv::map::Entity_t &entity ) { xv::map::SetConnections( scene.m_Map, entity, copy ); } );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapProperties::RemoveOutput()
{
	QTreeWidgetItem *pItem = m_pOutputs->currentItem();
	if ( !pItem )
	{
		return;
	}
	const std::uint32_t nObject = pItem->data( 0, ROLE_OBJECT ).toUInt();
	m_bFilling = true;
	delete pItem;
	m_bFilling = false;
	Apply( tr( "Delete Output" ), [ this, nObject, outputs = OutputsOf( nObject ) ]( const xv::map::CScene &scene ) {
		std::vector<xv::map::Connection_t> copy = outputs;
		return EditShown( scene, { nObject }, [ & ]( xv::map::Entity_t &entity ) { xv::map::SetConnections( scene.m_Map, entity, copy ); } );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapProperties::CommitComments()
{
	const std::string szComments = m_pComments->toPlainText().toStdString();
	if ( m_Sheet.m_nEntities != 1 || szComments == m_Sheet.m_szComments )
	{
		return;
	}
	Apply( tr( "Change Comments" ), [ this, szComments, selection = m_Document.GetSelection() ]( const xv::map::CScene &scene ) {
		return EditShown( scene, selection, [ & ]( xv::map::Entity_t &entity ) { xv::map::SetComments( scene.m_Map, entity, szComments ); } );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: The comments go in when the keyboard leaves them.
//-----------------------------------------------------------------------------
bool CMapProperties::eventFilter( QObject *pWatched, QEvent *pEvent )
{
	if ( pWatched == m_pComments && pEvent->type() == QEvent::FocusOut )
	{
		CommitComments();
	}
	return QWidget::eventFilter( pWatched, pEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::pair<std::string, std::string>> CMapProperties::GetKeyChoices( std::size_t nRow ) const
{
	if ( nRow >= m_Sheet.m_Rows.size() || !IsSmartEdit() )
	{
		return {};
	}
	return m_Sheet.m_Rows[ nRow ].m_Choices;
}

//-----------------------------------------------------------------------------
// Purpose: The output names the class has, the inputs the target's class takes, yes or no
//          for Only Once; the rest is typed.
//-----------------------------------------------------------------------------
std::vector<std::pair<std::string, std::string>> CMapProperties::GetOutputChoices( std::size_t nRow, int nColumn ) const
{
	std::vector<std::pair<std::string, std::string>> choices;
	const xv::map::CScene *pScene = m_Document.GetScene();
	if ( nRow >= m_Sheet.m_Outputs.size() || !pScene )
	{
		return choices;
	}
	switch ( nColumn )
	{
		case 0:
			for ( const std::string &szName : m_Sheet.m_OutputNames )
			{
				choices.emplace_back( szName, szName );
			}
			break;
		case 2:
		{
			// The inputs of whatever the target names (the first entity that answers to it).
			const std::string &szTarget = m_Sheet.m_Outputs[ nRow ].m_szEntity;
			for ( const xv::map::Entity_t &entity : pScene->m_Map.m_Entities )
			{
				const std::string_view szName = entity.GetValue( "targetname" );
				const bool bNamed = !szName.empty() && xv::map::TargetMatches( szTarget, szName );
				if ( entity.m_bDeleted || !( bNamed || xv::kv1::EqualsIgnoreCase( entity.GetClassname(), szTarget ) ) )
				{
					continue;
				}
				if ( const xv::map::FgdClass_t *pClass = pScene->m_pFgd ? pScene->m_pFgd->Find( entity.GetClassname() ) : nullptr )
				{
					for ( const xv::map::FgdIo_t &input : pClass->m_Inputs )
					{
						choices.emplace_back( input.m_szName, input.m_szName );
					}
				}
				break;
			}
			break;
		}
		case 5:
			choices.emplace_back( "0", tr( "No" ).toStdString() );
			choices.emplace_back( "1", tr( "Yes" ).toStdString() );
			break;
		default:
			break;
	}
	return choices;
}
