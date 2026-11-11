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
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QTimer>
#include <QThread>
#include "MaterialPictures.h"
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "ImageFiles.h"
#include "kv1/Kv1.h"
#include "material/Material.h"
#include "material/Preview.h"
#include "map/MapResources.h"
#include "map/Materials.h"

namespace
{

constexpr int SMALL_PICTURE = 128; // what is kept for every material; smaller sizes are scaled from it
constexpr int LARGE_PICTURE = 512; // made only for those shown at 256 or 512
// The pictures' size and the used-only tick, kept between openings.
constexpr char SIZE_SETTING[] = "maps/textureBrowserSize";
constexpr char ONLY_USED_SETTING[] = "maps/textureBrowserOnlyUsed";
constexpr char PREVIEW_SHAPE_SETTING[] = "maps/textureBrowserPreviewShape";
// The preview pane: its picture's side, the biggest texture it reads (a smaller mip past it), how
// long a hover rests before it is read, and its frames while a proxy animates (twenty a second).
constexpr int PREVIEW_SIZE = 256;
constexpr int PREVIEW_TEXTURE_SIZE = 512;
constexpr int PREVIEW_WAIT_MS = 60;
constexpr int PREVIEW_FRAME_MS = 50;

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

//-----------------------------------------------------------------------------
// Purpose: A VMT's shader: its first word, quoted or not, past any // comments. Reading only
//          that keeps listing every material's shader quick.
//-----------------------------------------------------------------------------
std::string_view ShaderOf( std::string_view szVmt )
{
	std::size_t i = 0;
	while ( i < szVmt.size() )
	{
		const char ch = szVmt[ i ];
		if ( ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || static_cast<unsigned char>( ch ) == 0xEF || static_cast<unsigned char>( ch ) == 0xBB || static_cast<unsigned char>( ch ) == 0xBF )
		{
			i++;
		}
		else if ( szVmt.substr( i, 2 ) == "//" )
		{
			const std::size_t nEnd = szVmt.find( '\n', i );
			i = nEnd == std::string_view::npos ? szVmt.size() : nEnd;
		}
		else
		{
			break;
		}
	}
	if ( i < szVmt.size() && szVmt[ i ] == '"' )
	{
		const std::size_t nEnd = szVmt.find( '"', i + 1 );
		return nEnd == std::string_view::npos ? std::string_view{} : szVmt.substr( i + 1, nEnd - i - 1 );
	}
	std::size_t nEnd = i;
	while ( nEnd < szVmt.size() && szVmt[ nEnd ] != ' ' && szVmt[ nEnd ] != '\t' && szVmt[ nEnd ] != '\r' && szVmt[ nEnd ] != '\n' && szVmt[ nEnd ] != '{' )
		nEnd++;
	return szVmt.substr( i, nEnd - i );
}

// Every word typed must be in the name; "Only used" keeps the map's materials.
class CMaterialFilter : public QSortFilterProxyModel
{
public:
	QStringList m_Words;
	QStringList m_Used; // upper case, sorted
	bool m_bOnlyUsed = false;
	bool m_bOnlySprites = false;

protected:
	bool filterAcceptsRow( int nRow, const QModelIndex &parent ) const override
	{
		const QModelIndex index = sourceModel()->index( nRow, 0, parent );
		const QString szName = index.data( CMaterialList::ROLE_NAME ).toString();
		if ( m_bOnlySprites && !index.data( CMaterialList::ROLE_SPRITE ).toBool() )
		{
			return false;
		}
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
			if ( m_bList )
				return {};
			return QSize( m_nSize + 12, m_nSize + 28 );
		case ROLE_SPRITE:
			return static_cast<std::size_t>( index.row() ) < m_Sprites.size() && m_Sprites[ static_cast<std::size_t>( index.row() ) ];
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
	m_Sprites.clear();
	endResetModel();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialList::SetSprites( std::vector<bool> sprites )
{
	m_Sprites = std::move( sprites );
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
CMaterialBrowser::CMaterialBrowser( const xv::vfs::CGameInfo &game, QStringList used, const QString &szCurrent, QWidget *pParent, bool bSprites )
	: QDialog( pParent ),
	  m_Game( game ),
	  m_Used( std::move( used ) ),
	  m_szPicked( szCurrent ),
	  m_bSprites( bSprites )
{
	setObjectName( QStringLiteral( "materialBrowser" ) );
	setWindowTitle( bSprites ? tr( "Sprites" ) : tr( "Textures" ) );
	resize( 1040, 600 ); // the grid, and the preview beside it
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
	if ( bSprites )
	{
		m_pOnlySprites = new QCheckBox( tr( "Only sprites" ) );
		m_pOnlySprites->setObjectName( QStringLiteral( "materialBrowserOnlySprites" ) );
		m_pOnlySprites->setToolTip( tr( "Materials drawn with the Sprite or SpriteCard shader, and everything under sprites/" ) );
		m_pOnlySprites->setChecked( true );
		pTop->addWidget( m_pOnlySprites );
	}
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
	// Beside the grid, the material under the mouse drawn live (or the one picked, away from it).
	m_pPreview = new CMaterialPreview( game, this );
	auto *pSplitter = new QSplitter( Qt::Horizontal );
	pSplitter->addWidget( m_pView );
	pSplitter->addWidget( m_pPreview );
	pSplitter->setStretchFactor( 0, 1 );
	pSplitter->setStretchFactor( 1, 0 );
	pLayout->addWidget( pSplitter, 1 );
	m_pView->setMouseTracking( true );
	m_pView->viewport()->installEventFilter( this );
	connect( m_pView, &QListView::entered, this, [ this ]( const QModelIndex &index ) { m_pPreview->Show( index.data( CMaterialList::ROLE_NAME ).toString() ); } );
	connect( m_pView->selectionModel(), &QItemSelectionModel::currentChanged, this, [ this ]( const QModelIndex &current ) {
		if ( !m_pView->viewport()->underMouse() && current.isValid() )
			m_pPreview->Show( current.data( CMaterialList::ROLE_NAME ).toString() );
	} );
	m_pPreview->Show( szCurrent.toUpper() );
	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	pLayout->addWidget( pButtons );

	connect( m_pWords, &QLineEdit::textChanged, this, &CMaterialBrowser::ApplyFilter );
	connect( m_pOnlyUsed, &QCheckBox::toggled, this, &CMaterialBrowser::ApplyFilter );
	if ( m_pOnlySprites )
		connect( m_pOnlySprites, &QCheckBox::toggled, this, &CMaterialBrowser::ApplyFilter );
	connect( m_pSize, &QComboBox::currentIndexChanged, this, [ this ] {
		const int nSize = m_pSize->currentData().toInt();
		m_pView->setIconSize( QSize( nSize, nSize ) );
		m_pList->SetSize( nSize );
		QSettings{}.setValue( QLatin1String( SIZE_SETTING ), nSize );
	} );
	connect( m_pOnlyUsed, &QCheckBox::toggled, this, []( bool bOn ) { QSettings{}.setValue( QLatin1String( ONLY_USED_SETTING ), bOn ); } );
	// The browser opens as it was last left.
	if ( const int nIndex = m_pSize->findData( QSettings{}.value( QLatin1String( SIZE_SETTING ), 64 ).toInt() ); nIndex >= 0 )
		m_pSize->setCurrentIndex( nIndex );
	m_pOnlyUsed->setChecked( QSettings{}.value( QLatin1String( ONLY_USED_SETTING ), false ).toBool() );
	connect( m_pView, &QListView::doubleClicked, this, [ this ] {
		PickCurrent();
		accept();
	} );
	// A material's right-click menu: use it, or copy its name or its file's path.
	m_pView->setContextMenuPolicy( Qt::CustomContextMenu );
	connect( m_pView, &QListView::customContextMenuRequested, this, [ this ]( const QPoint &at ) {
		const QModelIndex index = m_pView->indexAt( at );
		if ( !index.isValid() )
			return;
		m_pView->setCurrentIndex( index );
		const QString szName = index.data( CMaterialList::ROLE_NAME ).toString();
		QMenu menu( this );
		menu.setObjectName( QStringLiteral( "materialMenu" ) );
		menu.addAction( tr( "Use" ), this, [ this ] {
			PickCurrent();
			accept();
		} );
		menu.addAction( tr( "Copy Name" ), this, [ szName ] { QGuiApplication::clipboard()->setText( szName ); } )->setObjectName( QStringLiteral( "materialCopyName" ) );
		menu.addAction( tr( "Copy Path" ), this, [ szName ] { QGuiApplication::clipboard()->setText( QStringLiteral( "materials/%1.vmt" ).arg( szName.toLower() ) ); } );
		menu.exec( m_pView->viewport()->mapToGlobal( at ) );
	} );
	connect( pButtons, &QDialogButtonBox::accepted, this, [ this ] {
		PickCurrent();
		accept();
	} );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
	m_Worker = std::thread( [ this ] { Run(); } );
}

//-----------------------------------------------------------------------------
// What the preview pane read of a material: what to draw it with.
//-----------------------------------------------------------------------------
struct CMaterialPreview::Loaded_t
{
	QString m_szName;
	std::optional<xv::material::CMaterial> m_Material;
	xv::material::CTextureSet m_Textures;
	std::unique_ptr<xv::material::CProxies> m_pProxies;
	QString m_szProblem;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMaterialPreview::CMaterialPreview( const xv::vfs::CGameInfo &game, QWidget *pParent )
	: QWidget( pParent ),
	  m_Game( game )
{
	setObjectName( QStringLiteral( "materialPreview" ) );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	m_pPicture = new QLabel;
	m_pPicture->setObjectName( QStringLiteral( "materialPreviewPicture" ) );
	m_pPicture->setAlignment( Qt::AlignCenter );
	m_pPicture->setMinimumSize( PREVIEW_SIZE, PREVIEW_SIZE );
	m_pName = new QLabel;
	m_pName->setObjectName( QStringLiteral( "materialPreviewName" ) );
	m_pName->setWordWrap( true );
	m_pName->setTextInteractionFlags( Qt::TextSelectableByMouse );
	m_pAbout = new QLabel;
	m_pAbout->setObjectName( QStringLiteral( "materialPreviewAbout" ) );
	m_pAbout->setWordWrap( true );
	m_pShape = new QComboBox;
	m_pShape->setObjectName( QStringLiteral( "materialPreviewShape" ) );
	m_pShape->addItems( { tr( "Flat" ), tr( "Ball" ) } );
	m_pShape->setToolTip( tr( "Show the material on a flat square or a ball" ) );
	m_pShape->setCurrentIndex( QSettings{}.value( QLatin1String( PREVIEW_SHAPE_SETTING ), 0 ).toInt() == 1 ? 1 : 0 );
	pLayout->addWidget( m_pPicture );
	pLayout->addWidget( m_pName );
	pLayout->addWidget( m_pAbout );
	pLayout->addWidget( m_pShape );
	pLayout->addStretch();
	connect( m_pShape, &QComboBox::currentIndexChanged, this, [ this ]( int nShape ) {
		QSettings{}.setValue( QLatin1String( PREVIEW_SHAPE_SETTING ), nShape );
		Render();
	} );
	m_pWait = new QTimer( this );
	m_pWait->setSingleShot( true );
	m_pWait->setInterval( PREVIEW_WAIT_MS );
	connect( m_pWait, &QTimer::timeout, this, &CMaterialPreview::Load );
	m_pAnimate = new QTimer( this );
	m_pAnimate->setInterval( PREVIEW_FRAME_MS );
	connect( m_pAnimate, &QTimer::timeout, this, &CMaterialPreview::Render );
	m_Clock.start();
	m_Worker = std::thread( [ this ] {
		std::unique_ptr<xv::vfs::CFileSystem> pFileSystem;
		for ( ;; )
		{
			QString szName;
			int nGeneration = 0;
			{
				std::unique_lock lock( m_Mutex );
				m_Wake.wait( lock, [ this ] { return m_bStop || !m_szQueued.isEmpty(); } );
				if ( m_bStop )
					return;
				szName = std::exchange( m_szQueued, QString{} );
				nGeneration = m_nGeneration;
			}
			if ( !pFileSystem )
			{
				pFileSystem = std::make_unique<xv::vfs::CFileSystem>();
				pFileSystem->Mount( m_Game );
			}
			const xv::material::Reader_t read = [ &fileSystem = *pFileSystem ]( std::string_view szPath ) { return fileSystem.Read( szPath ); };
			auto pLoaded = std::make_shared<Loaded_t>();
			pLoaded->m_szName = szName;
			const std::string szPath = "materials/" + szName.toLower().toStdString() + ".vmt";
			if ( const auto bytes = read( szPath ) )
			{
				std::string szError;
				pLoaded->m_Material = xv::material::ParseMaterial( std::string_view{ reinterpret_cast<const char *>( bytes->data() ), bytes->size() }, read, {}, &szError );
				if ( pLoaded->m_Material )
				{
					pLoaded->m_Textures = xv::material::CTextureSet::Load( *pLoaded->m_Material, read, PREVIEW_TEXTURE_SIZE );
					pLoaded->m_pProxies = std::make_unique<xv::material::CProxies>( *pLoaded->m_Material );
				}
				else
				{
					pLoaded->m_szProblem = QString::fromStdString( szError );
				}
			}
			else
			{
				pLoaded->m_szProblem = tr( "%1 isn't in the game's files" ).arg( QString::fromStdString( szPath ) );
			}
			QMetaObject::invokeMethod( this, [ this, nGeneration, pLoaded = std::shared_ptr<const Loaded_t>( std::move( pLoaded ) ) ] {
				if ( nGeneration != m_nGeneration )
					return;
				m_pLoaded = pLoaded;
				m_szShown = pLoaded->m_szName;
				m_Clock.restart();
				if ( IsAnimated() )
					m_pAnimate->start();
				else
					m_pAnimate->stop();
				Render();
			} );
		}
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMaterialPreview::~CMaterialPreview()
{
	{
		const std::lock_guard lock( m_Mutex );
		m_bStop = true;
	}
	m_Wake.notify_all();
	if ( m_Worker.joinable() )
		m_Worker.join();
}

//-----------------------------------------------------------------------------
// Purpose: Read once the pointer has rested on it a moment, so sweeping across the grid
//          doesn't read every material on the way.
//-----------------------------------------------------------------------------
void CMaterialPreview::Show( const QString &szName )
{
	if ( szName == m_szWanted )
		return;
	m_szWanted = szName;
	m_nGeneration++;
	if ( szName.isEmpty() )
	{
		m_pWait->stop();
		m_pAnimate->stop();
		m_pLoaded.reset();
		m_szShown.clear();
		Render();
		return;
	}
	m_pName->setText( szName );
	m_pWait->start();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialPreview::Load()
{
	{
		const std::lock_guard lock( m_Mutex );
		m_szQueued = m_szWanted;
	}
	m_Wake.notify_one();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMaterialPreview::IsAnimated() const
{
	return m_pLoaded && m_pLoaded->m_pProxies && m_pLoaded->m_pProxies->IsAnimated();
}

//-----------------------------------------------------------------------------
// Purpose: The material as at now for its proxies, with what it is under it.
//-----------------------------------------------------------------------------
void CMaterialPreview::Render()
{
	if ( !m_pLoaded )
	{
		m_Picture = {};
		m_pPicture->clear();
		m_pName->setText( m_szWanted );
		m_pAbout->clear();
		return;
	}
	const Loaded_t &loaded = *m_pLoaded;
	m_pName->setText( loaded.m_szName );
	if ( !loaded.m_Material )
	{
		m_Picture = {};
		m_pPicture->clear();
		m_pAbout->setText( loaded.m_szProblem );
		return;
	}
	const float flTime = static_cast<float>( m_Clock.elapsed() ) / 1000.0f;
	const xv::material::CMaterial now = loaded.m_pProxies->At( flTime, [ &loaded ]( std::string_view szParameter ) { return loaded.m_Textures.FrameCount( szParameter ); } );
	xv::material::PreviewOptions_t options;
	options.m_nSize = PREVIEW_SIZE;
	options.m_nShape = m_pShape->currentIndex() == 1 ? xv::material::Shape_t::SHAPE_SPHERE : xv::material::Shape_t::SHAPE_FLAT;
	const xv::texture::CImage image = xv::material::RenderPreview( now, loaded.m_Textures, options );
	m_Picture = QImage( image.m_Pixels.data(), image.m_nWidth, image.m_nHeight, image.m_nWidth * 4, QImage::Format_RGBA8888 ).copy();
	m_pPicture->setPixmap( QPixmap::fromImage( m_Picture ) );
	QString szAbout = QString::fromStdString( loaded.m_Material->m_szShader );
	if ( const xv::material::Texture_t *pBase = loaded.m_Textures.Find( "$basetexture" ); pBase && pBase->m_Info )
		szAbout += QStringLiteral( " · %1×%2" ).arg( pBase->m_Info->m_nWidth ).arg( pBase->m_Info->m_nHeight );
	if ( IsAnimated() )
		szAbout += tr( " · animated" );
	m_pAbout->setText( szAbout );
}

//-----------------------------------------------------------------------------
// Purpose: Off the grid, the preview goes back to the material picked.
//-----------------------------------------------------------------------------
bool CMaterialBrowser::eventFilter( QObject *pObject, QEvent *pEvent )
{
	if ( pObject == m_pView->viewport() && pEvent->type() == QEvent::Leave )
	{
		const QModelIndex current = m_pView->currentIndex();
		m_pPreview->Show( current.isValid() ? current.data( CMaterialList::ROLE_NAME ).toString() : m_szPicked.toUpper() );
	}
	return QDialog::eventFilter( pObject, pEvent );
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
// Purpose:
//-----------------------------------------------------------------------------
std::optional<QString> CMaterialBrowser::PickFromList( QWidget *pParent, const xv::vfs::CGameInfo &game, const QStringList &used, const QString &szCurrent )
{
	CMaterialBrowser browser( game, used, szCurrent, pParent );
	browser.ShowAsList();
	if ( browser.exec() != QDialog::Accepted || browser.GetPicked().isEmpty() )
		return std::nullopt;
	return browser.GetPicked();
}

//-----------------------------------------------------------------------------
// Purpose: The view as a list: small pictures beside the names, a line each. The pictures are
//          still the 128 px ones, scaled down.
//-----------------------------------------------------------------------------
void CMaterialBrowser::ShowAsList()
{
	constexpr int LIST_ICON = 24;
	setWindowTitle( tr( "Materials" ) );
	resize( 860, 600 );
	m_pSize->hide();
	m_pList->SetAsList( true );
	m_pList->SetSize( LIST_ICON );
	m_pView->setViewMode( QListView::ListMode );
	m_pView->setFlow( QListView::TopToBottom );
	m_pView->setWrapping( false );
	m_pView->setWordWrap( false );
	m_pView->setSpacing( 0 );
	m_pView->setAlternatingRowColors( true );
	m_pView->setIconSize( QSize( LIST_ICON, LIST_ICON ) );
	m_pView->setTextElideMode( Qt::ElideMiddle );
}

//-----------------------------------------------------------------------------
// Purpose: Sprite keys name the material's file below materials/, with its extension; an old
//          .spr name finds the material of the same name.
//-----------------------------------------------------------------------------
std::optional<QString> CMaterialBrowser::PickSprite( QWidget *pParent, const xv::vfs::CGameInfo &game, const QStringList &used, const QString &szCurrent )
{
	QString szStart = szCurrent;
	if ( szStart.endsWith( QStringLiteral( ".vmt" ), Qt::CaseInsensitive ) || szStart.endsWith( QStringLiteral( ".spr" ), Qt::CaseInsensitive ) )
		szStart.chop( 4 );
	if ( szStart.startsWith( QStringLiteral( "materials/" ), Qt::CaseInsensitive ) )
		szStart.remove( 0, 10 );
	CMaterialBrowser browser( game, used, szStart, pParent, true );
	if ( browser.exec() != QDialog::Accepted || browser.GetPicked().isEmpty() )
		return std::nullopt;
	return browser.GetPicked().toLower() + QStringLiteral( ".vmt" );
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
	// For a sprite key: which are sprites, from each VMT's shader (its first key).
	std::vector<bool> sprites;
	if ( m_bSprites )
	{
		sprites.reserve( paths.size() );
		for ( const std::string &szPath : paths )
		{
			if ( m_bStop )
				return;
			bool bSprite = szPath.size() > 18 && xv::kv1::EqualsIgnoreCase( std::string_view{ szPath }.substr( 0, 18 ), "materials/sprites/" );
			if ( !bSprite )
			{
				const std::optional<std::string> vmt = fileSystem.ReadText( szPath );
				const std::string_view szShader = vmt ? ShaderOf( *vmt ) : std::string_view{};
				bSprite = xv::kv1::EqualsIgnoreCase( szShader, "Sprite" ) || xv::kv1::EqualsIgnoreCase( szShader, "SpriteCard" );
			}
			sprites.push_back( bSprite );
		}
	}
	QMetaObject::invokeMethod( this, [ this, names, sprites = std::move( sprites ) ]() mutable {
		m_pList->SetNames( names );
		m_pList->SetSprites( std::move( sprites ) );
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
	pFilter->m_bOnlySprites = m_pOnlySprites && m_pOnlySprites->isChecked();
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
