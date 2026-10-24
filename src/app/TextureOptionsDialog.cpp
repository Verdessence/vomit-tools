//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See TextureOptionsDialog.h.
//
//===========================================================================//
#include "TextureOptionsDialog.h"

#include <algorithm>
#include <array>
#include <bit>
#include <tuple>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ChoiceBoxes.h"
#include "InterfaceMode.h"
#include "Language.h"
#include "TexturePresets.h"
#include "sky/Sky.h"

using namespace xv::texture;

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Qs( std::string_view szText )
{
	return QString::fromUtf8( szText.data(), static_cast<qsizetype>( szText.size() ) );
}

// The formats offered, in groups; a separator goes between groups.
constexpr std::array<std::array<VtfFormat_t, 7>, 7> FORMAT_GROUPS{ {
	{ VtfFormat_t::DXT1, VtfFormat_t::DXT5, VtfFormat_t::DXT1_ONEBITALPHA, VtfFormat_t::DXT3, VtfFormat_t::VTF_FORMAT_NONE, VtfFormat_t::VTF_FORMAT_NONE, VtfFormat_t::VTF_FORMAT_NONE },
	{ VtfFormat_t::BGR888, VtfFormat_t::BGRA8888, VtfFormat_t::BGRX8888, VtfFormat_t::RGB888, VtfFormat_t::RGBA8888, VtfFormat_t::ABGR8888, VtfFormat_t::ARGB8888 },
	{ VtfFormat_t::BGR565, VtfFormat_t::RGB565, VtfFormat_t::BGRA4444, VtfFormat_t::BGRA5551, VtfFormat_t::BGRX5551, VtfFormat_t::VTF_FORMAT_NONE, VtfFormat_t::VTF_FORMAT_NONE },
	{ VtfFormat_t::I8, VtfFormat_t::IA88, VtfFormat_t::A8, VtfFormat_t::VTF_FORMAT_NONE, VtfFormat_t::VTF_FORMAT_NONE, VtfFormat_t::VTF_FORMAT_NONE, VtfFormat_t::VTF_FORMAT_NONE },
	{ VtfFormat_t::UV88, VtfFormat_t::UVWQ8888, VtfFormat_t::UVLX8888, VtfFormat_t::VTF_FORMAT_NONE, VtfFormat_t::VTF_FORMAT_NONE, VtfFormat_t::VTF_FORMAT_NONE, VtfFormat_t::VTF_FORMAT_NONE },
	{ VtfFormat_t::RGB888_BLUESCREEN, VtfFormat_t::BGR888_BLUESCREEN, VtfFormat_t::VTF_FORMAT_NONE, VtfFormat_t::VTF_FORMAT_NONE, VtfFormat_t::VTF_FORMAT_NONE, VtfFormat_t::VTF_FORMAT_NONE, VtfFormat_t::VTF_FORMAT_NONE },
	{ VtfFormat_t::RGBA16161616F, VtfFormat_t::RGBA16161616, VtfFormat_t::R32F, VtfFormat_t::RGB323232F, VtfFormat_t::RGBA32323232F, VtfFormat_t::VTF_FORMAT_NONE, VtfFormat_t::VTF_FORMAT_NONE },
} };
constexpr std::array<const char *, 7> FORMAT_GROUP_NAMES{ QT_TRANSLATE_NOOP( "CTextureOptionsDialog", "Compressed" ), QT_TRANSLATE_NOOP( "CTextureOptionsDialog", "8 bits a channel" ),
	QT_TRANSLATE_NOOP( "CTextureOptionsDialog", "Fewer bits a channel" ), QT_TRANSLATE_NOOP( "CTextureOptionsDialog", "Grey and alpha" ), QT_TRANSLATE_NOOP( "CTextureOptionsDialog", "Bump offsets (DuDv)" ),
	QT_TRANSLATE_NOOP( "CTextureOptionsDialog", "Bluescreen" ), QT_TRANSLATE_NOOP( "CTextureOptionsDialog", "HDR and 16 bits" ) };

// Flags people set by hand; the simple interface shows only the first ten unless others are set.
// The last three are set from the pictures and the kind when a texture is made.
constexpr std::array<std::pair<std::uint32_t, std::string_view>, 23> FLAGS{ {
	{ vtf_flag::CLAMPS, "CLAMPS" },
	{ vtf_flag::CLAMPT, "CLAMPT" },
	{ vtf_flag::NOMIP, "NOMIP" },
	{ vtf_flag::NOLOD, "NOLOD" },
	{ vtf_flag::POINTSAMPLE, "POINTSAMPLE" },
	{ vtf_flag::TRILINEAR, "TRILINEAR" },
	{ vtf_flag::ANISOTROPIC, "ANISOTROPIC" },
	{ vtf_flag::NORMAL, "NORMAL" },
	{ 0x8000000, "SSBUMP" },
	{ vtf_flag::HINT_DXT5, "HINT_DXT5" },
	{ 0x2000000, "CLAMPU" },
	{ vtf_flag::ALL_MIPS, "ALL_MIPS" },
	{ vtf_flag::PROCEDURAL, "PROCEDURAL" },
	{ 0x20000000, "BORDER" },
	{ 0x20000, "NODEBUGOVERRIDE" },
	{ 0x40000, "SINGLECOPY" },
	{ 0x4000000, "VERTEXTEXTURE" },
	{ 0x8000, "RENDERTARGET" },
	{ 0x10000, "DEPTHRENDERTARGET" },
	{ 0x800000, "NODEPTHBUFFER" },
	{ vtf_flag::ONEBITALPHA, "ONEBITALPHA" },
	{ vtf_flag::EIGHTBITALPHA, "EIGHTBITALPHA" },
	{ vtf_flag::ENVMAP, "ENVMAP" },
} };
constexpr std::size_t COMMON_FLAG_COUNT = 10;
constexpr std::uint32_t AUTOMATIC_FLAGS = vtf_flag::ONEBITALPHA | vtf_flag::EIGHTBITALPHA | vtf_flag::ENVMAP;

// Cube map faces in the order a VTF stores them, by the suffix skies use.
constexpr std::array<std::string_view, 6> CUBE_SUFFIXES{ "rt", "lf", "bk", "ft", "up", "dn" };

