//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See KvCard.h.
//
//===========================================================================//
#include "KvCard.h"

#include <algorithm>
#include <optional>
#include <random>

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "InterfaceMode.h"
#include "Language.h"
#include "SoundDocument.h"
#include "SoundIndex.h"
#include "SoundPlayer.h"
#include "SoundValues.h"
#include "sound/SoundPlayback.h"

using xv::kv1::CNode;
using xv::sound::Issue_t;
using xv::sound::NodePath_t;

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Qs( std::string_view szText )
{
	return QString::fromUtf8( szText.data(), static_cast<qsizetype>( szText.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
NodePath_t ChildPath( NodePath_t path, std::size_t nChild )
{
	path.push_back( nChild );
	return path;
}

//-----------------------------------------------------------------------------
// Purpose: A key or block as the interface shows it: tidied in the simple interface.
//-----------------------------------------------------------------------------
QString ShownKey( std::string_view szKey )
{
	return CInterfaceMode::Instance().IsSimple() ? Qs( ParticleNames().Sound( szKey ) ) : Qs( szKey );
}

//-----------------------------------------------------------------------------
// Purpose: The boxes of a value editor that take the keyboard, in order.
//-----------------------------------------------------------------------------
QList<QWidget *> FocusBoxes( QWidget *pEditor )
{
	if ( qobject_cast<QAbstractSpinBox *>( pEditor ) )
	{
		return { pEditor };
	}
	QList<QWidget *> boxes;
	for ( QAbstractSpinBox *pBox : pEditor->findChildren<QAbstractSpinBox *>() )
	{
		boxes << pBox;
	}
	return boxes.isEmpty() ? QList<QWidget *>{ pEditor } : boxes;
}

//-----------------------------------------------------------------------------
// Purpose: A light tint of the severity colour over the base colour, so it reads in light and dark
//          themes.
//          (Translucent brushes are not blended on stylesheet-styled tables, so the mix is done
//          here.)
//-----------------------------------------------------------------------------
QColor IssueColor( Issue_t::Severity_t nSeverity, const QColor &base )
{
	QColor tint;
	switch ( nSeverity )
	{
		case Issue_t::Severity_t::SEVERITY_ERROR: tint = QColor{ 220, 50, 50 }; break;
		case Issue_t::Severity_t::SEVERITY_WARNING: tint = QColor{ 235, 160, 20 }; break;
		default: tint = QColor{ 60, 140, 230 }; break;
	}
	constexpr double flStrength = 0.3;
	return QColor::fromRgbF( static_cast<float>( base.redF() + ( tint.redF() - base.redF() ) * flStrength ),
		static_cast<float>( base.greenF() + ( tint.greenF() - base.greenF() ) * flStrength ),
		static_cast<float>( base.blueF() + ( tint.blueF() - base.blueF() ) * flStrength ) );
}

const char *const CARD_STYLE = R"(
QLabel#cardTab {
	border: 1px solid palette(mid);
	border-bottom: none;
	border-top-left-radius: 7px;
	border-top-right-radius: 7px;
	padding: 4px 12px;
	background: palette(button);
	font-weight: 600;
}
QFrame#cardBody {
	border: 1px solid palette(mid);
	border-radius: 7px;
	border-top-left-radius: 0px;
	background: palette(base);
}
QTableWidget#cardTable {
	border: none;
	background: transparent;
	gridline-color: palette(midlight);
}
)";

} // namespace

