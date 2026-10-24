//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See TextureEditor.h.
//
//===========================================================================//
#include "TextureEditor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QAction>
#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMenu>
#include <QMimeData>
#include <QSaveFile>
#include <QUrl>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "ImageFiles.h"
#include "InterfaceMode.h"
#include "Language.h"
#include "TextureDocument.h"
#include "TextureOptionsDialog.h"
#include "SheetDialog.h"
#include "TexturePropertiesDialog.h"
#include "sky/Sky.h"
#include "texture/Build.h"
#include "texture/Sheet.h"

using namespace xv::texture;

// The picked image, scaled to fit (small ones enlarged, texels kept sharp) or zoomed (the wheel zooms at the pointer, a drag moves it, a
// double-click goes back to fitting), over a checkerboard; tiled 3×3 when asked.
class CTextureCanvas : public QWidget
{
public:
	CTextureCanvas() { setMinimumSize( 200, 200 ); }

	void SetImage( QImage image )
	{
		const bool bSameSize = image.size() == m_Image.size();
		m_Image = std::move( image );
		if ( !bSameSize )
		{
			m_flZoom = 0.0;
			m_Offset = {};
		}
		update();
	}
	void SetTiled( bool bTiled )
	{
		m_bTiled = bTiled;
		update();
	}
	void Fit()
	{
		m_flZoom = 0.0;
		m_Offset = {};
		update();
	}
	void SetZoom( double flZoom )
	{
		m_flZoom = flZoom;
		update();
	}
	// The scale it is drawn at now.
	[[nodiscard]] double GetScale() const
	{
		if ( m_flZoom > 0.0 || m_Image.isNull() )
		{
			return m_flZoom > 0.0 ? m_flZoom : 1.0;
		}
		const QSizeF content = QSizeF( m_Image.size() ) * ( m_bTiled ? 3.0 : 1.0 );
		return std::max( 1.0 / 64.0, std::min( ( width() - 16 ) / content.width(), ( height() - 16 ) / content.height() ) );
	}

protected:
	void paintEvent( QPaintEvent * ) override
	{
		QPainter painter( this );
		painter.fillRect( rect(), palette().color( QPalette::Window ).darker( 115 ) );
		if ( m_Image.isNull() )
		{
			return;
		}
		const double flScale = GetScale();
		const QSizeF tile = QSizeF( m_Image.size() ) * flScale;
		const int nTiles = m_bTiled ? 3 : 1;
		const QPointF origin = QPointF( width() - tile.width() * nTiles, height() - tile.height() * nTiles ) / 2.0 + m_Offset;
		const QRectF all{ origin, tile * nTiles };
		painter.save();
		painter.setClipRect( all );
		for ( int y = 0; y * 8 < all.height(); y++ )
		{
			for ( int x = 0; x * 8 < all.width(); x++ )
			{
				painter.fillRect( QRectF( all.left() + x * 8, all.top() + y * 8, 8, 8 ), ( x + y ) % 2 ? QColor( 200, 200, 200 ) : QColor( 240, 240, 240 ) );
			}
		}
		painter.restore();
		// Enlarged texels stay sharp, so each one can be seen.
		painter.setRenderHint( QPainter::SmoothPixmapTransform, flScale < 1.0 );
		for ( int ty = 0; ty < nTiles; ty++ )
		{
			for ( int tx = 0; tx < nTiles; tx++ )
			{
				painter.drawImage( QRectF( origin + QPointF( tx * tile.width(), ty * tile.height() ), tile ), m_Image );
			}
		}
	}
	void wheelEvent( QWheelEvent *pEvent ) override
	{
		const double flOld = GetScale();
		const double flNew = std::clamp( flOld * ( pEvent->angleDelta().y() > 0 ? 1.25 : 0.8 ), 1.0 / 64.0, 64.0 );
		// Keep the point under the pointer where it is.
		const QPointF centre = QPointF( width(), height() ) / 2.0;
		const QPointF from = pEvent->position() - centre - m_Offset;
		m_Offset += from - from * ( flNew / flOld );
		m_flZoom = flNew;
		update();
		pEvent->accept();
	}
	void mousePressEvent( QMouseEvent *pEvent ) override { m_DragFrom = pEvent->position(); }
	void mouseMoveEvent( QMouseEvent *pEvent ) override
	{
		if ( pEvent->buttons() & Qt::LeftButton )
		{
			m_Offset += pEvent->position() - m_DragFrom;
			m_DragFrom = pEvent->position();
			update();
		}
	}
	void mouseDoubleClickEvent( QMouseEvent * ) override { Fit(); }

private:
	QImage m_Image;
	double m_flZoom = 0.0; // 0: fit
	QPointF m_Offset;
	QPointF m_DragFrom;
	bool m_bTiled = false;
};

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Qs( std::string_view szText )
{
	return QString::fromUtf8( szText.data(), static_cast<qsizetype>( szText.size() ) );
}

// Cube map faces in the order a VTF stores them.
constexpr const char *FACE_NAMES[] = { "Right (rt)", "Left (lf)", "Back (bk)", "Front (ft)", "Up (up)", "Down (dn)", "Sphere map" };

//-----------------------------------------------------------------------------
// Purpose: The tag as hex, the way lang_en.txt keys resources ("100000").
//-----------------------------------------------------------------------------
std::string TagHex( const std::array<std::uint8_t, 3> &tag )
{
	static constexpr char DIGITS[] = "0123456789abcdef";
	std::string szHex;
	for ( const std::uint8_t n : tag )
	{
		szHex += DIGITS[ n >> 4 ];
		szHex += DIGITS[ n & 15 ];
	}
	return szHex;
}

//-----------------------------------------------------------------------------
// Purpose: The picked channels of a picture: one alone as grey, alpha alone as grey, alpha off
//          as solid.
//-----------------------------------------------------------------------------
void ApplyChannels( CImage &image, const bool ( &on )[ 4 ] )
{
	const int nColours = on[ 0 ] + on[ 1 ] + on[ 2 ];
	for ( std::size_t i = 0; i < image.m_Pixels.size(); i += 4 )
	{
		std::uint8_t *p = image.m_Pixels.data() + i;
		if ( nColours == 0 )
		{
			const std::uint8_t nGrey = on[ 3 ] ? p[ 3 ] : 0;
			p[ 0 ] = p[ 1 ] = p[ 2 ] = nGrey;
			p[ 3 ] = 255;
			continue;
		}
		if ( nColours == 1 )
		{
			const std::uint8_t nGrey = on[ 0 ] ? p[ 0 ] : on[ 1 ] ? p[ 1 ]
																  : p[ 2 ];
			p[ 0 ] = p[ 1 ] = p[ 2 ] = nGrey;
		}
		else
		{
			for ( int c = 0; c < 3; c++ )
			{
				p[ c ] = on[ c ] ? p[ c ] : 0;
			}
		}
		p[ 3 ] = on[ 3 ] ? p[ 3 ] : 255;
	}
}

