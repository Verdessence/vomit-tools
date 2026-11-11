//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapLightingPanel.h.
//
//===========================================================================//
#include "MapLightingPanel.h"

#include <algorithm>
#include <array>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QListWidget>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>

#include "Preferences.h"
#include "map/Edit.h"
#include "map/Shade.h"
#include "map/MapEditor.h"
#include "map/MapLighting.h"
#include "map/MapEntityFields.h"

namespace
{

// A "_light"-style value as written: red, green, blue and brightness (255 when left out, which
// is how vrad reads three numbers; one number is grey).
struct LightValue_t
{
	std::array<int, 3> m_Color{ 255, 255, 255 };
	double m_flBrightness = 255.0;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
LightValue_t ReadValue( std::string_view szText )
{
	std::vector<double> numbers;
	for ( const QString &szPart : QString::fromUtf8( szText.data(), static_cast<qsizetype>( szText.size() ) ).split( QLatin1Char( ' ' ), Qt::SkipEmptyParts ) )
	{
		bool bOk = false;
		const double fl = szPart.toDouble( &bOk );
		if ( bOk )
		{
			numbers.push_back( fl );
		}
	}
	LightValue_t value;
	if ( numbers.size() == 1 )
	{
		value.m_Color.fill( static_cast<int>( numbers[ 0 ] ) );
	}
	else if ( numbers.size() >= 3 )
	{
		for ( std::size_t i = 0; i < 3; i++ )
		{
			value.m_Color[ i ] = static_cast<int>( numbers[ i ] );
		}
		if ( numbers.size() >= 4 )
		{
			value.m_flBrightness = numbers[ 3 ];
		}
	}
	return value;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QString Text( std::string_view sz )
{
	return QString::fromUtf8( sz.data(), static_cast<qsizetype>( sz.size() ) );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapLightingPanel::CMapLightingPanel( CMapEditor &editor, QWidget *pParent )
	: QWidget( pParent ),
	  m_Editor( editor )
{
	setObjectName( QStringLiteral( "mapLightingPanel" ) );
	auto *pLayout = new QVBoxLayout( this );
	auto *pForm = new QFormLayout;
	m_pBrightness = new QDoubleSpinBox;
	m_pBrightness->setObjectName( QStringLiteral( "mapLightingBrightness" ) );
	m_pBrightness->setRange( 0.1, 8.0 );
	m_pBrightness->setSingleStep( 0.1 );
	m_pBrightness->setToolTip( tr( "The light times this before it is shown (no bake needed)" ) );
	pForm->addRow( tr( "Brightness" ), m_pBrightness );
	m_pMonitorGamma = new QDoubleSpinBox;
	m_pMonitorGamma->setObjectName( QStringLiteral( "mapLightingMonitorGamma" ) );
	m_pMonitorGamma->setRange( 1.6, 2.6 );
	m_pMonitorGamma->setSingleStep( 0.1 );
	m_pMonitorGamma->setDecimals( 1 );
	m_pMonitorGamma->setToolTip( tr( "The game's brightness setting (mat_monitorgamma): 2.2 by default, 1.6 the brightest, 2.6 the darkest" ) );
	pForm->addRow( tr( "Game gamma" ), m_pMonitorGamma );
	m_pBounces = new QSpinBox;
	m_pBounces->setObjectName( QStringLiteral( "mapLightingBounces" ) );
	m_pBounces->setRange( 0, 8 );
	m_pBounces->setToolTip( tr( "At most this many passes of bounced light, stopping when a pass adds under 1%; the rest are estimated, as vrad bounces until the light settles" ) );
	pForm->addRow( tr( "Bounces" ), m_pBounces );
	m_pLuxels = new QSpinBox;
	m_pLuxels->setObjectName( QStringLiteral( "mapLightingLuxels" ) );
	m_pLuxels->setRange( 1, 64 );
	m_pLuxels->setSuffix( tr( " million luxels" ) );
	m_pLuxels->setToolTip( tr( "More than this, and every other luxel is lit (and so on), for speed" ) );
	pForm->addRow( tr( "At most" ), m_pLuxels );
	m_pSkySamples = new QSpinBox;
	m_pSkySamples->setObjectName( QStringLiteral( "mapLightingSkySamples" ) );
	m_pSkySamples->setRange( 4, 512 );
	m_pSkySamples->setToolTip( tr( "Rays toward the sky from each sample, for the sky's ambient light: more is smoother and slower" ) );
	pForm->addRow( tr( "Sky samples" ), m_pSkySamples );
	m_pDelay = new QSpinBox;
	m_pDelay->setObjectName( QStringLiteral( "mapLightingDelay" ) );
	m_pDelay->setRange( 0, 10000 );
	m_pDelay->setSingleStep( 50 );
	m_pDelay->setSuffix( tr( " ms" ) );
	m_pDelay->setToolTip( tr( "How long after an edit the preview waits before baking again" ) );
	pForm->addRow( tr( "Wait after an edit" ), m_pDelay );
	auto *pRadiusRow = new QHBoxLayout;
	m_pInRadius = new QCheckBox( tr( "Only around the camera, within" ) );
	m_pInRadius->setObjectName( QStringLiteral( "mapLightingInRadius" ) );
	m_pInRadius->setToolTip( tr( "Bake only what is this near the 3D camera (everything still casts shadows): much faster on big maps" ) );
	m_pRadius = new QDoubleSpinBox;
	m_pRadius->setObjectName( QStringLiteral( "mapLightingRadius" ) );
	m_pRadius->setRange( 64, 32768 );
	m_pRadius->setSingleStep( 256 );
	m_pRadius->setDecimals( 0 );
	m_pRadius->setSuffix( tr( " units" ) );
	pRadiusRow->addWidget( m_pInRadius );
	pRadiusRow->addWidget( m_pRadius, 1 );
	pLayout->addLayout( pForm );
	pLayout->addLayout( pRadiusRow );
	m_pHdr = new QCheckBox( tr( "As the game with HDR (auto exposure, the lights' HDR values)" ) );
	m_pHdr->setObjectName( QStringLiteral( "mapLightingHdr" ) );
	m_pHdr->setToolTip( tr( "Off: as the game without HDR (mat_hdr_level 0 or 1), lightmaps doubled as Source draws them" ) );
	pLayout->addWidget( m_pHdr );
	m_pLightOnly = new QCheckBox( tr( "The light alone, textures grey (mat_fullbright 2)" ) );
	m_pLightOnly->setObjectName( QStringLiteral( "mapLightingOnly" ) );
	pLayout->addWidget( m_pLightOnly );
	m_pPropShadows = new QCheckBox( tr( "prop_static casts shadows" ) );
	m_pPropShadows->setObjectName( QStringLiteral( "mapLightingPropShadows" ) );
	pLayout->addWidget( m_pPropShadows );
	m_pSmoothShadows = new QCheckBox( tr( "Smooth shadow edges (as -final)" ) );
	m_pSmoothShadows->setObjectName( QStringLiteral( "mapLightingSmoothShadows" ) );
	m_pSmoothShadows->setToolTip( tr( "Luxels on a shadow's edge are lit across their area, not at their middle alone: no stair steps, a little slower" ) );
	pLayout->addWidget( m_pSmoothShadows );
	m_pAutoBake = new QCheckBox( tr( "Bake again after every edit" ) );
	m_pAutoBake->setObjectName( QStringLiteral( "mapLightingAutoBake" ) );
	pLayout->addWidget( m_pAutoBake );
	auto *pBake = new QPushButton( tr( "Bake Lighting" ) );
	pBake->setObjectName( QStringLiteral( "mapLightingBake" ) );
	connect( pBake, &QPushButton::clicked, this, [ this ] { m_Editor.BakeLightingNow(); } );
	pLayout->addWidget( pBake );
	m_pStatus = new QLabel;
	m_pStatus->setObjectName( QStringLiteral( "mapLightingStatus" ) );
	m_pStatus->setWordWrap( true );
	pLayout->addWidget( m_pStatus );

	// The entities that set the map's look, each folding, their keys as fields.
	const auto section = [ & ]( const QString &szTitle, const QString &szName, std::vector<std::string> classes, std::vector<EntityField_t> fields, bool bOpen ) {
		auto *pFields = new CMapEntityFields( m_Editor, std::move( classes ), std::move( fields ) );
		pLayout->addWidget( MakeFoldSection( szTitle, pFields, szName, bOpen ) );
		return pFields;
	};
	section( tr( "Sun and Sky" ), QStringLiteral( "mapLightingSun" ), { "light_environment" },
		{ { "angles", {}, {}, {} },
			{ "pitch", {}, {}, {} },
			{ "_light", {}, {}, {} },
			{ "_ambient", {}, {}, {} },
			{ "_lightHDR", {}, {}, {} },
			{ "_ambientHDR", {}, {}, {} },
			{ "_lightscaleHDR", {}, {}, {} },
			{ "_AmbientScaleHDR", {}, {}, {} },
			{ "SunSpreadAngle", {}, {}, {} } },
		true );
	section( tr( "Shadows" ), QStringLiteral( "mapLightingShadows" ), { "shadow_control" },
		{ { "angles", {}, {}, {} }, { "color", {}, {}, {} }, { "distance", {}, {}, {} }, { "disableallshadows", {}, {}, {} } }, false );
	section( tr( "Tone Map" ), QStringLiteral( "mapLightingTonemap" ), { "env_tonemap_controller" },
		{ { "targetname", {}, {}, {} },
			{ {}, "SetAutoExposureMin", tr( "Exposure at least" ), tr( "SetAutoExposureMin, sent by a logic_auto as the map starts: the darkest the auto exposure goes (the game's own: 0.5). The HDR lighting preview keeps to it." ) },
			{ {}, "SetAutoExposureMax", tr( "Exposure at most" ), tr( "SetAutoExposureMax, sent as the map starts: the brightest the auto exposure goes (the game's own: 2). The HDR lighting preview keeps to it." ) },
			{ {}, "SetBloomScale", tr( "Bloom scale" ), tr( "SetBloomScale, sent as the map starts: how strong HDR's bloom is (1 by default)" ) },
			{ {}, "SetTonemapRate", tr( "Adapt rate" ), tr( "SetTonemapRate, sent as the map starts: how fast the exposure follows the light" ) } },
		false );
	section( tr( "Colour Correction" ), QStringLiteral( "mapLightingColorCorrection" ), { "color_correction" },
		{ { "filename", {}, {}, {} }, { "maxweight", {}, {}, {} }, { "minfalloff", {}, {}, {} }, { "maxfalloff", {}, {}, {} }, { "fadeInDuration", {}, {}, {} }, { "fadeOutDuration", {}, {}, {} } },
		false );

	// Every light, short: picking one selects it (its keys are Object Properties').
	m_pLights = new QListWidget;
	m_pLights->setObjectName( QStringLiteral( "mapLightingLights" ) );
	m_pLights->setToolTip( tr( "Pick a light to select it; right-click for more" ) );
	m_pLights->setMinimumHeight( 80 );
	connect( m_pLights, &QListWidget::currentRowChanged, this, [ this ]( int nRow ) {
		if ( m_bFilling || nRow < 0 || static_cast<std::size_t>( nRow ) >= m_LightEntities.size() || !m_Editor.GetDocument().GetScene() )
			return;
		m_Editor.GetDocument().SetSelection( { m_Editor.GetDocument().GetScene()->EntityObject( m_LightEntities[ static_cast<std::size_t>( nRow ) ] ) } );
	} );
	m_pLights->setContextMenuPolicy( Qt::CustomContextMenu );
	connect( m_pLights, &QListWidget::customContextMenuRequested, this, [ this ]( const QPoint &at ) {
		const int nRow = m_pLights->indexAt( at ).row();
		if ( nRow < 0 || static_cast<std::size_t>( nRow ) >= m_LightEntities.size() )
			return;
		m_pLights->setCurrentRow( nRow );
		QMenu menu( this );
		menu.setObjectName( QStringLiteral( "mapLightMenu" ) );
		menu.addAction( tr( "Centre 3D View on It" ), this, [ this ] { m_Editor.RunAction( MapAction_t::MAP_ACTION_CENTER_3D, nullptr ); } );
		menu.addAction( tr( "Centre 2D Views on It" ), this, [ this ] { m_Editor.RunAction( MapAction_t::MAP_ACTION_CENTER_2D, nullptr ); } );
		menu.addSeparator();
		menu.addAction( tr( "Properties" ), this, [ this ] { m_Editor.RunAction( MapAction_t::MAP_ACTION_PROPERTIES, nullptr ); } );
		menu.exec( m_pLights->viewport()->mapToGlobal( at ) );
	} );
	QWidget *pLights = MakeFoldSection( tr( "Lights in the Map" ), m_pLights, QStringLiteral( "mapLightingLightList" ), false );
	m_pLightsHeader = pLights->findChild<QToolButton *>();
	pLayout->addWidget( pLights, 1 );
	pLayout->addStretch( 0 );

	LoadPreferences();
	for ( QSpinBox *pSpin : { m_pBounces, m_pLuxels, m_pDelay, m_pSkySamples } )
	{
		connect( pSpin, &QSpinBox::valueChanged, this, &CMapLightingPanel::StorePreferences );
	}
	connect( m_pBrightness, &QDoubleSpinBox::valueChanged, this, &CMapLightingPanel::StorePreferences );
	connect( m_pMonitorGamma, &QDoubleSpinBox::valueChanged, this, &CMapLightingPanel::StorePreferences );
	connect( m_pRadius, &QDoubleSpinBox::valueChanged, this, &CMapLightingPanel::StorePreferences );
	for ( QCheckBox *pCheck : { m_pHdr, m_pLightOnly, m_pPropShadows, m_pSmoothShadows, m_pAutoBake, m_pInRadius } )
	{
		connect( pCheck, &QCheckBox::toggled, this, &CMapLightingPanel::StorePreferences );
	}
	connect( &CPreferences::Instance(), &CPreferences::MapPreferencesChanged, this, &CMapLightingPanel::LoadPreferences );
	connect( &m_Editor.GetLighting(), &CMapLighting::Progress, this, &CMapLightingPanel::UpdateStatus );
	connect( &m_Editor.GetLighting(), &CMapLighting::Baked, this, &CMapLightingPanel::UpdateStatus );
	connect( &m_Editor.GetDocument(), &CMapDocument::SceneEdited, this, &CMapLightingPanel::Refresh );
	connect( &m_Editor.GetDocument(), &CMapDocument::Loaded, this, &CMapLightingPanel::Refresh );
	Refresh();
	UpdateStatus();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapLightingPanel::LoadPreferences()
{
	const MapPreferences_t &preferences = CPreferences::Instance().GetMapPreferences();
	const QSignalBlocker blockBrightness( m_pBrightness ), blockBounces( m_pBounces ), blockLuxels( m_pLuxels ), blockHdr( m_pHdr ), blockShadows( m_pPropShadows ), blockSmooth( m_pSmoothShadows ), blockAuto( m_pAutoBake );
	const QSignalBlocker blockOnly( m_pLightOnly ), blockGamma( m_pMonitorGamma );
	m_pMonitorGamma->setValue( preferences.m_flLightMonitorGamma );
	m_pLightOnly->setChecked( preferences.m_bLightOnly );
	const QSignalBlocker blockInRadius( m_pInRadius ), blockRadius( m_pRadius ), blockDelay( m_pDelay ), blockSky( m_pSkySamples );
	m_pInRadius->setChecked( preferences.m_bLightInRadius );
	m_pRadius->setValue( preferences.m_flLightRadius );
	m_pDelay->setValue( preferences.m_nLightDelay );
	m_pSkySamples->setValue( preferences.m_nLightSkySamples );
	m_pBrightness->setValue( preferences.m_flLightBrightness );
	m_pBounces->setValue( preferences.m_nLightBounces );
	m_pLuxels->setValue( preferences.m_nLightLuxels );
	m_pHdr->setChecked( preferences.m_bLightHdr );
	m_pPropShadows->setChecked( preferences.m_bLightPropShadows );
	m_pSmoothShadows->setChecked( preferences.m_bLightSmoothShadows );
	m_pAutoBake->setChecked( preferences.m_bLightAutoBake );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapLightingPanel::StorePreferences()
{
	MapPreferences_t preferences = CPreferences::Instance().GetMapPreferences();
	preferences.m_flLightBrightness = m_pBrightness->value();
	preferences.m_flLightMonitorGamma = m_pMonitorGamma->value();
	preferences.m_nLightBounces = m_pBounces->value();
	preferences.m_nLightLuxels = m_pLuxels->value();
	preferences.m_bLightHdr = m_pHdr->isChecked();
	preferences.m_bLightOnly = m_pLightOnly->isChecked();
	preferences.m_bLightPropShadows = m_pPropShadows->isChecked();
	preferences.m_bLightSmoothShadows = m_pSmoothShadows->isChecked();
	preferences.m_bLightAutoBake = m_pAutoBake->isChecked();
	preferences.m_bLightInRadius = m_pInRadius->isChecked();
	preferences.m_flLightRadius = m_pRadius->value();
	preferences.m_nLightDelay = m_pDelay->value();
	preferences.m_nLightSkySamples = m_pSkySamples->value();
	CPreferences::Instance().SetMapPreferences( preferences );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapLightingPanel::UpdateStatus()
{
	const CMapLighting &lighting = m_Editor.GetLighting();
	if ( lighting.IsBaking() )
	{
		m_pStatus->setText( tr( "Baking: %1% (%2)" ).arg( lighting.GetPercent() ).arg( lighting.GetStage().toLower() ) );
	}
	else if ( const auto &pLighting = lighting.GetLighting() )
	{
		QString szText = tr( "%n light(s), %1 by %2 luxels (every %3), in %4 s", nullptr, static_cast<int>( pLighting->m_nLights ) ).arg( pLighting->m_nWidth ).arg( pLighting->m_nHeight ).arg( pLighting->m_nStep ).arg( lighting.GetBakeMs() / 1000.0, 0, 'f', 1 );
		// Where the time went, stage by stage (none yet while only the direct light is in).
		if ( !pLighting->m_Stages.empty() )
		{
			szText += u'\n' + tr( "%1 luxels, %2 triangles in the way:" ).arg( QLocale{}.toString( static_cast<qulonglong>( pLighting->m_nLuxels ) ), QLocale{}.toString( static_cast<qulonglong>( pLighting->m_nTriangles ) ) );
			for ( const xv::map::BakeStage_t &stage : pLighting->m_Stages )
				szText += u'\n' + tr( "  %1: %2 s" ).arg( QString::fromStdString( stage.m_szName ) ).arg( stage.m_flMs / 1000.0, 0, 'f', 2 );
		}
		m_pStatus->setText( szText );
	}
	else
	{
		m_pStatus->setText( tr( "Not baked: show a 3D Lighting Preview, or Bake Lighting" ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: light, light_spot, light_glspot, light_dynamic and light_environment, each with its
//          colour, class and name.
//-----------------------------------------------------------------------------
void CMapLightingPanel::Refresh()
{
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	m_bFilling = true;
	const QSignalBlocker block( m_pLights );
	m_LightEntities.clear();
	m_pLights->clear();
	if ( pScene )
	{
		const auto &entities = std::as_const( pScene->m_Map.m_Entities );
		for ( std::size_t e = 0; e < entities.size(); e++ )
		{
			const xv::map::Entity_t &entity = entities[ e ];
			const std::string_view szClass = entity.GetClassname();
			if ( entity.m_bDeleted || ( szClass != "light" && szClass != "light_spot" && szClass != "light_glspot" && szClass != "light_dynamic" && szClass != "light_environment" ) )
				continue;
			const LightValue_t value = ReadValue( entity.GetValue( "_light" ) );
			QPixmap swatch( 12, 12 );
			swatch.fill( QColor( std::clamp( value.m_Color[ 0 ], 0, 255 ), std::clamp( value.m_Color[ 1 ], 0, 255 ), std::clamp( value.m_Color[ 2 ], 0, 255 ) ) );
			const std::string_view szName = entity.GetValue( "targetname" );
			auto *pItem = new QListWidgetItem( QIcon( swatch ), szName.empty() ? Text( szClass ) : QStringLiteral( "%1  %2" ).arg( Text( szClass ), Text( szName ) ), m_pLights );
			pItem->setToolTip( tr( "%1, brightness %2" ).arg( Text( entity.GetValue( "_light" ) ) ).arg( value.m_flBrightness ) );
			m_LightEntities.push_back( static_cast<std::uint32_t>( e ) );
		}
	}
	m_pLightsHeader->setText( tr( "Lights in the Map (%1)" ).arg( m_LightEntities.size() ) );
	m_bFilling = false;
}

//-----------------------------------------------------------------------------
// Purpose: Restore the lighting choices together, then bake with those defaults.
//-----------------------------------------------------------------------------
void CMapLightingPanel::ResetToDefault()
{
	const MapPreferences_t defaults;
	MapPreferences_t preferences = CPreferences::Instance().GetMapPreferences();
	preferences.m_flLightBrightness = defaults.m_flLightBrightness;
	preferences.m_flLightMonitorGamma = defaults.m_flLightMonitorGamma;
	preferences.m_nLightBounces = defaults.m_nLightBounces;
	preferences.m_nLightLuxels = defaults.m_nLightLuxels;
	preferences.m_bLightHdr = defaults.m_bLightHdr;
	preferences.m_bLightOnly = defaults.m_bLightOnly;
	preferences.m_bLightPropShadows = defaults.m_bLightPropShadows;
	preferences.m_bLightSmoothShadows = defaults.m_bLightSmoothShadows;
	preferences.m_bLightAutoBake = defaults.m_bLightAutoBake;
	preferences.m_bLightInRadius = defaults.m_bLightInRadius;
	preferences.m_flLightRadius = defaults.m_flLightRadius;
	preferences.m_nLightDelay = defaults.m_nLightDelay;
	preferences.m_nLightSkySamples = defaults.m_nLightSkySamples;
	CPreferences::Instance().SetMapPreferences( preferences );
	m_Editor.WantLighting();
	m_Editor.BakeLightingNow();
}