//-----------------------------------------------------------------------------
// Purpose: player may be null: no play buttons then.
//-----------------------------------------------------------------------------
CKvCard::CKvCard( CSoundDocument &document, const CSoundIndex &index, CSoundPlayer *pPlayer, NodePath_t path, QWidget *pParent )
	: QWidget( pParent ),
	  m_Document( document ),
	  m_Index( index ),
	  m_pPlayer( pPlayer ),
	  m_Path( std::move( path ) )
{
	const CNode *pNode = m_Document.Node( m_Path );
	Q_ASSERT( pNode && pNode->IsBlock() );
	const xv::sound::BlockDef_t *pDef = m_Document.BlockDef( m_Path );
	const bool bEntry = m_Path.size() == 1;
	if ( bEntry )
	{
		setStyleSheet( QString::fromLatin1( CARD_STYLE ) );
	}

	m_pTab = new QLabel( bEntry ? Qs( pNode->GetKey() ) : ShownKey( pNode->GetKey() ) );
	m_pTab->setObjectName( QStringLiteral( "cardTab" ) );
	m_pTab->setToolTip( pDef ? Qs( pDef->m_szHelp ) : tr( "Not a block the engine knows here" ) );
	if ( !bEntry && m_pTab->text() != Qs( pNode->GetKey() ) )
	{
		m_pTab->setToolTip( Qs( pNode->GetKey() ) + u'\n' + m_pTab->toolTip() );
	}
	if ( bEntry )
	{
		m_pTab->setToolTip( m_pTab->toolTip() + tr( "\nDouble-click to rename" ) );
		m_pTab->installEventFilter( this );
	}

	auto *pAdd = new QToolButton;
	pAdd->setText( QStringLiteral( "+" ) );
	pAdd->setToolTip( tr( "Add a key or block" ) );
	pAdd->setAutoRaise( true );
	pAdd->setPopupMode( QToolButton::InstantPopup );
	auto *pAddMenu = new QMenu( pAdd );
	connect( pAddMenu, &QMenu::aboutToShow, this, [ this, pAddMenu ] { FillAddMenu( pAddMenu ); } );
	pAdd->setMenu( pAddMenu );

	auto *pHeader = new QHBoxLayout;
	pHeader->setContentsMargins( 0, 0, 0, 0 );
	pHeader->setSpacing( 2 );
	pHeader->addWidget( m_pTab, 0, Qt::AlignBottom );
	if ( m_pPlayer && GetPlayable() )
	{
		auto *pPlay = new QToolButton;
		pPlay->setText( QStringLiteral( "▶" ) );
		pPlay->setAutoRaise( true );
		pPlay->setToolTip( bEntry && m_Document.GetKind() == xv::sound::FileKind_t::FILE_KIND_SOUNDSCAPE
				? tr( "Play this soundscape: its loops, random sounds and nested soundscapes" )
				: tr( "Play with these settings (volume, pitch, random picks)" ) );
		connect( pPlay, &QToolButton::clicked, this, &CKvCard::Play );
		pHeader->addWidget( pPlay, 0, Qt::AlignBottom );
	}
	pHeader->addStretch();
	pHeader->addWidget( pAdd, 0, Qt::AlignBottom );
	if ( !bEntry )
	{
		auto *pRemove = new QToolButton;
		pRemove->setText( QStringLiteral( "✕" ) );
		pRemove->setToolTip( tr( "Delete this block" ) );
		pRemove->setAutoRaise( true );
		connect( pRemove, &QToolButton::clicked, this, [ this ] { m_Document.RemoveNode( m_Path ); } );
		pHeader->addWidget( pRemove, 0, Qt::AlignBottom );
	}

	auto *pBody = new QFrame;
	pBody->setObjectName( QStringLiteral( "cardBody" ) );
	auto *pBodyLayout = new QVBoxLayout( pBody );
	pBodyLayout->setContentsMargins( 6, 6, 6, 6 );
	pBodyLayout->setSpacing( 8 );

	BuildTable( *pNode );
	pBodyLayout->addWidget( m_pTable );
	m_pTable->setVisible( !m_RowChild.empty() );

	for ( std::size_t i = 0; i < pNode->GetChildren().size(); i++ )
	{
		if ( pNode->Child( i ).IsBlock() )
		{
			auto *pCard = new CKvCard( m_Document, m_Index, m_pPlayer, ChildPath( m_Path, i ) );
			connect( pCard, &CKvCard::Added, this, &CKvCard::Added );
			m_Children.push_back( pCard );
			pBodyLayout->addWidget( pCard );
		}
	}
	if ( m_RowChild.empty() && m_Children.empty() )
	{
		auto *pEmpty = new QLabel( tr( "Empty. Use + to add keys or blocks." ) );
		pEmpty->setEnabled( false );
		pBodyLayout->addWidget( pEmpty );
	}

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->setSpacing( 0 );
	pLayout->addLayout( pHeader );
	pLayout->addWidget( pBody );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CKvCard::BuildTable( const CNode &node )
{
	const xv::sound::BlockDef_t *pDef = m_Document.BlockDef( m_Path );

	m_pTable = new QTableWidget( 0, 3 );
	m_pTable->setObjectName( QStringLiteral( "cardTable" ) );
	m_pTable->horizontalHeader()->hide();
	m_pTable->verticalHeader()->hide();
	m_pTable->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	m_pTable->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	m_pTable->setEditTriggers( QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed );
	m_pTable->setWordWrap( false );
	m_pTable->setItemDelegate( new CSoundValueDelegate( pDef, m_Index, m_Document, m_pTable ) );
	m_pTable->horizontalHeader()->setSectionResizeMode( CSoundValueDelegate::COLUMN_KEY_COLUMN, QHeaderView::ResizeToContents );
	m_pTable->horizontalHeader()->setSectionResizeMode( CSoundValueDelegate::COLUMN_VALUE_COLUMN, QHeaderView::Stretch );
	m_pTable->horizontalHeader()->setSectionResizeMode( CSoundValueDelegate::COLUMN_MEANING_COLUMN, QHeaderView::ResizeToContents );
	m_pTable->setContextMenuPolicy( Qt::CustomContextMenu );

	const bool bSimple = CInterfaceMode::Instance().IsSimple();
	m_bFilling = true;
	QFont meaningFont = m_pTable->font();
	meaningFont.setItalic( true );
	const QColor meaningColor = palette().color( QPalette::PlaceholderText );
	for ( std::size_t i = 0; i < node.GetChildren().size(); i++ )
	{
		const CNode &child = node.Child( i );
		if ( !child.IsPair() )
		{
			continue;
		}
		const int nRow = m_pTable->rowCount();
		m_pTable->insertRow( nRow );
		m_RowChild.push_back( i );

		const QString szKey = Qs( child.GetKey() );
		const QString szValue = Qs( child.GetValue() );
		const auto nKind = CSoundValueDelegate::KindOf( pDef, szKey );

		// The simple interface shows keys tidied and leaves them as they are.
		auto *pKeyItem = new QTableWidgetItem( ShownKey( child.GetKey() ) );
		pKeyItem->setData( Qt::UserRole, szKey );
		const xv::sound::KeyDef_t *pKeyDef = pDef ? xv::sound::FindKey( *pDef, szKey.toStdString() ) : nullptr;
		pKeyItem->setToolTip( pKeyDef ? Qs( pKeyDef->m_szHelp ) : tr( "Not a key the engine reads here" ) );
		if ( bSimple )
		{
			pKeyItem->setFlags( pKeyItem->flags() & ~Qt::ItemIsEditable );
			pKeyItem->setToolTip( szKey + u'\n' + pKeyItem->toolTip() );
		}
		auto *pValueItem = new QTableWidgetItem( szValue );
		auto *pMeaningItem = new QTableWidgetItem( DescribeValue( nKind, szValue ) );
		pMeaningItem->setFlags( Qt::ItemIsEnabled );
		pMeaningItem->setFont( meaningFont );
		pMeaningItem->setForeground( meaningColor );

		m_pTable->setItem( nRow, CSoundValueDelegate::COLUMN_KEY_COLUMN, pKeyItem );
		m_pTable->setItem( nRow, CSoundValueDelegate::COLUMN_VALUE_COLUMN, pValueItem );
		m_pTable->setItem( nRow, CSoundValueDelegate::COLUMN_MEANING_COLUMN, pMeaningItem );

		const NodePath_t valuePath = ChildPath( m_Path, i );
		QWidget *pEasier = bSimple ? SimpleValueEditor( nKind, szKey, szValue, [ this, valuePath ]( const QString &szText ) { m_Document.SetValue( valuePath, szText ); } ) : nullptr;
		if ( pEasier )
		{
			pValueItem->setFlags( pValueItem->flags() & ~Qt::ItemIsEditable );
			m_pTable->setCellWidget( nRow, CSoundValueDelegate::COLUMN_VALUE_COLUMN, pEasier );
			m_ValueEditors.emplace_back( valuePath, pEasier );
		}

		const std::string szGamePath = nKind == xv::sound::ValueKind_t::VALUE_KIND_WAVE ? xv::sound::WaveGamePath( child.GetValue() ) : std::string{};
		if ( m_pPlayer && !szGamePath.empty() )
		{
			auto *pCell = new QWidget;
			auto *pCellLayout = new QHBoxLayout( pCell );
			pCellLayout->setContentsMargins( 2, 0, 4, 0 );
			pCellLayout->setSpacing( 4 );
			auto *pPlay = new QToolButton;
			pPlay->setText( QStringLiteral( "▶" ) );
			pPlay->setAutoRaise( true );
			pPlay->setToolTip( tr( "Play %1 as it is" ).arg( QString::fromStdString( szGamePath ) ) );
			connect( pPlay, &QToolButton::clicked, this, [ this, szGamePath ] { m_pPlayer->PlayGameSound( szGamePath, m_Document.GetLocalRoot() ); } );
			auto *pMeaning = new QLabel( pMeaningItem->text() );
			pMeaning->setFont( meaningFont );
			pMeaning->setStyleSheet( QStringLiteral( "color: %1;" ).arg( meaningColor.name() ) );
			pCellLayout->addWidget( pPlay );
			pCellLayout->addWidget( pMeaning );
			pCellLayout->addStretch();
			pMeaningItem->setText( {} );
			m_pTable->setCellWidget( nRow, CSoundValueDelegate::COLUMN_MEANING_COLUMN, pCell );
		}
	}
	m_bFilling = false;
	// Exactly as tall as its rows: the card grows instead of the table scrolling, and doesn't take
	// spare height either (a short card would show its rows centred in empty space).
	m_pTable->resizeRowsToContents();
	m_pTable->setFixedHeight( m_pTable->verticalHeader()->length() + 2 * m_pTable->frameWidth() );
	m_pTable->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );

	// One click opens a value for editing (its choices drop down with it); keys still take a
	// double-click.
	connect( m_pTable, &QTableWidget::clicked, this, [ this ]( const QModelIndex &index ) {
		QTableWidgetItem *pItem = m_pTable->item( index.row(), index.column() );
		if ( index.column() == CSoundValueDelegate::COLUMN_VALUE_COLUMN && pItem && ( pItem->flags() & Qt::ItemIsEditable ) && !m_pTable->cellWidget( index.row(), index.column() ) )
		{
			m_pTable->editItem( pItem );
		}
	} );

	connect( m_pTable, &QTableWidget::itemChanged, this, [ this ]( QTableWidgetItem *pItem ) {
		if ( m_bFilling || pItem->row() >= static_cast<int>( m_RowChild.size() ) )
		{
			return;
		}
		const NodePath_t target = ChildPath( m_Path, m_RowChild[ static_cast<std::size_t>( pItem->row() ) ] );
		if ( pItem->column() == CSoundValueDelegate::COLUMN_KEY_COLUMN )
		{
			m_Document.SetKey( target, pItem->text() );
		}
		else if ( pItem->column() == CSoundValueDelegate::COLUMN_VALUE_COLUMN )
		{
			m_Document.SetValue( target, pItem->text() );
		}
	} );

	auto *pRemove = new QAction( tr( "Delete Key" ), m_pTable );
	pRemove->setShortcut( QKeySequence::Delete );
	pRemove->setShortcutContext( Qt::WidgetShortcut );
	connect( pRemove, &QAction::triggered, this, [ this ] { RemoveRow( m_pTable->currentRow() ); } );
	m_pTable->addAction( pRemove );

	connect( m_pTable, &QTableWidget::customContextMenuRequested, this, [ this, pRemove ]( const QPoint &position ) {
		QMenu menu;
		const int nRow = m_pTable->rowAt( position.y() );
		if ( nRow >= 0 )
		{
			m_pTable->setCurrentCell( nRow, CSoundValueDelegate::COLUMN_VALUE_COLUMN );
			pRemove->setText( tr( "Delete \"%1\"" ).arg( m_pTable->item( nRow, CSoundValueDelegate::COLUMN_KEY_COLUMN )->text() ) );
			menu.addAction( pRemove );
			menu.addSeparator();
		}
		FillAddMenu( &menu );
		menu.exec( m_pTable->viewport()->mapToGlobal( position ) );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CKvCard::FillAddMenu( QMenu *pMenu )
{
	pMenu->clear();
	const CNode *pNode = m_Document.Node( m_Path );
	const xv::sound::BlockDef_t *pDef = m_Document.BlockDef( m_Path );
	if ( !pNode || !pDef )
	{
		pMenu->addAction( tr( "Custom Key…" ), this, [ this ] {
			const QString szKey = QInputDialog::getText( this, tr( "Add Key" ), tr( "Key:" ) );
			if ( !szKey.isEmpty() )
			{
				AddKey( szKey );
			}
		} );
		return;
	}

	pMenu->addSection( tr( "Keys" ) );
	for ( const xv::sound::KeyDef_t &key : pDef->m_Keys )
	{
		// Only rndwave's "wave" may repeat; other keys are offered once.
		const bool bRepeatable = xv::kv1::EqualsIgnoreCase( pDef->m_szKey, "rndwave" );
		if ( !bRepeatable && pNode->Find( key.m_szKey ) )
		{
			continue;
		}
		QAction *pAction = pMenu->addAction( ShownKey( key.m_szKey ), this, [ this, szName = Qs( key.m_szKey ) ] { AddKey( szName ); } );
		pAction->setToolTip( Qs( key.m_szHelp ) );
	}
	pMenu->addAction( tr( "Custom Key…" ), this, [ this ] {
		const QString szKey = QInputDialog::getText( this, tr( "Add Key" ), tr( "Key:" ) );
		if ( !szKey.isEmpty() )
		{
			AddKey( szKey );
		}
	} );
	if ( !pDef->m_Blocks.empty() )
	{
		pMenu->addSection( tr( "Blocks" ) );
		for ( const std::string_view szBlock : pDef->m_Blocks )
		{
			const xv::sound::BlockDef_t *pBlockDef = m_Document.GetSchema().Block( szBlock );
			QAction *pAction = pMenu->addAction( ShownKey( szBlock ), this, [ this, szName = Qs( szBlock ) ] { AddBlock( szName ); } );
			pAction->setToolTip( pBlockDef ? Qs( pBlockDef->m_szHelp ) : QString{} );
		}
	}
	pMenu->setToolTipsVisible( true );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CKvCard::GetPairInsertIndex() const
{
	// New keys go after the existing keys, above any sub-blocks.
	const CNode *pNode = m_Document.Node( m_Path );
	std::size_t nIndex = 0;
	for ( std::size_t i = 0; i < pNode->GetChildren().size(); i++ )
	{
		if ( pNode->Child( i ).IsPair() )
		{
			nIndex = i + 1;
		}
	}
	return nIndex;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CKvCard::AddKey( const QString &szKey )
{
	const std::size_t nIndex = GetPairInsertIndex();
	const std::string szName = szKey.toStdString();
	m_Document.InsertChild( m_Path, nIndex, CNode::MakePair( szName, DefaultValue( m_Document, szName ).toStdString() ), tr( "Add %1" ).arg( szKey ) );
	emit Added( ChildPath( m_Path, nIndex ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CKvCard::AddBlock( const QString &szKey )
{
	const CNode *pNode = m_Document.Node( m_Path );
	const std::size_t nIndex = pNode->GetChildren().size();
	m_Document.InsertChild( m_Path, nIndex, BlockTemplate( m_Document, szKey.toStdString() ), tr( "Add %1" ).arg( szKey ) );
	emit Added( ChildPath( m_Path, nIndex ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CKvCard::RemoveRow( int nRow )
{
	if ( nRow >= 0 && nRow < static_cast<int>( m_RowChild.size() ) )
	{
		m_Document.RemoveNode( ChildPath( m_Path, m_RowChild[ static_cast<std::size_t>( nRow ) ] ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CKvCard::Rename()
{
	const CNode *pNode = m_Document.Node( m_Path );
	bool bOk = false;
	const QString szName = QInputDialog::getText( this, tr( "Rename" ), tr( "Name:" ), QLineEdit::Normal, Qs( pNode->GetKey() ), &bOk );
	if ( bOk && !szName.isEmpty() )
	{
		m_Document.SetKey( m_Path, szName );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CKvCard::eventFilter( QObject *pWatched, QEvent *pEvent )
{
	if ( pWatched == m_pTab && pEvent->type() == QEvent::MouseButtonDblClick )
	{
		Rename();
		return true;
	}
	return QWidget::eventFilter( pWatched, pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: Tints the rows and tabs that issues point at, here and in nested cards.
//-----------------------------------------------------------------------------
void CKvCard::ShowIssues( const std::vector<Issue_t> &issues )
{
	QString szTabProblems;
	std::optional<Issue_t::Severity_t> tabSeverity;
	for ( const Issue_t &issue : issues )
	{
		if ( issue.m_Path == m_Path )
		{
			szTabProblems += ( szTabProblems.isEmpty() ? QString{} : QStringLiteral( "\n" ) ) + QString::fromStdString( issue.m_szMessage );
			tabSeverity = std::max( tabSeverity.value_or( issue.m_nSeverity ), issue.m_nSeverity );
			continue;
		}
		if ( issue.m_Path.size() != m_Path.size() + 1 || !std::equal( m_Path.begin(), m_Path.end(), issue.m_Path.begin() ) )
		{
			continue;
		}
		const auto row = std::ranges::find( m_RowChild, issue.m_Path.back() );
		if ( row == m_RowChild.end() )
		{
			continue;
		}
		const int r = static_cast<int>( row - m_RowChild.begin() );
		for ( const int nColumn : { CSoundValueDelegate::COLUMN_KEY_COLUMN, CSoundValueDelegate::COLUMN_VALUE_COLUMN } )
		{
			QTableWidgetItem *pItem = m_pTable->item( r, nColumn );
			pItem->setBackground( IssueColor( issue.m_nSeverity, palette().color( QPalette::Base ) ) );
			pItem->setToolTip( QString::fromStdString( issue.m_szMessage ) );
		}
	}
	if ( tabSeverity )
	{
		m_pTab->setToolTip( szTabProblems );
		m_pTab->setStyleSheet( QStringLiteral( "background: %1;" ).arg( IssueColor( *tabSeverity, palette().color( QPalette::Button ) ).name() ) );
	}
	for ( CKvCard *pChild : m_Children )
	{
		pChild->ShowIssues( issues );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Opens the value editor of the pair at path, if it is in this card or a nested one.
//-----------------------------------------------------------------------------
bool CKvCard::BeginEdit( const NodePath_t &path )
{
	if ( path.size() == m_Path.size() + 1 && std::equal( m_Path.begin(), m_Path.end(), path.begin() ) )
	{
		const auto row = std::ranges::find( m_RowChild, path.back() );
		if ( row != m_RowChild.end() )
		{
			const int r = static_cast<int>( row - m_RowChild.begin() );
			if ( m_pTable->cellWidget( r, CSoundValueDelegate::COLUMN_VALUE_COLUMN ) )
			{
				return FocusValue( path, 0 );
			}
			m_pTable->setFocus();
			m_pTable->setCurrentCell( r, CSoundValueDelegate::COLUMN_VALUE_COLUMN );
			m_pTable->editItem( m_pTable->item( r, CSoundValueDelegate::COLUMN_VALUE_COLUMN ) );
			return true;
		}
	}
	return std::ranges::any_of( m_Children, [ &path ]( CKvCard *pChild ) { return pChild->BeginEdit( path ); } );
}

//-----------------------------------------------------------------------------
// Purpose: Which value box has the keyboard focus, here or in a nested card: its pair's path and
//          which of the editor's boxes (an X/Y/Z or "from … to" editor has more than one).
//-----------------------------------------------------------------------------
std::optional<std::pair<NodePath_t, int>> CKvCard::GetFocusedValue() const
{
	QWidget *pFocus = QApplication::focusWidget();
	if ( !pFocus )
	{
		return std::nullopt;
	}
	for ( const auto &[ path, pEditor ] : m_ValueEditors )
	{
		const QList<QWidget *> boxes = FocusBoxes( pEditor );
		for ( int i = 0; i < boxes.size(); i++ )
		{
			if ( boxes[ i ] == pFocus || boxes[ i ]->isAncestorOf( pFocus ) )
			{
				return std::pair{ path, i };
			}
		}
	}
	for ( const CKvCard *pChild : m_Children )
	{
		if ( auto focused = pChild->GetFocusedValue() )
		{
			return focused;
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: Gives the keyboard to a value box, here or in a nested card.
//-----------------------------------------------------------------------------
bool CKvCard::FocusValue( const NodePath_t &path, int nBox )
{
	for ( const auto &[ editorPath, pEditor ] : m_ValueEditors )
	{
		if ( editorPath == path )
		{
			const QList<QWidget *> boxes = FocusBoxes( pEditor );
			boxes.value( nBox, boxes.front() )->setFocus();
			return true;
		}
	}
	return std::ranges::any_of( m_Children, [ &path, nBox ]( CKvCard *pChild ) { return pChild->FocusValue( path, nBox ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CKvCard::GetPlayable() const
{
	const CNode *pNode = m_Document.Node( m_Path );
	if ( !pNode )
	{
		return false;
	}
	if ( m_Path.size() == 1 )
	{
		return true;
	}
	const std::string szKey = xv::sound::ToLower( pNode->GetKey() );
	if ( szKey == "rndwave" )
	{
		return true;
	}
	return m_Document.GetKind() == xv::sound::FileKind_t::FILE_KIND_SOUNDSCAPE && ( szKey == "playlooping" || szKey == "playrandom" || szKey == "playsoundscape" );
}

//-----------------------------------------------------------------------------
// Purpose: Plays this block with its settings; a soundscape entry plays as a whole soundscape.
//-----------------------------------------------------------------------------
void CKvCard::Play()
{
	const CNode *pNode = m_Document.Node( m_Path );
	if ( !m_pPlayer || !pNode )
	{
		return;
	}
	const auto lookup = [ this ]( std::string_view szName ) { return m_Index.FindSoundscape( szName, &m_Document ); };
	const bool bSoundscape = m_Document.GetKind() == xv::sound::FileKind_t::FILE_KIND_SOUNDSCAPE;
	if ( bSoundscape && m_Path.size() == 1 )
	{
		m_pPlayer->PlaySoundscape( xv::sound::PlanSoundscape( *pNode, lookup ), Qs( pNode->GetKey() ), m_Document.GetLocalRoot() );
		return;
	}
	if ( bSoundscape && xv::kv1::EqualsIgnoreCase( pNode->GetKey(), "playsoundscape" ) )
	{
		const CNode *pName = pNode->Find( "name" );
		const CNode *pTarget = pName ? lookup( pName->GetValue() ) : nullptr;
		if ( !pTarget )
		{
			emit m_pPlayer->Failed( tr( "No soundscape named “%1”" ).arg( pName ? Qs( pName->GetValue() ) : QString{} ) );
			return;
		}
		m_pPlayer->PlaySoundscape( xv::sound::PlanSoundscape( *pTarget, lookup ), Qs( pTarget->GetKey() ), m_Document.GetLocalRoot() );
		return;
	}

	static std::mt19937 random{ std::random_device{}() };
	NodePath_t parentPath = m_Path;
	parentPath.pop_back();
	const CNode *pParent = m_Path.size() > 1 ? m_Document.Node( parentPath ) : nullptr;
	if ( const auto request = xv::sound::PlayBlock( m_Document.GetKind(), *pNode, pParent, random ) )
	{
		m_pPlayer->Play( *request, m_Document.GetLocalRoot() );
	}
	else
	{
		emit m_pPlayer->Failed( tr( "“%1” has no sound to play" ).arg( Qs( pNode->GetKey() ) ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The card for the block at path, or nullptr.
//-----------------------------------------------------------------------------
CKvCard *CKvCard::CardFor( const NodePath_t &path )
{
	if ( path == m_Path )
	{
		return this;
	}
	for ( CKvCard *pChild : m_Children )
	{
		if ( CKvCard *pCard = pChild->CardFor( path ) )
		{
			return pCard;
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString DefaultValue( const CSoundDocument &document, std::string_view szKey )
{
	using xv::kv1::EqualsIgnoreCase;
	const bool bScript = document.GetKind() == xv::sound::FileKind_t::FILE_KIND_SOUNDSCRIPT;
	if ( EqualsIgnoreCase( szKey, "channel" ) )
		return QStringLiteral( "CHAN_AUTO" );
	if ( EqualsIgnoreCase( szKey, "volume" ) )
		return bScript ? QStringLiteral( "VOL_NORM" ) : QStringLiteral( "1" );
	if ( EqualsIgnoreCase( szKey, "pitch" ) )
		return bScript ? QStringLiteral( "PITCH_NORM" ) : QStringLiteral( "100" );
	if ( EqualsIgnoreCase( szKey, "soundlevel" ) )
		return QStringLiteral( "SNDLVL_NORM" );
	if ( EqualsIgnoreCase( szKey, "attenuation" ) || EqualsIgnoreCase( szKey, "compatibilityattenuation" ) )
		return QStringLiteral( "ATTN_NORM" );
	if ( EqualsIgnoreCase( szKey, "dsp" ) || EqualsIgnoreCase( szKey, "dsp_volume" ) )
		return QStringLiteral( "1" );
	if ( EqualsIgnoreCase( szKey, "time" ) )
		return QStringLiteral( "10,20" );
	if ( EqualsIgnoreCase( szKey, "origin" ) )
		return QStringLiteral( "0 0 0" );
	if ( EqualsIgnoreCase( szKey, "delay_msec" ) )
		return QStringLiteral( "0" );
	if ( EqualsIgnoreCase( szKey, "position" ) || EqualsIgnoreCase( szKey, "positionoverride" ) || EqualsIgnoreCase( szKey, "ambientpositionoverride" ) )
		return QStringLiteral( "0" );
	if ( EqualsIgnoreCase( szKey, "play_to_owner_only" ) || EqualsIgnoreCase( szKey, "suppress_on_restore" ) )
		return QStringLiteral( "1" );
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: Starting content for new blocks and entries, so they are valid as soon as they are
//          added.
//-----------------------------------------------------------------------------
CNode BlockTemplate( const CSoundDocument &document, std::string_view szKey )
{
	using xv::kv1::EqualsIgnoreCase;
	CNode block = CNode::MakeBlock( szKey );
	const auto add = [ & ]( std::string_view szName ) {
		block.AppendChild( CNode::MakePair( szName, DefaultValue( document, szName ).toStdString() ) );
	};
	if ( EqualsIgnoreCase( szKey, "playlooping" ) )
	{
		add( "volume" );
		add( "pitch" );
		add( "wave" );
	}
	else if ( EqualsIgnoreCase( szKey, "playrandom" ) )
	{
		add( "time" );
		add( "volume" );
		add( "pitch" );
		block.AppendChild( BlockTemplate( document, "rndwave" ) );
	}
	else if ( EqualsIgnoreCase( szKey, "playsoundscape" ) )
	{
		add( "name" );
		add( "volume" );
	}
	else if ( EqualsIgnoreCase( szKey, "rndwave" ) )
	{
		add( "wave" );
	}
	return block;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CNode EntryTemplate( const CSoundDocument &document, std::string_view szName )
{
	CNode entry = CNode::MakeBlock( szName );
	const auto add = [ & ]( std::string_view szKey ) {
		entry.AppendChild( CNode::MakePair( szKey, DefaultValue( document, szKey ).toStdString() ) );
	};
	if ( document.GetKind() == xv::sound::FileKind_t::FILE_KIND_SOUNDSCAPE )
	{
		add( "dsp" );
	}
	else
	{
		add( "channel" );
		add( "volume" );
		add( "pitch" );
		add( "soundlevel" );
		add( "wave" );
	}
	return entry;
}