// Where each face goes in the cross, as the sky editor lays it out (rt in the middle).
constexpr std::array<std::tuple<xv::sky::Face_t, int, int>, 6> CROSS_PLACES{ {
	{ xv::sky::Face_t::FACE_UP, 1, 0 },
	{ xv::sky::Face_t::FACE_BK, 0, 1 },
	{ xv::sky::Face_t::FACE_RT, 1, 1 },
	{ xv::sky::Face_t::FACE_FT, 2, 1 },
	{ xv::sky::Face_t::FACE_LF, 3, 1 },
	{ xv::sky::Face_t::FACE_DN, 1, 2 },
} };

} // namespace

//-----------------------------------------------------------------------------
// Purpose: A picture file as sources for a texture: light values too when it is HDR; every frame
//          of an animated GIF; a VTF gives its full-size images.
//-----------------------------------------------------------------------------
std::vector<SourceImage_t> ReadSources( const QString &szPath, QString *pError )
{
	QFile file{ szPath };
	if ( !file.open( QIODevice::ReadOnly ) )
	{
		*pError = file.errorString();
		return {};
	}
	const QByteArray data = file.readAll();
	const std::span bytes{ reinterpret_cast<const std::byte *>( data.constData() ), static_cast<std::size_t>( data.size() ) };
	std::string szError;
	if ( szPath.endsWith( u".vtf", Qt::CaseInsensitive ) )
	{
		const auto vtf = ReadVtf( bytes, &szError );
		if ( !vtf )
		{
			*pError = QString::fromStdString( szError );
			return {};
		}
		return SourcesOf( *vtf );
	}
	std::vector<SourceImage_t> sources;
	for ( CImage &frame : LoadImageFrames( bytes, &szError ) )
	{
		sources.push_back( { std::move( frame ), {} } );
	}
	if ( sources.empty() )
	{
		// Whatever Qt's image plugins read (WebP, JPEG XL, AVIF, EXR, TIFF...), every frame;
		// float pictures keep their light values.
		QBuffer buffer;
		buffer.setData( data );
		QImageReader reader( &buffer );
		for ( int i = 0; reader.canRead() && i < 4096; i++ )
		{
			const QImage image = reader.read();
			if ( image.isNull() )
			{
				break;
			}
			SourceImage_t source;
			const QImage eight = image.convertToFormat( QImage::Format_RGBA8888 );
			source.m_Image = CImage{ eight.width(), eight.height() };
			for ( int y = 0; y < eight.height(); y++ )
			{
				std::memcpy( source.m_Image.Pixel( 0, y ), eight.constScanLine( y ), static_cast<std::size_t>( eight.width() ) * 4 );
			}
			if ( image.format() == QImage::Format_RGBA32FPx4 || image.format() == QImage::Format_RGBA16FPx4 || image.format() == QImage::Format_RGBX32FPx4 || image.format() == QImage::Format_RGBX16FPx4 )
			{
				const QImage light = image.convertToFormat( QImage::Format_RGBA32FPx4 );
				source.m_Light.m_nWidth = light.width();
				source.m_Light.m_nHeight = light.height();
				source.m_Light.m_Pixels.resize( static_cast<std::size_t>( light.width() ) * static_cast<std::size_t>( light.height() ) * 4 );
				for ( int y = 0; y < light.height(); y++ )
				{
					std::memcpy( source.m_Light.m_Pixels.data() + static_cast<std::size_t>( y ) * static_cast<std::size_t>( light.width() ) * 4, light.constScanLine( y ), static_cast<std::size_t>( light.width() ) * 16 );
				}
			}
			sources.push_back( std::move( source ) );
			if ( !reader.jumpToNextImage() )
			{
				break;
			}
		}
	}
	if ( sources.empty() )
	{
		*pError = QString::fromStdString( szError );
		return {};
	}
	if ( auto light = LoadFloatImage( bytes ) )
	{
		sources.front().m_Light = std::move( *light );
	}
	return sources;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CTextureEditor::CTextureEditor( CTextureDocument *pDocument, QWidget *pParent )
	: QWidget( pParent ),
	  m_pDocument( pDocument )
{
	auto *pToolbar = new QToolBar;
	pToolbar->setToolButtonStyle( Qt::ToolButtonTextOnly );
	QAction *pImport = pToolbar->addAction( tr( "Import…" ), this, [ this ] {
		const QString szStart = m_pDocument->GetDiskPath().empty() ? QString{} : QFileInfo{ QString::fromStdString( m_pDocument->GetDiskPath().string() ) }.absolutePath();
		const QStringList files = QFileDialog::getOpenFileNames( this, tr( "Import Pictures" ), szStart,
			PictureFilter( true ) );
		if ( !files.isEmpty() )
		{
			ImportFiles( files );
		}
	} );
	pImport->setToolTip( tr( "Make this texture from pictures: one image, animation frames or a cube map's faces" ) );
	QAction *pConvert = pToolbar->addAction( tr( "Convert…" ), this, &CTextureEditor::Convert );
	pConvert->setToolTip( tr( "Write it again in another format or version, with other flags or mipmaps" ) );
	QAction *pProperties = pToolbar->addAction( tr( "Properties…" ), this, &CTextureEditor::EditProperties );
	pProperties->setToolTip( tr( "Change the flags, version, bump scale, reflectivity, thumbnail or resources without touching the pictures" ) );
	auto *pExport = new QToolButton;
	pExport->setText( tr( "Export" ) );
	pExport->setPopupMode( QToolButton::InstantPopup );
	pExport->setAutoRaise( true );
	auto *pExportMenu = new QMenu( pExport );
	pExportMenu->addAction( tr( "This Image…" ), this, [ this ] {
		const bool bHdr = IsFloatFormat( m_pDocument->GetVtf().m_Info.m_nFormat );
		const QString szStart = QDir{ m_pDocument->GetDiskPath().empty() ? QDir::homePath() : QFileInfo{ QString::fromStdString( m_pDocument->GetDiskPath().string() ) }.absolutePath() }.filePath( BaseName() + ( bHdr ? QStringLiteral( ".hdr" ) : QStringLiteral( ".png" ) ) );
		QStringList filters;
		for ( const QString &szType : PictureTypesForSaving() )
		{
			filters << QStringLiteral( "%1 (*.%2)" ).arg( szType.toUpper(), szType );
		}
		QString szFilter = bHdr ? QStringLiteral( "HDR (*.hdr)" ) : QStringLiteral( "PNG (*.png)" );
		const QString szFile = QFileDialog::getSaveFileName( this, tr( "Export Image" ), szStart, filters.join( QStringLiteral( ";;" ) ), &szFilter );
		if ( !szFile.isEmpty() && !ExportCurrent( szFile ) )
		{
			QMessageBox::warning( this, tr( "Export" ), tr( "Could not write %1." ).arg( szFile ) );
		}
	} );
	pExportMenu->addAction( tr( "All Images…" ), this, [ this ] {
		const QString szFolder = QFileDialog::getExistingDirectory( this, tr( "Export Every Frame, Face and Slice To" ) );
		if ( szFolder.isEmpty() )
		{
			return;
		}
		bool bOk = false;
		const QStringList types = PictureTypesForSaving();
		const QString szType = QInputDialog::getItem( this, tr( "Export" ), tr( "Save them as:" ), types, IsFloatFormat( m_pDocument->GetVtf().m_Info.m_nFormat ) ? static_cast<int>( types.indexOf( QStringLiteral( "hdr" ) ) ) : 0, false, &bOk );
		if ( bOk )
		{
			QMessageBox::information( this, tr( "Export" ), tr( "Saved %n picture(s) in %1.", nullptr, ExportAll( szFolder, szType ) ).arg( QDir::toNativeSeparators( szFolder ) ) );
		}
	} );
	pExportMenu->addAction( tr( "Copy Image" ), this, [ this ] {
		const VtfInfo_t &info = m_pDocument->GetVtf().m_Info;
		const int nMip = std::max( 0, m_pMip->currentIndex() );
		if ( const auto image = DecodeImage( info.m_nFormat, std::max( 1, info.m_nWidth >> nMip ), std::max( 1, info.m_nHeight >> nMip ), m_pDocument->GetVtf().Image( nMip, m_pFrame->value() - 1, std::max( 0, m_pFace->currentIndex() ), m_pSlice->value() ) ) )
		{
			QGuiApplication::clipboard()->setImage( ToQImage( *image ) );
		}
	} );
	pExport->setMenu( pExportMenu );
	pExport->setToolTip( tr( "Save pictures out of the texture, or copy the one shown" ) );
	pToolbar->addWidget( pExport );
	QAction *pMakeVmt = pToolbar->addAction( tr( "Make VMT…" ), this, &CTextureEditor::MakeMaterialRequested );
	pMakeVmt->setToolTip( tr( "Write a material that uses this texture, next to it" ) );
	QAction *pSheet = pToolbar->addAction( tr( "Sprite Sheet…" ), this, &CTextureEditor::EditSheet );
	pSheet->setToolTip( tr( "Pack pictures into a particle sprite sheet, or change this texture's sheet (mksheet's job)" ) );
	pToolbar->addSeparator();

	m_pMip = new QComboBox;
	m_pMip->setObjectName( QStringLiteral( "textureMip" ) );
	m_pMip->setToolTip( tr( "Mipmap: 0 is full size, each next one half as big" ) );
	m_pFrame = new QSpinBox;
	m_pFrame->setObjectName( QStringLiteral( "textureFrame" ) );
	m_pFrame->setPrefix( tr( "Frame " ) );
	m_pFace = new QComboBox;
	m_pFace->setObjectName( QStringLiteral( "textureFace" ) );
	m_pSlice = new QSpinBox;
	m_pSlice->setPrefix( tr( "Slice " ) );
	m_pSlice->setToolTip( tr( "Depth slice of a volume texture" ) );
	pToolbar->addWidget( m_pMip );
	m_pFrameAction = pToolbar->addWidget( m_pFrame );
	m_pPlay = pToolbar->addAction( tr( "▶" ) );
	m_pPlay->setCheckable( true );
	m_pPlay->setToolTip( tr( "Play the frames (a material's AnimatedTexture proxy sets the game's rate)" ) );
	m_pRate = new QSpinBox;
	m_pRate->setRange( 1, 60 );
	m_pRate->setValue( 10 );
	m_pRate->setSuffix( tr( " fps" ) );
	m_pRate->setToolTip( tr( "Frames a second while playing; AnimatedTextureFrameRate in the material" ) );
	m_pRateAction = pToolbar->addWidget( m_pRate );
	m_pFaceAction = pToolbar->addWidget( m_pFace );
	m_pSliceAction = pToolbar->addWidget( m_pSlice );
	pToolbar->addSeparator();

	const char *const CHANNEL_NAMES[] = { "R", "G", "B", "A" };
	for ( int i = 0; i < 4; i++ )
	{
		m_pChannels[ i ] = new QToolButton;
		m_pChannels[ i ]->setText( QString::fromLatin1( CHANNEL_NAMES[ i ] ) );
		m_pChannels[ i ]->setCheckable( true );
		m_pChannels[ i ]->setChecked( true );
		m_pChannels[ i ]->setAutoRaise( true );
		m_pChannels[ i ]->setToolTip( i == 3 ? tr( "Alpha: on, see-through parts show the checkerboard; alone, alpha shows as grey" ) : tr( "Show this channel; one alone shows as grey" ) );
		connect( m_pChannels[ i ], &QToolButton::toggled, this, &CTextureEditor::UpdateView );
		pToolbar->addWidget( m_pChannels[ i ] );
	}
	pToolbar->addSeparator();
	QAction *pTile = pToolbar->addAction( tr( "Tile" ) );
	pTile->setCheckable( true );
	pTile->setToolTip( tr( "Repeat it 3×3, to check how it tiles" ) );
	m_pCross = pToolbar->addAction( tr( "Cross" ) );
	m_pCross->setCheckable( true );
	m_pCross->setToolTip( tr( "All six faces of the cube map, unwrapped as they meet" ) );
	m_pFullSize = pToolbar->addAction( tr( "Full-Size Mips" ) );
	m_pFullSize->setCheckable( true );
	m_pFullSize->setToolTip( tr( "Show smaller mipmaps blown up to the full size, to see how blurry they get" ) );
	QAction *pFit = pToolbar->addAction( tr( "Fit" ) );
	QAction *pActual = pToolbar->addAction( tr( "1:1" ) );
	pActual->setToolTip( tr( "One texel to one screen pixel. The wheel zooms, dragging moves it, a double-click fits it again" ) );

	m_pCanvas = new CTextureCanvas;
	m_pStatus = new QLabel;
	m_pStatus->setObjectName( QStringLiteral( "textureStatus" ) );
	auto *pLeft = new QWidget;
	auto *pLeftLayout = new QVBoxLayout( pLeft );
	pLeftLayout->setContentsMargins( 0, 0, 0, 0 );
	pLeftLayout->addWidget( m_pCanvas, 1 );
	pLeftLayout->addWidget( m_pStatus );

	m_pInfo = new QTreeWidget;
	m_pInfo->setObjectName( QStringLiteral( "textureInfo" ) );
	m_pInfo->setHeaderHidden( true );
	m_pInfo->setColumnCount( 2 );
	m_pInfo->header()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
	m_pInfo->setIconSize( QSize( 32, 32 ) );

	auto *pSplitter = new QSplitter;
	pSplitter->addWidget( pLeft );
	pSplitter->addWidget( m_pInfo );
	pSplitter->setStretchFactor( 0, 3 );
	pSplitter->setSizes( { 760, 340 } );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	pLayout->setSpacing( 0 );
	pLayout->addWidget( pToolbar );
	pLayout->addWidget( pSplitter, 1 );

	m_pPlayTimer = new QTimer( this );
	m_pPlayTimer->setInterval( 100 );
	connect( m_pRate, &QSpinBox::valueChanged, this, [ this ]( int nRate ) { m_pPlayTimer->setInterval( 1000 / nRate ); } );
	connect( m_pCross, &QAction::toggled, this, &CTextureEditor::UpdateView );
	connect( m_pFullSize, &QAction::toggled, this, &CTextureEditor::UpdateView );
	setAcceptDrops( true );
	connect( m_pPlayTimer, &QTimer::timeout, this, [ this ] { m_pFrame->setValue( m_pFrame->value() % m_pFrame->maximum() + 1 ); } );
	connect( m_pPlay, &QAction::toggled, this, [ this ]( bool bOn ) { bOn ? m_pPlayTimer->start() : m_pPlayTimer->stop(); } );
	connect( pTile, &QAction::toggled, this, [ this ]( bool bOn ) { m_pCanvas->SetTiled( bOn ); } );
	connect( pFit, &QAction::triggered, this, [ this ] { m_pCanvas->Fit(); } );
	connect( pActual, &QAction::triggered, this, [ this ] { m_pCanvas->SetZoom( 1.0 ); } );
	connect( m_pMip, &QComboBox::currentIndexChanged, this, [ this ] {
		if ( !m_bFilling )
		{
			FillSelectors(); // a smaller mipmap of a volume texture has fewer slices
			UpdateView();
		}
	} );
	connect( m_pFrame, &QSpinBox::valueChanged, this, &CTextureEditor::UpdateView );
	connect( m_pFace, &QComboBox::currentIndexChanged, this, &CTextureEditor::UpdateView );
	connect( m_pSlice, &QSpinBox::valueChanged, this, &CTextureEditor::UpdateView );
	connect( m_pDocument, &CTextureDocument::Changed, this, &CTextureEditor::Refresh );
	connect( &CInterfaceMode::Instance(), &CInterfaceMode::Changed, this, &CTextureEditor::FillInfo );
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTextureEditor::Refresh()
{
	FillSelectors();
	FillInfo();
	UpdateView();
}

//-----------------------------------------------------------------------------
// Purpose: The mipmap, frame, face and slice pickers for what the file has, keeping the picks
//          that still fit.
//-----------------------------------------------------------------------------
void CTextureEditor::FillSelectors()
{
	const VtfInfo_t &info = m_pDocument->GetVtf().m_Info;
	m_bFilling = true;
	const int nMip = std::clamp( m_pMip->currentIndex(), 0, info.m_nMipCount - 1 );
	m_pMip->clear();
	for ( int nLevel = 0; nLevel < info.m_nMipCount; nLevel++ )
	{
		m_pMip->addItem( tr( "Mip %1 · %2×%3" ).arg( nLevel ).arg( std::max( 1, info.m_nWidth >> nLevel ) ).arg( std::max( 1, info.m_nHeight >> nLevel ) ) );
	}
	m_pMip->setCurrentIndex( nMip );
	m_pFrame->setRange( 1, info.m_nFrames );
	m_pFrame->setSuffix( tr( " of %1" ).arg( info.m_nFrames ) );
	m_pFrameAction->setVisible( info.m_nFrames > 1 );
	m_pPlay->setVisible( info.m_nFrames > 1 );
	m_pRateAction->setVisible( info.m_nFrames > 1 );
	m_pCross->setVisible( info.m_nFaces >= 6 );
	if ( info.m_nFrames <= 1 )
	{
		m_pPlay->setChecked( false );
	}
	const int nFace = std::clamp( m_pFace->currentIndex(), 0, info.m_nFaces - 1 );
	m_pFace->clear();
	for ( int i = 0; i < info.m_nFaces && i < 7; i++ )
	{
		m_pFace->addItem( tr( FACE_NAMES[ i ] ) );
	}
	m_pFace->setCurrentIndex( nFace );
	m_pFaceAction->setVisible( info.m_nFaces > 1 );
	const int nSlices = std::max( 1, info.m_nDepth >> nMip );
	m_pSlice->setRange( 0, nSlices - 1 );
	m_pSliceAction->setVisible( info.m_nDepth > 1 );
	m_bFilling = false;
}

//-----------------------------------------------------------------------------
// Purpose: What the file holds: header, flags, thumbnail and resources.
//-----------------------------------------------------------------------------
void CTextureEditor::FillInfo()
{
	const CVtf &vtf = m_pDocument->GetVtf();
	const VtfInfo_t &info = vtf.m_Info;
	const bool bSimple = CInterfaceMode::Instance().IsSimple();
	const xv::particles::CNames &names = ParticleNames();
	m_pInfo->clear();
	const auto group = [ this ]( const QString &szTitle ) {
		auto *pGroup = new QTreeWidgetItem( m_pInfo, { szTitle } );
		QFont bold = m_pInfo->font();
		bold.setBold( true );
		pGroup->setFont( 0, bold );
		pGroup->setFirstColumnSpanned( true );
		pGroup->setExpanded( true );
		return pGroup;
	};
	const auto row = []( QTreeWidgetItem *pParent, const QString &szName, const QString &szValue, const QString &szTip = {} ) {
		auto *pItem = new QTreeWidgetItem( pParent, { szName, szValue } );
		pItem->setToolTip( 0, szTip );
		pItem->setToolTip( 1, szTip );
		return pItem;
	};

	QTreeWidgetItem *pImage = group( tr( "Image" ) );
	row( pImage, tr( "Size" ), info.m_nDepth > 1 ? QStringLiteral( "%1 × %2 × %3" ).arg( info.m_nWidth ).arg( info.m_nHeight ).arg( info.m_nDepth ) : QStringLiteral( "%1 × %2" ).arg( info.m_nWidth ).arg( info.m_nHeight ) );
	const QString szFormat = Qs( FormatName( info.m_nFormat ) );
	row( pImage, tr( "Format" ), szFormat, Qs( names.Texture( "format:" + szFormat.toStdString() ).m_szMeaning ) );
	row( pImage, tr( "Mipmaps" ), info.m_nMipCount == 1 ? tr( "none" ) : QString::number( info.m_nMipCount ) );
	row( pImage, tr( "Frames" ), QString::number( info.m_nFrames ) );
	if ( info.m_nFrames > 1 || !bSimple )
	{
		row( pImage, tr( "First frame" ), info.m_nFirstFrame == 0xFFFF ? tr( "none (-1)" ) : QString::number( info.m_nFirstFrame ) );
	}
	if ( info.m_nFaces > 1 )
	{
		row( pImage, tr( "Faces" ), info.m_nFaces == 7 ? tr( "6, and a sphere map" ) : QString::number( info.m_nFaces ) );
	}
	auto *pReflect = row( pImage, tr( "Reflectivity" ),
		QStringLiteral( "%1 %2 %3" ).arg( info.m_vecReflectivity[ 0 ], 0, 'f', 3 ).arg( info.m_vecReflectivity[ 1 ], 0, 'f', 3 ).arg( info.m_vecReflectivity[ 2 ], 0, 'f', 3 ),
		tr( "The average colour in linear light; the map compiler bounces light off it" ) );
	QPixmap swatch( 16, 16 );
	swatch.fill( QColor::fromRgbF( std::pow( std::clamp( info.m_vecReflectivity[ 0 ], 0.0f, 1.0f ), 1.0f / 2.2f ), std::pow( std::clamp( info.m_vecReflectivity[ 1 ], 0.0f, 1.0f ), 1.0f / 2.2f ),
		std::pow( std::clamp( info.m_vecReflectivity[ 2 ], 0.0f, 1.0f ), 1.0f / 2.2f ) ) );
	pReflect->setIcon( 1, QIcon( swatch ) );
	if ( !bSimple || info.m_flBumpScale != 1.0f )
	{
		row( pImage, tr( "Bump scale" ), QString::number( static_cast<double>( info.m_flBumpScale ) ) );
	}

	QTreeWidgetItem *pFlags = group( tr( "Flags" ) );
	if ( info.m_nFlags == 0 )
	{
		row( pFlags, tr( "none" ), {} );
	}
	for ( const std::string &szFlag : FlagNames( info.m_nFlags ) )
	{
		const xv::particles::PlainName_t plain = names.Texture( "flag:" + szFlag );
		row( pFlags, bSimple ? Qs( plain.m_szName ) : Qs( szFlag ), {}, ( bSimple ? Qs( szFlag ) : Qs( plain.m_szName ) ) + u'\n' + Qs( plain.m_szMeaning ) )->setFirstColumnSpanned( true );
	}
	if ( !bSimple )
	{
		row( pFlags, tr( "Value" ), QStringLiteral( "0x%1" ).arg( info.m_nFlags, 8, 16, QLatin1Char( '0' ) ) );
	}

	QTreeWidgetItem *pFile = group( tr( "File" ) );
	row( pFile, tr( "Version" ), QStringLiteral( "%1.%2" ).arg( info.m_nVersionMajor ).arg( info.m_nVersionMinor ) );
	row( pFile, tr( "Size on disk" ), QLocale{}.formattedDataSize( static_cast<qint64>( vtf.m_Data.size() ) ) );
	const auto lowRes = vtf.LowRes();
	auto *pThumb = row( pFile, tr( "Thumbnail" ),
		lowRes.empty() ? tr( "none" ) : QStringLiteral( "%1 · %2×%3" ).arg( Qs( FormatName( info.m_nLowResFormat ) ) ).arg( info.m_nLowResWidth ).arg( info.m_nLowResHeight ),
		tr( "The small copy tools show as a preview" ) );
	if ( const auto thumb = lowRes.empty() ? std::nullopt : DecodeImage( info.m_nLowResFormat, info.m_nLowResWidth, info.m_nLowResHeight, lowRes ) )
	{
		pThumb->setIcon( 1, QIcon( QPixmap::fromImage( ToQImage( *thumb ).scaled( 32, 32, Qt::KeepAspectRatio, Qt::FastTransformation ) ) ) );
	}

	if ( !vtf.m_Resources.empty() )
	{
		QTreeWidgetItem *pResources = group( tr( "Resources" ) );
		for ( const VtfResource_t &resource : vtf.m_Resources )
		{
			const std::string szHex = TagHex( resource.m_Tag );
			const xv::particles::PlainName_t plain = names.Texture( "res:" + szHex );
			QString szValue;
			if ( resource.m_nFlags & VtfResource_t::NO_DATA )
			{
				szValue = resource.m_Tag == std::array<std::uint8_t, 3>{ 'L', 'O', 'D' }
					? tr( "clamp %1 × %2" ).arg( resource.m_nValue & 0xFF ).arg( ( resource.m_nValue >> 8 ) & 0xFF )
					: QStringLiteral( "0x%1" ).arg( resource.m_nValue, 8, 16, QLatin1Char( '0' ) );
			}
			else if ( resource.m_Tag == LOW_RES_RESOURCE || resource.m_Tag == IMAGE_RESOURCE )
			{
				szValue = tr( "at byte %1" ).arg( resource.m_nValue );
			}
			else
			{
				const auto data = vtf.Resource( resource.m_Tag );
				szValue = QLocale{}.formattedDataSize( static_cast<qint64>( data.size() ) );
			}
			const QString szName = plain.m_szName == szHex ? QStringLiteral( "%1 (%2)" ).arg( tr( "Unknown" ), Qs( szHex ) ) : Qs( plain.m_szName );
			QTreeWidgetItem *pResource = row( pResources, szName, szValue, Qs( plain.m_szMeaning ) );
			// The sheet in full: each sequence, and where each frame is.
			if ( resource.m_Tag == SHEET_RESOURCE )
			{
				if ( const auto sheet = ReadSheet( vtf.Resource( SHEET_RESOURCE ) ) )
				{
					pResource->setText( 1, pResource->text( 1 ) + tr( " · version %1, %n sequence(s)", nullptr, static_cast<int>( sheet->m_Sequences.size() ) ).arg( sheet->m_nVersion ) );
					for ( const CSheet::Sequence_t &sequence : sheet->m_Sequences )
					{
						auto *pSequence = row( pResource, tr( "Sequence %1" ).arg( sequence.m_nNumber ),
							tr( "%n frame(s) · %1 long%2", nullptr, static_cast<int>( sequence.m_Frames.size() ) ).arg( static_cast<double>( sequence.m_flTotalTime ) ).arg( sequence.m_bClamp ? tr( " · holds the last" ) : tr( " · loops" ) ) );
						for ( std::size_t f = 0; f < sequence.m_Frames.size(); f++ )
						{
							const auto &rect = sequence.m_Frames[ f ].m_Rect;
							const auto at = [ &info ]( float flEdge, bool bAcross ) { return static_cast<int>( std::lround( flEdge * static_cast<float>( bAcross ? info.m_nWidth : info.m_nHeight ) ) ); };
							row( pSequence, tr( "Frame %1" ).arg( f ),
								tr( "lasts %1 · at %2,%3 · %4×%5" )
									.arg( static_cast<double>( sequence.m_Frames[ f ].m_flDuration ) )
									.arg( at( rect[ 0 ], true ) )
									.arg( at( rect[ 1 ], false ) )
									.arg( at( rect[ 2 ], true ) - at( rect[ 0 ], true ) )
									.arg( at( rect[ 3 ], false ) - at( rect[ 1 ], false ) ) );
						}
					}
				}
			}
			else if ( resource.m_Tag == std::array<std::uint8_t, 3>{ 'K', 'V', 'D' } )
			{
				const auto text = vtf.Resource( resource.m_Tag );
				row( pResource, QString{}, QString::fromUtf8( reinterpret_cast<const char *>( text.data() ), static_cast<qsizetype>( text.size() ) ).left( 400 ) )->setFirstColumnSpanned( true );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draws the picked image with the picked channels.
//-----------------------------------------------------------------------------
void CTextureEditor::UpdateView()
{
	if ( m_bFilling )
	{
		return;
	}
	const CVtf &vtf = m_pDocument->GetVtf();
	const VtfInfo_t &info = vtf.m_Info;
	const int nMip = std::max( 0, m_pMip->currentIndex() );
	const int nWidth = std::max( 1, info.m_nWidth >> nMip );
	const int nHeight = std::max( 1, info.m_nHeight >> nMip );
	std::string szError;
	const auto bytes = vtf.Image( nMip, m_pFrame->value() - 1, std::max( 0, m_pFace->currentIndex() ), m_pSlice->value() );
	const auto image = bytes.empty() ? std::nullopt : DecodeImage( info.m_nFormat, nWidth, nHeight, bytes, &szError );
	if ( !image )
	{
		m_Shown = {};
		m_pCanvas->SetImage( {} );
		m_pStatus->setText( tr( "Can't show this image: %1" ).arg( bytes.empty() ? tr( "it isn't in the file" ) : QString::fromStdString( szError ) ) );
		return;
	}

	bool on[ 4 ];
	for ( int c = 0; c < 4; c++ )
	{
		on[ c ] = m_pChannels[ c ]->isChecked();
	}
	CImage shown = *image;
	if ( m_pCross->isChecked() && info.m_nFaces >= 6 )
	{
		// Turned and mirrored the way the sky editor shows a cube map's faces, so they join.
		shown = CImage{ nWidth * 4, nHeight * 3 };
		for ( const auto &[ nFace, nColumn, nRow ] : CROSS_PLACES )
		{
			const auto face = xv::sky::CubeMapFace( vtf, nFace, nullptr, nMip, m_pFrame->value() - 1 );
			for ( int y = 0; face && y < std::min( face->m_nHeight, nHeight ); y++ )
			{
				std::memcpy( shown.Pixel( nColumn * nWidth, nRow * nHeight + y ), face->Pixel( 0, y ), static_cast<std::size_t>( std::min( face->m_nWidth, nWidth ) ) * 4 );
			}
		}
	}
	ApplyChannels( shown, on );
	m_Shown = ToQImage( shown );
	// A smaller mipmap blown up to the full size, texels kept sharp.
	if ( m_pFullSize->isChecked() && nMip > 0 )
	{
		m_Shown = m_Shown.scaled( m_Shown.size() * ( 1 << std::min( nMip, 12 ) ), Qt::IgnoreAspectRatio, Qt::FastTransformation );
	}
	m_pCanvas->SetImage( m_Shown );
	QString szWhere = tr( "Mip %1: %2×%3" ).arg( nMip ).arg( nWidth ).arg( nHeight );
	if ( info.m_nFrames > 1 )
	{
		szWhere += tr( " · frame %1 of %2" ).arg( m_pFrame->value() ).arg( info.m_nFrames );
	}
	if ( info.m_nFaces > 1 )
	{
		szWhere += QStringLiteral( " · " ) + ( m_pCross->isChecked() ? tr( "all faces" ) : m_pFace->currentText() );
	}
	if ( IsFloatFormat( info.m_nFormat ) )
	{
		szWhere += tr( " · HDR, shown cut to 0-1" );
	}
	m_pStatus->setText( szWhere );
}

//-----------------------------------------------------------------------------
// Purpose: For tests and the main window: picks an image to show.
//-----------------------------------------------------------------------------
void CTextureEditor::ShowImage( int nMip, int nFrame, int nFace, int nSlice )
{
	m_pMip->setCurrentIndex( nMip );
	m_pFrame->setValue( nFrame + 1 );
	m_pFace->setCurrentIndex( nFace );
	m_pSlice->setValue( nSlice );
	UpdateView();
}

//-----------------------------------------------------------------------------
// Purpose: Which channels are drawn: "rgba" for all, "a" for alpha alone...
//-----------------------------------------------------------------------------
void CTextureEditor::SetChannels( const QString &szChannels )
{
	const QString szLower = szChannels.toLower();
	for ( int c = 0; c < 4; c++ )
	{
		const QSignalBlocker blocker{ m_pChannels[ c ] };
		m_pChannels[ c ]->setChecked( szLower.contains( QStringLiteral( "rgba" )[ c ] ) );
	}
	UpdateView();
}

//-----------------------------------------------------------------------------
// Purpose: Makes the texture from pictures on disk, after asking how; one undo step.
//-----------------------------------------------------------------------------
bool CTextureEditor::ImportFiles( const QStringList &files )
{
	std::vector<NamedSource_t> sources;
	QStringList failures;
	for ( const QString &szPath : files )
	{
		QString szError;
		std::vector<SourceImage_t> read = ReadSources( szPath, &szError );
		if ( read.empty() )
		{
			failures << QStringLiteral( "%1: %2" ).arg( QFileInfo{ szPath }.fileName(), szError );
		}
		for ( std::size_t i = 0; i < read.size(); i++ )
		{
			const QString szName = read.size() == 1 ? QFileInfo{ szPath }.fileName() : tr( "%1 (picture %2)" ).arg( QFileInfo{ szPath }.fileName() ).arg( i + 1 );
			sources.push_back( { szName, std::move( read[ i ] ) } );
		}
	}
	if ( !failures.isEmpty() )
	{
		QMessageBox::warning( this, tr( "Import" ), tr( "Some pictures couldn't be read:\n%1" ).arg( failures.join( u'\n' ) ) );
	}
	if ( sources.empty() )
	{
		return false;
	}
	// The usual choices for the pictures, keeping this texture's flags, version and bump scale.
	BuildOptions_t initial = CTextureOptionsDialog::DefaultsFor( sources );
	const BuildOptions_t current = OptionsOf( m_pDocument->GetVtf() );
	initial.m_Write.m_nFlags = current.m_Write.m_nFlags;
	initial.m_Write.m_nVersionMinor = current.m_Write.m_nVersionMinor;
	initial.m_Write.m_flBumpScale = current.m_Write.m_flBumpScale;
	const QString szFirst = sources.front().m_szName;
	CTextureOptionsDialog dialog( CTextureOptionsDialog::MODE_IMPORT, std::move( sources ), initial, this );
	if ( dialog.exec() != QDialog::Accepted )
	{
		return false;
	}
	std::string szError;
	auto bytes = BuildVtf( dialog.GetSources(), dialog.GetOptions(), &szError );
	if ( bytes.empty() )
	{
		QMessageBox::warning( this, tr( "Import" ), tr( "Couldn't make the texture: %1." ).arg( QString::fromStdString( szError ) ) );
		return false;
	}
	return m_pDocument->Replace( std::move( bytes ), files.size() == 1 ? tr( "Import %1" ).arg( szFirst ) : tr( "Import %n picture(s)", nullptr, static_cast<int>( files.size() ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: Writes the texture again with other choices, after asking which; one undo step.
//-----------------------------------------------------------------------------
bool CTextureEditor::Convert()
{
	const CVtf &vtf = m_pDocument->GetVtf();
	if ( vtf.m_Info.m_nFormat == VtfFormat_t::P8 )
	{
		QMessageBox::information( this, tr( "Convert" ), tr( "P8 (paletted) textures can't be read yet." ) );
		return false;
	}
	if ( vtf.m_Info.m_nDepth > 1 )
	{
		QMessageBox::information( this, tr( "Convert" ), tr( "Volume textures can't be written yet." ) );
		return false;
	}
	std::vector<NamedSource_t> sources;
	for ( SourceImage_t &source : SourcesOf( vtf ) )
	{
		sources.push_back( { m_pDocument->GetFileName(), std::move( source ) } );
	}
	CTextureOptionsDialog dialog( CTextureOptionsDialog::MODE_CONVERT, std::move( sources ), OptionsOf( vtf ), this );
	if ( dialog.exec() != QDialog::Accepted )
	{
		return false;
	}
	std::string szError;
	const BuildOptions_t options = dialog.GetOptions();
	auto bytes = BuildVtf( dialog.GetSources(), options, &szError );
	if ( bytes.empty() )
	{
		QMessageBox::warning( this, tr( "Convert" ), tr( "Couldn't convert the texture: %1." ).arg( QString::fromStdString( szError ) ) );
		return false;
	}
	return m_pDocument->Replace( std::move( bytes ), tr( "Convert to %1" ).arg( Qs( FormatName( options.m_Write.m_nFormat ) ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: Changes the header without touching the pictures, after asking; one undo step.
//-----------------------------------------------------------------------------
bool CTextureEditor::EditProperties()
{
	CTexturePropertiesDialog dialog( m_pDocument->GetVtf(), this );
	if ( dialog.exec() != QDialog::Accepted )
	{
		return false;
	}
	auto bytes = RewriteVtf( m_pDocument->GetVtf(), dialog.GetEdit() );
	if ( bytes.empty() )
	{
		QMessageBox::warning( this, tr( "Properties" ), tr( "A volume texture needs version 7.2 or later." ) );
		return false;
	}
	if ( bytes == m_pDocument->GetBytes() )
	{
		return false;
	}
	return m_pDocument->Replace( std::move( bytes ), tr( "Change properties" ) );
}

//-----------------------------------------------------------------------------
// Purpose: The texture's name without its folder or ending.
//-----------------------------------------------------------------------------
QString CTextureEditor::BaseName() const
{
	return QFileInfo{ m_pDocument->GetFileName() }.completeBaseName();
}

namespace
{

//-----------------------------------------------------------------------------
// Purpose: One image of a texture as a picture file: light values for HDR into .hdr.
//-----------------------------------------------------------------------------
std::vector<std::byte> PictureFile( const CVtf &vtf, int nMip, int nFrame, int nFace, int nSlice, ImageFileType_t nType )
{
	const VtfInfo_t &info = vtf.m_Info;
	const int nWidth = std::max( 1, info.m_nWidth >> nMip );
	const int nHeight = std::max( 1, info.m_nHeight >> nMip );
	const auto bytes = vtf.Image( nMip, nFrame, nFace, nSlice );
	if ( bytes.empty() )
	{
		return {};
	}
	if ( nType == ImageFileType_t::IMAGE_FILE_TYPE_HDR )
	{
		const auto light = DecodeFloat( info.m_nFormat, nWidth, nHeight, bytes );
		return light ? SaveFloatImage( *light ) : std::vector<std::byte>{};
	}
	const auto image = DecodeImage( info.m_nFormat, nWidth, nHeight, bytes );
	return image ? SaveImage( *image, nType ) : std::vector<std::byte>{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool WriteBytes( const QString &szPath, const std::vector<std::byte> &bytes )
{
	QSaveFile file{ szPath };
	return !bytes.empty() && file.open( QIODevice::WriteOnly ) && file.write( reinterpret_cast<const char *>( bytes.data() ), static_cast<qint64>( bytes.size() ) ) == static_cast<qint64>( bytes.size() ) && file.commit();
}

} // namespace

namespace
{

// The picture types written without Qt's plugins.
const QStringList CORE_TYPES{ QStringLiteral( "png" ), QStringLiteral( "tga" ), QStringLiteral( "bmp" ), QStringLiteral( "jpg" ), QStringLiteral( "jpeg" ), QStringLiteral( "hdr" ), QStringLiteral( "qoi" ), QStringLiteral( "pfm" ) };

//-----------------------------------------------------------------------------
// Purpose: One image of a texture as a picture file of any type, by the path's ending: ours for
//          the core types, Qt's plugins for the rest (EXR with the light values).
//-----------------------------------------------------------------------------
bool WritePicture( const CVtf &vtf, int nMip, int nFrame, int nFace, int nSlice, const QString &szPath )
{
	const QString szType = QFileInfo{ szPath }.suffix().toLower();
	if ( CORE_TYPES.contains( szType ) )
	{
		return WriteBytes( szPath, PictureFile( vtf, nMip, nFrame, nFace, nSlice, ImageFileTypeFor( szType.toStdString() ) ) );
	}
	const VtfInfo_t &info = vtf.m_Info;
	const int nWidth = std::max( 1, info.m_nWidth >> nMip );
	const int nHeight = std::max( 1, info.m_nHeight >> nMip );
	const auto bytes = vtf.Image( nMip, nFrame, nFace, nSlice );
	QImage image;
	if ( szType == u"exr" )
	{
		if ( const auto light = DecodeFloat( info.m_nFormat, nWidth, nHeight, bytes ) )
		{
			image = QImage( nWidth, nHeight, QImage::Format_RGBA32FPx4 );
			for ( int y = 0; y < nHeight; y++ )
			{
				std::memcpy( image.scanLine( y ), light->Pixel( 0, y ), static_cast<std::size_t>( nWidth ) * 16 );
			}
		}
	}
	else if ( const auto decoded = DecodeImage( info.m_nFormat, nWidth, nHeight, bytes ) )
	{
		image = ToQImage( *decoded );
	}
	QImageWriter writer( szPath, szType.toLatin1() );
	return !image.isNull() && writer.write( image );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The types pictures can be saved as: ours, then what Qt's plugins add.
//-----------------------------------------------------------------------------
QStringList PictureTypesForSaving()
{
	QStringList types{ QStringLiteral( "png" ), QStringLiteral( "tga" ), QStringLiteral( "bmp" ), QStringLiteral( "jpg" ), QStringLiteral( "hdr" ), QStringLiteral( "qoi" ), QStringLiteral( "pfm" ) };
	for ( const QByteArray &type : QImageWriter::supportedImageFormats() )
	{
		const QString szType = QString::fromLatin1( type ).toLower();
		if ( !types.contains( szType ) && szType != u"jpeg" && szType != u"tif" && szType != u"ico" && szType != u"cur" && szType != u"icns" && szType != u"xbm" && szType != u"xpm" && szType != u"pbm" && szType != u"pgm" && szType != u"ppm" )
		{
			types << szType;
		}
	}
	return types;
}

//-----------------------------------------------------------------------------
// Purpose: The open dialog's filter for pictures: ours and what Qt's plugins read.
//-----------------------------------------------------------------------------
QString PictureFilter( bool bWithVtf )
{
	QStringList patterns = PicturePatterns();
	if ( bWithVtf )
	{
		patterns << QStringLiteral( "*.vtf" );
	}
	return QObject::tr( "Pictures (%1);;All files (*)" ).arg( patterns.join( u' ' ) );
}

//-----------------------------------------------------------------------------
// Purpose: The picture files read: ours, and what Qt's plugins read ("*.png", "*.webp"...).
//-----------------------------------------------------------------------------
QStringList PicturePatterns()
{
	QStringList patterns{ QStringLiteral( "*.png" ), QStringLiteral( "*.apng" ), QStringLiteral( "*.tga" ), QStringLiteral( "*.jpg" ), QStringLiteral( "*.jpeg" ), QStringLiteral( "*.bmp" ),
		QStringLiteral( "*.psd" ), QStringLiteral( "*.gif" ), QStringLiteral( "*.hdr" ), QStringLiteral( "*.qoi" ), QStringLiteral( "*.pfm" ), QStringLiteral( "*.ppm" ), QStringLiteral( "*.pgm" ) };
	for ( const QByteArray &type : QImageReader::supportedImageFormats() )
	{
		const QString szPattern = QStringLiteral( "*." ) + QString::fromLatin1( type ).toLower();
		if ( !patterns.contains( szPattern ) )
		{
			patterns << szPattern;
		}
	}
	patterns.removeAll( QStringLiteral( "*.svg" ) );
	patterns.removeAll( QStringLiteral( "*.svgz" ) );
	patterns.removeAll( QStringLiteral( "*.pdf" ) );
	return patterns;
}

//-----------------------------------------------------------------------------
// Purpose: Saves the picked image as a picture file, by the path's ending.
//-----------------------------------------------------------------------------
bool CTextureEditor::ExportCurrent( const QString &szPath )
{
	return WritePicture( m_pDocument->GetVtf(), std::max( 0, m_pMip->currentIndex() ), m_pFrame->value() - 1, std::max( 0, m_pFace->currentIndex() ), m_pSlice->value(), szPath );
}

//-----------------------------------------------------------------------------
// Purpose: Saves every frame, face and slice of the full-size image into a folder.
//-----------------------------------------------------------------------------
int CTextureEditor::ExportAll( const QString &szFolder, const QString &szExtension )
{
	return ExportPictures( m_pDocument->GetVtf(), szFolder, BaseName(), szExtension );
}

//-----------------------------------------------------------------------------
// Purpose: Every frame, face and slice of a texture's full-size image as picture files.
//-----------------------------------------------------------------------------
int ExportPictures( const CVtf &vtf, const QString &szFolder, const QString &szBaseName, const QString &szExtension )
{
	static constexpr const char *FACE_SUFFIXES[] = { "rt", "lf", "bk", "ft", "up", "dn" };
	const VtfInfo_t &info = vtf.m_Info;
	const int nFaces = info.m_nFaces == 7 ? 6 : info.m_nFaces;
	QDir{}.mkpath( szFolder );
	int nSaved = 0;
	for ( int nFrame = 0; nFrame < info.m_nFrames; nFrame++ )
	{
		for ( int nFace = 0; nFace < nFaces; nFace++ )
		{
			for ( int nSlice = 0; nSlice < std::max( 1, info.m_nDepth ); nSlice++ )
			{
				QString szName = szBaseName;
				if ( info.m_nFrames > 1 )
				{
					szName += QStringLiteral( "_%1" ).arg( nFrame, 3, 10, QLatin1Char( '0' ) );
				}
				if ( nFaces == 6 )
				{
					szName += u'_' + QString::fromLatin1( FACE_SUFFIXES[ nFace ] );
				}
				if ( info.m_nDepth > 1 )
				{
					szName += QStringLiteral( "_slice%1" ).arg( nSlice );
				}
				if ( WritePicture( vtf, 0, nFrame, nFace, nSlice, QDir{ szFolder }.filePath( szName + u'.' + szExtension ) ) )
				{
					nSaved++;
				}
			}
		}
	}
	return nSaved;
}

//-----------------------------------------------------------------------------
// Purpose: A cube map's six faces as a cross.
//-----------------------------------------------------------------------------
void CTextureEditor::SetCrossView( bool bCross )
{
	m_pCross->setChecked( bCross );
}

//-----------------------------------------------------------------------------
// Purpose: Pictures dropped on the editor are imported.
//-----------------------------------------------------------------------------
void CTextureEditor::dragEnterEvent( QDragEnterEvent *pEvent )
{
	const auto urls = pEvent->mimeData()->urls();
	if ( !urls.isEmpty() && std::ranges::all_of( urls, &QUrl::isLocalFile ) )
	{
		pEvent->acceptProposedAction();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTextureEditor::dropEvent( QDropEvent *pEvent )
{
	QStringList files;
	for ( const QUrl &url : pEvent->mimeData()->urls() )
	{
		files << url.toLocalFile();
	}
	pEvent->acceptProposedAction();
	// After the drop finishes: the import asks with a dialog.
	QTimer::singleShot( 0, this, [ this, files ] { ImportFiles( files ); } );
}

//-----------------------------------------------------------------------------
// Purpose: The sprite sheet editor; what it makes replaces the texture as one undo step.
//-----------------------------------------------------------------------------
bool CTextureEditor::EditSheet()
{
	CSheetDialog dialog( &m_pDocument->GetVtf(), this );
	if ( !m_pDocument->GetVtf().Resource( SHEET_RESOURCE ).empty() )
	{
		dialog.StartFromTexture();
	}
	if ( dialog.exec() != QDialog::Accepted )
	{
		return false;
	}
	auto bytes = dialog.BuildTexture();
	return !bytes.empty() && m_pDocument->Replace( std::move( bytes ), tr( "Make sprite sheet" ) );
}
