//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ParticleBrowser.h.
//
//===========================================================================//
#include "ParticleBrowser.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPixmap>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "ParticlePreview.h"
#include "map/MapParticles.h"
#include "vfs/Vfs.h"

namespace
{

//-----------------------------------------------------------------------------
// Purpose: What a system spawns (its children) and what spawns it, by name, in its own file.
//-----------------------------------------------------------------------------
std::pair<QStringList, QStringList> FamilyOf( const CMapParticles::SystemSource_t &source )
{
	QStringList children;
	QStringList parents;
	const auto &definitions = source.m_pLibrary->m_Definitions;
	for ( const auto &child : definitions[ source.m_nDefinition ].m_Children )
	{
		if ( child.m_nDefinition < definitions.size() )
			children << QString::fromStdString( definitions[ child.m_nDefinition ].m_szName );
	}
	for ( std::size_t i = 0; i < definitions.size(); i++ )
	{
		if ( std::ranges::any_of( definitions[ i ].m_Children, [ & ]( const auto &child ) { return child.m_nDefinition == source.m_nDefinition; } ) )
			parents << QString::fromStdString( definitions[ i ].m_szName );
	}
	return { children, parents };
}

//-----------------------------------------------------------------------------
// Purpose: "parent", "child" or "parent, child" (empty for neither), as the list marks it.
//-----------------------------------------------------------------------------
QString FamilyWords( bool bParent, bool bChild )
{
	if ( bParent && bChild )
		return QCoreApplication::translate( "CParticleBrowser", "parent, child" );
	if ( bParent )
		return QCoreApplication::translate( "CParticleBrowser", "parent" );
	if ( bChild )
		return QCoreApplication::translate( "CParticleBrowser", "child" );
	return {};
}

constexpr char GRID_SETTING[] = "maps/particleBrowserGrid";
// The grid's pictures: their side, how long the pictures may take a pass (so the browser stays
// responsive while it makes them), and how often a pass runs.
constexpr int PICTURE_SIZE = 96;
constexpr int PICTURE_BUDGET_MS = 25;
constexpr int PICTURE_PASS_MS = 30;
// How long the mouse rests on a system before it plays.
constexpr int PLAY_WAIT_MS = 80;

//-----------------------------------------------------------------------------
// Purpose: Every word must be in the name, in any order and case (as the texture browser's).
//-----------------------------------------------------------------------------
class CWordsFilter : public QSortFilterProxyModel
{
public:
	void SetWords( const QString &szWords )
	{
		beginFilterChange();
		m_Words = szWords.split( QLatin1Char( ' ' ), Qt::SkipEmptyParts );
		endFilterChange( QSortFilterProxyModel::Direction::Rows );
	}

protected:
	[[nodiscard]] bool filterAcceptsRow( int nRow, const QModelIndex &parent ) const override
	{
		const QString szName = sourceModel()->index( nRow, 0, parent ).data( CParticleBrowser::ROLE_NAME ).toString();
		return std::ranges::all_of( m_Words, [ &szName ]( const QString &szWord ) { return szName.contains( szWord, Qt::CaseInsensitive ); } );
	}

private:
	QStringList m_Words;
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParticleBrowser::CParticleBrowser( const CMapParticles &particles, const QString &szCurrent, QWidget *pParent )
	: QDialog( pParent ),
	  m_Particles( particles ),
	  m_szPicked( szCurrent )
{
	setObjectName( QStringLiteral( "particleBrowser" ) );
	setWindowTitle( tr( "Particle Systems" ) );
	resize( 1040, 600 );
	auto *pLayout = new QVBoxLayout( this );
	auto *pTop = new QHBoxLayout;
	pTop->addWidget( new QLabel( tr( "Filter" ) ) );
	m_pWords = new QLineEdit;
	m_pWords->setObjectName( QStringLiteral( "particleBrowserFilter" ) );
	m_pWords->setClearButtonEnabled( true );
	pTop->addWidget( m_pWords, 1 );
	m_pList = new QToolButton;
	m_pList->setObjectName( QStringLiteral( "particleBrowserList" ) );
	m_pList->setText( tr( "List" ) );
	m_pList->setToolTip( tr( "The systems' names, with the file each is from" ) );
	m_pList->setCheckable( true );
	m_pGrid = new QToolButton;
	m_pGrid->setObjectName( QStringLiteral( "particleBrowserGrid" ) );
	m_pGrid->setText( tr( "Grid" ) );
	m_pGrid->setToolTip( tr( "A picture of each system, a moment after it starts" ) );
	m_pGrid->setCheckable( true );
	pTop->addWidget( m_pList );
	pTop->addWidget( m_pGrid );
	pLayout->addLayout( pTop );

	m_pModel = new QStandardItemModel( this );
	const std::shared_ptr<const xv::vfs::CFileSystem> pFileSystem = particles.GetFileSystem();
	for ( const std::string &szName : particles.GetSystemNames() )
	{
		auto *pItem = new QStandardItem( QString::fromStdString( szName ) );
		pItem->setEditable( false );
		pItem->setData( QString::fromStdString( szName ), ROLE_NAME );
		if ( const auto source = particles.GetSystemSource( szName ) )
		{
			pItem->setToolTip( source->m_bListed ? QString::fromStdString( source->m_szFile ) : tr( "%1 (no manifest lists it: the game won't load it)" ).arg( QString::fromStdString( source->m_szFile ) ) );
			const auto [ children, parents ] = FamilyOf( *source );
			pItem->setData( FamilyWords( !children.isEmpty(), !parents.isEmpty() ), ROLE_FAMILY );
		}
		m_pModel->appendRow( pItem );
	}
	auto *pFilter = new CWordsFilter;
	pFilter->setParent( this );
	pFilter->setSourceModel( m_pModel );
	m_pFilter = pFilter;
	m_pView = new QListView;
	m_pView->setObjectName( QStringLiteral( "particleBrowserView" ) );
	m_pView->setModel( m_pFilter );
	m_pView->setUniformItemSizes( true );
	m_pView->setMouseTracking( true );
	m_pView->viewport()->installEventFilter( this );

	// Beside them, the system under the mouse playing, with what it is and where it is from.
	auto *pSide = new QWidget;
	auto *pSideLayout = new QVBoxLayout( pSide );
	pSideLayout->setContentsMargins( 0, 0, 0, 0 );
	m_pPreview = new CParticlePreview;
	m_pPreview->setObjectName( QStringLiteral( "particleBrowserPreview" ) );
	m_pPreview->setMinimumSize( 320, 280 );
	m_pName = new QLabel;
	m_pName->setObjectName( QStringLiteral( "particleBrowserName" ) );
	m_pName->setTextInteractionFlags( Qt::TextSelectableByMouse );
	m_pFile = new QLabel;
	m_pFile->setObjectName( QStringLiteral( "particleBrowserFile" ) );
	m_pFile->setWordWrap( true );
	m_pFile->setTextInteractionFlags( Qt::TextSelectableByMouse );
	pSideLayout->addWidget( m_pPreview, 1 );
	pSideLayout->addWidget( m_pName );
	pSideLayout->addWidget( m_pFile );
	m_pFamily = new QLabel;
	m_pFamily->setObjectName( QStringLiteral( "particleBrowserFamily" ) );
	m_pFamily->setWordWrap( true );
	m_pFamily->setTextInteractionFlags( Qt::TextSelectableByMouse );
	pSideLayout->addWidget( m_pFamily );
	auto *pSplitter = new QSplitter( Qt::Horizontal );
	pSplitter->addWidget( m_pView );
	pSplitter->addWidget( pSide );
	pSplitter->setStretchFactor( 0, 1 );
	pSplitter->setStretchFactor( 1, 1 );
	pLayout->addWidget( pSplitter, 1 );
	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	pLayout->addWidget( pButtons );

	// The pictures are drawn by a preview of their own, so the one playing keeps playing.
	m_pPainter = new CParticlePreview( this );
	m_pPainter->hide();
	const xv::particles::FileReader_t read = [ pFileSystem ]( std::string_view szPath ) { return pFileSystem ? pFileSystem->Read( szPath ) : std::nullopt; };
	m_pPreview->SetReader( read );
	m_pPainter->SetReader( read );

	m_pWait = new QTimer( this );
	m_pWait->setSingleShot( true );
	m_pWait->setInterval( PLAY_WAIT_MS );
	connect( m_pWait, &QTimer::timeout, this, &CParticleBrowser::ShowPending );
	m_pPictures = new QTimer( this );
	m_pPictures->setInterval( PICTURE_PASS_MS );
	connect( m_pPictures, &QTimer::timeout, this, &CParticleBrowser::MakePictures );

	connect( m_pWords, &QLineEdit::textChanged, this, [ pFilter ]( const QString &szWords ) { pFilter->SetWords( szWords ); } );
	connect( m_pList, &QToolButton::clicked, this, [ this ] { SetGrid( false ); } );
	connect( m_pGrid, &QToolButton::clicked, this, [ this ] { SetGrid( true ); } );
	connect( m_pView, &QListView::entered, this, [ this ]( const QModelIndex &index ) {
		m_szPending = index.data( ROLE_NAME ).toString();
		m_pWait->start();
	} );
	connect( m_pView->selectionModel(), &QItemSelectionModel::currentChanged, this, [ this ]( const QModelIndex &current ) {
		if ( !m_pView->viewport()->underMouse() && current.isValid() )
			Play( current.data( ROLE_NAME ).toString() );
	} );
	connect( m_pView, &QListView::doubleClicked, this, [ this ] {
		PickCurrent();
		accept();
	} );
	connect( pButtons, &QDialogButtonBox::accepted, this, [ this ] {
		PickCurrent();
		accept();
	} );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );

