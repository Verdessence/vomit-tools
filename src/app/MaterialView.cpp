//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MaterialView.h.
//
//===========================================================================//
#include "MaterialView.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "FileViews.h"
#include "Graphics.h"
#include "ImageFiles.h"
#include "InterfaceMode.h"
#include "Language.h"
#include "texture/Vtf.h"

#ifdef VOMIT_TOOLS_GPU
#include "GpuView.h"
#include "MaterialRenderer.h"
#endif

namespace material = xv::material;

namespace
{

constexpr int ROLE_GAME_PATH = Qt::UserRole + 1;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Qs( std::string_view szText )
{
	return QString::fromUtf8( szText.data(), static_cast<qsizetype>( szText.size() ) );
}

//-----------------------------------------------------------------------------
// Purpose: "512×512 DXT5, 3 frames, CLAMPS CLAMPT".
//-----------------------------------------------------------------------------
QString DescribeTexture( const material::Texture_t &texture )
{
	switch ( texture.m_nKind )
	{
		case material::TextureKind_t::TEXTURE_KIND_MAP_CUBEMAP: return QObject::tr( "the map's nearest cube map" );
		case material::TextureKind_t::TEXTURE_KIND_RENDER_TARGET: return QObject::tr( "drawn by the engine each frame" );
		default: break;
	}
	if ( !texture.m_Info )
	{
		return Qs( texture.m_szProblem );
	}
	const xv::texture::VtfInfo_t &info = *texture.m_Info;
	QString szText = QObject::tr( "%1×%2 %3" ).arg( info.m_nWidth ).arg( info.m_nHeight ).arg( Qs( xv::texture::FormatName( info.m_nFormat ) ) );
	if ( texture.m_Cube )
	{
		szText += QObject::tr( ", cube map" );
	}
	if ( info.m_nFrames > 1 )
	{
		szText += QObject::tr( ", %n frame(s)", nullptr, info.m_nFrames );
	}
	if ( texture.m_nMip > 0 )
	{
		szText += QObject::tr( ", shown at %1×%2" ).arg( std::max( 1, info.m_nWidth >> texture.m_nMip ) ).arg( std::max( 1, info.m_nHeight >> texture.m_nMip ) );
	}
	return szText;
}

//-----------------------------------------------------------------------------
// Purpose: A texture's first frame, fitted into a square of side nSize. Only pictures keep their
//          alpha: in bump maps, masks and HDR textures it holds something else.
//-----------------------------------------------------------------------------
QPixmap Thumbnail( const material::Texture_t &texture, int nSize )
{
	QPixmap pixmap( nSize, nSize );
	pixmap.fill( Qt::transparent );
	if ( texture.m_Frames.empty() || texture.m_Frames.front().Empty() )
	{
		return pixmap;
	}
	const QString szParameter = Qs( texture.m_szParameter ).toLower();
	const bool bPicture = szParameter.startsWith( u"$basetexture" ) || szParameter == u"$texture2" || szParameter == u"$detail";
	QImage image = ToQImage( texture.m_Frames.front() );
	if ( !bPicture )
	{
		image = image.convertToFormat( QImage::Format_RGB32 );
	}
	image = image.scaled( nSize, nSize, Qt::KeepAspectRatio, Qt::SmoothTransformation );
	QPainter painter( &pixmap );
	painter.drawImage( ( nSize - image.width() ) / 2, ( nSize - image.height() ) / 2, image );
	return pixmap;
}

} // namespace

// The preview image, fitted to the widget; dragging sets which way the light comes from.
class CMaterialCanvas : public QWidget
{
public:
	explicit CMaterialCanvas( QWidget *pParent = nullptr )
		: QWidget( pParent )
	{
		setObjectName( QStringLiteral( "materialCanvas" ) );
		setMinimumSize( 160, 160 );
		setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
		setCursor( Qt::CrossCursor );
		setToolTip( tr( "Drag to move the light. Double-click to put it back." ) );
	}

	std::function<void( xv::CVec3 )> m_pfnLight;
	std::function<void()> m_pfnResized;
	QImage m_Image;
	QWidget *m_pGpu = nullptr; // the graphics card's view, over the whole canvas

	//-----------------------------------------------------------------------------
	// Purpose: The square the image is drawn in.
	//-----------------------------------------------------------------------------
	[[nodiscard]] QRect Square() const
	{
		const int nSide = std::min( width(), height() );
		return { ( width() - nSide ) / 2, ( height() - nSide ) / 2, nSide, nSide };
	}

