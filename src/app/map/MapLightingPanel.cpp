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
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "Preferences.h"
#include "map/Edit.h"
#include "map/MapEditor.h"
#include "map/MapLighting.h"

namespace
{

enum Column_t
{
	COLUMN_CLASS,
	COLUMN_NAME,
	COLUMN_COLOUR,
	COLUMN_BRIGHTNESS,
	COLUMN_FIFTY,
	COLUMN_ZERO,
	COLUMN_CONES,
	COLUMN_COUNT,
};

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
std::string WriteValue( const LightValue_t &value )
{
	return QStringLiteral( "%1 %2 %3 %4" ).arg( value.m_Color[ 0 ] ).arg( value.m_Color[ 1 ] ).arg( value.m_Color[ 2 ] ).arg( value.m_flBrightness ).toStdString();
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
	m_pBounces = new QSpinBox;
	m_pBounces->setObjectName( QStringLiteral( "mapLightingBounces" ) );
	m_pBounces->setRange( 0, 8 );
	m_pBounces->setToolTip( tr( "Passes of light bounced between faces; the rest are estimated, as vrad bounces until the light settles" ) );
	pForm->addRow( tr( "Bounces" ), m_pBounces );
	m_pLuxels = new QSpinBox;
	m_pLuxels->setObjectName( QStringLiteral( "mapLightingLuxels" ) );
	m_pLuxels->setRange( 1, 64 );
	m_pLuxels->setSuffix( tr( " million luxels" ) );
	m_pLuxels->setToolTip( tr( "More than this, and every other luxel is lit (and so on), for speed" ) );
	pForm->addRow( tr( "At most" ), m_pLuxels );
	pLayout->addLayout( pForm );
	m_pHdr = new QCheckBox( tr( "The lights' HDR values" ) );
	m_pHdr->setObjectName( QStringLiteral( "mapLightingHdr" ) );
	pLayout->addWidget( m_pHdr );
	m_pPropShadows = new QCheckBox( tr( "prop_static casts shadows" ) );
	m_pPropShadows->setObjectName( QStringLiteral( "mapLightingPropShadows" ) );
	pLayout->addWidget( m_pPropShadows );
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

	m_pTable = new QTableWidget( 0, COLUMN_COUNT );
	m_pTable->setObjectName( QStringLiteral( "mapLightingTable" ) );
	m_pTable->setHorizontalHeaderLabels( { tr( "Class" ), tr( "Name" ), tr( "Colour" ), tr( "Brightness" ), tr( "50%" ), tr( "0%" ), tr( "Cones" ) } );
	m_pTable->horizontalHeaderItem( COLUMN_FIFTY )->setToolTip( tr( "_fifty_percent_distance: where it is half as bright (0: constant, linear and quadratic)" ) );
	m_pTable->horizontalHeaderItem( COLUMN_ZERO )->setToolTip( tr( "_zero_percent_distance: where it has gone" ) );
	m_pTable->horizontalHeaderItem( COLUMN_CONES )->setToolTip( tr( "A spot's inner and outer cones, in degrees" ) );
	m_pTable->verticalHeader()->hide();
	m_pTable->setSelectionBehavior( QAbstractItemView::SelectRows );
	m_pTable->setSelectionMode( QAbstractItemView::SingleSelection );
	m_pTable->horizontalHeader()->setStretchLastSection( true );
	connect( m_pTable, &QTableWidget::itemChanged, this, &CMapLightingPanel::Edited );
	connect( m_pTable, &QTableWidget::cellDoubleClicked, this, [ this ]( int nRow, int nColumn ) {
		if ( nColumn == COLUMN_COLOUR )
		{
			PickColour( nRow );
		}
	} );
	connect( m_pTable, &QTableWidget::currentCellChanged, this, [ this ]( int nRow ) {
		// The light picked in the table is picked in the views too.
		if ( m_bFilling || nRow < 0 || static_cast<std::size_t>( nRow ) >= m_Rows.size() || !m_Editor.GetDocument().GetScene() )
		{
			return;
		}
		m_Editor.GetDocument().SetSelection( { m_Editor.GetDocument().GetScene()->EntityObject( m_Rows[ static_cast<std::size_t>( nRow ) ].m_nEntity ) } );
	} );
	pLayout->addWidget( new QLabel( tr( "Lights (double-click a colour to change it):" ) ) );
	pLayout->addWidget( m_pTable, 1 );

	LoadPreferences();
	for ( QSpinBox *pSpin : { m_pBounces, m_pLuxels } )
	{
		connect( pSpin, &QSpinBox::valueChanged, this, &CMapLightingPanel::StorePreferences );
	}
	connect( m_pBrightness, &QDoubleSpinBox::valueChanged, this, &CMapLightingPanel::StorePreferences );
	for ( QCheckBox *pCheck : { m_pHdr, m_pPropShadows, m_pAutoBake } )
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
	const QSignalBlocker blockBrightness( m_pBrightness ), blockBounces( m_pBounces ), blockLuxels( m_pLuxels ), blockHdr( m_pHdr ), blockShadows( m_pPropShadows ), blockAuto( m_pAutoBake );
	m_pBrightness->setValue( preferences.m_flLightBrightness );
	m_pBounces->setValue( preferences.m_nLightBounces );
	m_pLuxels->setValue( preferences.m_nLightLuxels );
	m_pHdr->setChecked( preferences.m_bLightHdr );
	m_pPropShadows->setChecked( preferences.m_bLightPropShadows );
	m_pAutoBake->setChecked( preferences.m_bLightAutoBake );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapLightingPanel::StorePreferences()
{
	MapPreferences_t preferences = CPreferences::Instance().GetMapPreferences();
	preferences.m_flLightBrightness = m_pBrightness->value();
	preferences.m_nLightBounces = m_pBounces->value();
	preferences.m_nLightLuxels = m_pLuxels->value();
	preferences.m_bLightHdr = m_pHdr->isChecked();
	preferences.m_bLightPropShadows = m_pPropShadows->isChecked();
	preferences.m_bLightAutoBake = m_pAutoBake->isChecked();
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
		m_pStatus->setText( tr( "%n light(s), %1 by %2 luxels (every %3), in %4 s", nullptr, static_cast<int>( pLighting->m_nLights ) ).arg( pLighting->m_nWidth ).arg( pLighting->m_nHeight ).arg( pLighting->m_nStep ).arg( lighting.GetBakeMs() / 1000.0, 0, 'f', 1 ) );
	}
	else
	{
		m_pStatus->setText( tr( "Not baked: show a 3D Lighting Preview, or Bake Lighting" ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: light, light_spot, light_glspot and light_environment (its sun, then its ambient).
//-----------------------------------------------------------------------------
void CMapLightingPanel::Refresh()
{
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	m_bFilling = true;
	const QSignalBlocker block( m_pTable );
	m_Rows.clear();
	m_pTable->setRowCount( 0 );
	if ( !pScene )
	{
		m_bFilling = false;
		return;
	}
	const auto &entities = std::as_const( pScene->m_Map.m_Entities );
	for ( std::size_t e = 0; e < entities.size(); e++ )
	{
		const xv::map::Entity_t &entity = entities[ e ];
		const std::string_view szClass = entity.GetClassname();
		if ( entity.m_bDeleted || ( szClass != "light" && szClass != "light_spot" && szClass != "light_glspot" && szClass != "light_environment" ) )
		{
			continue;
		}
		const bool bSpot = szClass == "light_spot" || szClass == "light_glspot";
		const bool bSun = szClass == "light_environment";
		for ( const std::string_view szKey : { std::string_view{ "_light" }, std::string_view{ "_ambient" } } )
		{
			if ( szKey == "_ambient" && !bSun )
			{
				continue;
			}
			const int nRow = m_pTable->rowCount();
			m_pTable->insertRow( nRow );
			m_Rows.push_back( { static_cast<std::uint32_t>( e ), std::string( szKey ) } );
			auto cell = [ & ]( int nColumn, const QString &szText, bool bEditable ) {
				auto *pItem = new QTableWidgetItem( szText );
				pItem->setFlags( bEditable ? pItem->flags() | Qt::ItemIsEditable : pItem->flags() & ~Qt::ItemIsEditable );
				m_pTable->setItem( nRow, nColumn, pItem );
				return pItem;
			};
			cell( COLUMN_CLASS, szKey == "_ambient" ? tr( "%1 (ambient)" ).arg( Text( szClass ) ) : Text( szClass ), false );
			cell( COLUMN_NAME, Text( entity.GetValue( "targetname" ) ), false );
			const LightValue_t value = ReadValue( entity.GetValue( szKey ) );
			QTableWidgetItem *pColour = cell( COLUMN_COLOUR, QString{}, false );
			pColour->setBackground( QColor( std::clamp( value.m_Color[ 0 ], 0, 255 ), std::clamp( value.m_Color[ 1 ], 0, 255 ), std::clamp( value.m_Color[ 2 ], 0, 255 ) ) );
			pColour->setToolTip( QStringLiteral( "%1 %2 %3" ).arg( value.m_Color[ 0 ] ).arg( value.m_Color[ 1 ] ).arg( value.m_Color[ 2 ] ) );
			cell( COLUMN_BRIGHTNESS, QString::number( value.m_flBrightness ), true );
			const bool bFalloff = !bSun;
			cell( COLUMN_FIFTY, bFalloff ? Text( entity.GetValue( "_fifty_percent_distance" ) ) : QString{}, bFalloff );
			cell( COLUMN_ZERO, bFalloff ? Text( entity.GetValue( "_zero_percent_distance" ) ) : QString{}, bFalloff );
			cell( COLUMN_CONES, bSpot ? QStringLiteral( "%1 / %2" ).arg( Text( entity.GetValue( "_inner_cone" ) ), Text( entity.GetValue( "_cone" ) ) ) : QString{}, bSpot );
		}
	}
	m_pTable->resizeColumnsToContents();
	m_bFilling = false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapLightingPanel::SetKey( std::uint32_t nEntity, const std::string &szKey, const std::string &szValue, const QString &szName )
{
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || nEntity >= pScene->m_Map.m_Entities.size() )
	{
		return;
	}
	xv::map::Entity_t entity = std::as_const( pScene->m_Map.m_Entities )[ nEntity ];
	if ( entity.GetValue( szKey ) == szValue )
	{
		return;
	}
	xv::map::SetKeyValue( pScene->m_Map, entity, szKey, szValue );
	xv::map::MapEdit_t edit;
	edit.m_Entities.emplace_back( nEntity, std::move( entity ) );
	m_Editor.GetDocument().Edit( std::move( edit ), szName );
}

//-----------------------------------------------------------------------------
// Purpose: A cell typed in: the brightness, a falloff distance, or a spot's cones.
//-----------------------------------------------------------------------------
void CMapLightingPanel::Edited( QTableWidgetItem *pItem )
{
	if ( m_bFilling || !pItem || static_cast<std::size_t>( pItem->row() ) >= m_Rows.size() )
	{
		return;
	}
	const Row_t row = m_Rows[ static_cast<std::size_t>( pItem->row() ) ];
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene )
	{
		return;
	}
	const xv::map::Entity_t &entity = std::as_const( pScene->m_Map.m_Entities )[ row.m_nEntity ];
	const QString szText = pItem->text().trimmed();
	switch ( pItem->column() )
	{
		case COLUMN_BRIGHTNESS:
		{
			bool bOk = false;
			const double flBrightness = szText.toDouble( &bOk );
			if ( !bOk || flBrightness < 0.0 )
			{
				break;
			}
			LightValue_t value = ReadValue( entity.GetValue( row.m_szKey ) );
			value.m_flBrightness = flBrightness;
			SetKey( row.m_nEntity, row.m_szKey, WriteValue( value ), tr( "Light Brightness" ) );
			return;
		}
		case COLUMN_FIFTY:
		case COLUMN_ZERO:
		{
			bool bOk = szText.isEmpty();
			const double flDistance = bOk ? 0.0 : szText.toDouble( &bOk );
			if ( !bOk || flDistance < 0.0 )
			{
				break;
			}
			SetKey( row.m_nEntity, pItem->column() == COLUMN_FIFTY ? "_fifty_percent_distance" : "_zero_percent_distance", szText.isEmpty() ? std::string{ "0" } : QString::number( flDistance ).toStdString(), tr( "Light Falloff" ) );
			return;
		}
		case COLUMN_CONES:
		{
			const QStringList parts = szText.split( QLatin1Char( '/' ) );
			bool bInner = false, bOuter = false;
			const double flInner = parts.value( 0 ).trimmed().toDouble( &bInner );
			const double flOuter = parts.size() > 1 ? parts[ 1 ].trimmed().toDouble( &bOuter ) : flInner;
			if ( !bInner || ( parts.size() > 1 && !bOuter ) )
			{
				break;
			}
			// Both keys in one step to undo.
			xv::map::Entity_t changed = entity;
			xv::map::SetKeyValue( pScene->m_Map, changed, "_inner_cone", QString::number( flInner ).toStdString() );
			xv::map::SetKeyValue( pScene->m_Map, changed, "_cone", QString::number( std::max( flInner, flOuter ) ).toStdString() );
			xv::map::MapEdit_t edit;
			edit.m_Entities.emplace_back( row.m_nEntity, std::move( changed ) );
			m_Editor.GetDocument().Edit( std::move( edit ), tr( "Light Cones" ) );
			return;
		}
		default: break;
	}
	// Not a value it takes: back to what the light has.
	Refresh();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapLightingPanel::PickColour( int nRow )
{
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	if ( !pScene || nRow < 0 || static_cast<std::size_t>( nRow ) >= m_Rows.size() )
	{
		return;
	}
	const Row_t row = m_Rows[ static_cast<std::size_t>( nRow ) ];
	LightValue_t value = ReadValue( std::as_const( pScene->m_Map.m_Entities )[ row.m_nEntity ].GetValue( row.m_szKey ) );
	const QColor colour = QColorDialog::getColor( QColor( std::clamp( value.m_Color[ 0 ], 0, 255 ), std::clamp( value.m_Color[ 1 ], 0, 255 ), std::clamp( value.m_Color[ 2 ], 0, 255 ) ), this, tr( "Light Colour" ) );
	if ( !colour.isValid() )
	{
		return;
	}
	value.m_Color = { colour.red(), colour.green(), colour.blue() };
	SetKey( row.m_nEntity, row.m_szKey, WriteValue( value ), tr( "Light Colour" ) );
}
