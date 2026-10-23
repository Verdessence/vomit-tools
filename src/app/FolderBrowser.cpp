//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See FolderBrowser.h.
//
//===========================================================================//
#include "FolderBrowser.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CFolderBrowser::CFolderBrowser( QWidget *pParent )
	: QWidget( pParent ),
	  m_pModel( new QFileSystemModel( this ) )
{
	m_pModel->setReadOnly( true );
	m_pModel->setFilter( QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden );

	auto *pOpen = new QToolButton;
	pOpen->setText( tr( "Open Folder…" ) );
	pOpen->setToolButtonStyle( Qt::ToolButtonTextOnly );
	connect( pOpen, &QToolButton::clicked, this, &CFolderBrowser::OpenFolderRequested );
	auto *pUp = new QToolButton;
	pUp->setText( QStringLiteral( "↑" ) );
	pUp->setToolTip( tr( "Parent folder" ) );
	connect( pUp, &QToolButton::clicked, this, [ this ] {
		if ( !m_szFolder.isEmpty() )
		{
			const QString szParent = QFileInfo{ m_szFolder }.dir().absolutePath();
			if ( szParent != m_szFolder )
			{
				SetFolder( szParent );
			}
		}
	} );
	m_pPath = new QLabel;
	m_pPath->setTextInteractionFlags( Qt::TextSelectableByMouse );
	m_pPath->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );

	auto *pBar = new QHBoxLayout;
	pBar->setContentsMargins( 0, 0, 0, 0 );
	pBar->addWidget( pOpen );
	pBar->addWidget( pUp );
	pBar->addWidget( m_pPath, 1 );

	m_pView = new QTreeView;
	m_pView->setModel( m_pModel );
	m_pView->setUniformRowHeights( true );
	m_pView->setSortingEnabled( true );
	m_pView->sortByColumn( 0, Qt::AscendingOrder );
	m_pView->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
	m_pView->header()->setStretchLastSection( false );
	m_pView->setColumnHidden( 2, true ); // type
	m_pView->setToolTip( tr( "Double-click a file to open it; soundscapes and soundscripts open in the editor" ) );
	connect( m_pView->selectionModel(), &QItemSelectionModel::currentChanged, this, [ this ]( const QModelIndex &index ) {
		if ( index.isValid() && !m_pModel->isDir( index ) )
		{
			emit FileSelected( m_pModel->filePath( index ) );
		}
	} );
	connect( m_pView, &QTreeView::activated, this, [ this ]( const QModelIndex &index ) {
		if ( index.isValid() && !m_pModel->isDir( index ) )
		{
			emit FileActivated( m_pModel->filePath( index ) );
		}
	} );

	auto *pEmpty = new QLabel( tr( "Open a folder (a mod, a custom/ folder...) to browse its files, or drop files and folders onto the window." ) );
	pEmpty->setWordWrap( true );
	pEmpty->setAlignment( Qt::AlignCenter );
	pEmpty->setEnabled( false );
	m_pEmpty = pEmpty;

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->addLayout( pBar );
	pLayout->addWidget( m_pView, 1 );
	pLayout->addWidget( m_pEmpty, 1 );
	m_pView->hide();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CFolderBrowser::SetFolder( const QString &szFolder )
{
	m_szFolder = QDir::cleanPath( QFileInfo{ szFolder }.absoluteFilePath() );
	m_pModel->setRootPath( m_szFolder );
	m_pView->setRootIndex( m_pModel->index( m_szFolder ) );
	m_pPath->setText( QDir::toNativeSeparators( m_szFolder ) );
	m_pPath->setToolTip( m_pPath->text() );
	m_pView->show();
	m_pEmpty->hide();
}