	//-----------------------------------------------------------------------------
	// Purpose: The square in device pixels, as the graphics card draws it.
	//-----------------------------------------------------------------------------
	[[nodiscard]] QRect DeviceSquare() const
	{
		const QRect square = Square();
		const qreal flRatio = devicePixelRatioF();
		return { static_cast<int>( std::lround( square.x() * flRatio ) ), static_cast<int>( std::lround( square.y() * flRatio ) ), static_cast<int>( std::lround( square.width() * flRatio ) ),
			static_cast<int>( std::lround( square.height() * flRatio ) ) };
	}

	//-----------------------------------------------------------------------------
	// Purpose: The side, in device pixels, worth rendering at.
	//-----------------------------------------------------------------------------
	[[nodiscard]] int RenderSize() const
	{
		return std::clamp( static_cast<int>( std::lround( Square().width() * devicePixelRatioF() ) ), 64, 512 );
	}

protected:
	//-----------------------------------------------------------------------------
	// Purpose:
	//-----------------------------------------------------------------------------
	void paintEvent( QPaintEvent * ) override
	{
		if ( m_pGpu )
		{
			return; // the graphics card's view covers the canvas
		}
		QPainter painter( this );
		painter.setRenderHint( QPainter::SmoothPixmapTransform );
		if ( !m_Image.isNull() )
		{
			painter.drawImage( Square(), m_Image );
		}
	}

	//-----------------------------------------------------------------------------
	// Purpose:
	//-----------------------------------------------------------------------------
	void mousePressEvent( QMouseEvent *pEvent ) override
	{
		PointLight( pEvent->position() );
	}

	//-----------------------------------------------------------------------------
	// Purpose:
	//-----------------------------------------------------------------------------
	void mouseMoveEvent( QMouseEvent *pEvent ) override
	{
		if ( pEvent->buttons() & Qt::LeftButton )
		{
			PointLight( pEvent->position() );
		}
	}

	//-----------------------------------------------------------------------------
	// Purpose:
	//-----------------------------------------------------------------------------
	void mouseDoubleClickEvent( QMouseEvent * ) override
	{
		if ( m_pfnLight )
		{
			m_pfnLight( { -0.45f, 0.55f, 0.7f } );
		}
	}

