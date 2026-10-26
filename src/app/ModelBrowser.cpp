//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See ModelBrowser.h.
//
//===========================================================================//
#include "ModelBrowser.h"

#include <cstring>
#include <map>
#include <memory>

#include <QAbstractListModel>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QSplitter>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include "ImageFiles.h"
#include "model/Edit.h"
#include "model/Mdl.h"
#include "model/Model.h"
#include "particles/Render.h"

namespace
{

// Pictures are drawn at the largest size the slider offers and shrunk to the size picked: icons
// are scaled down, never up.
constexpr int PICTURE_SIZE = 256;
// Bumped when pictures are drawn differently, so old ones are drawn again.
constexpr int PICTURE_VERSION = 2;
// Headers go to the window this many at a time.
constexpr std::size_t HEADER_BATCH = 64;
// Materials kept loaded while drawing; props of a folder share most of theirs.
constexpr std::size_t SPRITE_CACHE = 256;

//-----------------------------------------------------------------------------
// Purpose: Where a model's picture is kept: by its path and checksum, so a recompiled model is
//          drawn again. ".none" marks a model that has nothing to draw.
//-----------------------------------------------------------------------------
QString PicturePath( const QString &szModel, int nChecksum, bool bNone )
{
	const QByteArray hash = QCryptographicHash::hash( szModel.toLower().toUtf8(), QCryptographicHash::Md5 ).toHex();
	return QStringLiteral( "%1/%2_%3%4" ).arg( CModelBrowser::CacheFolder(), QString::fromLatin1( hash ) ).arg( static_cast<quint32>( nChecksum ), 8, 16, QChar( u'0' ) ).arg( bNone ? QStringLiteral( ".none" ) : QStringLiteral( ".png" ) );
}

//-----------------------------------------------------------------------------
// Purpose: The count of the .mdl's own textures (header @204); 0 when it can't be read.
//-----------------------------------------------------------------------------
int TextureCount( const std::vector<std::byte> &mdl )
{
	std::int32_t nCount = 0;
	if ( mdl.size() >= 208 )
		std::memcpy( &nCount, mdl.data() + 204, 4 );
	return nCount > 0 && nCount < 4096 ? nCount : 0;
}

//-----------------------------------------------------------------------------
// Purpose: The materials a model draws with, loaded once and shared between models.
//-----------------------------------------------------------------------------
class CSpriteCache
{
public:
	const xv::particles::Sprite_t *Get( const std::string &szMaterial, const xv::particles::FileReader_t &pfnRead )
	{
		if ( const auto found = m_Sprites.find( szMaterial ); found != m_Sprites.end() )
			return found->second.get();
		auto pSprite = std::make_shared<xv::particles::Sprite_t>( xv::particles::LoadSprite( szMaterial, pfnRead ) );
		return m_Sprites.emplace( szMaterial, std::move( pSprite ) ).first->second.get();
	}