//-----------------------------------------------------------------------------
// Purpose: Which cube face a picture's name says it is (0-5 in VTF order), or -1.
//-----------------------------------------------------------------------------
int CubeFaceOf( const QString &szName )
{
	const QString szBase = QFileInfo{ szName }.completeBaseName().toLower();
	for ( std::size_t i = 0; i < CUBE_SUFFIXES.size(); i++ )
	{
		if ( szBase.endsWith( Qs( CUBE_SUFFIXES[ i ] ) ) )
		{
			return static_cast<int>( i );
		}
	}
	return -1;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool HasClearParts( const NamedSource_t &source )
{
	const auto &pixels = source.m_Source.m_Image.m_Pixels;
	for ( std::size_t i = 3; i < pixels.size(); i += 4 )
	{
		if ( pixels[ i ] != 255 )
		{
			return true;
		}
	}
	return false;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The starting choices for pictures.
//-----------------------------------------------------------------------------
BuildOptions_t CTextureOptionsDialog::DefaultsFor( const std::vector<NamedSource_t> &sources )
{
	BuildOptions_t options;
	const bool bAlpha = std::ranges::any_of( sources, HasClearParts );
	const bool bLight = std::ranges::any_of( sources, []( const NamedSource_t &source ) { return !source.m_Source.m_Light.Empty(); } );
	options.m_Write.m_nFormat = bLight ? VtfFormat_t::RGBA16161616F : bAlpha ? VtfFormat_t::DXT5
																			 : VtfFormat_t::DXT1;
	options.m_Write.m_nVersionMinor = 2;
	options.m_Write.m_bMipmaps = true;
	options.m_Write.m_nMipFilter = MipFilter_t::MIP_FILTER_KAISER;
	options.m_Write.m_bLowRes = true;
	options.m_nResize = ResizeMode_t::RESIZE_NEAREST;
	return options;
}

//-----------------------------------------------------------------------------
// Purpose: initial gives the starting choices.
//-----------------------------------------------------------------------------
CTextureOptionsDialog::CTextureOptionsDialog( Mode_t nMode, std::vector<NamedSource_t> sources, const BuildOptions_t &initial, QWidget *pParent )
	: QDialog( pParent ),
	  m_nMode( nMode ),
	  m_Sources( std::move( sources ) ),
	  m_Initial( initial )
{
	setWindowTitle( nMode == MODE_IMPORT ? tr( "Create VTF" ) : nMode == MODE_BATCH ? tr( "Settings for Each Picture" )
																					: tr( "Convert Texture" ) );
	const bool bSimple = CInterfaceMode::Instance().IsSimple();
	const xv::particles::CNames &names = ParticleNames();

	// Six pictures named like a sky's faces start as a cube map, in the VTF's order; one picture
	// twice as wide as tall is offered as a panorama.
	bool bCube = nMode == MODE_CONVERT ? initial.m_nFaces == 6 : false;
	if ( nMode == MODE_IMPORT && m_Sources.size() == 6 )
	{
		std::array<int, 6> faces{};
		for ( std::size_t i = 0; i < 6; i++ )
		{
			faces[ i ] = CubeFaceOf( m_Sources[ i ].m_szName );
		}
		std::array<int, 6> sorted = faces;
		std::ranges::sort( sorted );
		if ( sorted == std::array<int, 6>{ 0, 1, 2, 3, 4, 5 } )
		{
			std::ranges::stable_sort( m_Sources, {}, []( const NamedSource_t &source ) { return CubeFaceOf( source.m_szName ); } );
			bCube = true;
		}
	}

	// Presets, at the top: picking one sets everything below.
	auto *pPresetRow = new QHBoxLayout;
	m_pPreset = new QComboBox;
	m_pPreset->setObjectName( QStringLiteral( "texturePreset" ) );
	auto *pSavePreset = new QPushButton( tr( "Save…" ) );
	pSavePreset->setToolTip( tr( "Keep these choices as a preset of your own" ) );
	auto *pDeletePreset = new QPushButton( tr( "Delete" ) );
	pPresetRow->addWidget( new QLabel( tr( "Preset" ) ) );
	pPresetRow->addWidget( m_pPreset, 1 );
	pPresetRow->addWidget( pSavePreset );
	pPresetRow->addWidget( pDeletePreset );

	// ---- Basics ---------------------------------------------------------------------------
	auto *pBasics = new QWidget;
	auto *pForm = new QFormLayout( pBasics );
	pForm->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );
	if ( nMode == MODE_IMPORT )
	{
		m_pList = new QListWidget;
		m_pList->setObjectName( QStringLiteral( "textureSources" ) );
		m_pList->setMaximumHeight( 120 );
		for ( std::size_t i = 0; i < m_Sources.size(); i++ )
		{
			const auto &image = m_Sources[ i ].m_Source.m_Image;
			auto *pItem = new QListWidgetItem( QStringLiteral( "%1  (%2×%3)" ).arg( m_Sources[ i ].m_szName ).arg( image.m_nWidth ).arg( image.m_nHeight ), m_pList );
			pItem->setData( Qt::UserRole, static_cast<int>( i ) );
		}
		auto *pUp = new QPushButton( tr( "Up" ) );
		auto *pDown = new QPushButton( tr( "Down" ) );
		connect( pUp, &QPushButton::clicked, this, [ this ] { MoveSelected( -1 ); } );
		connect( pDown, &QPushButton::clicked, this, [ this ] { MoveSelected( 1 ); } );
		auto *pButtons = new QVBoxLayout;
		pButtons->addWidget( pUp );
		pButtons->addWidget( pDown );
		pButtons->addStretch();
		auto *pPictures = new QHBoxLayout;
		pPictures->addWidget( m_pList, 1 );
		pPictures->addLayout( pButtons );
		pForm->addRow( tr( "Pictures" ), pPictures );

		m_pKind = new QComboBox;
		m_pKind->setObjectName( QStringLiteral( "textureKind" ) );
		m_pKind->addItem( tr( "One image (the first picture)" ), KIND_SINGLE );
		m_pKind->addItem( tr( "Animation: a frame per picture" ), KIND_FRAMES );
		m_pKind->addItem( tr( "Cube map: 6 pictures a frame, rt lf bk ft up dn" ), KIND_CUBE );
		m_pKind->addItem( tr( "Cube map from a panorama (equirectangular, e.g. an HDRI)" ), KIND_PANORAMA );
		m_pKind->addItem( tr( "Volume: a slice per picture (7.2 or later)" ), KIND_VOLUME );
		const auto &first = m_Sources.empty() ? CImage{} : m_Sources.front().m_Source.m_Image;
		const bool bPanorama = m_Sources.size() == 1 && first.m_nWidth == first.m_nHeight * 2 && first.m_nWidth >= 256;
		m_pKind->setCurrentIndex( m_pKind->findData( bCube ? KIND_CUBE : m_Sources.size() > 1 ? KIND_FRAMES
																							  : KIND_SINGLE ) );
		pForm->addRow( tr( "Make" ), m_pKind );

		m_pPanoramaSize = new QComboBox;
		for ( int nSize = 4096; nSize >= 64; nSize /= 2 )
		{
			m_pPanoramaSize->addItem( tr( "%1 per face" ).arg( nSize ), nSize );
		}
		m_pPanoramaSize->setCurrentIndex( std::max( 0, m_pPanoramaSize->findData( std::clamp( NearestPowerOfTwo( std::max( 1, first.m_nWidth / 4 ) ), 64, 4096 ) ) ) );
		m_pPanoramaYaw = new QDoubleSpinBox;
		m_pPanoramaYaw->setRange( -180.0, 180.0 );
		m_pPanoramaYaw->setSuffix( QStringLiteral( "°" ) );
		m_pPanoramaYaw->setPrefix( tr( "turned " ) );
		m_pPanoramaYaw->setToolTip( tr( "The panorama's middle faces along +X (yaw 0 in Hammer); turn it to face elsewhere" ) );
		auto *pPanoramaRow = new QHBoxLayout;
		pPanoramaRow->addWidget( m_pPanoramaSize );
		pPanoramaRow->addWidget( m_pPanoramaYaw );
		pPanoramaRow->addStretch();
		m_pPanoramaLabel = new QLabel( tr( "Panorama" ) );
		pForm->addRow( m_pPanoramaLabel, pPanoramaRow );
		if ( bPanorama )
		{
			m_pKind->setItemText( m_pKind->findData( KIND_PANORAMA ), m_pKind->itemText( m_pKind->findData( KIND_PANORAMA ) ) + tr( " · this picture looks like one" ) );
		}
	}

	// Each format next to what it is for, under the group it belongs to.
	const auto fillFormats = [ &names ]( QComboBox *pBox, bool bWithSame ) {
		std::vector<Choice_t> choices;
		if ( bWithSame )
		{
			choices.push_back( { tr( "the same" ), tr( "As Format, see-through parts or not" ), static_cast<int>( VtfFormat_t::VTF_FORMAT_NONE ) } );
		}
		for ( std::size_t i = 0; i < FORMAT_GROUPS.size(); i++ )
		{
			std::vector<Choice_t> group;
			for ( const VtfFormat_t nFormat : FORMAT_GROUPS[ i ] )
			{
				if ( nFormat == VtfFormat_t::VTF_FORMAT_NONE || ( bWithSame && !HasAlpha( nFormat ) ) )
				{
					continue;
				}
				const QString szName = Qs( FormatName( nFormat ) );
				group.push_back( { szName, Qs( names.Texture( "format:" + szName.toStdString() ).m_szMeaning ), static_cast<int>( nFormat ) } );
			}
			if ( !group.empty() )
			{
				choices.push_back( { tr( FORMAT_GROUP_NAMES[ i ] ), {}, {}, true } );
				choices.insert( choices.end(), group.begin(), group.end() );
			}
		}
		FillListBox( pBox, choices );
	};
	m_pFormat = new QComboBox;
	m_pFormat->setObjectName( QStringLiteral( "textureFormat" ) );
	fillFormats( m_pFormat, false );
	m_pAlphaFormat = new QComboBox;
	m_pAlphaFormat->setObjectName( QStringLiteral( "textureAlphaFormat" ) );
	fillFormats( m_pAlphaFormat, true );
	m_pAlphaFormat->setToolTip( tr( "Pictures with see-through parts are made in this format instead (only formats that keep alpha)" ) );
	m_pFormatNote = new QLabel;
	m_pFormatNote->setWordWrap( true );
	m_pFormatNote->setEnabled( false );
	pForm->addRow( tr( "Format" ), m_pFormat );
	pForm->addRow( QString{}, m_pFormatNote );
	pForm->addRow( tr( "With alpha" ), m_pAlphaFormat );

	m_pVersion = new QComboBox;
	for ( int nMinor = 1; nMinor <= 5; nMinor++ )
	{
		m_pVersion->addItem( QStringLiteral( "7.%1" ).arg( nMinor ), nMinor );
	}
	m_pVersion->setToolTip( tr( "7.2 opens in every Source game and tool. 7.3 and later can carry sprite sheets and other resources." ) );
	pForm->addRow( tr( "Version" ), m_pVersion );

	m_pResize = new QComboBox;
	m_pResize->addItem( tr( "Keep the size" ), static_cast<int>( ResizeMode_t::RESIZE_KEEP ) );
	m_pResize->addItem( tr( "Nearest power of two" ), static_cast<int>( ResizeMode_t::RESIZE_NEAREST ) );
	m_pResize->addItem( tr( "Next power of two up" ), static_cast<int>( ResizeMode_t::RESIZE_UP ) );
	m_pResize->addItem( tr( "Next power of two down" ), static_cast<int>( ResizeMode_t::RESIZE_DOWN ) );
	m_pResize->addItem( tr( "Nearest multiple of 4" ), static_cast<int>( ResizeMode_t::RESIZE_MULTIPLE_OF_4 ) );
	m_pResize->addItem( tr( "Exact size" ), static_cast<int>( ResizeMode_t::RESIZE_EXACT ) );
	m_pResize->setToolTip( tr( "The game expects sides that are powers of two (256, 512, 1024…); DXT needs multiples of 4." ) );
	m_pExactWidth = new QSpinBox;
	m_pExactWidth->setObjectName( QStringLiteral( "textureExactWidth" ) );
	m_pExactHeight = new QSpinBox;
	m_pExactHeight->setObjectName( QStringLiteral( "textureExactHeight" ) );
	for ( QSpinBox *pSide : { m_pExactWidth, m_pExactHeight } )
	{
		pSide->setRange( 1, 65535 );
		pSide->setAccelerated( true );
	}
	m_pExactWidth->setToolTip( tr( "Width in texels" ) );
	m_pExactHeight->setToolTip( tr( "Height in texels" ) );
	m_pExactWidth->setSuffix( tr( " wide" ) );
	m_pExactHeight->setSuffix( tr( " high" ) );
	m_pExactBy = new QLabel( QStringLiteral( "×" ) );
	m_pMaxSize = new QComboBox;
	m_pMaxSize->addItem( tr( "no limit" ), 0 );
	for ( int nSize = 8192; nSize >= 16; nSize /= 2 )
	{
		m_pMaxSize->addItem( tr( "at most %1" ).arg( nSize ), nSize );
	}
	m_pKeepAspect = new QCheckBox( tr( "Keep shape" ) );
	m_pKeepAspect->setToolTip( tr( "Size the longer side and scale the shorter to match" ) );
	m_pSize = new QLabel;
	auto *pSizeRow = new QHBoxLayout;
	pSizeRow->addWidget( m_pResize );
	pSizeRow->addWidget( m_pExactWidth );
	pSizeRow->addWidget( m_pExactBy );
	pSizeRow->addWidget( m_pExactHeight );
	pSizeRow->addWidget( m_pMaxSize );
	pSizeRow->addWidget( m_pKeepAspect );
	pSizeRow->addWidget( m_pSize, 1 );
	pForm->addRow( tr( "Size" ), pSizeRow );

	m_pMipmaps = new QCheckBox( tr( "Mipmaps" ) );
	m_pMipmaps->setToolTip( tr( "Smaller copies the game draws far away; without them distant surfaces shimmer." ) );
	m_pMipFilter = new QComboBox;
	m_pMipFilter->setObjectName( QStringLiteral( "textureMipFilter" ) );
	m_pMipFilter->addItem( tr( "Kaiser (sharpest)" ), static_cast<int>( MipFilter_t::MIP_FILTER_KAISER ) );
	m_pMipFilter->addItem( tr( "Mitchell (sharp)" ), static_cast<int>( MipFilter_t::MIP_FILTER_MITCHELL ) );
	m_pMipFilter->addItem( tr( "Triangle" ), static_cast<int>( MipFilter_t::MIP_FILTER_TRIANGLE ) );
	m_pMipFilter->addItem( tr( "Box (softest)" ), static_cast<int>( MipFilter_t::MIP_FILTER_BOX ) );
	m_pMipFilter->setToolTip( tr( "How each mipmap is made. Edges wrap round unless Clamp S / Clamp T are set." ) );
	auto *pMipRow = new QHBoxLayout;
	pMipRow->addWidget( m_pMipmaps );
	pMipRow->addWidget( m_pMipFilter );
	pMipRow->addStretch();
	pForm->addRow( QString{}, pMipRow );

	m_pFlags = MakeFlagList( initial.m_Write.m_nFlags, false );
	m_pFlags->setMinimumHeight( 160 );
	pForm->addRow( tr( "Flags" ), m_pFlags );

	// ---- Advanced -------------------------------------------------------------------------
	auto *pAdvanced = new QWidget;
	auto *pMore = new QFormLayout( pAdvanced );
	pMore->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );
	m_pLowRes = new QCheckBox( tr( "Thumbnail" ) );
	m_pLowRes->setToolTip( tr( "The small DXT1 copy tools show as a preview." ) );
	m_pAlphaFlags = new QCheckBox( tr( "Alpha flags from the pictures" ) );
	m_pAlphaFlags->setToolTip( tr( "Sets One-Bit Alpha or Eight-Bit Alpha by what the alpha holds, as Valve's tools do." ) );
	m_pStripAlpha = new QCheckBox( tr( "Strip alpha" ) );
	m_pStripAlpha->setToolTip( tr( "Make every picture solid first, so an opaque format loses nothing" ) );
	pMore->addRow( QString{}, m_pLowRes );
	pMore->addRow( QString{}, m_pAlphaFlags );
	pMore->addRow( QString{}, m_pStripAlpha );

	m_pHeightToNormal = new QCheckBox( tr( "Normal map from height" ) );
	m_pHeightToNormal->setObjectName( QStringLiteral( "textureHeightToNormal" ) );
	m_pHeightToNormal->setToolTip( tr( "Take the pictures as height (brighter is higher) and make a normal map for $bumpmap. Sets Normal Map." ) );
	m_pNormalScale = new QDoubleSpinBox;
	m_pNormalScale->setRange( 0.1, 50.0 );
	m_pNormalScale->setSingleStep( 0.5 );
	m_pNormalScale->setPrefix( tr( "steepness " ) );
	m_pGreenUp = new QCheckBox( tr( "Green up" ) );
	m_pGreenUp->setToolTip( tr( "Off: green points down the picture (DirectX style), as Source's normal maps usually are. On: OpenGL style." ) );
	auto *pNormal = new QHBoxLayout;
	pNormal->addWidget( m_pHeightToNormal );
	pNormal->addWidget( m_pNormalScale );
	pNormal->addWidget( m_pGreenUp );
	pNormal->addStretch();
	pMore->addRow( tr( "Normal map" ), pNormal );

	m_pDistanceAlpha = new QCheckBox( tr( "Distance alpha ($distancealpha)" ) );
	m_pDistanceAlpha->setObjectName( QStringLiteral( "textureDistanceAlpha" ) );
	m_pDistanceAlpha->setToolTip( tr( "Alpha becomes the distance from the shape's edge, so signs and icons stay sharp up close. Make it from a picture several times the size." ) );
	m_pDistanceSpread = new QDoubleSpinBox;
	m_pDistanceSpread->setRange( 0.5, 64.0 );
	m_pDistanceSpread->setPrefix( tr( "spread " ) );
	m_pDistanceSpread->setSuffix( tr( " texels" ) );
	auto *pDistance = new QHBoxLayout;
	pDistance->addWidget( m_pDistanceAlpha );
	pDistance->addWidget( m_pDistanceSpread );
	pDistance->addStretch();
	pMore->addRow( tr( "Alpha" ), pDistance );

	m_pBumpScale = new QDoubleSpinBox;
	m_pBumpScale->setRange( -100.0, 100.0 );
	m_pBumpScale->setDecimals( 3 );
	m_pBumpScale->setSingleStep( 0.1 );
	m_pFirstFrame = new QSpinBox;
	m_pFirstFrame->setRange( 0, 65534 );
	pMore->addRow( tr( "Bump scale" ), m_pBumpScale );
	pMore->addRow( tr( "First frame" ), m_pFirstFrame );

	auto *pTabs = new QTabWidget;
	pTabs->addTab( pBasics, tr( "Basics" ) );
	pTabs->addTab( pAdvanced, tr( "Advanced" ) );

	m_pNotes = new QLabel;
	m_pNotes->setObjectName( QStringLiteral( "textureNotes" ) );
	m_pNotes->setWordWrap( true );

	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	pButtons->button( QDialogButtonBox::Ok )->setText( nMode == MODE_IMPORT ? tr( "Create" ) : nMode == MODE_BATCH ? tr( "OK" )
																												   : tr( "Convert" ) );
	connect( pButtons, &QDialogButtonBox::accepted, this, [ this ] {
		SetLastTexturePreset( m_pPreset->currentData().toString() );
		accept();
	} );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->addLayout( pPresetRow );
	pLayout->addWidget( pTabs );
	pLayout->addWidget( m_pNotes );
	pLayout->addWidget( pButtons );
	resize( 780, sizeHint().height() );

	// An exact size starts as the pictures' own.
	if ( !m_Sources.empty() )
	{
		const SourceImage_t &first = m_Sources.front().m_Source;
		const bool bLight = !first.m_Light.Empty();
		m_pExactWidth->setValue( bLight ? first.m_Light.m_nWidth : first.m_Image.m_nWidth );
		m_pExactHeight->setValue( bLight ? first.m_Light.m_nHeight : first.m_Image.m_nHeight );
	}
	ApplyOptions( initial );
	FillPresets( {} );
	connect( m_pPreset, &QComboBox::activated, this, [ this ] {
		const QString szName = m_pPreset->currentData().toString();
		for ( const TexturePreset_t &preset : TexturePresets() )
		{
			if ( preset.m_szName == szName )
			{
				ApplyOptions( preset.m_Options );
			}
		}
	} );
	connect( pSavePreset, &QPushButton::clicked, this, [ this ] {
		bool bOk = false;
		const QString szName = QInputDialog::getText( this, tr( "Save Preset" ), tr( "Name:" ), QLineEdit::Normal, {}, &bOk ).trimmed();
		if ( !bOk || szName.isEmpty() )
		{
			return;
		}
		if ( !SaveTexturePreset( szName, GetOptions() ) )
		{
			QMessageBox::information( this, tr( "Save Preset" ), tr( "A built-in preset has that name; pick another." ) );
			return;
		}
		FillPresets( szName );
	} );
	connect( pDeletePreset, &QPushButton::clicked, this, [ this ] {
		const QString szName = m_pPreset->currentData().toString();
		if ( !szName.isEmpty() )
		{
			DeleteTexturePreset( szName );
			FillPresets( {} );
		}
	} );
	connect( m_pPreset, &QComboBox::currentIndexChanged, this, [ this, pDeletePreset ] { pDeletePreset->setEnabled( m_pPreset->currentData( Qt::UserRole + 1 ).toBool() ); } );

	for ( QComboBox *pBox : { m_pFormat, m_pAlphaFormat, m_pVersion, m_pResize, m_pMaxSize, m_pMipFilter } )
	{
		connect( pBox, &QComboBox::currentIndexChanged, this, &CTextureOptionsDialog::Update );
	}
	for ( QSpinBox *pSide : { m_pExactWidth, m_pExactHeight } )
	{
		connect( pSide, &QSpinBox::valueChanged, this, &CTextureOptionsDialog::Update );
	}
	if ( m_pKind )
	{
		connect( m_pKind, &QComboBox::currentIndexChanged, this, &CTextureOptionsDialog::Update );
		connect( m_pPanoramaSize, &QComboBox::currentIndexChanged, this, &CTextureOptionsDialog::Update );
	}
	for ( QCheckBox *pBox : { m_pMipmaps, m_pHeightToNormal, m_pLowRes, m_pKeepAspect, m_pDistanceAlpha, m_pStripAlpha } )
	{
		connect( pBox, &QCheckBox::toggled, this, &CTextureOptionsDialog::Update );
	}
	if ( bSimple )
	{
		// The simple interface leaves the header numbers to the regular one.
		m_pBumpScale->setEnabled( false );
		m_pFirstFrame->setEnabled( false );
	}
	// An import starts from the preset used last, if there is one.
	if ( nMode == MODE_IMPORT && !LastTexturePreset().isEmpty() )
	{
		for ( const TexturePreset_t &preset : TexturePresets() )
		{
			if ( preset.m_szName == LastTexturePreset() )
			{
				FillPresets( preset.m_szName );
				ApplyOptions( preset.m_Options );
			}
		}
	}
	Update();
}