	//-----------------------------------------------------------------------------
	// Purpose:
	//-----------------------------------------------------------------------------
	void resizeEvent( QResizeEvent * ) override
	{
		if ( m_pGpu )
		{
			m_pGpu->setGeometry( rect() );
		}
		if ( m_pfnResized )
		{
			m_pfnResized();
		}
	}

private:
	//-----------------------------------------------------------------------------
	// Purpose: The light comes from where the pointer is over the square, towards the viewer.
	//-----------------------------------------------------------------------------
	void PointLight( QPointF point )
	{
		const QRect square = Square();
		const float x = std::clamp( static_cast<float>( ( point.x() - square.left() ) / std::max( 1, square.width() ) ) * 2.0f - 1.0f, -1.0f, 1.0f );
		const float y = std::clamp( 1.0f - static_cast<float>( ( point.y() - square.top() ) / std::max( 1, square.height() ) ) * 2.0f, -1.0f, 1.0f );
		if ( m_pfnLight )
		{
			m_pfnLight( xv::Normalized( { x, y, std::sqrt( std::max( 0.08f, 1.0f - x * x - y * y ) ) } ) );
		}
	}
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMaterialView::CMaterialView( const QString &szPath, const QString &szSource, std::span<const std::byte> bytes, material::Reader_t pfnRead, QWidget *pParent )
	: QWidget( pParent )
{
	const std::string szText{ reinterpret_cast<const char *>( bytes.data() ), bytes.size() };
	std::string szError;
	m_Material = material::ParseMaterial( szText, pfnRead, {}, &szError );
	if ( m_Material )
	{
		m_Textures = material::CTextureSet::Load( *m_Material, pfnRead );
		m_pProxies = std::make_unique<material::CProxies>( *m_Material );
	}

	m_pSummary = new QLabel;
	m_pSummary->setObjectName( QStringLiteral( "materialSummary" ) );
	m_pSummary->setWordWrap( true );
	m_pSummary->setTextInteractionFlags( Qt::TextSelectableByMouse );

	// The preview and how it is shown.
	m_pCanvas = new CMaterialCanvas;
	m_pCanvas->m_pfnLight = [ this ]( xv::CVec3 vecLight ) {
		m_vecLight = vecLight;
		Render();
	};
	// Resizing redraws once it settles, not at every step of a dragged splitter.
	auto *pResized = new QTimer( this );
	pResized->setSingleShot( true );
	pResized->setInterval( 40 );
	connect( pResized, &QTimer::timeout, this, [ this ] { Render(); } );
	m_pCanvas->m_pfnResized = [ pResized ] { pResized->start(); };
	m_pShape = new QComboBox;
	m_pShape->setObjectName( QStringLiteral( "materialShape" ) );
	m_pShape->addItems( { tr( "Flat" ), tr( "Ball" ) } );
	m_pShape->setToolTip( tr( "Show the material on a flat square or a ball" ) );
	m_pRepeat = new QSpinBox;
	m_pRepeat->setObjectName( QStringLiteral( "materialRepeat" ) );
	m_pRepeat->setRange( 1, 8 );
	m_pRepeat->setPrefix( QStringLiteral( "×" ) );
	m_pRepeat->setToolTip( tr( "How many times the texture repeats across the square" ) );
	m_pBackdrop = new QComboBox;
	m_pBackdrop->setObjectName( QStringLiteral( "materialBackdrop" ) );
	m_pBackdrop->addItems( { tr( "Checkers" ), tr( "Dark" ), tr( "Light" ) } );
	m_pBackdrop->setToolTip( tr( "What shows through translucent, added and cut-out parts" ) );
	m_pPlay = new QPushButton( tr( "Pause" ) );
	m_pPlay->setObjectName( QStringLiteral( "materialPlay" ) );
	m_pPlay->setCheckable( true );
	m_pPlay->setToolTip( tr( "Run the material's proxies (animated textures, scrolling, pulsing)" ) );
	m_pHdr = new QCheckBox( tr( "HDR" ) );
	m_pHdr->setObjectName( QStringLiteral( "materialHdr" ) );
	m_pHdr->setToolTip( tr( "Show the sky's HDR texture, as the game does with HDR on" ) );
	m_pExposure = new QSlider( Qt::Horizontal );
	m_pExposure->setObjectName( QStringLiteral( "materialExposure" ) );
	m_pExposure->setRange( -40, 40 ); // tenths of a stop
	m_pExposure->setMaximumWidth( 140 );
	m_pExposure->setToolTip( tr( "Exposure, in stops: lower it to see how bright the sun and clouds are" ) );
	m_pExposureLabel = new QLabel;
	m_pExposureLabel->setMinimumWidth( 56 );

	auto *pControls = new QHBoxLayout;
	pControls->addWidget( m_pShape );
	pControls->addWidget( m_pRepeat );
	pControls->addWidget( m_pBackdrop );
	pControls->addWidget( m_pPlay );
	pControls->addWidget( m_pHdr );
	pControls->addWidget( m_pExposure );
	pControls->addWidget( m_pExposureLabel );
	pControls->addStretch();

	m_pNotes = new QLabel;
	m_pNotes->setObjectName( QStringLiteral( "materialNotes" ) );
	m_pNotes->setWordWrap( true );
	m_pNotes->setTextInteractionFlags( Qt::TextSelectableByMouse );
	m_pNotes->setAlignment( Qt::AlignTop | Qt::AlignLeft );
	m_pNotes->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Maximum );

	auto *pPreviewSide = new QWidget;
	auto *pPreviewLayout = new QVBoxLayout( pPreviewSide );
	pPreviewLayout->setContentsMargins( 0, 0, 0, 0 );
	pPreviewLayout->addWidget( m_pCanvas, 1 );
	pPreviewLayout->addLayout( pControls );
	pPreviewLayout->addWidget( m_pNotes );

