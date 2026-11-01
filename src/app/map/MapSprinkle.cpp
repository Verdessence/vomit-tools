//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapSprinkle.h.
//
//===========================================================================//
#include "map/MapSprinkle.h"

#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapSprinklePanel::CMapSprinklePanel( QWidget *pParent )
	: QWidget( pParent )
{
	setObjectName( QStringLiteral( "mapSprinkle" ) );
	auto *pForm = new QFormLayout( this );
	pForm->setContentsMargins( 0, 0, 0, 0 );

	auto *pTypeRow = new QHBoxLayout;
	m_pType = new QComboBox;
	m_pType->setObjectName( QStringLiteral( "mapSprinkleType" ) );
	m_pType->setToolTip( tr( "The definitions in the game's scripts/hammer/sprinkle and Hammer++'s scripts/sprinkle" ) );
	auto *pReload = new QPushButton( tr( "Reload" ) );
	pReload->setObjectName( QStringLiteral( "mapSprinkleReload" ) );
	pTypeRow->addWidget( m_pType, 1 );
	pTypeRow->addWidget( pReload );
	pForm->addRow( tr( "Type" ), pTypeRow );

	m_pMode = new QComboBox;
	m_pMode->setObjectName( QStringLiteral( "mapSprinkleMode" ) );
	m_pMode->addItem( tr( "Additive" ), static_cast<int>( xv::map::SprinkleMode_t::SPRINKLE_MODE_ADDITIVE ) );
	m_pMode->addItem( tr( "Subtractive" ), static_cast<int>( xv::map::SprinkleMode_t::SPRINKLE_MODE_SUBTRACTIVE ) );
	m_pMode->addItem( tr( "Replace" ), static_cast<int>( xv::map::SprinkleMode_t::SPRINKLE_MODE_REPLACE ) );
	m_pMode->addItem( tr( "Overwrite" ), static_cast<int>( xv::map::SprinkleMode_t::SPRINKLE_MODE_OVERWRITE ) );
	m_pMode->setToolTip( tr( "Ctrl while painting takes away instead" ) );
	pForm->addRow( tr( "Mode" ), m_pMode );

	auto *pDensityRow = new QHBoxLayout;
	m_pDensity = new QSlider( Qt::Horizontal );
	m_pDensity->setObjectName( QStringLiteral( "mapSprinkleDensity" ) );
	m_pDensity->setRange( 0, 100 );
	m_pDensity->setValue( 100 );
	m_pDensityValue = new QLabel( QStringLiteral( "100%" ) );
	m_pDensityValue->setMinimumWidth( fontMetrics().horizontalAdvance( QStringLiteral( "100%" ) ) );
	pDensityRow->addWidget( m_pDensity, 1 );
	pDensityRow->addWidget( m_pDensityValue );
	pForm->addRow( tr( "Density" ), pDensityRow );
	connect( m_pDensity, &QSlider::valueChanged, this, [ this ]( int n ) { m_pDensityValue->setText( QStringLiteral( "%1%" ).arg( n ) ); } );

	m_pDefinitionGrid = new QCheckBox( tr( "Use grid size from definition" ) );
	m_pDefinitionGrid->setObjectName( QStringLiteral( "mapSprinkleDefinitionGrid" ) );
	m_pDefinitionGrid->setChecked( true );
	pForm->addRow( m_pDefinitionGrid );
	auto *pGridRow = new QHBoxLayout;
	auto spin = [ & ]( const char *pszName, double flMin, double flMax, double flValue ) {
		auto *pSpin = new QDoubleSpinBox;
		pSpin->setObjectName( QString::fromLatin1( pszName ) );
		pSpin->setRange( flMin, flMax );
		pSpin->setDecimals( 0 );
		pSpin->setValue( flValue );
		return pSpin;
	};
	m_pGridX = spin( "mapSprinkleGridX", 1, 4096, 64 );
	m_pGridY = spin( "mapSprinkleGridY", 1, 4096, 64 );
	auto *pUseGrid = new QPushButton( tr( "Use Grid" ) );
	pUseGrid->setObjectName( QStringLiteral( "mapSprinkleUseGrid" ) );
	pUseGrid->setToolTip( tr( "The editor's snap grid" ) );
	pGridRow->addWidget( m_pGridX );
	pGridRow->addWidget( m_pGridY );
	pGridRow->addWidget( pUseGrid );
	pForm->addRow( tr( "Grid" ), pGridRow );
	auto enableGrid = [ this, pUseGrid ] {
		const bool bOwn = !m_pDefinitionGrid->isChecked();
		m_pGridX->setEnabled( bOwn );
		m_pGridY->setEnabled( bOwn );
		pUseGrid->setEnabled( bOwn );
	};
	connect( m_pDefinitionGrid, &QCheckBox::toggled, this, enableGrid );
	enableGrid();
	connect( pUseGrid, &QPushButton::clicked, this, [ this ] {
		m_pGridX->setValue( m_nEditorGrid );
		m_pGridY->setValue( m_nEditorGrid );
	} );

	m_pRandomYaw = new QCheckBox( tr( "Random yaw" ) );
	m_pRandomYaw->setObjectName( QStringLiteral( "mapSprinkleRandomYaw" ) );
	m_pRandomYaw->setChecked( true );
	pForm->addRow( m_pRandomYaw );
	m_pOffset = spin( "mapSprinkleOffset", -1024, 1024, 0 );
	m_pOffset->setToolTip( tr( "Out from the surface, for props whose bottom isn't at their origin" ) );
	pForm->addRow( tr( "Offset" ), m_pOffset );
	m_pRadius = spin( "mapSprinkleRadius", 1, 8192, 128 );
	m_pRadius->setToolTip( tr( "Right-drag in the 3D view, or [ and ], to size the brush" ) );
	pForm->addRow( tr( "Radius" ), m_pRadius );
	m_pNotes = new QLabel;
	m_pNotes->setObjectName( QStringLiteral( "mapSprinkleNotes" ) );
	m_pNotes->setWordWrap( true );
	m_pNotes->setTextInteractionFlags( Qt::TextSelectableByMouse );
	pForm->addRow( m_pNotes );

	connect( m_pType, &QComboBox::currentIndexChanged, this, &CMapSprinklePanel::TypeChanged );
	connect( pReload, &QPushButton::clicked, this, [ this ] { Load( m_GameDir ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapSprinklePanel::Load( const std::optional<std::filesystem::path> &gameDir )
{
	m_GameDir = gameDir;
	const QString szWas = m_pType->currentText();
	std::vector<std::string> errors;
	m_Types.clear();
	if ( gameDir )
	{
		const auto folders = xv::map::SprinkleFolders( *gameDir );
		m_Types = xv::map::FindSprinkleTypes( folders, &errors );
	}
	{
		const QSignalBlocker blocker( m_pType );
		m_pType->clear();
		for ( const auto &type : m_Types )
		{
			m_pType->addItem( QString::fromStdString( type.m_szName ) );
			m_pType->setItemData( m_pType->count() - 1, QString::fromStdString( type.m_File.string() ), Qt::ToolTipRole );
		}
		const int nWas = m_pType->findText( szWas );
		if ( nWas >= 0 )
			m_pType->setCurrentIndex( nWas );
	}
	QStringList notes;
	if ( m_Types.empty() )
		notes << tr( "No sprinkle definitions: put .txt files in the game's scripts/hammer/sprinkle." );
	for ( const std::string &szError : errors )
		notes << QString::fromStdString( szError );
	m_pNotes->setText( notes.join( QLatin1Char( '\n' ) ) );
	m_pNotes->setVisible( !notes.isEmpty() );
	TypeChanged();
}

//-----------------------------------------------------------------------------
// Purpose: The grid boxes show the definition's grid while it is the one used.
//-----------------------------------------------------------------------------
void CMapSprinklePanel::TypeChanged()
{
	if ( const xv::map::SprinkleType_t *pType = GetType(); pType && m_pDefinitionGrid->isChecked() )
	{
		m_pGridX->setValue( pType->m_Grid[ 0 ] );
		m_pGridY->setValue( pType->m_Grid[ 1 ] );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const xv::map::SprinkleType_t *CMapSprinklePanel::GetType() const
{
	const int n = m_pType->currentIndex();
	return n >= 0 && n < static_cast<int>( m_Types.size() ) ? &m_Types[ static_cast<std::size_t>( n ) ] : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::SprinkleOptions_t CMapSprinklePanel::GetOptions( std::uint32_t nSeed ) const
{
	xv::map::SprinkleOptions_t options;
	options.m_nMode = static_cast<xv::map::SprinkleMode_t>( m_pMode->currentData().toInt() );
	options.m_flDensity = m_pDensity->value() / 100.0;
	if ( !m_pDefinitionGrid->isChecked() )
		options.m_Grid = std::array<double, 2>{ m_pGridX->value(), m_pGridY->value() };
	options.m_bRandomYaw = m_pRandomYaw->isChecked();
	options.m_flOffset = m_pOffset->value();
	options.m_nSeed = nSeed;
	return options;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CMapSprinklePanel::GetRadius() const
{
	return m_pRadius->value();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapSprinklePanel::SetRadius( double flRadius )
{
	m_pRadius->setValue( flRadius );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CMapSprinklePanel::GetDensity() const
{
	return m_pDensity->value() / 100.0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapSprinklePanel::SetDensity( double flDensity )
{
	m_pDensity->setValue( static_cast<int>( std::lround( std::clamp( flDensity, 0.0, 1.0 ) * 100.0 ) ) );
}