	SetGrid( QSettings{}.value( QLatin1String( GRID_SETTING ), false ).toBool() );
	// The one the key names, picked and playing to start with.
	const QModelIndexList found = m_pModel->match( m_pModel->index( 0, 0 ), ROLE_NAME, szCurrent, 1, Qt::MatchFixedString );
	if ( !found.isEmpty() )
	{
		const QModelIndex index = m_pFilter->mapFromSource( found.front() );
		m_pView->setCurrentIndex( index );
		m_pView->scrollTo( index, QAbstractItemView::PositionAtCenter );
	}
	Play( szCurrent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParticleBrowser::~CParticleBrowser() = default;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<QString> CParticleBrowser::Pick( QWidget *pParent, const CMapParticles &particles, const QString &szCurrent )
{
	CParticleBrowser browser( particles, szCurrent, pParent );
	if ( browser.exec() != QDialog::Accepted || browser.GetPicked().isEmpty() )
	{
		return std::nullopt;
	}
	return browser.GetPicked();
}

//-----------------------------------------------------------------------------
// Purpose: The list (names with their files) or the grid (pictures), remembered.
//-----------------------------------------------------------------------------
void CParticleBrowser::SetGrid( bool bGrid )
{
	m_pList->setChecked( !bGrid );
	m_pGrid->setChecked( bGrid );
	QSettings{}.setValue( QLatin1String( GRID_SETTING ), bGrid );
	// Until its picture is made, a tile holds a clear one of its size: the grid measures every
	// item as one, so they all have room for a picture.
	QPixmap clear( PICTURE_SIZE, PICTURE_SIZE );
	clear.fill( Qt::transparent );
	const QIcon waiting( clear );
	for ( int i = 0; i < m_pModel->rowCount(); i++ )
	{
		QStandardItem *pItem = m_pModel->item( i );
		const QString szName = pItem->data( ROLE_NAME ).toString();
		// A parent or a child marked after the name: in the list, before its file; in the grid,
		// under it.
		const QString szFamily = pItem->data( ROLE_FAMILY ).toString();
		QString szText = szName;
		if ( !szFamily.isEmpty() )
			szText += bGrid ? QStringLiteral( "\n(%1)" ).arg( szFamily ) : QStringLiteral( "  [%1]" ).arg( szFamily );
		if ( !bGrid && !pItem->toolTip().isEmpty() )
			szText += QStringLiteral( "    " ) + pItem->toolTip();
		pItem->setText( szText );
		const auto picture = m_Pictures.find( szName );
		pItem->setIcon( !bGrid ? QIcon{} : picture != m_Pictures.end() && !picture->second.isNull() ? picture->second
																									: waiting );
	}
	// The size kept for every item is the old mode's until it is measured again.
	m_pView->setUniformItemSizes( false );
	if ( bGrid )
	{
		m_pView->setViewMode( QListView::IconMode );
		m_pView->setResizeMode( QListView::Adjust );
		m_pView->setMovement( QListView::Static );
		m_pView->setIconSize( { PICTURE_SIZE, PICTURE_SIZE } );
		m_pView->setGridSize( { PICTURE_SIZE + 32, PICTURE_SIZE + 40 } );
		m_pView->setWordWrap( true );
		m_pPictures->start();
	}
	else
	{
		m_pView->setViewMode( QListView::ListMode );
		m_pView->setGridSize( {} );
		m_pView->setIconSize( {} );
		m_pView->setWordWrap( false );
		m_pPictures->stop();
	}
	m_pView->setUniformItemSizes( true );
	m_pView->doItemsLayout();
	if ( m_pView->currentIndex().isValid() )
	{
		m_pView->scrollTo( m_pView->currentIndex() );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CParticleBrowser::IsGrid() const
{
	return m_pGrid->isChecked();
}

//-----------------------------------------------------------------------------
// Purpose: Off the systems, the one picked plays again.
//-----------------------------------------------------------------------------
bool CParticleBrowser::eventFilter( QObject *pObject, QEvent *pEvent )
{
	if ( pObject == m_pView->viewport() && pEvent->type() == QEvent::Leave )
	{
		m_pWait->stop();
		const QModelIndex current = m_pView->currentIndex();
		Play( current.isValid() ? current.data( ROLE_NAME ).toString() : m_szPicked );
	}
	return QDialog::eventFilter( pObject, pEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleBrowser::ShowPending()
{
	if ( m_pView->viewport()->underMouse() || !m_szPending.isEmpty() )
	{
		Play( m_szPending );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A system from its file's systems, from the start, framed.
//-----------------------------------------------------------------------------
void CParticleBrowser::Play( const QString &szName )
{
	if ( szName == m_szPlaying )
	{
		return;
	}
	m_szPlaying = szName;
	m_pName->setText( szName );
	const auto source = szName.isEmpty() ? std::nullopt : m_Particles.GetSystemSource( szName.toStdString() );
	if ( !source )
	{
		m_pFile->setText( szName.isEmpty() ? QString{} : tr( "The game has no system of this name." ) );
		m_pFamily->clear();
		m_pPreview->SetLibrary( nullptr );
		return;
	}
	m_pFile->setText( source->m_bListed ? QString::fromStdString( source->m_szFile ) : tr( "%1: no manifest lists this file, so the game won't load it" ).arg( QString::fromStdString( source->m_szFile ) ) );
	const auto [ children, parents ] = FamilyOf( *source );
	QStringList family;
	if ( !children.isEmpty() )
		family << tr( "Spawns %1" ).arg( children.join( QStringLiteral( ", " ) ) );
	if ( !parents.isEmpty() )
		family << tr( "Spawned by %1" ).arg( parents.join( QStringLiteral( ", " ) ) );
	m_pFamily->setText( family.isEmpty() ? tr( "Neither spawns nor is spawned by another system" ) : family.join( QStringLiteral( "; " ) ) );
	m_pPreview->SetLibrary( source->m_pLibrary );
	m_pPreview->SetSystem( source->m_pLibrary->m_Definitions[ source->m_nDefinition ].m_szName );
	m_pPreview->SetPlaying( true );
}

//-----------------------------------------------------------------------------
// Purpose: Pictures for the grid's systems in view that have none yet, as many as a pass has
//          time for.
//-----------------------------------------------------------------------------
void CParticleBrowser::MakePictures()
{
	if ( !IsGrid() || !isVisible() )
	{
		return;
	}
	QElapsedTimer clock;
	clock.start();
	const QRect shown = m_pView->viewport()->rect();
	for ( int nRow = 0; nRow < m_pFilter->rowCount() && clock.elapsed() < PICTURE_BUDGET_MS; nRow++ )
	{
		const QModelIndex index = m_pFilter->index( nRow, 0 );
		if ( !m_pView->visualRect( index ).intersects( shown ) )
		{
			continue;
		}
		const QString szName = index.data( ROLE_NAME ).toString();
		if ( m_Pictures.contains( szName ) )
		{
			continue;
		}
		QIcon picture;
		if ( const auto source = m_Particles.GetSystemSource( szName.toStdString() ) )
		{
			m_pPainter->SetLibrary( source->m_pLibrary );
			const QImage image = m_pPainter->Thumbnail( source->m_pLibrary->m_Definitions[ source->m_nDefinition ].m_szName, PICTURE_SIZE );
			if ( !image.isNull() )
			{
				picture = QIcon( QPixmap::fromImage( image ) );
			}
		}
		m_Pictures.emplace( szName, picture );
		m_pModel->itemFromIndex( m_pFilter->mapToSource( index ) )->setIcon( picture );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CParticleBrowser::PickCurrent()
{
	const QModelIndex current = m_pView->currentIndex();
	if ( current.isValid() )
	{
		m_szPicked = current.data( ROLE_NAME ).toString();
	}
}