//-----------------------------------------------------------------------------
// Purpose: The preset list, with szCurrent picked (or "(custom)").
//-----------------------------------------------------------------------------
void CTextureOptionsDialog::FillPresets( const QString &szCurrent )
{
	const QSignalBlocker blocker{ m_pPreset };
	m_pPreset->clear();
	m_pPreset->addItem( tr( "(custom)" ), QString{} );
	bool bUser = false;
	for ( const TexturePreset_t &preset : TexturePresets() )
	{
		if ( !preset.m_bBuiltin && !bUser )
		{
			m_pPreset->insertSeparator( m_pPreset->count() );
			bUser = true;
		}
		m_pPreset->addItem( preset.m_szName, preset.m_szName );
		m_pPreset->setItemData( m_pPreset->count() - 1, !preset.m_bBuiltin, Qt::UserRole + 1 );
	}
	m_pPreset->setCurrentIndex( std::max( 0, m_pPreset->findData( szCurrent ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: Sets every control from a set of choices (a preset's, or the starting ones).
//-----------------------------------------------------------------------------
void CTextureOptionsDialog::ApplyOptions( const BuildOptions_t &options )
{
	m_bApplying = true;
	m_pFormat->setCurrentIndex( std::max( 0, m_pFormat->findData( static_cast<int>( options.m_Write.m_nFormat ) ) ) );
	m_pAlphaFormat->setCurrentIndex( std::max( 0, m_pAlphaFormat->findData( static_cast<int>( options.m_nAlphaFormat ) ) ) );
	m_pVersion->setCurrentIndex( std::clamp( options.m_Write.m_nVersionMinor, 1, 5 ) - 1 );
	m_pResize->setCurrentIndex( std::max( 0, m_pResize->findData( static_cast<int>( options.m_nResize ) ) ) );
	m_pMaxSize->setCurrentIndex( std::max( 0, m_pMaxSize->findData( options.m_nMaxSize ) ) );
	if ( options.m_nResize == ResizeMode_t::RESIZE_EXACT )
	{
		m_pExactWidth->setValue( options.m_nExactWidth );
		m_pExactHeight->setValue( options.m_nExactHeight );
	}
	m_pKeepAspect->setChecked( options.m_bKeepAspect );
	m_pMipmaps->setChecked( options.m_Write.m_bMipmaps );
	m_pMipFilter->setCurrentIndex( std::max( 0, m_pMipFilter->findData( static_cast<int>( options.m_Write.m_nMipFilter ) ) ) );
	m_pLowRes->setChecked( options.m_Write.m_bLowRes );
	m_pAlphaFlags->setChecked( options.m_bAlphaFlags );
	m_pStripAlpha->setChecked( options.m_bStripAlpha );
	m_pHeightToNormal->setChecked( options.m_bHeightToNormal );
	m_pNormalScale->setValue( static_cast<double>( options.m_flNormalScale ) );
	m_pGreenUp->setChecked( options.m_bGreenUp );
	m_pDistanceAlpha->setChecked( options.m_bDistanceAlpha );
	m_pDistanceSpread->setValue( static_cast<double>( options.m_flDistanceSpread ) );
	m_pBumpScale->setValue( static_cast<double>( options.m_Write.m_flBumpScale ) );
	m_pFirstFrame->setValue( options.m_Write.m_nFirstFrame );
	for ( int i = 0; i < m_pFlags->topLevelItemCount(); i++ )
	{
		QTreeWidgetItem *pItem = m_pFlags->topLevelItem( i );
		const auto nBit = static_cast<std::uint32_t>( pItem->data( 0, Qt::UserRole ).toULongLong() );
		pItem->setCheckState( 0, options.m_Write.m_nFlags & nBit ? Qt::Checked : Qt::Unchecked );
	}
	m_FlagsNotListed = options.m_Write.m_nFlags & ~AUTOMATIC_FLAGS;
	m_bApplying = false;
	Update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CTextureOptionsDialog::Kind_t CTextureOptionsDialog::GetKind() const
{
	if ( m_pKind )
	{
		return static_cast<Kind_t>( m_pKind->currentData().toInt() );
	}
	if ( m_nMode == MODE_BATCH )
	{
		return KIND_SINGLE;
	}
	return m_Initial.m_nFaces == 6 ? KIND_CUBE : m_Initial.m_Write.m_nDepth > 1 ? KIND_VOLUME
																				: KIND_FRAMES;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
BuildOptions_t CTextureOptionsDialog::GetOptions() const
{
	BuildOptions_t options = m_Initial;
	options.m_Write.m_nFormat = static_cast<VtfFormat_t>( m_pFormat->currentData().toInt() );
	options.m_nAlphaFormat = static_cast<VtfFormat_t>( m_pAlphaFormat->currentData().toInt() );
	options.m_Write.m_nVersionMinor = m_pVersion->currentData().toInt();
	options.m_Write.m_bMipmaps = m_pMipmaps->isChecked();
	options.m_Write.m_nMipFilter = static_cast<MipFilter_t>( m_pMipFilter->currentData().toInt() );
	options.m_Write.m_bLowRes = m_pLowRes->isChecked();
	options.m_Write.m_flBumpScale = static_cast<float>( m_pBumpScale->value() );
	options.m_Write.m_nFirstFrame = m_pFirstFrame->value();
	options.m_nResize = static_cast<ResizeMode_t>( m_pResize->currentData().toInt() );
	options.m_nMaxSize = m_pMaxSize->currentData().toInt();
	options.m_nExactWidth = m_pExactWidth->value();
	options.m_nExactHeight = m_pExactHeight->value();
	options.m_bKeepAspect = m_pKeepAspect->isChecked();
	options.m_bAlphaFlags = m_pAlphaFlags->isChecked();
	const Kind_t nKind = GetKind();
	options.m_nFaces = nKind == KIND_CUBE || nKind == KIND_PANORAMA ? 6 : 1;
	options.m_Write.m_nDepth = nKind == KIND_VOLUME ? std::max<int>( 1, m_pList ? m_pList->count() : static_cast<int>( m_Sources.size() ) ) : m_nMode == MODE_CONVERT ? m_Initial.m_Write.m_nDepth
																																									  : 1;
	options.m_bStripAlpha = m_pStripAlpha->isChecked();
	options.m_bHeightToNormal = m_pHeightToNormal->isChecked();
	options.m_flNormalScale = static_cast<float>( m_pNormalScale->value() );
	options.m_bGreenUp = m_pGreenUp->isChecked();
	options.m_bDistanceAlpha = m_pDistanceAlpha->isChecked();
	options.m_flDistanceSpread = static_cast<float>( m_pDistanceSpread->value() );
	// Flags not listed (unknown bits, ones the simple interface leaves out) stay as they were.
	options.m_Write.m_nFlags = FlagsFrom( m_pFlags, m_FlagsNotListed );
	return options;
}

//-----------------------------------------------------------------------------
// Purpose: The pictures in the order they go in (for a single image, only the first; for a
//          panorama, the six faces made from it).
//-----------------------------------------------------------------------------
std::vector<SourceImage_t> CTextureOptionsDialog::GetSources() const
{
	std::vector<SourceImage_t> sources;
	if ( !m_pList )
	{
		for ( const NamedSource_t &source : m_Sources )
		{
			sources.push_back( source.m_Source );
		}
		return sources;
	}
	for ( int nRow = 0; nRow < m_pList->count(); nRow++ )
	{
		sources.push_back( m_Sources[ static_cast<std::size_t>( m_pList->item( nRow )->data( Qt::UserRole ).toInt() ) ].m_Source );
		if ( GetKind() == KIND_SINGLE || GetKind() == KIND_PANORAMA )
		{
			break;
		}
	}
	if ( GetKind() == KIND_PANORAMA && !sources.empty() )
	{
		const SourceImage_t &panorama = sources.front();
		const bool bLight = !panorama.m_Light.Empty();
		std::vector<SourceImage_t> faces;
		for ( CFloatImage &face : xv::sky::CubeMapFromPanorama( bLight ? panorama.m_Light : ToFloat( panorama.m_Image ), m_pPanoramaSize->currentData().toInt(), static_cast<float>( m_pPanoramaYaw->value() ) ) )
		{
			SourceImage_t source;
			source.m_Image = ToEightBit( face );
			if ( bLight )
			{
				source.m_Light = std::move( face );
			}
			faces.push_back( std::move( source ) );
		}
		return faces;
	}
	return sources;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTextureOptionsDialog::MoveSelected( int nBy )
{
	const int nRow = m_pList->currentRow();
	const int nTo = nRow + nBy;
	if ( nRow < 0 || nTo < 0 || nTo >= m_pList->count() )
	{
		return;
	}
	QListWidgetItem *pItem = m_pList->takeItem( nRow );
	m_pList->insertItem( nTo, pItem );
	m_pList->setCurrentRow( nTo );
	Update();
}

//-----------------------------------------------------------------------------
// Purpose: What the choices make, and anything to know about them.
//-----------------------------------------------------------------------------
QString CTextureOptionsDialog::Notes() const
{
	const BuildOptions_t options = GetOptions();
	QStringList notes;
	if ( m_nMode == MODE_BATCH )
	{
		return tr( "Each picture in the folder becomes a texture of its own with these settings." );
	}
	if ( GetKind() == KIND_PANORAMA )
	{
		const auto &first = m_Sources.empty() ? CImage{} : m_Sources.front().m_Source.m_Image;
		if ( first.m_nWidth != first.m_nHeight * 2 )
		{
			notes << tr( "A panorama is twice as wide as tall; this picture is %1×%2." ).arg( first.m_nWidth ).arg( first.m_nHeight );
		}
	}
	const std::vector<SourceImage_t> sources = GetKind() == KIND_PANORAMA ? std::vector<SourceImage_t>{} : GetSources();
	if ( sources.empty() && GetKind() != KIND_PANORAMA )
	{
		return tr( "No pictures." );
	}
	const bool bClear = !options.m_bStripAlpha && ( options.m_bDistanceAlpha || std::ranges::any_of( m_Sources, HasClearParts ) );
	const VtfFormat_t nFormat = bClear && options.m_nAlphaFormat != VtfFormat_t::VTF_FORMAT_NONE ? options.m_nAlphaFormat : options.m_Write.m_nFormat;
	if ( GetKind() == KIND_VOLUME && options.m_Write.m_nVersionMinor < 2 )
	{
		notes << tr( "<b>Volume textures need version 7.2 or later.</b>" );
	}
	if ( !sources.empty() && sources.size() % static_cast<std::size_t>( options.m_nFaces ) != 0 )
	{
		notes << tr( "<b>A cube map takes 6 pictures for each frame; there are %1.</b>" ).arg( sources.size() );
	}
	int nWidth = 0;
	int nHeight = 0;
	if ( GetKind() == KIND_PANORAMA )
	{
		nWidth = nHeight = m_pPanoramaSize->currentData().toInt();
	}
	else
	{
		const SourceImage_t &first = sources.front();
		const bool bLight = !first.m_Light.Empty();
		std::tie( nWidth, nHeight ) = TargetSizes( bLight ? first.m_Light.m_nWidth : first.m_Image.m_nWidth, bLight ? first.m_Light.m_nHeight : first.m_Image.m_nHeight, options );
	}
	if ( !IsPowerOfTwo( nWidth ) || !IsPowerOfTwo( nHeight ) )
	{
		notes << tr( "%1×%2 isn't a power of two on each side; older Source games may refuse it or draw it wrong." ).arg( nWidth ).arg( nHeight );
	}
	if ( bClear && !HasAlpha( nFormat ) )
	{
		notes << tr( "The pictures have see-through parts, which %1 doesn't keep." ).arg( Qs( FormatName( nFormat ) ) );
	}
	const bool bLight = std::ranges::any_of( m_Sources, []( const NamedSource_t &source ) { return !source.m_Source.m_Light.Empty(); } );
	if ( bLight && !IsFloatFormat( nFormat ) )
	{
		notes << tr( "The HDR light values are cut to 0-1 in %1." ).arg( Qs( FormatName( nFormat ) ) );
	}
	const bool bSheet = std::ranges::any_of( options.m_Write.m_Resources, []( const VtfExtraResource_t &r ) { return r.m_Tag == SHEET_RESOURCE; } );
	if ( !options.m_Write.m_Resources.empty() && options.m_Write.m_nVersionMinor < 3 )
	{
		notes << ( bSheet ? tr( "<b>The sprite sheet needs version 7.3 or later; it will be left out.</b>" ) : tr( "The texture's extra resources need 7.3 or later and will be left out." ) );
	}
	if ( options.m_bDistanceAlpha && !HasAlpha( nFormat ) )
	{
		notes << tr( "<b>Distance alpha needs a format with alpha.</b>" );
	}
	if ( m_nMode == MODE_CONVERT )
	{
		const VtfFormat_t nFrom = m_Initial.m_Write.m_nFormat;
		const bool bLossyFrom = nFrom == VtfFormat_t::DXT1 || nFrom == VtfFormat_t::DXT1_ONEBITALPHA || nFrom == VtfFormat_t::DXT3 || nFrom == VtfFormat_t::DXT5;
		if ( bLossyFrom && nFormat != nFrom && ( nFormat == VtfFormat_t::DXT1 || nFormat == VtfFormat_t::DXT5 || nFormat == VtfFormat_t::DXT3 || nFormat == VtfFormat_t::DXT1_ONEBITALPHA ) )
		{
			notes << tr( "Compressing an already compressed texture again loses a little more detail." );
		}
		notes << tr( "Mipmaps are made again from the full-size image." );
	}

	// Roughly what it weighs.
	const std::size_t nPictures = GetKind() == KIND_PANORAMA ? 6 : sources.size();
	const std::size_t nImages = GetKind() == KIND_VOLUME ? nPictures : nPictures / static_cast<std::size_t>( options.m_nFaces ) * static_cast<std::size_t>( options.m_nFaces == 6 && options.m_Write.m_nVersionMinor < 5 ? 7 : options.m_nFaces );
	std::size_t nBytes = 0;
	const int nLevels = options.m_Write.m_bMipmaps ? std::bit_width( static_cast<unsigned>( std::max( nWidth, nHeight ) ) ) : 1;
	for ( int nLevel = 0; nLevel < nLevels; nLevel++ )
	{
		nBytes += ImageSize( nFormat, std::max( 1, nWidth >> nLevel ), std::max( 1, nHeight >> nLevel ) ) * nImages;
	}
	notes.prepend( tr( "Makes %1×%2 %3, about %4." ).arg( nWidth ).arg( nHeight ).arg( Qs( FormatName( nFormat ) ) ).arg( QLocale{}.formattedDataSize( static_cast<qint64>( nBytes ) ) ) );
	return notes.join( QStringLiteral( "<br>" ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTextureOptionsDialog::Update()
{
	if ( !m_pDistanceSpread )
	{
		return; // still being built
	}
	const auto nFormat = static_cast<VtfFormat_t>( m_pFormat->currentData().toInt() );
	m_pFormatNote->setText( Qs( ParticleNames().Texture( "format:" + std::string{ FormatName( nFormat ) } ).m_szMeaning ) );
	m_pAlphaFlags->setEnabled( HasAlpha( nFormat ) || m_pAlphaFormat->currentIndex() > 0 );
	m_pNormalScale->setEnabled( m_pHeightToNormal->isChecked() );
	m_pGreenUp->setEnabled( m_pHeightToNormal->isChecked() );
	m_pDistanceSpread->setEnabled( m_pDistanceAlpha->isChecked() );
	m_pMipFilter->setEnabled( m_pMipmaps->isChecked() );
	const bool bExact = static_cast<ResizeMode_t>( m_pResize->currentData().toInt() ) == ResizeMode_t::RESIZE_EXACT;
	m_pExactWidth->setVisible( bExact );
	m_pExactBy->setVisible( bExact );
	m_pExactHeight->setVisible( bExact );
	m_pMaxSize->setVisible( !bExact );
	m_pKeepAspect->setVisible( !bExact );
	if ( m_pKind )
	{
		const bool bPanorama = GetKind() == KIND_PANORAMA;
		m_pPanoramaLabel->setVisible( bPanorama );
		m_pPanoramaSize->setVisible( bPanorama );
		m_pPanoramaYaw->setVisible( bPanorama );
	}
	const BuildOptions_t options = GetOptions();
	// Changing anything a preset sets makes the choices the user's own.
	if ( !m_bApplying && m_pPreset->currentIndex() > 0 )
	{
		for ( const TexturePreset_t &preset : TexturePresets() )
		{
			if ( preset.m_szName == m_pPreset->currentData().toString() && !SameTextureChoices( preset.m_Options, options ) )
			{
				m_pPreset->setCurrentIndex( 0 );
			}
		}
	}
	if ( !m_Sources.empty() && GetKind() != KIND_PANORAMA )
	{
		const SourceImage_t &first = m_Sources.front().m_Source;
		const bool bLight = !first.m_Light.Empty();
		const auto [ nWidth, nHeight ] = TargetSizes( bLight ? first.m_Light.m_nWidth : first.m_Image.m_nWidth, bLight ? first.m_Light.m_nHeight : first.m_Image.m_nHeight, options );
		m_pSize->setText( tr( "→ %1×%2" ).arg( nWidth ).arg( nHeight ) );
	}
	else
	{
		m_pSize->clear();
	}
	m_pNotes->setText( Notes() );
}

//-----------------------------------------------------------------------------
// Purpose: The flags to offer.
//-----------------------------------------------------------------------------
std::vector<ListedFlag_t> ListedFlags( std::uint32_t nFlags, bool bHeaderFlags )
{
	const bool bSimple = CInterfaceMode::Instance().IsSimple();
	std::vector<ListedFlag_t> listed;
	for ( std::size_t i = 0; i < FLAGS.size(); i++ )
	{
		const auto &[ nBit, szName ] = FLAGS[ i ];
		const bool bSet = ( nFlags & nBit ) != 0;
		const bool bAutomatic = ( AUTOMATIC_FLAGS & nBit ) != 0;
		// The simple interface leaves rare flags out unless the texture has them; flags set from
		// the pictures are only listed for a header edit.
		if ( ( bSimple && i >= COMMON_FLAG_COUNT && !bSet && !bAutomatic ) || ( bAutomatic && !bHeaderFlags ) )
		{
			continue;
		}
		listed.push_back( { nBit, szName, nBit == vtf_flag::ENVMAP } );
	}
	return listed;
}

//-----------------------------------------------------------------------------
// Purpose: A list of the texture flags to tick, with what each does.
//-----------------------------------------------------------------------------
QTreeWidget *MakeFlagList( std::uint32_t nFlags, bool bHeaderFlags )
{
	const bool bSimple = CInterfaceMode::Instance().IsSimple();
	auto *pFlags = new QTreeWidget;
	pFlags->setObjectName( QStringLiteral( "textureFlags" ) );
	pFlags->setHeaderHidden( true );
	pFlags->setRootIsDecorated( false );
	pFlags->setColumnCount( 2 );
	pFlags->header()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
	for ( const ListedFlag_t &flag : ListedFlags( nFlags, bHeaderFlags ) )
	{
		const xv::particles::PlainName_t plain = ParticleNames().Texture( "flag:" + std::string{ flag.m_szName } );
		auto *pItem = new QTreeWidgetItem( pFlags, { bSimple ? Qs( plain.m_szName ) : Qs( flag.m_szName ), Qs( plain.m_szMeaning ) } );
		pItem->setData( 0, Qt::UserRole, static_cast<qulonglong>( flag.m_nBit ) );
		pItem->setCheckState( 0, nFlags & flag.m_nBit ? Qt::Checked : Qt::Unchecked );
		pItem->setToolTip( 0, bSimple ? Qs( flag.m_szName ) : Qs( plain.m_szName ) );
		pItem->setForeground( 1, pFlags->palette().color( QPalette::PlaceholderText ) );
		if ( flag.m_bFixed )
		{
			pItem->setFlags( pItem->flags() & ~Qt::ItemIsEnabled ); // follows the faces
		}
	}
	pFlags->setMinimumHeight( 180 );
	return pFlags;
}

//-----------------------------------------------------------------------------
// Purpose: The flags ticked in a MakeFlagList list, over the ones it doesn't list (nOthers).
//-----------------------------------------------------------------------------
std::uint32_t FlagsFrom( const QTreeWidget *pFlags, std::uint32_t nOthers )
{
	std::uint32_t nFlags = nOthers;
	for ( int i = 0; i < pFlags->topLevelItemCount(); i++ )
	{
		const QTreeWidgetItem *pItem = pFlags->topLevelItem( i );
		const auto nBit = static_cast<std::uint32_t>( pItem->data( 0, Qt::UserRole ).toULongLong() );
		nFlags = pItem->checkState( 0 ) == Qt::Checked ? nFlags | nBit : nFlags & ~nBit;
	}
	return nFlags;
}
