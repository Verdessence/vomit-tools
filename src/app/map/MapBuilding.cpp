//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapBuilding.h.
//
//===========================================================================//
#include "map/MapBuilding.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CBuildingPreview::CBuildingPreview( QWidget *pParent )
	: QWidget( pParent )
{
	setObjectName( QStringLiteral( "mapBuildingPreview" ) );
	setMinimumSize( 220, 120 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBuildingPreview::SetShapes( std::vector<xv::map::SolidGeometry_t> shapes, const xv::map::Box_t &box )
{
	m_Shapes = std::move( shapes );
	m_Box = box;
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Each half fits the box and what reaches out of it (an arch's add height), at one
//          scale for both, so the two views line up along x.
//-----------------------------------------------------------------------------
void CBuildingPreview::paintEvent( QPaintEvent * )
{
	QPainter painter( this );
	painter.fillRect( rect(), palette().base() );
	xv::map::Box_t bounds = m_Box;
	for ( const auto &shape : m_Shapes )
	{
		bounds.Add( shape.m_Bounds.m_Mins );
		bounds.Add( shape.m_Bounds.m_Maxs );
	}
	if ( bounds.IsEmpty() )
		return;
	const xv::map::Point_t size = bounds.GetSize();
	const QRectF halves[] = { QRectF( rect() ).adjusted( 8, 20, -width() / 2.0 - 4, -8 ), QRectF( rect() ).adjusted( width() / 2.0 + 4, 20, -8, -8 ) };
	const double flScale = std::min( { halves[ 0 ].width() / std::max( size[ 0 ], 1.0 ), halves[ 0 ].height() / std::max( size[ 1 ], 1.0 ), halves[ 1 ].height() / std::max( size[ 2 ], 1.0 ) } );
	const char *pszTitles[] = { QT_TR_NOOP( "Top" ), QT_TR_NOOP( "Front" ) };
	painter.setRenderHint( QPainter::Antialiasing, true );
	for ( int nView = 0; nView < 2; nView++ )
	{
		const QRectF &half = halves[ nView ];
		const std::size_t nUp = nView == 0 ? 1 : 2;
		painter.setPen( palette().text().color() );
		painter.drawText( QPointF{ half.left(), half.top() - 6 }, tr( pszTitles[ nView ] ) );
		auto map = [ & ]( const xv::map::Point_t &p ) {
			const double flX = half.center().x() + ( p[ 0 ] - ( bounds.m_Mins[ 0 ] + bounds.m_Maxs[ 0 ] ) / 2.0 ) * flScale;
			const double flY = half.center().y() - ( p[ nUp ] - ( bounds.m_Mins[ nUp ] + bounds.m_Maxs[ nUp ] ) / 2.0 ) * flScale;
			return QPointF{ flX, flY };
		};
		QPen box{ palette().mid().color(), 1.0, Qt::DashLine };
		painter.setPen( box );
		painter.setBrush( Qt::NoBrush );
		painter.drawRect( QRectF{ map( m_Box.m_Mins ), map( m_Box.m_Maxs ) }.normalized() );
		painter.setPen( QPen{ palette().highlight().color(), 1.0 } );
		QColor fill = palette().highlight().color();
		fill.setAlpha( 40 );
		painter.setBrush( fill );
		for ( const auto &shape : m_Shapes )
		{
			for ( const auto &face : shape.m_Faces )
			{
				QPolygonF polygon;
				for ( const auto &p : face.m_Points )
					polygon << map( p );
				painter.drawPolygon( polygon );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapBuildingPanel::CMapBuildingPanel( QWidget *pParent )
	: QWidget( pParent )
{
	setObjectName( QStringLiteral( "mapBuilding" ) );
	const xv::map::Box_t box{ { 0, 0, 0 }, { 256, 256, 128 } };
	auto *pLayout = new QVBoxLayout( this );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	auto *pTop = new QFormLayout;
	pLayout->addLayout( pTop );
	m_pKind = new QComboBox;
	m_pKind->setObjectName( QStringLiteral( "mapBuildingKind" ) );
	m_pKind->addItem( tr( "Stairs" ), static_cast<int>( xv::map::BuildingKind_t::BUILDING_KIND_STAIRS ) );
	m_pKind->addItem( tr( "Spiral stairs" ), static_cast<int>( xv::map::BuildingKind_t::BUILDING_KIND_SPIRAL_STAIRS ) );
	m_pKind->addItem( tr( "Arch" ), static_cast<int>( xv::map::BuildingKind_t::BUILDING_KIND_ARCH ) );
	m_pKind->addItem( tr( "Room" ), static_cast<int>( xv::map::BuildingKind_t::BUILDING_KIND_ROOM ) );
	m_pKind->addItem( tr( "Wall with windows" ), static_cast<int>( xv::map::BuildingKind_t::BUILDING_KIND_WINDOW_WALL ) );
	m_pKind->addItem( tr( "Railing" ), static_cast<int>( xv::map::BuildingKind_t::BUILDING_KIND_RAILING ) );
	pTop->addRow( tr( "Template" ), m_pKind );

	// The box, as the block tool left it (or the selection's), editable.
	auto *pBox = new QGridLayout;
	const char *pszAxes[] = { "x", "y", "z" };
	pBox->addWidget( new QLabel( tr( "From" ) ), 1, 0 );
	pBox->addWidget( new QLabel( tr( "To" ) ), 2, 0 );
	for ( int a = 0; a < 3; a++ )
	{
		pBox->addWidget( new QLabel( QString::fromLatin1( pszAxes[ a ] ) ), 0, a + 1, Qt::AlignHCenter );
		for ( int nRow = 0; nRow < 2; nRow++ )
		{
			auto *pSpin = new QDoubleSpinBox;
			pSpin->setObjectName( QStringLiteral( "mapBuilding%1%2" ).arg( nRow ? QStringLiteral( "Max" ) : QStringLiteral( "Min" ) ).arg( QLatin1String( pszAxes[ a ] ) ) );
			pSpin->setRange( -65536, 65536 );
			pSpin->setDecimals( 2 );
			pSpin->setValue( nRow ? box.m_Maxs[ a ] : box.m_Mins[ a ] );
			pBox->addWidget( pSpin, nRow + 1, a + 1 );
			( nRow ? m_Maxs : m_Mins )[ a ] = pSpin;
		}
	}
	pTop->addRow( tr( "Box" ), pBox );

	auto spin = [ this ]( const char *pszName, int nMin, int nMax, int nValue ) {
		auto *pSpin = new QSpinBox;
		pSpin->setObjectName( QString::fromLatin1( pszName ) );
		pSpin->setRange( nMin, nMax );
		pSpin->setValue( nValue );
		connect( pSpin, &QSpinBox::valueChanged, this, &CMapBuildingPanel::Update );
		return pSpin;
	};
	auto number = [ this ]( const char *pszName, double flMin, double flMax, double flValue ) {
		auto *pSpin = new QDoubleSpinBox;
		pSpin->setObjectName( QString::fromLatin1( pszName ) );
		pSpin->setRange( flMin, flMax );
		pSpin->setDecimals( 2 );
		pSpin->setValue( flValue );
		connect( pSpin, &QDoubleSpinBox::valueChanged, this, &CMapBuildingPanel::Update );
		return pSpin;
	};
	auto check = [ this ]( const QString &szText, bool bOn ) {
		auto *pCheck = new QCheckBox( szText );
		pCheck->setChecked( bOn );
		connect( pCheck, &QCheckBox::toggled, this, &CMapBuildingPanel::Update );
		return pCheck;
	};
	const xv::map::BuildingParams_t defaults;
	m_pPages = new QStackedWidget;
	auto page = [ this ]() {
		auto *pPage = new QWidget;
		auto *pForm = new QFormLayout( pPage );
		pForm->setContentsMargins( 0, 0, 0, 0 );
		m_pPages->addWidget( pPage );
		return pForm;
	};
	{
		auto *pForm = page();
		m_pSteps = spin( "mapBuildingSteps", 1, 256, defaults.m_nSteps );
		m_pDirection = new QComboBox;
		m_pDirection->setObjectName( QStringLiteral( "mapBuildingDirection" ) );
		m_pDirection->addItems( { tr( "Up toward +x" ), tr( "Up toward -x" ), tr( "Up toward +y" ), tr( "Up toward -y" ) } );
		connect( m_pDirection, &QComboBox::currentIndexChanged, this, &CMapBuildingPanel::Update );
		m_pSolidUnder = check( tr( "Solid underneath" ), defaults.m_bSolidUnder );
		pForm->addRow( tr( "Steps" ), m_pSteps );
		pForm->addRow( tr( "Climbs" ), m_pDirection );
		pForm->addRow( m_pSolidUnder );
	}
	{
		auto *pForm = page();
		m_pSpiralSteps = spin( "mapBuildingSpiralSteps", 1, 512, 16 );
		m_pDegreesPerStep = number( "mapBuildingDegreesPerStep", 1, 179, defaults.m_flDegreesPerStep );
		m_pInnerRadius = number( "mapBuildingInnerRadius", 0, 65536, defaults.m_flInnerRadius );
		m_pSpiralStart = number( "mapBuildingSpiralStart", -360, 360, 0 );
		m_pSpiralSolidUnder = check( tr( "Solid underneath" ), false );
		pForm->addRow( tr( "Steps" ), m_pSpiralSteps );
		pForm->addRow( tr( "Degrees a step" ), m_pDegreesPerStep );
		pForm->addRow( tr( "Inner radius" ), m_pInnerRadius );
		pForm->addRow( tr( "Start angle" ), m_pSpiralStart );
		pForm->addRow( m_pSpiralSolidUnder );
	}
	{
		auto *pForm = page();
		m_pWallWidth = number( "mapBuildingWallWidth", -65536, 65536, defaults.m_flWallWidth );
		m_pWallWidth->setToolTip( tr( "Negative: the walls reach out from the box's ellipse" ) );
		m_pSides = spin( "mapBuildingSides", 3, 100, defaults.m_nSides );
		m_pArc = number( "mapBuildingArc", 8, 360, defaults.m_flArc );
		auto *pCircle = new QPushButton( tr( "Circle" ) );
		connect( pCircle, &QPushButton::clicked, this, [ this ] { m_pArc->setValue( 360 ); } );
		auto *pArcRow = new QHBoxLayout;
		pArcRow->addWidget( m_pArc, 1 );
		pArcRow->addWidget( pCircle );
		m_pStartAngle = number( "mapBuildingStartAngle", -360, 360, defaults.m_flStartAngle );
		m_pAddHeight = number( "mapBuildingAddHeight", -4096, 4096, defaults.m_flAddHeight );
		m_pAddHeight->setToolTip( tr( "Each piece this much above the one before: a winding stair" ) );
		pForm->addRow( tr( "Wall width" ), m_pWallWidth );
		pForm->addRow( tr( "Number of sides" ), m_pSides );
		pForm->addRow( tr( "Arc" ), pArcRow );
		pForm->addRow( tr( "Start angle" ), m_pStartAngle );
		pForm->addRow( tr( "Add height" ), m_pAddHeight );
	}
	{
		auto *pForm = page();
		m_pThickness = number( "mapBuildingThickness", 1, 4096, defaults.m_flThickness );
		auto *pDoors = new QHBoxLayout;
		const QString szWalls[] = { tr( "+x" ), tr( "-x" ), tr( "+y" ), tr( "-y" ) };
		for ( std::size_t n = 0; n < 4; n++ )
		{
			m_Doors[ n ] = check( szWalls[ n ], defaults.m_Doors[ n ] );
			m_Doors[ n ]->setObjectName( QStringLiteral( "mapBuildingDoor%1" ).arg( n ) );
			pDoors->addWidget( m_Doors[ n ] );
		}
		m_pDoorWidth = number( "mapBuildingDoorWidth", 1, 4096, defaults.m_flDoorWidth );
		m_pDoorHeight = number( "mapBuildingDoorHeight", 1, 4096, defaults.m_flDoorHeight );
		pForm->addRow( tr( "Wall thickness" ), m_pThickness );
		pForm->addRow( tr( "Doorways in" ), pDoors );
		pForm->addRow( tr( "Doorway width" ), m_pDoorWidth );
		pForm->addRow( tr( "Doorway height" ), m_pDoorHeight );
	}
	{
		auto *pForm = page();
		m_pColumns = spin( "mapBuildingColumns", 1, 64, defaults.m_nColumns );
		m_pRows = spin( "mapBuildingRows", 1, 64, defaults.m_nRows );
		m_pWindowWidth = number( "mapBuildingWindowWidth", 1, 4096, defaults.m_flWindowWidth );
		m_pWindowHeight = number( "mapBuildingWindowHeight", 1, 4096, defaults.m_flWindowHeight );
		m_pSill = number( "mapBuildingSill", 0, 4096, defaults.m_flSill );
		pForm->addRow( tr( "Columns" ), m_pColumns );
		pForm->addRow( tr( "Rows" ), m_pRows );
		pForm->addRow( tr( "Window width" ), m_pWindowWidth );
		pForm->addRow( tr( "Window height" ), m_pWindowHeight );
		pForm->addRow( tr( "Sill height" ), m_pSill );
	}
	{
		auto *pForm = page();
		m_pPostSize = number( "mapBuildingPostSize", 1, 4096, defaults.m_flPostSize );
		m_pPostSpacing = number( "mapBuildingPostSpacing", 1, 4096, defaults.m_flPostSpacing );
		m_pRailHeight = number( "mapBuildingRailHeight", 1, 4096, defaults.m_flRailHeight );
		pForm->addRow( tr( "Post size" ), m_pPostSize );
		pForm->addRow( tr( "Post spacing" ), m_pPostSpacing );
		pForm->addRow( tr( "Rail height" ), m_pRailHeight );
	}
	pLayout->addWidget( m_pPages );
	m_pPreview = new CBuildingPreview;
	pLayout->addWidget( m_pPreview, 1 );
	m_pProblem = new QLabel;
	m_pProblem->setObjectName( QStringLiteral( "mapBuildingProblem" ) );
	m_pProblem->setWordWrap( true );
	pLayout->addWidget( m_pProblem );
	m_pOk = new QPushButton( tr( "Create" ) );
	m_pOk->setObjectName( QStringLiteral( "mapBuildingCreate" ) );
	m_pOk->setToolTip( tr( "Add the brushes, as one group" ) );
	pLayout->addWidget( m_pOk, 0, Qt::AlignRight );
	connect( m_pOk, &QPushButton::clicked, this, &CMapBuildingPanel::CreateRequested );
	connect( m_pKind, &QComboBox::currentIndexChanged, this, [ this ]( int n ) {
		m_pPages->setCurrentIndex( n );
		Update();
	} );
	for ( auto *pSpin : m_Mins )
		connect( pSpin, &QDoubleSpinBox::valueChanged, this, &CMapBuildingPanel::Update );
	for ( auto *pSpin : m_Maxs )
		connect( pSpin, &QDoubleSpinBox::valueChanged, this, &CMapBuildingPanel::Update );
	Update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::BuildingKind_t CMapBuildingPanel::GetKind() const
{
	return static_cast<xv::map::BuildingKind_t>( m_pKind->currentData().toInt() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::Box_t CMapBuildingPanel::GetBox() const
{
	xv::map::Box_t box;
	for ( std::size_t a = 0; a < 3; a++ )
	{
		box.m_Mins[ a ] = std::min( m_Mins[ a ]->value(), m_Maxs[ a ]->value() );
		box.m_Maxs[ a ] = std::max( m_Mins[ a ]->value(), m_Maxs[ a ]->value() );
	}
	return box;
}

//-----------------------------------------------------------------------------
// Purpose: The block tool's box (or the selection's), when the panel is called up.
//-----------------------------------------------------------------------------
void CMapBuildingPanel::SetBox( const xv::map::Box_t &box )
{
	for ( std::size_t a = 0; a < 3; a++ )
	{
		const QSignalBlocker blockMin( m_Mins[ a ] );
		const QSignalBlocker blockMax( m_Maxs[ a ] );
		m_Mins[ a ]->setValue( box.m_Mins[ a ] );
		m_Maxs[ a ]->setValue( box.m_Maxs[ a ] );
	}
	Update();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::BuildingParams_t CMapBuildingPanel::GetParams() const
{
	xv::map::BuildingParams_t params;
	const bool bSpiral = GetKind() == xv::map::BuildingKind_t::BUILDING_KIND_SPIRAL_STAIRS;
	params.m_nSteps = bSpiral ? m_pSpiralSteps->value() : m_pSteps->value();
	params.m_nDirection = static_cast<xv::map::BuildingDirection_t>( m_pDirection->currentIndex() );
	params.m_bSolidUnder = bSpiral ? m_pSpiralSolidUnder->isChecked() : m_pSolidUnder->isChecked();
	params.m_flDegreesPerStep = m_pDegreesPerStep->value();
	params.m_flInnerRadius = m_pInnerRadius->value();
	params.m_flWallWidth = m_pWallWidth->value();
	params.m_nSides = m_pSides->value();
	params.m_flArc = m_pArc->value();
	params.m_flStartAngle = bSpiral ? m_pSpiralStart->value() : m_pStartAngle->value();
	params.m_flAddHeight = m_pAddHeight->value();
	params.m_flThickness = m_pThickness->value();
	for ( std::size_t n = 0; n < 4; n++ )
		params.m_Doors[ n ] = m_Doors[ n ]->isChecked();
	params.m_flDoorWidth = m_pDoorWidth->value();
	params.m_flDoorHeight = m_pDoorHeight->value();
	params.m_nColumns = m_pColumns->value();
	params.m_nRows = m_pRows->value();
	params.m_flWindowWidth = m_pWindowWidth->value();
	params.m_flWindowHeight = m_pWindowHeight->value();
	params.m_flSill = m_pSill->value();
	params.m_flPostSize = m_pPostSize->value();
	params.m_flPostSpacing = m_pPostSpacing->value();
	params.m_flRailHeight = m_pRailHeight->value();
	return params;
}

//-----------------------------------------------------------------------------
// Purpose: The brushes made on a map of their own, so trying numbers leaves nothing in the
//          real one.
//-----------------------------------------------------------------------------
void CMapBuildingPanel::Update()
{
	if ( !m_pOk )
		return;
	const xv::map::CMap scratch;
	std::string szError;
	const xv::map::Box_t box = GetBox();
	std::vector<xv::map::SolidGeometry_t> shapes;
	for ( const xv::map::Solid_t &solid : xv::map::GenerateBuilding( scratch, GetKind(), box, GetParams(), {}, &szError ) )
		shapes.push_back( xv::map::BuildSolid( solid ) );
	m_pPreview->SetShapes( std::move( shapes ), box );
	m_pProblem->setText( QString::fromStdString( szError ) );
	m_pProblem->setVisible( !szError.empty() );
	m_pOk->setEnabled( szError.empty() );
}
