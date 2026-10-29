//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MaterialBrowser.h.
//
//===========================================================================//
#include "MaterialBrowser.h"

#include <algorithm>
#include <chrono>

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPixmap>
#include <QTimer>
#include <QThread>
#include "MaterialPictures.h"
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "ImageFiles.h"
#include "kv1/Kv1.h"
#include "map/MapResources.h"
#include "map/Materials.h"

namespace
{

constexpr int SMALL_PICTURE = 128; // what is kept for every material; smaller sizes are scaled from it
constexpr int LARGE_PICTURE = 512; // made only for those shown at 256 or 512

//-----------------------------------------------------------------------------
// Purpose: Hammer's name for a material file: "materials/brick/wall.vmt" is BRICK/WALL.
//-----------------------------------------------------------------------------
QString HammerName( const std::string &szPath )
{
	QString szName = QString::fromStdString( szPath );
	if ( szName.startsWith( QStringLiteral( "materials/" ), Qt::CaseInsensitive ) )
	{
		szName.remove( 0, 10 );
	}
	if ( szName.endsWith( QStringLiteral( ".vmt" ), Qt::CaseInsensitive ) )
	{
		szName.chop( 4 );
	}
	return szName.toUpper();
}

// Every word typed must be in the name; "Only used" keeps the map's materials.
class CMaterialFilter : public QSortFilterProxyModel
{
public:
	QStringList m_Words;
	QStringList m_Used; // upper case, sorted
	bool m_bOnlyUsed = false;

protected:
	bool filterAcceptsRow( int nRow, const QModelIndex &parent ) const override
	{
		const QString szName = sourceModel()->index( nRow, 0, parent ).data( CMaterialList::ROLE_NAME ).toString();
		if ( m_bOnlyUsed && !std::ranges::binary_search( m_Used, szName ) )
		{
			return false;
		}
		return std::ranges::all_of( m_Words, [ & ]( const QString &szWord ) { return szName.contains( szWord, Qt::CaseInsensitive ); } );
	}
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMaterialList::CMaterialList( QObject *pParent )
	: QAbstractListModel( pParent )
{
	m_pNotify = new QTimer( this );
	m_pNotify->setSingleShot( true );
	m_pNotify->setInterval( 30 );
	connect( m_pNotify, &QTimer::timeout, this, [ this ] {
		if ( m_nFirstPending <= m_nLastPending )
			emit dataChanged( index( m_nFirstPending ), index( m_nLastPending ), { Qt::DecorationRole } );
		m_nFirstPending = std::numeric_limits<int>::max();
		m_nLastPending = -1;
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CMaterialList::rowCount( const QModelIndex &parent ) const
{
	return parent.isValid() ? 0 : static_cast<int>( m_Names.size() );
}

//-----------------------------------------------------------------------------
// Purpose: A row without its picture yet asks for it; the worker takes those first.
//-----------------------------------------------------------------------------
QVariant CMaterialList::data( const QModelIndex &index, int nRole ) const
{
	if ( !index.isValid() || index.row() >= m_Names.size() )
	{
		return {};
	}
	switch ( nRole )
	{
		case Qt::DisplayRole:
		case ROLE_NAME:
			return m_Names[ index.row() ];
		case Qt::ToolTipRole:
			return m_Names[ index.row() ];
		case Qt::DecorationRole:
		{
			const QIcon &icon = m_Pictures[ static_cast<std::size_t>( index.row() ) ];
			if ( icon.isNull() )
			{
				const std::lock_guard lock( m_Mutex );
				if ( std::ranges::find( m_Wanted, index.row() ) == m_Wanted.end() )
				{
					m_Wanted.push_back( index.row() );
				}
				return {};
			}
			return icon;
		}
		case Qt::SizeHintRole:
			return QSize( m_nSize + 12, m_nSize + 28 );
		default:
			return {};
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialList::SetNames( QStringList names )
{
	m_pNotify->stop();
	m_nFirstPending = std::numeric_limits<int>::max();
	m_nLastPending = -1;
	beginResetModel();
	m_Names = std::move( names );
	m_Pictures.assign( static_cast<std::size_t>( m_Names.size() ), QIcon{} );
	endResetModel();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialList::SetPicture( int nRow, const QImage &picture, int nPictureSize )
{
	if ( nRow < 0 || nRow >= m_Names.size() || nPictureSize != m_nPictureSize )
	{
		return;
	}
	Q_ASSERT( QThread::currentThread() == thread() );
	m_Pictures[ static_cast<std::size_t>( nRow ) ] = QIcon( QPixmap::fromImage( picture ) );
	m_nFirstPending = std::min( m_nFirstPending, nRow );
	m_nLastPending = std::max( m_nLastPending, nRow );
	if ( !m_pNotify->isActive() )
		m_pNotify->start();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialList::SetSize( int nSize )
{
	m_nSize = nSize;
	if ( const int nPictureSize = nSize > SMALL_PICTURE ? LARGE_PICTURE : SMALL_PICTURE; nPictureSize != m_nPictureSize )
	{
		m_nPictureSize = nPictureSize;
		m_Pictures.assign( m_Pictures.size(), QIcon{} );
		const std::lock_guard lock( m_Mutex );
		m_Wanted.clear();
	}
	if ( !m_Names.isEmpty() )
	{
		emit dataChanged( index( 0 ), index( m_Names.size() - 1 ), { Qt::SizeHintRole, Qt::DecorationRole } );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<int> CMaterialList::TakeWanted()
{
	const std::lock_guard lock( m_Mutex );
	std::vector<int> wanted;
	if ( !m_Wanted.empty() )
	{
		wanted.push_back( m_Wanted.front() );
		m_Wanted.erase( m_Wanted.begin() );
	}
	return wanted;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMaterialBrowser::CMaterialBrowser( const xv::vfs::CGameInfo &game, QStringList used, const QString &szCurrent, QWidget *pParent )
	: QDialog( pParent ),
	  m_Game( game ),
	  m_Used( std::move( used ) ),
	  m_szPicked( szCurrent )
{
	setObjectName( QStringLiteral( "materialBrowser" ) );
	setWindowTitle( tr( "Textures" ) );
	resize( 760, 560 );
	for ( QString &szName : m_Used )
	{
		szName = szName.toUpper();
	}
	std::ranges::sort( m_Used );

	auto *pLayout = new QVBoxLayout( this );
	auto *pTop = new QHBoxLayout;
	pTop->addWidget( new QLabel( tr( "Filter" ) ) );
	m_pWords = new QLineEdit;
	m_pWords->setObjectName( QStringLiteral( "materialBrowserFilter" ) );
	m_pWords->setPlaceholderText( tr( "Words the name must have" ) );
	m_pWords->setClearButtonEnabled( true );
	pTop->addWidget( m_pWords, 1 );
	m_pSize = new QComboBox;
	m_pSize->setObjectName( QStringLiteral( "materialBrowserSize" ) );
	for ( const int nSize : { 32, 64, 128, 256, 512 } )
	{
		m_pSize->addItem( tr( "%1×%1" ).arg( nSize ), nSize );
	}
	m_pSize->setCurrentIndex( 1 );
	pTop->addWidget( m_pSize );
	m_pOnlyUsed = new QCheckBox( tr( "Only used textures" ) );
	m_pOnlyUsed->setObjectName( QStringLiteral( "materialBrowserOnlyUsed" ) );
	pTop->addWidget( m_pOnlyUsed );
	pLayout->addLayout( pTop );

	m_pList = new CMaterialList( this );
	auto *pFilter = new CMaterialFilter;
	pFilter->setParent( this );
	pFilter->setSourceModel( m_pList );
	m_pFilter = pFilter;
	m_pView = new QListView;
	m_pView->setObjectName( QStringLiteral( "materialBrowserView" ) );
	connect( m_pList, &CMaterialList::dataChanged, m_pView, [ this ] { m_pView->viewport()->update(); } );
	m_pView->setModel( m_pFilter );
	m_pView->setViewMode( QListView::IconMode );
	m_pView->setResizeMode( QListView::Adjust );
	m_pView->setMovement( QListView::Static );
	m_pView->setUniformItemSizes( true );
	m_pView->setIconSize( QSize( 64, 64 ) );
	m_pView->setSpacing( 4 );
	m_pView->setWordWrap( true );
	pLayout->addWidget( m_pView, 1 );
	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	pLayout->addWidget( pButtons );

	connect( m_pWords, &QLineEdit::textChanged, this, &CMaterialBrowser::ApplyFilter );
	connect( m_pOnlyUsed, &QCheckBox::toggled, this, &CMaterialBrowser::ApplyFilter );
	connect( m_pSize, &QComboBox::currentIndexChanged, this, [ this ] {
		const int nSize = m_pSize->currentData().toInt();
		m_pView->setIconSize( QSize( nSize, nSize ) );
		m_pList->SetSize( nSize );
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
	m_Worker = std::thread( [ this ] { Run(); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMaterialBrowser::~CMaterialBrowser()
{
	m_bStop = true;
	if ( m_Worker.joinable() )
	{
		m_Worker.join();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString CMaterialBrowser::CacheFolder()
{
	return MaterialPictureCacheFolder();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<QString> CMaterialBrowser::Pick( QWidget *pParent, const xv::vfs::CGameInfo &game, const QStringList &used, const QString &szCurrent )
{
	CMaterialBrowser browser( game, used, szCurrent, pParent );
	if ( browser.exec() != QDialog::Accepted || browser.GetPicked().isEmpty() )
	{
		return std::nullopt;
	}
	return browser.GetPicked();
}

//-----------------------------------------------------------------------------
// Purpose: Lists the materials (every VMT under materials/ but the models'), then makes
//          pictures: the rows the view asks for first, then the rest in order. A picture is a
//          material's base texture, 128 px across, kept on disk by the material's name; at the
//          two biggest sizes they are 512 px, and only the rows shown are made.
//-----------------------------------------------------------------------------
void CMaterialBrowser::Run()
{
	xv::vfs::CFileSystem fileSystem;
	fileSystem.Mount( m_Game );
	QStringList names;
	std::vector<std::string> paths;
	for ( const std::string &szPath : fileSystem.Find( "materials", ".vmt", true ) )
	{
		if ( szPath.size() > 17 && xv::kv1::EqualsIgnoreCase( std::string_view{ szPath }.substr( 0, 17 ), "materials/models/" ) )
		{
			continue;
		}
		paths.push_back( szPath );
	}
	std::ranges::sort( paths );
	for ( const std::string &szPath : paths )
	{
		names << HammerName( szPath );
	}
	QMetaObject::invokeMethod( this, [ this, names ] {
		m_pList->SetNames( names );
		m_bListed = true;
		ApplyFilter(); }, Qt::QueuedConnection );
	std::vector<bool> done( paths.size(), false );
	std::size_t nNext = 0;
	int nDoneSize = SMALL_PICTURE;
	while ( !m_bStop )
	{
		// Another size: every row is made again, but only the big ones the view shows.
		const int nPictureSize = m_pList->GetPictureSize();
		if ( nPictureSize != nDoneSize )
		{
			done.assign( paths.size(), false );
			nNext = 0;
			nDoneSize = nPictureSize;
		}
		std::vector<int> wanted = m_pList->TakeWanted();
		int nRow = -1;
		for ( const int n : wanted )
		{
			if ( n >= 0 && static_cast<std::size_t>( n ) < paths.size() && !done[ static_cast<std::size_t>( n ) ] )
			{
				nRow = n;
				break;
			}
		}
		if ( nRow < 0 && nPictureSize == LARGE_PICTURE )
		{
			std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
			continue;
		}
		if ( nRow < 0 )
		{
			while ( nNext < paths.size() && done[ nNext ] )
			{
				nNext++;
			}
			if ( nNext >= paths.size() )
			{
				std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
				continue;
			}
			nRow = static_cast<int>( nNext );
		}
		done[ static_cast<std::size_t>( nRow ) ] = true;
		const QString szName = names[ nRow ];
		const QImage picture = LoadMaterialPicture( fileSystem, QString::fromStdString( m_Game.m_GameDir.string() ), szName, nPictureSize ).m_Image;
		QMetaObject::invokeMethod( this, [ this, nRow, picture, nPictureSize ] { m_pList->SetPicture( nRow, picture, nPictureSize ); }, Qt::QueuedConnection );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialBrowser::ApplyFilter()
{
	auto *pFilter = static_cast<CMaterialFilter *>( m_pFilter );
	pFilter->m_Words = m_pWords->text().split( QLatin1Char( ' ' ), Qt::SkipEmptyParts );
	pFilter->m_Used = m_Used;
	pFilter->m_bOnlyUsed = m_pOnlyUsed->isChecked();
	pFilter->invalidate();
	// The current texture, where it is listed, starts selected.
	for ( int i = 0; !m_szPicked.isEmpty() && i < m_pFilter->rowCount(); i++ )
	{
		if ( m_pFilter->index( i, 0 ).data( CMaterialList::ROLE_NAME ).toString() == m_szPicked.toUpper() )
		{
			m_pView->setCurrentIndex( m_pFilter->index( i, 0 ) );
			break;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialBrowser::PickCurrent()
{
	const QModelIndex current = m_pView->currentIndex();
	if ( current.isValid() )
	{
		m_szPicked = current.data( CMaterialList::ROLE_NAME ).toString();
	}
}