	// Between models only: a model being drawn holds pointers to its sprites.
	void Trim()
	{
		if ( m_Sprites.size() >= SPRITE_CACHE )
			m_Sprites.clear();
	}

private:
	std::map<std::string, std::shared_ptr<xv::particles::Sprite_t>> m_Sprites;
};

//-----------------------------------------------------------------------------
// Purpose: The model in its default body and skin, from the side the viewer starts at, filling
//          the picture. Null when it has no mesh to draw.
//-----------------------------------------------------------------------------
QImage DrawModel( const std::string &szPath, const xv::model::Reader_t &pfnRead, CSpriteCache &sprites, int &nTriangles, std::string &szProblem )
{
	auto model = xv::model::LoadModel( szPath, pfnRead, &szProblem );
	if ( !model || model->m_Vertices.empty() )
	{
		if ( szProblem.empty() )
			szProblem = "no mesh";
		return {};
	}
	nTriangles = model->GetTriangleCount();
	sprites.Trim();
	std::vector<const xv::particles::Sprite_t *> materials;
	for ( const std::string &szMaterial : model->m_Materials )
		materials.push_back( sprites.Get( szMaterial, pfnRead ) );

	xv::particles::CCamera camera;
	camera.m_nWidth = camera.m_nHeight = PICTURE_SIZE;
	camera.m_vecTarget = ( model->m_vecMin + model->m_vecMax ) * 0.5f;
	camera.m_flYaw = 135;
	camera.m_flPitch = 15;
	// A sphere around the bounds just fits the 60 degree view at twice its radius.
	camera.m_flDistance = std::max( 10.0f, Length( model->m_vecMax - model->m_vecMin ) );
	const auto meshes = xv::particles::LayoutMesh( *model, {}, camera, [ &materials ]( std::size_t i ) { return i < materials.size() ? materials[ i ] : nullptr; } );
	xv::texture::CImage image;
	xv::particles::RenderScene( camera, meshes, {}, image, { .m_bGrid = false, .m_pfnModelSprites = {} } );
	return ToQImage( image );
}

//-----------------------------------------------------------------------------
// Purpose: The words of a search, lower case.
//-----------------------------------------------------------------------------
QStringList Words( const QString &szText )
{
	return szText.toLower().split( u' ', Qt::SkipEmptyParts );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The models, with a picture each once it is drawn.
//-----------------------------------------------------------------------------
class CModelList : public QAbstractListModel
{
public:
	using QAbstractListModel::QAbstractListModel;

	enum Role_t
	{
		ROLE_PATH = Qt::UserRole + 1
	};

	[[nodiscard]] int rowCount( const QModelIndex &parent = {} ) const override { return parent.isValid() ? 0 : static_cast<int>( m_Models.size() ); }

	[[nodiscard]] QVariant data( const QModelIndex &index, int nRole ) const override
	{
		if ( !index.isValid() || index.row() >= rowCount() )
			return {};
		const BrowsedModel_t &model = m_Models[ static_cast<std::size_t>( index.row() ) ];
		switch ( nRole )
		{
			case Qt::DisplayRole:
				return QFileInfo( model.m_szPath ).completeBaseName();
			case Qt::DecorationRole:
				// An icon, not a pixmap: the view scales icons to its icon size, pixmaps it only crops.
				return m_Icons[ static_cast<std::size_t>( index.row() ) ].isNull() ? Placeholder() : m_Icons[ static_cast<std::size_t>( index.row() ) ];
			case Qt::ToolTipRole:
				return ToolTip( model );
			case ROLE_PATH:
				return model.m_szPath;
			default:
				return {};
		}
	}

	void SetPaths( const QStringList &paths )
	{
		beginResetModel();
		m_Models.assign( static_cast<std::size_t>( paths.size() ), {} );
		m_Icons.assign( m_Models.size(), {} );
		for ( qsizetype i = 0; i < paths.size(); i++ )
			m_Models[ static_cast<std::size_t>( i ) ].m_szPath = paths[ i ];
		endResetModel();
	}

	void SetHeaders( int nFirst, std::vector<BrowsedModel_t> headers )
	{
		for ( std::size_t i = 0; i < headers.size(); i++ )
		{
			const std::size_t nRow = static_cast<std::size_t>( nFirst ) + i;
			if ( nRow >= m_Models.size() )
				break;
			headers[ i ].m_szPath = m_Models[ nRow ].m_szPath;
			m_Models[ nRow ] = std::move( headers[ i ] );
		}
		emit dataChanged( index( nFirst ), index( std::min( rowCount(), nFirst + static_cast<int>( headers.size() ) ) - 1 ) );
	}

	void SetPicture( int nRow, const QImage &picture, int nTriangles, const QString &szProblem )
	{
		if ( nRow < 0 || nRow >= rowCount() )
			return;
		BrowsedModel_t &model = m_Models[ static_cast<std::size_t>( nRow ) ];
		model.m_bPictured = true;
		model.m_Picture = picture;
		model.m_nTriangles = nTriangles;
		model.m_szProblem = szProblem;
		m_Icons[ static_cast<std::size_t>( nRow ) ] = picture.isNull() ? QIcon{} : QIcon( QPixmap::fromImage( picture ) );
		emit dataChanged( index( nRow ), index( nRow ) );
	}

	[[nodiscard]] const std::vector<BrowsedModel_t> &GetModels() const { return m_Models; }

private:
	// A plain square while the picture isn't drawn, or when there is nothing to draw.
	static QIcon Placeholder()
	{
		static const QIcon placeholder = [] {
			QPixmap pixmap( PICTURE_SIZE, PICTURE_SIZE );
			pixmap.fill( QColor( 33, 36, 38 ) );
			return QIcon( pixmap );
		}();
		return placeholder;
	}

	static QString ToolTip( const BrowsedModel_t &model )
	{
		QStringList lines{ model.m_szPath };
		if ( model.m_bRead )
		{
			if ( !model.m_szSurface.isEmpty() )
				lines << CModelBrowser::tr( "Surface property: %1" ).arg( model.m_szSurface );
			if ( !model.m_Textures.isEmpty() )
				lines << CModelBrowser::tr( "Materials: %1" ).arg( model.m_Textures.join( QStringLiteral( ", " ) ) );
			if ( !model.m_Folders.isEmpty() )
				lines << CModelBrowser::tr( "Material folders: %1" ).arg( model.m_Folders.join( QStringLiteral( ", " ) ) );
			lines << CModelBrowser::tr( "%n bone(s)", nullptr, model.m_nBones ) + QStringLiteral( " · " ) + CModelBrowser::tr( "%n sequence(s)", nullptr, model.m_nSequences );
		}
		if ( model.m_nTriangles >= 0 )
			lines << CModelBrowser::tr( "%n triangle(s)", nullptr, model.m_nTriangles );
		if ( !model.m_szProblem.isEmpty() )
			lines << CModelBrowser::tr( "Not drawn: %1" ).arg( model.m_szProblem );
		return lines.join( u'\n' );
	}

	std::vector<BrowsedModel_t> m_Models;
	std::vector<QIcon> m_Icons;
};

//-----------------------------------------------------------------------------
// Purpose: The models that match the search: every word somewhere in the fields looked in.
//-----------------------------------------------------------------------------
class CModelFilter : public QSortFilterProxyModel
{
public:
	using QSortFilterProxyModel::QSortFilterProxyModel;

	void SetSearch( const QString &szText, CModelBrowser::Search_t nWhere, bool bWithoutMesh )
	{
		m_Words = Words( szText );
		m_nWhere = nWhere;
		m_bWithoutMesh = bWithoutMesh;
		Refilter();
	}

	// Rows are looked at again: their headers or pictures came in.
	void Refilter()
	{
#if QT_VERSION >= QT_VERSION_CHECK( 6, 10, 0 )
		beginFilterChange();
		endFilterChange( QSortFilterProxyModel::Direction::Rows );
#else
		invalidateRowsFilter(); // CI builds on Qt 6.8, before begin/endFilterChange
#endif
	}

protected:
	[[nodiscard]] bool filterAcceptsRow( int nRow, const QModelIndex & ) const override
	{
		const auto &models = static_cast<const CModelList *>( sourceModel() )->GetModels();
		if ( nRow < 0 || nRow >= static_cast<int>( models.size() ) )
			return false;
		const BrowsedModel_t &model = models[ static_cast<std::size_t>( nRow ) ];
		// Until its header is read, a model counts as having a mesh.
		if ( !m_bWithoutMesh && model.m_bRead && ( !model.m_bHasMesh || ( model.m_bPictured && model.m_Picture.isNull() ) ) )
			return false;
		using Search_t = CModelBrowser::Search_t;
		const bool bName = m_nWhere == Search_t::SEARCH_ANYTHING || m_nWhere == Search_t::SEARCH_NAME;
		const bool bMaterial = m_nWhere == Search_t::SEARCH_ANYTHING || m_nWhere == Search_t::SEARCH_MATERIAL;
		const bool bSurface = m_nWhere == Search_t::SEARCH_ANYTHING || m_nWhere == Search_t::SEARCH_SURFACE;
		for ( const QString &szWord : m_Words )
		{
			bool bFound = bName && model.m_szPath.contains( szWord, Qt::CaseInsensitive );
			bFound = bFound || ( bSurface && model.m_szSurface.contains( szWord, Qt::CaseInsensitive ) );
			if ( !bFound && bMaterial )
			{
				for ( const QString &szTexture : model.m_Textures )
				{
					bFound = szTexture.contains( szWord, Qt::CaseInsensitive );
					// "props_c17/oildrum001" names a material with its folder.
					for ( qsizetype i = 0; !bFound && i < model.m_Folders.size(); i++ )
						bFound = ( model.m_Folders[ i ] + szTexture ).contains( szWord, Qt::CaseInsensitive );
					if ( bFound )
						break;
				}
			}
			if ( !bFound )
				return false;
		}
		return true;
	}

private:
	QStringList m_Words;
	CModelBrowser::Search_t m_nWhere = CModelBrowser::Search_t::SEARCH_ANYTHING;
	bool m_bWithoutMesh = false;
};

//-----------------------------------------------------------------------------
// Purpose: The search line, the grid, and the reading started.
//-----------------------------------------------------------------------------
CModelBrowser::CModelBrowser( const xv::vfs::CGameInfo &game, QWidget *pParent )
	: QWidget( pParent ),
	  m_pList( new CModelList( this ) ),
	  m_pFilter( new CModelFilter( this ) )
{
	m_pFilter->setSourceModel( m_pList );

	m_pSearch = new QLineEdit;
	m_pSearch->setObjectName( QStringLiteral( "modelBrowserSearch" ) );
	m_pSearch->setPlaceholderText( tr( "Search models" ) );
	m_pSearch->setClearButtonEnabled( true );
	m_pWhere = new QComboBox;
	m_pWhere->addItem( tr( "Anything" ), static_cast<int>( Search_t::SEARCH_ANYTHING ) );
	m_pWhere->addItem( tr( "Name" ), static_cast<int>( Search_t::SEARCH_NAME ) );
	m_pWhere->addItem( tr( "Material" ), static_cast<int>( Search_t::SEARCH_MATERIAL ) );
	m_pWhere->addItem( tr( "Surface property" ), static_cast<int>( Search_t::SEARCH_SURFACE ) );
	m_pWithoutMesh = new QCheckBox( tr( "Models without a mesh" ) );
	m_pWithoutMesh->setToolTip( tr( "Also shows animation-only models and ones that couldn't be drawn" ) );
	m_pSize = new QSlider( Qt::Horizontal );
	m_pSize->setRange( 64, PICTURE_SIZE );
	m_pSize->setValue( 128 );
	m_pSize->setMaximumWidth( 140 );
	m_pSize->setToolTip( tr( "Picture size" ) );
	m_pCount = new QLabel;
	m_pCount->setObjectName( QStringLiteral( "modelBrowserCount" ) );

	m_pView = new QListView;
	m_pView->setObjectName( QStringLiteral( "modelBrowserView" ) );
	m_pView->setModel( m_pFilter );
	m_pView->setViewMode( QListView::IconMode );
	m_pView->setResizeMode( QListView::Adjust );
	m_pView->setMovement( QListView::Static );
	m_pView->setUniformItemSizes( true );
	m_pView->setSpacing( 4 );
	m_pView->setWordWrap( true );
	m_pView->setTextElideMode( Qt::ElideMiddle );
	m_pView->setSelectionMode( QAbstractItemView::SingleSelection );
	m_pView->setContextMenuPolicy( Qt::CustomContextMenu );
	const auto resize = [ this ] {
		const int nSize = m_pSize->value();
		m_pView->setIconSize( QSize( nSize, nSize ) );
		m_pView->setGridSize( QSize( nSize + 24, nSize + 3 * fontMetrics().height() + 12 ) );
	};
	resize();

	auto *pBar = new QHBoxLayout;
	pBar->addWidget( m_pSearch, 1 );
	pBar->addWidget( new QLabel( tr( "in" ) ) );
	pBar->addWidget( m_pWhere );
	pBar->addWidget( m_pWithoutMesh );
	pBar->addWidget( m_pSize );
	auto *pLayout = new QVBoxLayout( this );
	pLayout->addLayout( pBar );
	m_Game = game;
	m_pDetails = new CModelDetails;
	m_pDetails->SetGame( game );
	m_pSplitter = new QSplitter;
	m_pSplitter->setObjectName( QStringLiteral( "modelBrowserSplitter" ) );
	m_pSplitter->addWidget( m_pView );
	m_pSplitter->addWidget( m_pDetails );
	m_pSplitter->setStretchFactor( 0, 1 );
	m_pSplitter->setStretchFactor( 1, 0 );
	m_pSplitter->setSizes( { 800, 340 } );
	const auto state = QSettings{}.value( QStringLiteral( "modelBrowser/detailsSplitter" ) ).toByteArray();
	if ( !state.isEmpty() )
		m_pSplitter->restoreState( state );
	pLayout->addWidget( m_pSplitter, 1 );
	connect( m_pView->selectionModel(), &QItemSelectionModel::currentChanged, this, [ this ]( const QModelIndex &index ) { m_pDetails->SelectModel( index.data( CModelList::ROLE_PATH ).toString() ); } );
	connect( m_pDetails, &CModelDetails::OpenModelRequested, this, &CModelBrowser::OpenModelRequested );
	connect( m_pDetails, &CModelDetails::OpenMaterialRequested, this, &CModelBrowser::OpenMaterialRequested );
	connect( m_pDetails, &CModelDetails::ShowInGameBrowser, this, &CModelBrowser::ShowInGameBrowser );
	pLayout->addWidget( m_pCount );

	// What's on screen is drawn first; asked for a moment after scrolling stops.
	auto *pVisible = new QTimer( this );
	pVisible->setSingleShot( true );
	pVisible->setInterval( 60 );
	connect( pVisible, &QTimer::timeout, this, &CModelBrowser::AskForVisible );
	const auto askSoon = [ pVisible ] { pVisible->start(); };
	connect( m_pView->verticalScrollBar(), &QScrollBar::valueChanged, this, askSoon );
	connect( m_pFilter, &QAbstractItemModel::layoutChanged, this, askSoon );
	connect( m_pFilter, &QAbstractItemModel::modelReset, this, askSoon );
	connect( m_pFilter, &QAbstractItemModel::rowsInserted, this, askSoon );
	connect( m_pFilter, &QAbstractItemModel::rowsRemoved, this, askSoon );

	const auto search = [ this ] { SetSearch( m_pSearch->text(), static_cast<Search_t>( m_pWhere->currentData().toInt() ) ); };
	connect( m_pSearch, &QLineEdit::textChanged, this, search );
	connect( m_pWhere, &QComboBox::currentIndexChanged, this, search );
	connect( m_pWithoutMesh, &QCheckBox::toggled, this, search );
	connect( m_pSize, &QSlider::valueChanged, this, [ resize, askSoon ] {
		resize();
		askSoon();
	} );
	connect( m_pView, &QListView::activated, this, [ this ]( const QModelIndex &index ) { emit OpenModelRequested( index.data( CModelList::ROLE_PATH ).toString() ); } );
	connect( m_pView, &QListView::customContextMenuRequested, this, [ this ]( const QPoint &point ) {
		const QModelIndex index = m_pView->indexAt( point );
		if ( !index.isValid() )
			return;
		const QString szPath = index.data( CModelList::ROLE_PATH ).toString();
		QMenu menu;
		menu.addAction( tr( "Open in Model Viewer" ), this, [ this, szPath ] { emit OpenModelRequested( szPath ); } );
		menu.addAction( tr( "Copy Path" ), this, [ szPath ] { QApplication::clipboard()->setText( szPath ); } );
		menu.exec( m_pView->viewport()->mapToGlobal( point ) );
	} );
	connect( this, &CModelBrowser::Progress, this, &CModelBrowser::UpdateCount );

	Start( game );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CModelBrowser::~CModelBrowser()
{
	QSettings{}.setValue( QStringLiteral( "modelBrowser/detailsSplitter" ), m_pSplitter->saveState() );
	Stop();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CModelBrowser::SetGame( const xv::vfs::CGameInfo &game )
{
	m_Game = game;
	m_pDetails->SetGame( game, m_pfnDetailsRead, m_pfnWhere );
	Stop();
	Start( game );
}

//-----------------------------------------------------------------------------
// Purpose: Applied straight away; the boxes show what was asked for.
//-----------------------------------------------------------------------------
void CModelBrowser::SetSearch( const QString &szText, Search_t nWhere )
{
	if ( m_pSearch->text() != szText )
	{
		const QSignalBlocker blocker( m_pSearch );
		m_pSearch->setText( szText );
	}
	if ( const int nIndex = m_pWhere->findData( static_cast<int>( nWhere ) ); nIndex != m_pWhere->currentIndex() )
	{
		const QSignalBlocker blocker( m_pWhere );
		m_pWhere->setCurrentIndex( nIndex );
	}
	m_pFilter->SetSearch( szText, nWhere, m_pWithoutMesh->isChecked() );
	UpdateCount();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const std::vector<BrowsedModel_t> &CModelBrowser::GetModels() const
{
	return m_pList->GetModels();
}

QStringList CModelBrowser::GetShown() const
{
	QStringList shown;
	for ( int i = 0; i < m_pFilter->rowCount(); i++ )
		shown << m_pFilter->index( i, 0 ).data( CModelList::ROLE_PATH ).toString();
	return shown;
}

bool CModelBrowser::IsFinished() const
{
	return m_bFinished;
}

QString CModelBrowser::CacheFolder()
{
	return QStandardPaths::writableLocation( QStandardPaths::CacheLocation ) + QStringLiteral( "/model-pictures/v%1" ).arg( PICTURE_VERSION );
}

//-----------------------------------------------------------------------------
// Purpose: Each start gets a number; anything an earlier thread still had on its way is dropped.
//-----------------------------------------------------------------------------
void CModelBrowser::Start( const xv::vfs::CGameInfo &game )
{
	m_nGeneration++;
	m_pList->SetPaths( {} );
	m_bStop = false;
	m_bFinished = false;
	{
		const std::lock_guard lock( m_Mutex );
		m_Wanted.clear();
	}
	UpdateCount();
	m_Worker = std::thread( [ this, game ] { Run( game ); } );
}

void CModelBrowser::Stop()
{
	m_bStop = true;
	if ( m_Worker.joinable() )
		m_Worker.join();
}

//-----------------------------------------------------------------------------
// Purpose: The thread: mounts the game, lists its models, reads every header, then draws.
//          Everything for the window goes to it as a queued call tagged with this start's number.
//-----------------------------------------------------------------------------
void CModelBrowser::Run( xv::vfs::CGameInfo game )
{
	const int nGeneration = m_nGeneration;
	const auto post = [ this, nGeneration ]( auto pfnCall ) {
		QMetaObject::invokeMethod( this, [ this, nGeneration, pfnCall = std::move( pfnCall ) ]() mutable {
			if ( nGeneration == m_nGeneration )
				pfnCall(); }, Qt::QueuedConnection );
	};

	xv::vfs::CFileSystem fileSystem;
	fileSystem.Mount( game );
	const xv::model::Reader_t read = [ &fileSystem ]( std::string_view szPath ) { return fileSystem.Read( szPath ); };
	const std::vector<std::string> paths = fileSystem.Find( "models", ".mdl", true );
	QStringList names;
	for ( const std::string &szPath : paths )
		names << QString::fromStdString( szPath );
	post( [ this, names ] {
		m_pList->SetPaths( names );
		emit Progress();
	} );

	// Headers first: searching needs them, and they're quick.
	std::vector<int> checksums( paths.size(), 0 );
	std::vector<BrowsedModel_t> batch;
	std::size_t nFirst = 0;
	for ( std::size_t i = 0; i < paths.size() && !m_bStop; i++ )
	{
		BrowsedModel_t header;
		header.m_bRead = true;
		if ( const auto studio = xv::model::ReadStudio( paths[ i ], read ) )
		{
			header.m_szSurface = QString::fromStdString( studio->m_szSurface );
			for ( const std::string &szFolder : studio->m_Folders )
				header.m_Folders << QString::fromStdString( szFolder ).replace( u'\\', u'/' );
			if ( studio->m_pBytes )
				for ( int t = 0, nCount = TextureCount( *studio->m_pBytes ); t < nCount; t++ )
					header.m_Textures << QString::fromStdString( xv::model::GetName( *studio->m_pBytes, xv::model::Name_t::NAME_TEXTURE, t ) ).replace( u'\\', u'/' );
			header.m_nChecksum = checksums[ i ] = studio->m_nChecksum;
			header.m_nBones = static_cast<int>( studio->m_Bones.size() );
			header.m_nSequences = static_cast<int>( studio->m_Sequences.size() );
			header.m_bHasMesh = !studio->m_Bodygroups.empty();
		}
		batch.push_back( std::move( header ) );
		if ( batch.size() == HEADER_BATCH || i + 1 == paths.size() )
		{
			post( [ this, nFirst, headers = std::move( batch ) ]() mutable {
				m_pList->SetHeaders( static_cast<int>( nFirst ), std::move( headers ) );
				m_pFilter->Refilter();
				emit Progress();
			} );
			batch.clear();
			nFirst = i + 1;
		}
	}

	// Then pictures: the rows asked for first, the rest in order.
	QDir().mkpath( CacheFolder() );
	// Pictures from earlier versions are never read again.
	for ( int nOld = 1; nOld < PICTURE_VERSION; nOld++ )
		QDir( QStandardPaths::writableLocation( QStandardPaths::CacheLocation ) + QStringLiteral( "/model-pictures/v%1" ).arg( nOld ) ).removeRecursively();
	CSpriteCache sprites;
	std::vector<bool> drawn( paths.size(), false );
	std::size_t nNext = 0;
	while ( !m_bStop )
	{
		int nRow = -1;
		{
			const std::lock_guard lock( m_Mutex );
			while ( !m_Wanted.empty() && nRow < 0 )
			{
				const int nWanted = m_Wanted.front();
				m_Wanted.pop_front();
				if ( nWanted >= 0 && nWanted < static_cast<int>( paths.size() ) && !drawn[ static_cast<std::size_t>( nWanted ) ] )
					nRow = nWanted;
			}
		}
		while ( nRow < 0 && nNext < paths.size() )
			if ( !drawn[ nNext++ ] )
				nRow = static_cast<int>( nNext - 1 );
		if ( nRow < 0 )
			break;
		drawn[ static_cast<std::size_t>( nRow ) ] = true;

		const QString szPath = names[ nRow ];
		const QString szPicture = PicturePath( szPath, checksums[ static_cast<std::size_t>( nRow ) ], false );
		const QString szNone = PicturePath( szPath, checksums[ static_cast<std::size_t>( nRow ) ], true );
		QImage picture;
		int nTriangles = -1;
		std::string szProblem;
		if ( QFile::exists( szNone ) )
			szProblem = "no mesh";
		else if ( !picture.load( szPicture ) )
		{
			picture = DrawModel( paths[ static_cast<std::size_t>( nRow ) ], read, sprites, nTriangles, szProblem );
			if ( !picture.isNull() )
				picture.save( szPicture );
			else if ( QFile none( szNone ); none.open( QIODevice::WriteOnly ) )
				none.write( szProblem.c_str() );
		}
		post( [ this, nRow, picture, nTriangles, szProblem = QString::fromStdString( szProblem ) ] {
			m_pList->SetPicture( nRow, picture, nTriangles, szProblem );
			if ( picture.isNull() && !m_pWithoutMesh->isChecked() )
				m_pFilter->Refilter();
			emit Progress();
		} );
	}
	if ( !m_bStop )
		post( [ this ] {
			m_bFinished = true;
			emit Progress();
		} );
}

//-----------------------------------------------------------------------------
// Purpose: The rows on screen that have no picture yet go to the front of the thread's queue.
//-----------------------------------------------------------------------------
void CModelBrowser::AskForVisible()
{
	const QRect viewport = m_pView->viewport()->rect();
	std::deque<int> wanted;
	for ( int i = 0; i < m_pFilter->rowCount(); i++ )
	{
		const QModelIndex index = m_pFilter->index( i, 0 );
		if ( !m_pView->visualRect( index ).intersects( viewport ) )
			continue;
		const int nRow = m_pFilter->mapToSource( index ).row();
		if ( !m_pList->GetModels()[ static_cast<std::size_t>( nRow ) ].m_bPictured )
			wanted.push_back( nRow );
	}
	const std::lock_guard lock( m_Mutex );
	m_Wanted = std::move( wanted );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CModelBrowser::UpdateCount()
{
	const auto &models = m_pList->GetModels();
	const auto nRead = std::ranges::count_if( models, []( const BrowsedModel_t &model ) { return model.m_bRead; } );
	const auto nPictured = std::ranges::count_if( models, []( const BrowsedModel_t &model ) { return model.m_bPictured; } );
	QString szText = tr( "%1 of %2 models shown" ).arg( m_pFilter->rowCount() ).arg( models.size() );
	if ( models.empty() && !m_bFinished )
		szText = tr( "Listing models…" );
	else if ( nRead < static_cast<qsizetype>( models.size() ) )
		szText += QStringLiteral( " · " ) + tr( "reading %1 of %2" ).arg( nRead ).arg( models.size() );
	else if ( nPictured < static_cast<qsizetype>( models.size() ) )
		szText += QStringLiteral( " · " ) + tr( "drawing pictures, %1 of %2" ).arg( nPictured ).arg( models.size() );
	m_pCount->setText( szText );
}

//-----------------------------------------------------------------------------
// Purpose: Supplies current game reads and winning mount provenance to the details panel.
//-----------------------------------------------------------------------------
void CModelBrowser::SetDetailsReader( xv::model::Reader_t pfnRead, CModelDetails::Location_t pfnWhere )
{
	m_pfnDetailsRead = std::move( pfnRead );
	m_pfnWhere = std::move( pfnWhere );
	m_pDetails->SetGame( m_Game, m_pfnDetailsRead, m_pfnWhere );
}