	// What the material says.
	m_pParameters = new QTreeWidget;
	m_pParameters->setObjectName( QStringLiteral( "materialParameters" ) );
	m_pParameters->setHeaderLabels( { tr( "Parameter" ), tr( "Value" ), tr( "From" ) } );
	m_pParameters->setRootIsDecorated( true );
	m_pParameters->setUniformRowHeights( true );
	m_pParameters->header()->setSectionResizeMode( QHeaderView::Interactive );
	m_pTextureList = new QListWidget;
	m_pTextureList->setObjectName( QStringLiteral( "materialTextures" ) );
	m_pTextureList->setViewMode( QListView::IconMode );
	m_pTextureList->setIconSize( { 96, 96 } );
	m_pTextureList->setResizeMode( QListView::Adjust );
	m_pTextureList->setWordWrap( true );
	m_pTextureList->setGridSize( { 150, 150 } );
	m_pTextureList->setMovement( QListView::Static );
	auto *pTabs = new QTabWidget;
	m_pTabs = pTabs;
	pTabs->setObjectName( QStringLiteral( "materialTabs" ) );
	pTabs->addTab( m_pParameters, tr( "Parameters" ) );
	pTabs->addTab( m_pTextureList, tr( "Textures" ) );
	pTabs->addTab( new CKeyValuesView( szPath, bytes ), tr( "Text" ) );

	auto *pSplitter = new QSplitter( Qt::Horizontal );
	pSplitter->addWidget( pPreviewSide );
	pSplitter->addWidget( pTabs );
	pSplitter->setStretchFactor( 0, 1 );
	pSplitter->setStretchFactor( 1, 1 );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->addWidget( m_pSummary );
	pLayout->addWidget( pSplitter, 1 );

	if ( !m_Material )
	{
		m_pSummary->setText( tr( "<b>%1</b> · not a material: %2" ).arg( szPath.toHtmlEscaped(), Qs( szError ).toHtmlEscaped() ) );
		pPreviewSide->setEnabled( false );
		pTabs->setCurrentIndex( 2 );
		return;
	}

	const bool bHdr = material::HasHdr( *m_Material, m_Textures );
	m_pHdr->setVisible( bHdr );
	m_pExposure->setVisible( bHdr );
	m_pExposureLabel->setVisible( bHdr );
	m_pExposure->setEnabled( false );
	m_pPlay->setVisible( m_pProxies->IsAnimated() );
	m_pPlay->setChecked( false );

	m_pTimer = new QTimer( this );
	m_pTimer->setInterval( 50 );
	connect( m_pTimer, &QTimer::timeout, this, [ this ] {
		m_flTime = m_flClockStart + static_cast<float>( m_Clock.elapsed() ) / 1000.0f;
		Render();
	} );
	connect( m_pPlay, &QPushButton::toggled, this, [ this ]( bool bPaused ) {
		m_pPlay->setText( bPaused ? tr( "Play" ) : tr( "Pause" ) );
		if ( bPaused )
		{
			m_pTimer->stop();
			return;
		}
		m_flClockStart = m_flTime;
		m_Clock.start();
		m_pTimer->start();
	} );
	const auto rerender = [ this ] { Render(); };
	connect( m_pShape, &QComboBox::currentIndexChanged, this, rerender );
	connect( m_pRepeat, &QSpinBox::valueChanged, this, rerender );
	connect( m_pBackdrop, &QComboBox::currentIndexChanged, this, rerender );
	connect( m_pHdr, &QCheckBox::toggled, this, [ this ]( bool bOn ) {
		m_pExposure->setEnabled( bOn );
		Render();
	} );
	connect( m_pExposure, &QSlider::valueChanged, this, rerender );
	connect( m_pParameters, &QTreeWidget::itemActivated, this, [ this ]( QTreeWidgetItem *pItem ) {
		if ( const QString szPath = pItem->data( 0, ROLE_GAME_PATH ).toString(); !szPath.isEmpty() )
		{
			emit OpenRequested( szPath );
		}
	} );
	connect( m_pTextureList, &QListWidget::itemActivated, this, [ this ]( QListWidgetItem *pItem ) {
		if ( const QString szPath = pItem->data( ROLE_GAME_PATH ).toString(); !szPath.isEmpty() )
		{
			emit OpenRequested( szPath );
		}
	} );
	connect( &CInterfaceMode::Instance(), &CInterfaceMode::Changed, this, &CMaterialView::FillParameters );

