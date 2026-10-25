//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ManifestDialog.h.
//
//===========================================================================//
#include "ManifestDialog.h"

#include <algorithm>

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "kv1/Kv1.h"
#include "particles/Particles.h"
#include "sound/SoundLint.h"
#include "vfs/Vfs.h"

namespace
{

constexpr int ROLE_LISTED = Qt::UserRole + 1; // whether the manifest lists it now

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Listed files first, in the manifest's order; then the rest, by path.
//-----------------------------------------------------------------------------
CManifestDialog::CManifestDialog( Format_t nFormat, const QString &szManifestPath, const std::string &szCurrent, std::vector<File_t> files, QWidget *pParent )
	: QDialog( pParent ),
	  m_nFormat( nFormat ),
	  m_szCurrent( szCurrent )
{
	setWindowTitle( tr( "Generate %1" ).arg( szManifestPath ) );
	resize( 1100, 640 );
	const bool bParticles = nFormat == Format_t::FORMAT_PARTICLES;
	const auto current = xv::kv1::Parse( szCurrent );
	std::vector<std::string> listed = bParticles ? xv::particles::ManifestFiles( current.m_Document ) : xv::sound::ManifestFiles( current.m_Document );

	m_pFiles = new QTreeWidget;
	m_pFiles->setObjectName( QStringLiteral( "manifestFiles" ) );
	m_pFiles->setColumnCount( COLUMN_COUNT );
	m_pFiles->setHeaderLabels( { tr( "File" ), tr( "Precache" ), tr( "Status" ) } );
	m_pFiles->setRootIsDecorated( false );
	m_pFiles->setUniformRowHeights( true );
	m_pFiles->setColumnHidden( COLUMN_PRECACHE, !bParticles );
	m_pFiles->header()->setStretchLastSection( true );
	m_pFiles->setColumnWidth( COLUMN_FILE, 380 );
	const auto addRow = [ & ]( const QString &szPath, bool bListed, const File_t *pFile ) {
		auto *pItem = new QTreeWidgetItem( m_pFiles );
		pItem->setText( COLUMN_FILE, szPath );
		pItem->setData( COLUMN_FILE, ROLE_LISTED, bListed );
		pItem->setFlags( pItem->flags() | Qt::ItemIsUserCheckable );
		pItem->setCheckState( COLUMN_FILE, bListed || ( pFile && pFile->m_bSuggested ) ? Qt::Checked : Qt::Unchecked );
		if ( bParticles )
		{
			pItem->setCheckState( COLUMN_PRECACHE, xv::particles::IsPrecached( current.m_Document, szPath.toStdString() ) ? Qt::Checked : Qt::Unchecked );
			pItem->setToolTip( COLUMN_PRECACHE, tr( "Precache every system in the file as the game starts ('!' before the path)" ) );
		}
		QStringList status;
		status << ( bListed ? tr( "listed" ) : tr( "not listed" ) );
		if ( pFile && !pFile->m_bExists )
		{
			status << tr( "missing from the game" );
		}
		if ( pFile && !pFile->m_szWhy.isEmpty() )
		{
			status << pFile->m_szWhy;
		}
		pItem->setText( COLUMN_STATUS, status.join( QStringLiteral( " · " ) ) );
		pItem->setToolTip( COLUMN_STATUS, pItem->text( COLUMN_STATUS ) );
	};
	for ( const std::string &szListed : listed )
	{
		const QString szPath = QString::fromStdString( szListed );
		if ( ItemFor( szPath ) )
		{
			continue; // listed twice
		}
		const auto it = std::ranges::find_if( files, [ &szListed ]( const File_t &file ) { return xv::vfs::NormalizePath( file.m_szPath.toStdString() ) == szListed; } );
		File_t missing{ szPath, false, false, {} };
		addRow( szPath, true, it != files.end() ? &*it : &missing );
	}
	std::ranges::sort( files, []( const File_t &a, const File_t &b ) { return a.m_szPath.compare( b.m_szPath, Qt::CaseInsensitive ) < 0; } );
	for ( const File_t &file : files )
	{
		if ( !ItemFor( file.m_szPath ) )
		{
			addRow( file.m_szPath, false, &file );
		}
	}
	connect( m_pFiles, &QTreeWidget::itemChanged, this, &CManifestDialog::UpdatePreview );

	m_pFilter = new QLineEdit;
	m_pFilter->setPlaceholderText( tr( "Filter files…" ) );
	m_pFilter->setClearButtonEnabled( true );
	connect( m_pFilter, &QLineEdit::textChanged, this, [ this ]( const QString &szText ) {
		for ( int i = 0; i < m_pFiles->topLevelItemCount(); i++ )
		{
			m_pFiles->topLevelItem( i )->setHidden( !m_pFiles->topLevelItem( i )->text( COLUMN_FILE ).contains( szText, Qt::CaseInsensitive ) );
		}
	} );
	const auto tickAll = [ this ]( bool bTick, bool bOnlyMissing ) {
		for ( int i = 0; i < m_pFiles->topLevelItemCount(); i++ )
		{
			QTreeWidgetItem *pItem = m_pFiles->topLevelItem( i );
			if ( !pItem->isHidden() && ( !bOnlyMissing || pItem->text( COLUMN_STATUS ).contains( tr( "missing from the game" ) ) ) )
			{
				pItem->setCheckState( COLUMN_FILE, bTick ? Qt::Checked : Qt::Unchecked );
			}
		}
	};
	auto *pAll = new QPushButton( tr( "Tick All" ) );
	connect( pAll, &QPushButton::clicked, this, [ tickAll ] { tickAll( true, false ); } );
	auto *pNone = new QPushButton( tr( "Untick All" ) );
	connect( pNone, &QPushButton::clicked, this, [ tickAll ] { tickAll( false, false ); } );
	auto *pMissing = new QPushButton( tr( "Untick Missing" ) );
	pMissing->setToolTip( tr( "Stop listing files the game doesn't have" ) );
	connect( pMissing, &QPushButton::clicked, this, [ tickAll ] { tickAll( false, true ); } );
	m_pCount = new QLabel;
	auto *pTools = new QHBoxLayout;
	pTools->addWidget( m_pFilter, 1 );
	pTools->addWidget( pAll );
	pTools->addWidget( pNone );
	pTools->addWidget( pMissing );

	auto *pLeft = new QWidget;
	auto *pLeftLayout = new QVBoxLayout( pLeft );
	pLeftLayout->setContentsMargins( 0, 0, 0, 0 );
	pLeftLayout->addLayout( pTools );
	pLeftLayout->addWidget( m_pFiles, 1 );
	pLeftLayout->addWidget( m_pCount );

	m_pPreview = new QPlainTextEdit;
	m_pPreview->setObjectName( QStringLiteral( "manifestPreview" ) );
	m_pPreview->setReadOnly( true );
	m_pPreview->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
	m_pPreview->setLineWrapMode( QPlainTextEdit::NoWrap );
	m_pPreview->setTabStopDistance( 4 * m_pPreview->fontMetrics().horizontalAdvance( u' ' ) );

	auto *pSplitter = new QSplitter;
	pSplitter->addWidget( pLeft );
	pSplitter->addWidget( m_pPreview );
	pSplitter->setSizes( { 680, 420 } );

	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Save | QDialogButtonBox::Cancel );
	connect( pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );

