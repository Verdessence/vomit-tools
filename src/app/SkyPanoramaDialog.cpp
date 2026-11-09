//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SkyPanoramaDialog.h.
//
//===========================================================================//
#include "SkyPanoramaDialog.h"

#include <algorithm>
#include <cmath>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include "SkyView.h"
#include "texture/Image.h"

namespace
{

constexpr auto BRIGHTNESS_KEY = "skyPanorama/brightness";
constexpr auto EXPOSURE_KEY = "skyPanorama/exposure";
constexpr auto CURVE_KEY = "skyPanorama/curve";
constexpr int PREVIEW_SIZE = 256;
// The preview's panorama: wide enough for PREVIEW_SIZE faces with a sample or two to spare.
constexpr int PREVIEW_PANORAMA_WIDTH = PREVIEW_SIZE * 8;

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Lays out the controls; the HDR ones only for light values.
//-----------------------------------------------------------------------------
CSkyPanoramaDialog::CSkyPanoramaDialog( xv::texture::CFloatImage panorama, bool bHdr, const QString &szFileName, QWidget *pParent )
	: QDialog( pParent ),
	  m_Panorama( std::move( panorama ) ),
	  m_bHdr( bHdr )
{
	setWindowTitle( tr( "Import Panorama" ) );
	m_PreviewPanorama = m_Panorama.m_nWidth > PREVIEW_PANORAMA_WIDTH ? xv::texture::Resize( m_Panorama, PREVIEW_PANORAMA_WIDTH, std::max( 1, PREVIEW_PANORAMA_WIDTH / 2 ) ) : m_Panorama;

	auto *pAbout = new QLabel( tr( "%1: %2×%3, %4" ).arg( szFileName ).arg( m_Panorama.m_nWidth ).arg( m_Panorama.m_nHeight ).arg( m_bHdr ? tr( "light values (HDR)" ) : tr( "an 8-bit picture" ) ) );
	pAbout->setWordWrap( true );

	auto *pForm = new QFormLayout;
	m_pSize = new QComboBox;
	m_pSize->setObjectName( QStringLiteral( "panoramaSize" ) );
	for ( int nSize = 256; nSize <= 4096; nSize *= 2 )
	{
		m_pSize->addItem( tr( "%1 × %1" ).arg( nSize ), nSize );
	}
	// About the panorama's own detail (a face spans a quarter of its width), up to 2048.
	const int nNatural = std::clamp( xv::texture::NearestPowerOfTwo( std::max( 1, m_Panorama.m_nWidth / 4 ) ), 256, 2048 );
	m_pSize->setCurrentIndex( std::max( 0, m_pSize->findData( nNatural ) ) );
	m_pSize->setToolTip( tr( "Each face's width and height. A face spans a quarter of the panorama's width, so more than that adds no detail" ) );
	pForm->addRow( tr( "Face size" ), m_pSize );

	m_pYaw = new QDoubleSpinBox;
	m_pYaw->setObjectName( QStringLiteral( "panoramaYaw" ) );
	m_pYaw->setRange( -180.0, 180.0 );
	m_pYaw->setDecimals( 1 );
	m_pYaw->setSingleStep( 5.0 );
	m_pYaw->setWrapping( true );
	m_pYaw->setSuffix( QStringLiteral( "°" ) );
	m_pYaw->setToolTip( tr( "The panorama's middle faces along +X (yaw 0 in Hammer, the rt face); turn it to the left by this much" ) );
	pForm->addRow( tr( "Turn" ), m_pYaw );

	const QSettings settings;
	if ( m_bHdr )
	{
		m_pBrightness = new QDoubleSpinBox;
		m_pBrightness->setObjectName( QStringLiteral( "panoramaBrightness" ) );
		m_pBrightness->setRange( -10.0, 10.0 );
		m_pBrightness->setDecimals( 1 );
		m_pBrightness->setSingleStep( 0.5 );
		m_pBrightness->setSuffix( tr( " stops" ) );
		m_pBrightness->setValue( settings.value( BRIGHTNESS_KEY, 0.0 ).toDouble() );
		m_pBrightness->setToolTip( tr( "HDRIs come at any brightness, so the sky is brought to about where Valve's day skies sit (0 stops). "
									   "Raise it for a brighter sky under HDR, lower it for dusk" ) );
		pForm->addRow( tr( "HDR brightness" ), m_pBrightness );
		m_pLevel = new QLabel;
		m_pLevel->setEnabled( false );
		pForm->addRow( QString{}, m_pLevel );

		m_pExposure = new QDoubleSpinBox;
		m_pExposure->setObjectName( QStringLiteral( "panoramaExposure" ) );
		m_pExposure->setRange( -10.0, 10.0 );
		m_pExposure->setDecimals( 1 );
		m_pExposure->setSingleStep( 0.25 );
		m_pExposure->setSuffix( tr( " stops" ) );
		m_pExposure->setValue( settings.value( EXPOSURE_KEY, 0.0 ).toDouble() );
		m_pExposure->setToolTip( tr( "How much brighter the LDR faces (seen with HDR off, and in the preview) are than the HDR set" ) );
		pForm->addRow( tr( "LDR exposure" ), m_pExposure );

		m_pCurve = new QComboBox;
		m_pCurve->setObjectName( QStringLiteral( "panoramaCurve" ) );
		m_pCurve->addItem( tr( "Filmic (soft highlights)" ), static_cast<int>( xv::sky::ToneCurve_t::TONE_CURVE_FILMIC ) );
		m_pCurve->addItem( tr( "Reinhard" ), static_cast<int>( xv::sky::ToneCurve_t::TONE_CURVE_REINHARD ) );
		m_pCurve->addItem( tr( "Clip" ), static_cast<int>( xv::sky::ToneCurve_t::TONE_CURVE_CLIP ) );
		m_pCurve->setCurrentIndex( std::max( 0, m_pCurve->findData( settings.value( CURVE_KEY, static_cast<int>( xv::sky::ToneCurve_t::TONE_CURVE_FILMIC ) ).toInt() ) ) );
		m_pCurve->setToolTip( tr( "How the LDR faces fit the light values under white: Filmic rolls the sun and clouds off gently, Clip cuts them off" ) );
		pForm->addRow( tr( "LDR curve" ), m_pCurve );
	}

	m_pView = new CSkyView;
	m_pView->setObjectName( QStringLiteral( "panoramaPreview" ) );
	m_pView->SetShowLabels( true );
	m_pView->setMinimumSize( 360, 280 );
	m_pView->SetCube( &m_PreviewCube );
	auto *pPreviewLabel = new QLabel( m_bHdr ? tr( "The LDR faces (drag to look around):" ) : tr( "The faces (drag to look around):" ) );

	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	pButtons->button( QDialogButtonBox::Ok )->setText( tr( "Import" ) );
	connect( pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );

	auto *pLayout = new QVBoxLayout( this );
	pLayout->addWidget( pAbout );
	pLayout->addLayout( pForm );
	pLayout->addWidget( pPreviewLabel );
	pLayout->addWidget( m_pView, 1 );
	pLayout->addWidget( pButtons );

	// Spin boxes change on every keystroke: the preview waits for a pause.
	m_pPreviewTimer = new QTimer( this );
	m_pPreviewTimer->setSingleShot( true );
	m_pPreviewTimer->setInterval( 60 );
	connect( m_pPreviewTimer, &QTimer::timeout, this, &CSkyPanoramaDialog::UpdatePreview );
	const auto later = [ this ] { m_pPreviewTimer->start(); };
	connect( m_pYaw, &QDoubleSpinBox::valueChanged, this, later );
	if ( m_bHdr )
	{
		connect( m_pBrightness, &QDoubleSpinBox::valueChanged, this, later );
		connect( m_pExposure, &QDoubleSpinBox::valueChanged, this, later );
		connect( m_pCurve, &QComboBox::currentIndexChanged, this, later );
	}
	UpdatePreview();
}

//-----------------------------------------------------------------------------
// Purpose: The choices as the core takes them.
//-----------------------------------------------------------------------------
xv::sky::PanoramaImport_t CSkyPanoramaDialog::GetOptions() const
{
	xv::sky::PanoramaImport_t options;
	options.m_nSize = m_pSize->currentData().toInt();
	options.m_flYaw = static_cast<float>( m_pYaw->value() );
	if ( m_bHdr )
	{
		options.m_flBrightness = static_cast<float>( m_pBrightness->value() );
		options.m_ToneMap.m_flExposure = static_cast<float>( m_pExposure->value() );
		options.m_ToneMap.m_nCurve = static_cast<xv::sky::ToneCurve_t>( m_pCurve->currentData().toInt() );
	}
	return options;
}

//-----------------------------------------------------------------------------
// Purpose: The panorama made into faces at the chosen size.
//-----------------------------------------------------------------------------
xv::sky::PanoramaSky_t CSkyPanoramaDialog::MakeSky() const
{
	return xv::sky::SkyFromPanorama( m_Panorama, GetOptions(), m_bHdr );
}

//-----------------------------------------------------------------------------
// Purpose: Keeps the HDR choices for the next panorama.
//-----------------------------------------------------------------------------
void CSkyPanoramaDialog::accept()
{
	if ( m_bHdr )
	{
		QSettings settings;
		settings.setValue( BRIGHTNESS_KEY, m_pBrightness->value() );
		settings.setValue( EXPOSURE_KEY, m_pExposure->value() );
		settings.setValue( CURVE_KEY, m_pCurve->currentData() );
	}
	QDialog::accept();
}

//-----------------------------------------------------------------------------
// Purpose: The preview's faces, small, from the small panorama.
//-----------------------------------------------------------------------------
void CSkyPanoramaDialog::UpdatePreview()
{
	xv::sky::PanoramaImport_t options = GetOptions();
	options.m_nSize = PREVIEW_SIZE;
	xv::sky::PanoramaSky_t sky = xv::sky::SkyFromPanorama( m_PreviewPanorama, options, m_bHdr );
	m_PreviewCube.m_Faces = std::move( sky.m_Faces );
	if ( m_pLevel )
	{
		// How far the panorama itself sits from a stock sky, before the brightness chosen here.
		const double flStops = -std::log2( static_cast<double>( sky.m_flScale ) ) + m_pBrightness->value();
		const QString szLevel = std::abs( flStops ) < 0.05 ? tr( "as bright as a stock sky" )
			: flStops < 0.0                                ? tr( "%1 stops darker than a stock sky" ).arg( -flStops, 0, 'f', 1 )
														   : tr( "%1 stops brighter than a stock sky" ).arg( flStops, 0, 'f', 1 );
		m_pLevel->setText( tr( "Light values × %1 (the panorama is %2)" ).arg( static_cast<double>( sky.m_flScale ), 0, 'g', 3 ).arg( szLevel ) );
	}
	m_pView->Refresh();
}