	FillParameters();
	FillTextures();
	UpdateNotes();
	const xv::particles::CNames &names = ParticleNames();
	const xv::particles::PlainName_t shader = names.Shader( m_Material->m_szShader );
	m_pSummary->setText( tr( "<b>%1</b> · %2 material · %n parameter(s)", nullptr, static_cast<int>( m_Material->m_Parameters.size() ) ).arg( szPath.toHtmlEscaped(), Qs( m_Material->m_szShader ).toHtmlEscaped() ) + tr( " · %n texture(s)", nullptr, static_cast<int>( m_Textures.GetTextures().size() ) ) + ( szSource.isEmpty() ? QString{} : tr( " · from %1" ).arg( szSource.toHtmlEscaped() ) ) );
	m_pSummary->setToolTip( Qs( shader.m_szMeaning ) );
	if ( m_pProxies->IsAnimated() )
	{
		m_Clock.start();
		m_pTimer->start();
	}
	SetUseGpu( CGraphics::Instance().IsGpu() );
	Render();
}

CMaterialView::~CMaterialView() = default;

//-----------------------------------------------------------------------------
// Purpose: The material on a flat square or a ball, as at a time for its proxies.
//-----------------------------------------------------------------------------
xv::texture::CImage CMaterialView::RenderPicture( int nSize, material::Shape_t nShape, material::Backdrop_t nBackdrop, float flTime, xv::CVec3 vecLight ) const
{
	if ( !m_Material || !m_pProxies )
	{
		return {};
	}
	const material::CMaterial now = m_pProxies->At( flTime, [ this ]( std::string_view szParameter ) { return m_Textures.FrameCount( szParameter ); } );
	material::PreviewOptions_t options;
	options.m_nSize = nSize;
	options.m_nShape = nShape;
	options.m_vecLight = vecLight;
	options.m_nBackdrop = nBackdrop;
	return material::RenderPreview( now, m_Textures, options );
}

//-----------------------------------------------------------------------------
// Purpose: A small picture of the texture a parameter names.
//-----------------------------------------------------------------------------
QIcon CMaterialView::TextureIcon( std::string_view szParameter, int nSize, QString *pDescription ) const
{
	const material::Texture_t *pTexture = m_Material ? m_Textures.Find( szParameter ) : nullptr;
	if ( !pTexture )
	{
		return {};
	}
	if ( pDescription )
	{
		*pDescription = DescribeTexture( *pTexture );
	}
	if ( pTexture->m_Frames.empty() || pTexture->m_Frames.front().Empty() )
	{
		return {};
	}
	return QIcon( Thumbnail( *pTexture, nSize ) );
}

//-----------------------------------------------------------------------------
// Purpose: Only the preview and its controls.
//-----------------------------------------------------------------------------
void CMaterialView::SetPreviewOnly( bool bOnly )
{
	m_pTabs->setVisible( !bOnly );
	m_pSummary->setVisible( !bOnly );
}