	auto *pIntro = new QLabel( szCurrent.empty() ? tr( "%1 doesn't exist yet; it's made from the ticked files." ).arg( szManifestPath )
												 : tr( "Ticked files are listed in %1. Files staying listed keep their place and comments; new ones go at the end." ).arg( szManifestPath ) );
	pIntro->setWordWrap( true );
	m_pNotes = new QLabel;
	m_pNotes->setObjectName( QStringLiteral( "manifestNotes" ) );
	m_pNotes->setWordWrap( true );
	m_pNotes->setTextInteractionFlags( Qt::TextSelectableByMouse );
	m_pNotes->hide();
	auto *pLayout = new QVBoxLayout( this );
	pLayout->addWidget( pIntro );
	pLayout->addWidget( m_pNotes );
	pLayout->addWidget( pSplitter, 1 );
	pLayout->addWidget( pButtons );
	UpdatePreview();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QTreeWidgetItem *CManifestDialog::ItemFor( const QString &szPath ) const
{
	const std::string szWanted = xv::vfs::NormalizePath( szPath.toStdString() );
	for ( int i = 0; i < m_pFiles->topLevelItemCount(); i++ )
	{
		if ( xv::vfs::NormalizePath( m_pFiles->topLevelItem( i )->text( COLUMN_FILE ).toStdString() ) == szWanted )
		{
			return m_pFiles->topLevelItem( i );
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The current manifest with unticked files taken out and ticked ones added; a new one
//          gets its block first.
//-----------------------------------------------------------------------------
std::string CManifestDialog::GetText() const
{
	auto parsed = xv::kv1::Parse( m_szCurrent );
	auto &document = parsed.m_Document;
	if ( std::ranges::none_of( document.m_Roots, &xv::kv1::CNode::IsBlock ) )
	{
		const char *pszBlock = m_nFormat == Format_t::FORMAT_SOUNDSCAPES ? "soundscapes_manifest" : m_nFormat == Format_t::FORMAT_SOUNDSCRIPTS ? "game_sounds_manifest"
																																			   : "particles_manifest";
		document.AppendRoot( xv::kv1::CNode::MakeBlock( pszBlock ) );
	}
	for ( int i = 0; i < m_pFiles->topLevelItemCount(); i++ )
	{
		const QTreeWidgetItem *pItem = m_pFiles->topLevelItem( i );
		const std::string szPath = pItem->text( COLUMN_FILE ).toStdString();
		const bool bListed = pItem->data( COLUMN_FILE, ROLE_LISTED ).toBool();
		const bool bWanted = pItem->checkState( COLUMN_FILE ) == Qt::Checked;
		if ( m_nFormat == Format_t::FORMAT_PARTICLES )
		{
			const bool bPrecache = pItem->checkState( COLUMN_PRECACHE ) == Qt::Checked;
			if ( bListed && !bWanted )
			{
				xv::particles::RemoveFromManifest( document, szPath );
			}
			else if ( bListed )
			{
				xv::particles::SetPrecached( document, szPath, bPrecache );
			}
			else if ( bWanted )
			{
				xv::particles::AddToManifest( document, szPath, bPrecache );
			}
			continue;
		}
		if ( bListed && !bWanted )
		{
			xv::sound::RemoveFromManifest( document, szPath );
		}
		else if ( !bListed && bWanted )
		{
			xv::sound::AddToManifest( document, m_nFormat == Format_t::FORMAT_SOUNDSCAPES ? xv::sound::FileKind_t::FILE_KIND_SOUNDSCAPE : xv::sound::FileKind_t::FILE_KIND_SOUNDSCRIPT, szPath );
		}
	}
	return xv::kv1::Write( document );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CManifestDialog::UpdatePreview()
{
	int nTicked = 0;
	int nAdded = 0;
	int nRemoved = 0;
	for ( int i = 0; i < m_pFiles->topLevelItemCount(); i++ )
	{
		const QTreeWidgetItem *pItem = m_pFiles->topLevelItem( i );
		const bool bListed = pItem->data( COLUMN_FILE, ROLE_LISTED ).toBool();
		const bool bWanted = pItem->checkState( COLUMN_FILE ) == Qt::Checked;
		nTicked += bWanted;
		nAdded += bWanted && !bListed;
		nRemoved += bListed && !bWanted;
	}
	m_pCount->setText( tr( "%1 listed: %2 added, %3 taken out" ).arg( nTicked ).arg( nAdded ).arg( nRemoved ) );
	const int nScroll = m_pPreview->verticalScrollBar()->value();
	m_pPreview->setPlainText( QString::fromStdString( GetText() ) );
	m_pPreview->verticalScrollBar()->setValue( nScroll );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CManifestDialog::SetListed( const QString &szPath, bool bListed )
{
	if ( QTreeWidgetItem *pItem = ItemFor( szPath ) )
	{
		pItem->setCheckState( COLUMN_FILE, bListed ? Qt::Checked : Qt::Unchecked );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CManifestDialog::SetPrecached( const QString &szPath, bool bPrecached )
{
	if ( QTreeWidgetItem *pItem = ItemFor( szPath ) )
	{
		pItem->setCheckState( COLUMN_PRECACHE, bPrecached ? Qt::Checked : Qt::Unchecked );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CManifestDialog::SetNotes( const QString &szNotes )
{
	m_pNotes->setText( szNotes );
	m_pNotes->setVisible( !szNotes.isEmpty() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CManifestDialog::GetRowCount() const
{
	return m_pFiles->topLevelItemCount();
}
