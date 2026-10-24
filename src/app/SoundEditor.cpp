//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SoundEditor.h.
//
//===========================================================================//
#include "SoundEditor.h"

#include <algorithm>

#include <QAction>
#include <QApplication>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QUndoStack>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include "InterfaceMode.h"
#include "KvClipboard.h"
#include "KvCard.h"
#include "SoundDocument.h"
#include "SoundIndex.h"
#include "SoundPlayer.h"

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
QIcon SeverityIcon( Issue_t::Severity_t nSeverity )
{
	switch ( nSeverity )
	{
		case Issue_t::Severity_t::SEVERITY_ERROR: return QApplication::style()->standardIcon( QStyle::SP_MessageBoxCritical );
		case Issue_t::Severity_t::SEVERITY_WARNING: return QApplication::style()->standardIcon( QStyle::SP_MessageBoxWarning );
		default: return QApplication::style()->standardIcon( QStyle::SP_MessageBoxInformation );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QVariant PathToVariant( const NodePath_t &path )
{
	QVariantList list;
	for ( const std::size_t nIndex : path )
	{
		list << QVariant::fromValue( static_cast<qulonglong>( nIndex ) );
	}
	return list;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
NodePath_t VariantToPath( const QVariant &value )
{
	NodePath_t path;
	for ( const QVariant &index : value.toList() )
	{
		path.push_back( static_cast<std::size_t>( index.toULongLong() ) );
	}
	return path;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: player may be null: no playback then.
//-----------------------------------------------------------------------------
CSoundEditor::CSoundEditor( CSoundDocument *pDocument, const CSoundIndex &index, CSoundPlayer *pPlayer, QWidget *pParent )
	: QWidget( pParent ),
	  m_pDocument( pDocument ),
	  m_Index( index ),
	  m_pPlayer( pPlayer )
{
	const QString szNoun = Qs( m_pDocument->GetSchema().m_szEntryNoun );

	auto *pToolbar = new QToolBar;
	pToolbar->setToolButtonStyle( Qt::ToolButtonTextOnly );
	if ( m_pPlayer )
	{
		QAction *pPlay = pToolbar->addAction( tr( "▶ Play" ), this, [ this ] {
			if ( m_pCard )
			{
				m_pCard->Play();
			}
		} );
		pPlay->setShortcut( QKeySequence{ Qt::CTRL | Qt::Key_Return } );
		pPlay->setShortcutContext( Qt::WidgetWithChildrenShortcut );
		pPlay->setToolTip( m_pDocument->GetKind() == xv::sound::FileKind_t::FILE_KIND_SOUNDSCAPE
				? tr( "Play the selected soundscape (%1). Positions, distance falloff and DSP aren't simulated." ).arg( pPlay->shortcut().toString( QKeySequence::NativeText ) )
				: tr( "Play the selected sound with its volume, pitch and random picks (%1)" ).arg( pPlay->shortcut().toString( QKeySequence::NativeText ) ) );
		QAction *pStop = pToolbar->addAction( tr( "■ Stop" ), m_pPlayer, &CSoundPlayer::Stop );
		pStop->setShortcut( QKeySequence{ Qt::CTRL | Qt::Key_Period } );
		pStop->setShortcutContext( Qt::WidgetWithChildrenShortcut );
		pStop->setToolTip( tr( "Stop all sounds (%1)" ).arg( pStop->shortcut().toString( QKeySequence::NativeText ) ) );
		addAction( pPlay );
		addAction( pStop );
		pToolbar->addSeparator();
	}
	pToolbar->addAction( tr( "New %1…" ).arg( szNoun ), this, &CSoundEditor::AddEntry );
	m_pDuplicate = pToolbar->addAction( tr( "Duplicate" ), this, &CSoundEditor::DuplicateEntry );
	m_pDelete = pToolbar->addAction( tr( "Delete" ), this, &CSoundEditor::DeleteEntry );
	pToolbar->addSeparator();
	m_pAddToManifest = pToolbar->addAction( tr( "Add to Manifest" ), this, [ this ] { emit AddToManifestRequested( m_pDocument ); } );
	m_pAddToManifest->setToolTip( tr( "List this file in %1 so the game loads it" ).arg( Qs( xv::sound::ManifestPathFor( m_pDocument->GetKind() ) ) ) );
	auto *pSpacer = new QWidget;
	pSpacer->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
	pToolbar->addWidget( pSpacer );
	m_pSummary = new QLabel;
	pToolbar->addWidget( m_pSummary );

	m_pFilter = new QLineEdit;
	m_pFilter->setPlaceholderText( tr( "Filter %1s…" ).arg( szNoun ) );
	m_pFilter->setClearButtonEnabled( true );
	m_pEntries = new QListWidget;
	m_pEntries->setObjectName( QStringLiteral( "entryList" ) );
	m_pEntries->setUniformItemSizes( true );
	auto *pLeft = new QWidget;
	auto *pLeftLayout = new QVBoxLayout( pLeft );
	pLeftLayout->setContentsMargins( 0, 0, 0, 0 );
	pLeftLayout->addWidget( m_pFilter );
	pLeftLayout->addWidget( m_pEntries );

	m_pScroll = new QScrollArea;
	m_pScroll->setWidgetResizable( true );
	m_pScroll->setFrameShape( QFrame::NoFrame );

	auto *pTop = new QSplitter;
	pTop->addWidget( pLeft );
	pTop->addWidget( m_pScroll );
	pTop->setStretchFactor( 1, 3 );
	pTop->setSizes( { 240, 760 } );

	m_pIssueList = new QListWidget;
	m_pIssueList->setObjectName( QStringLiteral( "issueList" ) );
	auto *pVertical = new QSplitter( Qt::Vertical );
	pVertical->addWidget( pTop );
	pVertical->addWidget( m_pIssueList );
	pVertical->setStretchFactor( 0, 4 );
	pVertical->setStretchFactor( 1, 1 );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->setSpacing( 0 );
	pLayout->addWidget( pToolbar );
	pLayout->addWidget( pVertical );

	connect( m_pFilter, &QLineEdit::textChanged, this, [ this ]( const QString &szText ) {
		for ( int nRow = 0; nRow < m_pEntries->count(); nRow++ )
		{
			QListWidgetItem *pItem = m_pEntries->item( nRow );
			pItem->setHidden( !pItem->text().contains( szText, Qt::CaseInsensitive ) );
		}
	} );
	connect( m_pEntries, &QListWidget::currentItemChanged, this, [ this ]( QListWidgetItem *pItem ) {
		if ( !pItem )
		{
			return;
		}
		const auto nRoot = static_cast<std::size_t>( pItem->data( Qt::UserRole ).toULongLong() );
		if ( m_Current != nRoot )
		{
			m_Current = nRoot;
			ShowEntry();
		}
	} );
	// Whole entries copy and paste, within this file or into another.
	m_pEntries->setContextMenuPolicy( Qt::CustomContextMenu );
	connect( m_pEntries, &QListWidget::customContextMenuRequested, this, [ this ]( const QPoint &at ) {
		if ( QListWidgetItem *pItem = m_pEntries->itemAt( at ) )
		{
			m_pEntries->setCurrentItem( pItem );
		}
		const bool bOnEntry = m_pEntries->itemAt( at ) && m_Current;
		const QString szName = bOnEntry ? m_pEntries->currentItem()->text() : QString{};
		const QString szNoun = Qs( m_pDocument->GetSchema().m_szEntryNoun );
		QMenu menu;
		if ( bOnEntry )
		{
			menu.addAction( tr( "Copy \"%1\"" ).arg( szName ), this, &CSoundEditor::CopyEntry )->setShortcut( QKeySequence::Copy );
		}
		QAction *pPaste = menu.addAction( tr( "Paste" ), this, &CSoundEditor::PasteEntries );
		pPaste->setShortcut( QKeySequence::Paste );
		pPaste->setEnabled( CanPasteNodes() );
		if ( bOnEntry )
		{
			menu.addAction( tr( "Duplicate" ), this, &CSoundEditor::DuplicateEntry );
		}
		menu.addSeparator();
		menu.addAction( tr( "New %1…" ).arg( szNoun ), this, &CSoundEditor::AddEntry );
		if ( bOnEntry )
		{
			menu.addSeparator();
			menu.addAction( tr( "Rename…" ), this, [ this ] { emit m_pEntries->itemDoubleClicked( m_pEntries->currentItem() ); } );
			menu.addAction( tr( "Delete \"%1\"" ).arg( szName ), this, &CSoundEditor::DeleteEntry )->setShortcut( QKeySequence::Delete );
		}
		menu.exec( m_pEntries->viewport()->mapToGlobal( at ) );
	} );
	auto *pCopy = new QAction( tr( "Copy" ), m_pEntries );
	pCopy->setShortcut( QKeySequence::Copy );
	pCopy->setShortcutContext( Qt::WidgetShortcut );
	connect( pCopy, &QAction::triggered, this, &CSoundEditor::CopyEntry );
	m_pEntries->addAction( pCopy );
	auto *pPasteKey = new QAction( tr( "Paste" ), m_pEntries );
	pPasteKey->setShortcut( QKeySequence::Paste );
	pPasteKey->setShortcutContext( Qt::WidgetShortcut );
	connect( pPasteKey, &QAction::triggered, this, &CSoundEditor::PasteEntries );
	m_pEntries->addAction( pPasteKey );
	connect( m_pEntries, &QListWidget::itemDoubleClicked, this, [ this ] {
		if ( m_pCard )
		{
			if ( const xv::kv1::CNode *pNode = m_pDocument->Node( { *m_Current } ) )
			{
				bool bOk = false;
				const QString szName = QInputDialog::getText( this, tr( "Rename" ), tr( "Name:" ), QLineEdit::Normal, Qs( pNode->GetKey() ), &bOk );
				if ( bOk && !szName.isEmpty() )
				{
					m_pDocument->SetKey( { *m_Current }, szName );
				}
			}
		}
	} );
	connect( m_pIssueList, &QListWidget::itemActivated, this, [ this ]( QListWidgetItem *pItem ) {
		JumpTo( VariantToPath( pItem->data( Qt::UserRole ) ) );
	} );
	connect( m_pDocument, &CSoundDocument::Changed, this, &CSoundEditor::ScheduleRefresh );
	connect( &CInterfaceMode::Instance(), &CInterfaceMode::Changed, this, &CSoundEditor::ShowEntry );

	// Open on the first entry.
	for ( std::size_t i = 0; i < m_pDocument->GetDocument().m_Roots.size(); i++ )
	{
		if ( m_pDocument->GetDocument().m_Roots[ i ].IsBlock() )
		{
			m_Current = i;
			break;
		}
	}
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose: Re-run the linter, e.g. after the game was remounted.
//-----------------------------------------------------------------------------
void CSoundEditor::Relint()
{
	Lint();
	RebuildEntryList();
	if ( m_pCard )
	{
		m_pCard->ShowIssues( m_Issues );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundEditor::SelectEntry( std::size_t nRoot )
{
	m_Current = nRoot;
	RebuildEntryList();
	ShowEntry();
}

//-----------------------------------------------------------------------------
// Purpose: Edits arrive while a table is still inside its itemChanged handler; rebuilding the card
//          right then would delete the table under it, so the rebuild waits for the event loop.
//-----------------------------------------------------------------------------
void CSoundEditor::ScheduleRefresh()
{
	if ( !m_bRefreshQueued )
	{
		m_bRefreshQueued = true;
		QTimer::singleShot( 0, this, &CSoundEditor::Refresh );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundEditor::Refresh()
{
	m_bRefreshQueued = false;
	const auto &roots = m_pDocument->GetDocument().m_Roots;
	if ( m_Current && *m_Current >= roots.size() )
	{
		m_Current = roots.empty() ? std::nullopt : std::optional{ roots.size() - 1 };
	}
	Lint();
	RebuildEntryList();
	ShowEntry();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundEditor::Lint()
{
	const auto context = m_Index.LintContext( *m_pDocument );
	m_bListedInManifest = context.m_ListedInManifest.value_or( true );
	m_Issues = xv::sound::Lint( m_pDocument->GetDocument(), m_pDocument->GetKind(), context );
	m_pAddToManifest->setEnabled( !m_bListedInManifest );
	ShowIssueList();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundEditor::ShowIssueList()
{
	m_pIssueList->clear();
	int counts[ 3 ] = {};
	for ( const auto &diagnostic : m_pDocument->GetParseDiagnostics() )
	{
		const auto nSeverity = diagnostic.m_nSeverity == xv::kv1::Diagnostic_t::Severity_t::SEVERITY_ERROR ? Issue_t::Severity_t::SEVERITY_ERROR : Issue_t::Severity_t::SEVERITY_WARNING;
		auto *pItem = new QListWidgetItem( SeverityIcon( nSeverity ), tr( "Line %1: %2 (as loaded)" ).arg( diagnostic.m_nLine ).arg( Qs( diagnostic.m_szMessage ) ), m_pIssueList );
		pItem->setData( Qt::UserRole, PathToVariant( {} ) );
		counts[ static_cast<int>( nSeverity ) ]++;
	}
	for ( const Issue_t &issue : m_Issues )
	{
		QString szWhere;
		if ( !issue.m_Path.empty() )
		{
			NodePath_t prefix;
			for ( const std::size_t nIndex : issue.m_Path )
			{
				prefix.push_back( nIndex );
				if ( const xv::kv1::CNode *pNode = m_pDocument->Node( prefix ) )
				{
					szWhere += ( szWhere.isEmpty() ? QString{} : QStringLiteral( " › " ) ) + Qs( pNode->GetKey() );
				}
			}
			szWhere += QStringLiteral( ": " );
		}
		auto *pItem = new QListWidgetItem( SeverityIcon( issue.m_nSeverity ), szWhere + Qs( issue.m_szMessage ), m_pIssueList );
		pItem->setData( Qt::UserRole, PathToVariant( issue.m_Path ) );
		counts[ static_cast<int>( issue.m_nSeverity ) ]++;
	}
	const int nErrors = counts[ static_cast<int>( Issue_t::Severity_t::SEVERITY_ERROR ) ];
	const int nWarnings = counts[ static_cast<int>( Issue_t::Severity_t::SEVERITY_WARNING ) ];
	const auto count = []( int n, const QString &szOne, const QString &szMany ) { return ( n == 1 ? szOne : szMany ).arg( n ); };
	m_pSummary->setText( nErrors + nWarnings == 0
			? tr( "No problems found" )
			: count( nErrors, tr( "%1 error" ), tr( "%1 errors" ) ) + QStringLiteral( " · " ) + count( nWarnings, tr( "%1 warning" ), tr( "%1 warnings" ) ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundEditor::RebuildEntryList()
{
	const QSignalBlocker blocker{ m_pEntries };
	m_pEntries->clear();
	const auto &roots = m_pDocument->GetDocument().m_Roots;
	for ( std::size_t i = 0; i < roots.size(); i++ )
	{
		if ( !roots[ i ].IsBlock() )
		{
			continue;
		}
		auto *pItem = new QListWidgetItem( Qs( roots[ i ].GetKey() ), m_pEntries );
		pItem->setData( Qt::UserRole, QVariant::fromValue( static_cast<qulonglong>( i ) ) );
		std::optional<Issue_t::Severity_t> worst;
		for ( const Issue_t &issue : m_Issues )
		{
			if ( !issue.m_Path.empty() && issue.m_Path[ 0 ] == i && issue.m_nSeverity != Issue_t::Severity_t::SEVERITY_INFO )
			{
				worst = std::max( worst.value_or( issue.m_nSeverity ), issue.m_nSeverity );
			}
		}
		if ( worst )
		{
			pItem->setIcon( SeverityIcon( *worst ) );
		}
		pItem->setHidden( !pItem->text().contains( m_pFilter->text(), Qt::CaseInsensitive ) );
		if ( m_Current == i )
		{
			m_pEntries->setCurrentItem( pItem );
		}
	}
	const bool bSelected = m_Current && *m_Current < roots.size() && roots[ *m_Current ].IsBlock();
	m_pDuplicate->setEnabled( bSelected );
	m_pDelete->setEnabled( bSelected );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundEditor::ShowEntry()
{
	const int nScroll = m_pScroll->verticalScrollBar()->value();
	const bool bSameEntry = m_pCard && m_Current && m_pCard->GetPath() == NodePath_t{ *m_Current };
	// A value box being stepped through keeps the keyboard across the rebuild.
	const auto focused = bSameEntry ? m_pCard->GetFocusedValue() : std::nullopt;

	auto *pHost = new QWidget;
	auto *pLayout = new QVBoxLayout( pHost );
	pLayout->setContentsMargins( 12, 12, 12, 12 );
	m_pCard = nullptr;
	const auto &roots = m_pDocument->GetDocument().m_Roots;
	if ( m_Current && *m_Current < roots.size() && roots[ *m_Current ].IsBlock() )
	{
		m_pCard = new CKvCard( *m_pDocument, m_Index, m_pPlayer, { *m_Current } );
		connect( m_pCard, &CKvCard::Added, this, [ this ]( const NodePath_t &path ) { m_PendingEdit = path; } );
		m_pCard->ShowIssues( m_Issues );
		pLayout->addWidget( m_pCard );
	}
	else
	{
		auto *pEmpty = new QLabel( tr( "Nothing selected. Use “New %1…” to add one." ).arg( Qs( m_pDocument->GetSchema().m_szEntryNoun ) ) );
		pEmpty->setEnabled( false );
		pLayout->addWidget( pEmpty );
	}
	pLayout->addStretch();
	m_pScroll->setWidget( pHost ); // deletes the previous card

	if ( bSameEntry )
	{
		QTimer::singleShot( 0, this, [ this, nScroll, focused ] {
			m_pScroll->verticalScrollBar()->setValue( nScroll );
			if ( focused && m_pCard )
			{
				m_pCard->FocusValue( focused->first, focused->second );
			}
		} );
	}
	if ( m_PendingEdit && m_pCard )
	{
		const NodePath_t path = *m_PendingEdit;
		m_PendingEdit.reset();
		QTimer::singleShot( 0, this, [ this, path ] {
			if ( !m_pCard )
			{
				return;
			}
			if ( !m_pCard->BeginEdit( path ) )
			{
				if ( CKvCard *pAdded = m_pCard->CardFor( path ) )
				{
					m_pScroll->ensureWidgetVisible( pAdded );
				}
			}
		} );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundEditor::JumpTo( const NodePath_t &path )
{
	if ( path.empty() )
	{
		return;
	}
	if ( m_Current != path[ 0 ] )
	{
		m_Current = path[ 0 ];
		RebuildEntryList();
		ShowEntry();
	}
	QTimer::singleShot( 0, this, [ this, path ] {
		if ( !m_pCard || m_pCard->BeginEdit( path ) )
		{
			return;
		}
		if ( CKvCard *pCard = m_pCard->CardFor( path ) )
		{
			m_pScroll->ensureWidgetVisible( pCard );
		}
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundEditor::AddEntry()
{
	const QString szNoun = Qs( m_pDocument->GetSchema().m_szEntryNoun );
	bool bOk = false;
	const QString szName = QInputDialog::getText( this, tr( "New %1" ).arg( szNoun ), tr( "Name:" ), QLineEdit::Normal, {}, &bOk ).trimmed();
	if ( !bOk )
	{
		return;
	}
	if ( szName.isEmpty() )
	{
		QMessageBox::warning( this, tr( "No name" ), tr( "A %1 needs a name; nothing was added." ).arg( szNoun ) );
		return;
	}
	const std::size_t nIndex = m_Current ? *m_Current + 1 : m_pDocument->GetDocument().m_Roots.size();
	m_pDocument->InsertEntry( nIndex, EntryTemplate( *m_pDocument, szName.toStdString() ), tr( "New %1" ).arg( szName ) );
	m_Current = nIndex;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundEditor::DuplicateEntry()
{
	if ( !m_Current )
	{
		return;
	}
	xv::kv1::CNode copy = m_pDocument->GetDocument().m_Roots[ *m_Current ];
	const QString szName = Qs( copy.GetKey() ) + QStringLiteral( "_copy" );
	copy.SetKey( szName.toStdString() );
	m_pDocument->InsertEntry( *m_Current + 1, std::move( copy ), tr( "Duplicate as %1" ).arg( szName ) );
	m_Current = *m_Current + 1;
}

//-----------------------------------------------------------------------------
// Purpose: The picked entry, as KeyValues on the clipboard.
//-----------------------------------------------------------------------------
void CSoundEditor::CopyEntry()
{
	if ( m_Current && *m_Current < m_pDocument->GetDocument().m_Roots.size() )
	{
		CopyNodes( { m_pDocument->GetDocument().m_Roots[ *m_Current ] } );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The clipboard's blocks as entries after the picked one, as one undo step; a name the
//          file has already gets _copy.
//-----------------------------------------------------------------------------
void CSoundEditor::PasteEntries()
{
	std::vector<xv::kv1::CNode> nodes = PastedNodes();
	std::erase_if( nodes, []( const xv::kv1::CNode &node ) { return !node.IsBlock(); } );
	if ( nodes.empty() )
	{
		if ( CanPasteNodes() )
		{
			QMessageBox::information( this, tr( "Paste" ), tr( "Those are keys: paste them into a block on the right." ) );
		}
		return;
	}
	const auto taken = [ this ]( const std::string &szName ) {
		return std::ranges::any_of( m_pDocument->GetDocument().m_Roots, [ &szName ]( const xv::kv1::CNode &root ) { return xv::kv1::EqualsIgnoreCase( root.GetKey(), szName ); } );
	};
	std::size_t nIndex = m_Current ? *m_Current + 1 : m_pDocument->GetDocument().m_Roots.size();
	m_pDocument->GetUndoStack()->beginMacro( tr( "Paste %n entry(s)", nullptr, static_cast<int>( nodes.size() ) ) );
	for ( xv::kv1::CNode &node : nodes )
	{
		std::string szName = node.GetKey();
		while ( taken( szName ) )
		{
			szName += "_copy";
		}
		node.SetKey( szName );
		m_pDocument->InsertEntry( nIndex, std::move( node ), tr( "Paste %1" ).arg( Qs( szName ) ) );
		m_Current = nIndex++;
	}
	m_pDocument->GetUndoStack()->endMacro();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSoundEditor::DeleteEntry()
{
	if ( !m_Current )
	{
		return;
	}
	const QString szName = Qs( m_pDocument->GetDocument().m_Roots[ *m_Current ].GetKey() );
	if ( QMessageBox::question( this, tr( "Delete" ), tr( "Delete “%1”?" ).arg( szName ) ) == QMessageBox::Yes )
	{
		m_pDocument->RemoveNode( { *m_Current } );
	}
}