//-----------------------------------------------------------------------------
// Purpose: Draws on the graphics card, at the full size of the square, or in software.
//-----------------------------------------------------------------------------
void CMaterialView::SetUseGpu( bool bOn )
{
	delete m_pCanvas->m_pGpu;
	m_pCanvas->m_pGpu = nullptr;
#ifdef VOMIT_TOOLS_GPU
	if ( bOn )
	{
		auto *pView = new CGpuView( std::make_unique<CMaterialRenderer>( [ this ] {
			CMaterialRenderer::Frame_t frame;
			if ( m_Now )
			{
				const QRect square = m_pCanvas->DeviceSquare();
				frame.m_pMaterial = &*m_Now;
				frame.m_pTextures = &m_Textures;
				frame.m_Options = Options();
				frame.m_Options.m_nSize = std::max( 1, square.width() );
				frame.m_Corner = square.topLeft();
			}
			frame.m_Background = palette().color( QPalette::Window );
			return frame;
		} ),
			m_pCanvas );
		pView->setObjectName( QStringLiteral( "materialGpuView" ) );
		pView->setAttribute( Qt::WA_TransparentForMouseEvents );
		pView->setGeometry( m_pCanvas->rect() );
		// If the graphics card can't be used after all, draw in software.
		connect( pView, &QRhiWidget::renderFailed, this, [ this ] {
			QTimer::singleShot( 0, this, [ this ] {
				SetUseGpu( false );
				Render();
			} );
		} );
		m_pCanvas->m_pGpu = pView;
		pView->show();
	}
#else
	Q_UNUSED( bOn );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: The preview as drawn now.
//-----------------------------------------------------------------------------
QImage CMaterialView::GetPreview() const
{
#ifdef VOMIT_TOOLS_GPU
	if ( auto *pView = qobject_cast<QRhiWidget *>( m_pCanvas->m_pGpu ) )
	{
		// A hidden widget has no window render target. Qt's temporary renderer can return stale
		// snapshots on some drivers or crash during shutdown, so draw this snapshot in software.
		if ( !pView->isVisible() )
		{
			return m_Now ? ToQImage( material::RenderPreview( *m_Now, m_Textures, Options() ) ) : QImage{};
		}
		return pView->grabFramebuffer().copy( m_pCanvas->DeviceSquare() );
	}
#endif
	return m_Preview;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
material::PreviewOptions_t CMaterialView::Options() const
{
	material::PreviewOptions_t options;
	// Smaller while animating, so playing stays smooth; the canvas scales it up.
	options.m_nSize = m_pTimer && m_pTimer->isActive() ? std::min( 320, m_pCanvas->RenderSize() ) : m_pCanvas->RenderSize();
	options.m_nShape = m_pShape->currentIndex() == 1 ? material::Shape_t::SHAPE_SPHERE : material::Shape_t::SHAPE_FLAT;
	options.m_vecLight = m_vecLight;
	options.m_flRepeat = static_cast<float>( m_pRepeat->value() );
	options.m_nBackdrop = static_cast<material::Backdrop_t>( m_pBackdrop->currentIndex() );
	options.m_bHdr = m_pHdr->isVisible() && m_pHdr->isChecked();
	options.m_flExposure = static_cast<float>( m_pExposure->value() ) / 10.0f;
	return options;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialView::RenderAt( float flTime )
{
	m_flTime = flTime;
	Render();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialView::Render()
{
	if ( !m_Material || !m_pProxies )
	{
		return;
	}
	m_Now = m_pProxies->At( m_flTime, [ this ]( std::string_view szParameter ) { return m_Textures.FrameCount( szParameter ); } );
	if ( m_pCanvas->m_pGpu )
	{
		m_pCanvas->m_pGpu->update();
	}
	else
	{
		m_Preview = ToQImage( material::RenderPreview( *m_Now, m_Textures, Options() ) );
		m_pCanvas->m_Image = m_Preview;
		m_pCanvas->update();
	}
	m_pExposureLabel->setText( tr( "%1 stops" ).arg( static_cast<double>( m_pExposure->value() ) / 10.0, 0, 'f', 1 ) );
}

//-----------------------------------------------------------------------------
// Purpose: The parameters as the material ends up with them, then its proxies and how it was read.
//          The simple interface names them in words, with the key in the tooltip.
//-----------------------------------------------------------------------------
void CMaterialView::FillParameters()
{
	m_pParameters->clear();
	if ( !m_Material )
	{
		return;
	}
	const bool bSimple = CInterfaceMode::Instance().IsSimple();
	const xv::particles::CNames &names = ParticleNames();
	for ( const material::Parameter_t &parameter : m_Material->m_Parameters )
	{
		const xv::particles::PlainName_t plain = names.MaterialParameter( parameter.m_szKey );
		QString szValue = Qs( parameter.m_szValue );
		if ( bSimple )
		{
			for ( const xv::particles::NamedValue_t &choice : names.Choices( parameter.m_szKey ) )
			{
				if ( choice.m_szValue == parameter.m_szValue )
				{
					szValue = tr( "%1 (%2)" ).arg( szValue, Qs( choice.m_szName ) );
				}
			}
		}
		auto *pItem = new QTreeWidgetItem( m_pParameters, { bSimple ? Qs( plain.m_szName ) : Qs( parameter.m_szKey ), szValue, Qs( parameter.m_szFrom ) } );
		const QString szMeaning = Qs( plain.m_szMeaning );
		pItem->setToolTip( 0, bSimple ? ( szMeaning.isEmpty() ? Qs( parameter.m_szKey ) : QStringLiteral( "%1\n%2" ).arg( Qs( parameter.m_szKey ), szMeaning ) ) : szMeaning );
		if ( const material::Texture_t *pTexture = material::IsTexture( parameter.m_szKey, parameter.m_szValue ) ? m_Textures.Find( parameter.m_szKey ) : nullptr )
		{
			pItem->setIcon( 1, QIcon( Thumbnail( *pTexture, 32 ) ) );
			pItem->setToolTip( 1, QStringLiteral( "%1\n%2" ).arg( Qs( pTexture->m_szPath.empty() ? pTexture->m_szValue : pTexture->m_szPath ), DescribeTexture( *pTexture ) ) );
			if ( !pTexture->m_szProblem.empty() )
			{
				pItem->setForeground( 1, QColor( 200, 60, 60 ) );
			}
			if ( !pTexture->m_szPath.empty() && pTexture->m_Info )
			{
				pItem->setData( 0, ROLE_GAME_PATH, Qs( pTexture->m_szPath ) );
			}
		}
	}
	if ( !m_Material->m_Proxies.empty() )
	{
		auto *pProxies = new QTreeWidgetItem( m_pParameters, { tr( "Proxies" ) } );
		const auto &skipped = m_pProxies->GetSkipped();
		for ( const material::Proxy_t &proxy : m_Material->m_Proxies )
		{
			const bool bRuns = std::ranges::find( skipped, proxy.m_szName ) == skipped.end();
			auto *pProxy = new QTreeWidgetItem( pProxies, { Qs( proxy.m_szName ), bRuns ? tr( "runs in the preview" ) : tr( "needs the game; not run" ) } );
			if ( !bRuns )
			{
				pProxy->setForeground( 1, palette().color( QPalette::Disabled, QPalette::Text ) );
			}
			for ( const auto &[ szKey, szValue ] : proxy.m_Settings )
			{
				new QTreeWidgetItem( pProxy, { Qs( szKey ), Qs( szValue ) } );
			}
		}
		pProxies->setExpanded( true );
	}
	if ( !m_Material->m_Notes.empty() )
	{
		auto *pNotes = new QTreeWidgetItem( m_pParameters, { tr( "How it was read" ) } );
		for ( const std::string &szNote : m_Material->m_Notes )
		{
			auto *pNote = new QTreeWidgetItem( pNotes, { Qs( szNote ) } );
			pNote->setFirstColumnSpanned( true );
			// "It is a patch of materials/x.vmt": double-click opens the included material.
			if ( const auto nAt = szNote.find( "materials/" ); nAt != std::string::npos && szNote.ends_with( ".vmt" ) )
			{
				pNote->setData( 0, ROLE_GAME_PATH, Qs( szNote.substr( nAt ) ) );
			}
		}
		pNotes->setExpanded( true );
	}
	m_pParameters->resizeColumnToContents( 0 );
	m_pParameters->setColumnWidth( 1, std::max( 160, m_pParameters->columnWidth( 1 ) ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMaterialView::FillTextures()
{
	m_pTextureList->clear();
	for ( const material::Texture_t &texture : m_Textures.GetTextures() )
	{
		auto *pItem = new QListWidgetItem( QIcon( Thumbnail( texture, 96 ) ), QStringLiteral( "%1\n%2" ).arg( Qs( texture.m_szParameter ), DescribeTexture( texture ) ), m_pTextureList );
		pItem->setToolTip( Qs( texture.m_szPath.empty() ? texture.m_szValue : texture.m_szPath ) );
		if ( !texture.m_szPath.empty() && texture.m_Info )
		{
			pItem->setData( ROLE_GAME_PATH, Qs( texture.m_szPath ) );
		}
		if ( !texture.m_szProblem.empty() )
		{
			pItem->setForeground( QColor( 200, 60, 60 ) );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: What the preview leaves out, and the proxies it doesn't run.
//-----------------------------------------------------------------------------
void CMaterialView::UpdateNotes()
{
	QStringList lines;
	for ( const std::string &szNote : material::PreviewNotes( *m_Material, m_Textures ) )
	{
		lines << Qs( szNote ).toHtmlEscaped();
	}
	if ( !m_pProxies->GetSkipped().empty() )
	{
		QStringList skipped;
		for ( const std::string &szName : m_pProxies->GetSkipped() )
		{
			skipped << Qs( szName ).toHtmlEscaped();
		}
		lines << tr( "Proxies that need the game aren't run: %1." ).arg( skipped.join( QStringLiteral( ", " ) ) );
	}
	m_pNotes->setText( lines.isEmpty() ? QString{} : QStringLiteral( "<small>%1</small>" ).arg( lines.join( QStringLiteral( "<br>" ) ) ) );
	m_pNotes->setVisible( !lines.isEmpty() );
}
