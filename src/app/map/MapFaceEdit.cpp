//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapFaceEdit.h.
//
//===========================================================================//
#include "MapFaceEdit.h"

#include <cmath>
#include <optional>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "map/MapEditor.h"

namespace
{

//-----------------------------------------------------------------------------
// Purpose: A spin box that shows nothing until a value is put in it, for faces that differ.
//-----------------------------------------------------------------------------
QDoubleSpinBox *MakeSpin( const QString &szName, double flMin, double flMax, double flStep, int nDecimals )
{
	auto *pSpin = new QDoubleSpinBox;
	pSpin->setObjectName( szName );
	pSpin->setRange( flMin, flMax );
	pSpin->setSingleStep( flStep );
	pSpin->setDecimals( nDecimals );
	pSpin->setKeyboardTracking( false );
	pSpin->setSpecialValueText( QStringLiteral( " " ) );
	return pSpin;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapFaceEdit::CMapFaceEdit( CMapEditor &editor, QWidget *pParent )
	: QWidget( pParent ),
	  m_Editor( editor )
{
	setObjectName( QStringLiteral( "mapFaceEdit" ) );
	// Hammer's two tabs: the texture sheet and the Displacement tab.
	auto *pOuter = new QVBoxLayout( this );
	pOuter->setContentsMargins( 0, 0, 0, 0 );
	m_pTabs = new QTabWidget;
	m_pTabs->setObjectName( QStringLiteral( "mapFaceEditTabs" ) );
	auto *pTexturePage = new QWidget;
	auto *pDispPage = new QWidget;
	m_pTabs->addTab( pTexturePage, tr( "Texture" ) );
	m_pTabs->addTab( pDispPage, tr( "Displacement" ) );
	pOuter->addWidget( m_pTabs );
	BuildDisplacementTab( pDispPage );
	auto *pLayout = new QVBoxLayout( pTexturePage );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	auto *pGrid = new QGridLayout;
	// Hammer's fields: the minimum is the "differs" blank, below any value the file can hold.
	m_pScaleX = MakeSpin( QStringLiteral( "mapFaceScaleX" ), -10000, 10000, 0.05, 4 );
	m_pScaleY = MakeSpin( QStringLiteral( "mapFaceScaleY" ), -10000, 10000, 0.05, 4 );
	m_pShiftX = MakeSpin( QStringLiteral( "mapFaceShiftX" ), -100000, 100000, 1, 2 );
	m_pShiftY = MakeSpin( QStringLiteral( "mapFaceShiftY" ), -100000, 100000, 1, 2 );
	m_pRotation = MakeSpin( QStringLiteral( "mapFaceRotation" ), -361, 360, 15, 2 );
	m_pLightmap = new QSpinBox;
	m_pLightmap->setObjectName( QStringLiteral( "mapFaceLightmap" ) );
	m_pLightmap->setRange( 0, 4096 );
	m_pLightmap->setKeyboardTracking( false );
	m_pLightmap->setSpecialValueText( QStringLiteral( " " ) );
	pGrid->addWidget( new QLabel( tr( "Scale X" ) ), 0, 0 );
	pGrid->addWidget( m_pScaleX, 0, 1 );
	pGrid->addWidget( new QLabel( tr( "Y" ) ), 0, 2 );
	pGrid->addWidget( m_pScaleY, 0, 3 );
	pGrid->addWidget( new QLabel( tr( "Shift X" ) ), 1, 0 );
	pGrid->addWidget( m_pShiftX, 1, 1 );
	pGrid->addWidget( new QLabel( tr( "Y" ) ), 1, 2 );
	pGrid->addWidget( m_pShiftY, 1, 3 );
	pGrid->addWidget( new QLabel( tr( "Rotation" ) ), 2, 0 );
	pGrid->addWidget( m_pRotation, 2, 1 );
	pGrid->addWidget( new QLabel( tr( "Lightmap" ) ), 2, 2 );
	pGrid->addWidget( m_pLightmap, 2, 3 );
	pLayout->addLayout( pGrid );

	auto *pJustify = new QHBoxLayout;
	pJustify->addWidget( new QLabel( tr( "Justify" ) ) );
	const struct
	{
		const char *m_pszName;
		QString m_szText;
		xv::map::Justify_t m_nHow;
	} buttons[] = {
		{ "mapFaceJustifyL", tr( "L" ), xv::map::Justify_t::JUSTIFY_LEFT },
		{ "mapFaceJustifyR", tr( "R" ), xv::map::Justify_t::JUSTIFY_RIGHT },
		{ "mapFaceJustifyFit", tr( "Fit" ), xv::map::Justify_t::JUSTIFY_FIT },
		{ "mapFaceJustifyT", tr( "T" ), xv::map::Justify_t::JUSTIFY_TOP },
		{ "mapFaceJustifyB", tr( "B" ), xv::map::Justify_t::JUSTIFY_BOTTOM },
		{ "mapFaceJustifyC", tr( "C" ), xv::map::Justify_t::JUSTIFY_CENTER },
	};
	for ( const auto &button : buttons )
	{
		auto *pButton = new QPushButton( button.m_szText );
		pButton->setObjectName( QString::fromLatin1( button.m_pszName ) );
		pButton->setMaximumWidth( 36 );
		connect( pButton, &QPushButton::clicked, this, [ this, nHow = button.m_nHow ] { Justify( nHow ); } );
		pJustify->addWidget( pButton );
	}
	pJustify->addStretch( 1 );
	pLayout->addLayout( pJustify );
	m_pTreatAsOne = new QCheckBox( tr( "Treat as one" ) );
	m_pTreatAsOne->setObjectName( QStringLiteral( "mapFaceTreatAsOne" ) );
	m_pTreatAsOne->setToolTip( tr( "Justify and fit across all the selected faces together" ) );
	auto *pAlign = new QHBoxLayout;
	pAlign->addWidget( m_pTreatAsOne );
	pAlign->addStretch( 1 );
	pAlign->addWidget( new QLabel( tr( "Align" ) ) );
	m_pAlignWorld = new QRadioButton( tr( "World" ) );
	m_pAlignWorld->setObjectName( QStringLiteral( "mapFaceAlignWorld" ) );
	m_pAlignFace = new QRadioButton( tr( "Face" ) );
	m_pAlignFace->setObjectName( QStringLiteral( "mapFaceAlignFace" ) );
	m_pAlignWorld->setAutoExclusive( false );
	m_pAlignFace->setAutoExclusive( false );
	pAlign->addWidget( m_pAlignWorld );
	pAlign->addWidget( m_pAlignFace );
	pLayout->addLayout( pAlign );

	auto *pTexture = new QHBoxLayout;
	pTexture->addWidget( new QLabel( tr( "Texture" ) ) );
	m_pTexture = new QLineEdit;
	m_pTexture->setObjectName( QStringLiteral( "mapFaceTexture" ) );
	m_pTexture->setToolTip( tr( "The current texture: what Apply and a click in Apply mode put on faces" ) );
	pTexture->addWidget( m_pTexture, 1 );
	auto *pBrowse = new QPushButton( tr( "Browse…" ) );
	pBrowse->setObjectName( QStringLiteral( "mapFaceBrowse" ) );
	pTexture->addWidget( pBrowse );
	pLayout->addLayout( pTexture );

	auto *pBottom = new QHBoxLayout;
	pBottom->addWidget( new QLabel( tr( "Mode" ) ) );
	m_pMode = new QComboBox;
	m_pMode->setObjectName( QStringLiteral( "mapFaceMode" ) );
	m_pMode->addItem( tr( "Lift+Select" ), static_cast<int>( FaceEditMode_t::FACE_EDIT_MODE_LIFT_SELECT ) );
	m_pMode->addItem( tr( "Lift" ), static_cast<int>( FaceEditMode_t::FACE_EDIT_MODE_LIFT ) );
	m_pMode->addItem( tr( "Select" ), static_cast<int>( FaceEditMode_t::FACE_EDIT_MODE_SELECT ) );
	m_pMode->addItem( tr( "Apply (texture only)" ), static_cast<int>( FaceEditMode_t::FACE_EDIT_MODE_APPLY ) );
	m_pMode->addItem( tr( "Apply (texture + values)" ), static_cast<int>( FaceEditMode_t::FACE_EDIT_MODE_APPLY_VALUES ) );
	pBottom->addWidget( m_pMode, 1 );
	auto *pApply = new QPushButton( tr( "Apply" ) );
	pApply->setObjectName( QStringLiteral( "mapFaceApply" ) );
	pApply->setToolTip( tr( "The current texture and the sheet's values onto the selected faces" ) );
	pBottom->addWidget( pApply );
	pLayout->addLayout( pBottom );
	pLayout->addStretch( 1 );

	auto spin = [ this ]( QDoubleSpinBox *pSpin, const QString &szName, auto pfnSet ) {
		connect( pSpin, &QDoubleSpinBox::valueChanged, this, [ this, pSpin, szName, pfnSet ]( double flValue ) {
			if ( !m_bFilling && flValue > pSpin->minimum() )
			{
				FieldChanged( szName, [ pfnSet, flValue ]( xv::map::Side_t &side ) { pfnSet( side, flValue ); } );
			}
		} );
	};
	spin( m_pScaleX, tr( "Texture Scale" ), []( xv::map::Side_t &side, double fl ) { side.m_UAxis.m_flScale = fl; } );
	spin( m_pScaleY, tr( "Texture Scale" ), []( xv::map::Side_t &side, double fl ) { side.m_VAxis.m_flScale = fl; } );
	spin( m_pShiftX, tr( "Texture Shift" ), []( xv::map::Side_t &side, double fl ) { side.m_UAxis.m_flShift = fl; } );
	spin( m_pShiftY, tr( "Texture Shift" ), []( xv::map::Side_t &side, double fl ) { side.m_VAxis.m_flShift = fl; } );
	connect( m_pRotation, &QDoubleSpinBox::valueChanged, this, [ this ]( double flValue ) {
		if ( !m_bFilling && flValue > m_pRotation->minimum() )
		{
			m_Editor.EditSelectedFaces( tr( "Texture Rotation" ), [ flValue ]( xv::map::Side_t &side, const xv::map::Point_t &normal, const std::vector<xv::map::Point_t> & ) { xv::map::SetRotation( side, normal, flValue ); } );
		}
	} );
	connect( m_pLightmap, &QSpinBox::valueChanged, this, [ this ]( int nValue ) {
		if ( !m_bFilling && nValue > 0 )
		{
			FieldChanged( tr( "Lightmap Scale" ), [ nValue ]( xv::map::Side_t &side ) { side.m_nLightmapScale = nValue; } );
		}
	} );
	connect( m_pAlignWorld, &QRadioButton::clicked, this, [ this ] { Align( true ); } );
	connect( m_pAlignFace, &QRadioButton::clicked, this, [ this ] { Align( false ); } );
	connect( m_pTexture, &QLineEdit::editingFinished, this, [ this ] { m_Editor.SetCurrentMaterial( m_pTexture->text() ); } );
	connect( pBrowse, &QPushButton::clicked, this, [ this ] { m_Editor.BrowseMaterial(); } );
	connect( pApply, &QPushButton::clicked, this, [ this ] { m_Editor.ApplyCurrentTexture( true ); } );
}

//-----------------------------------------------------------------------------
// Purpose: Hammer's Displacement tab: the buttons that work on the selected faces at once,
//          the attributes, Noise's range, and the brush the 3D views paint with.
//-----------------------------------------------------------------------------
void CMapFaceEdit::BuildDisplacementTab( QWidget *pPage )
{
	auto *pLayout = new QVBoxLayout( pPage );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	auto button = [ & ]( QGridLayout *pGrid, int nRow, int nColumn, const QString &szText, const QString &szName, const QString &szTip, auto pfn ) {
		auto *pButton = new QPushButton( szText );
		pButton->setObjectName( szName );
		pButton->setToolTip( szTip );
		connect( pButton, &QPushButton::clicked, this, pfn );
		pGrid->addWidget( pButton, nRow, nColumn );
		return pButton;
	};
	auto *pButtons = new QGridLayout;
	button( pButtons, 0, 0, tr( "Create" ), QStringLiteral( "mapDispCreate" ), tr( "A displacement at the power below on each selected four-sided face" ), [ this ] { m_Editor.CreateDisplacements( GetDispPower() ); } );
	button( pButtons, 0, 1, tr( "Destroy" ), QStringLiteral( "mapDispDestroy" ), tr( "The selected faces' displacements taken off: plain faces again" ), [ this ] { m_Editor.DestroyDisplacements(); } );
	button( pButtons, 0, 2, tr( "Invert Alpha" ), QStringLiteral( "mapDispInvertAlpha" ), tr( "The two textures of a blended material swapped over the selected displacements" ), [ this ] { m_Editor.InvertDisplacementAlpha(); } );
	button( pButtons, 1, 0, tr( "Sew" ), QStringLiteral( "mapDispSew" ), tr( "The selected displacements' edges put together where their faces meet (and onto plain faces among them)" ), [ this ] { m_Editor.SewDisplacements(); } );
	button( pButtons, 1, 1, tr( "Subdivide" ), QStringLiteral( "mapDispSubdivide" ), tr( "The selected displacements smoothed together as one surface" ), [ this ] { m_Editor.SubdivideDisplacements(); } );
	button( pButtons, 1, 2, tr( "Noise" ), QStringLiteral( "mapDispNoise" ), tr( "Every point moved along its face's normal by a random amount in the range" ), [ this ] { m_Editor.NoiseDisplacements( m_pNoiseMin->value(), m_pNoiseMax->value() ); } );
	pLayout->addLayout( pButtons );

	auto *pForm = new QGridLayout;
	m_pPower = new QSpinBox;
	m_pPower->setObjectName( QStringLiteral( "mapDispPower" ) );
	m_pPower->setRange( 2, 4 );
	m_pPower->setValue( 3 );
	m_pPower->setToolTip( tr( "2: a 5 by 5 grid, 3: 9 by 9, 4: 17 by 17 (heavy, and prone to physics trouble)" ) );
	m_pElevation = new QDoubleSpinBox;
	m_pElevation->setObjectName( QStringLiteral( "mapDispElevation" ) );
	m_pElevation->setRange( -100000, 100000 );
	m_pElevation->setDecimals( 2 );
	m_pElevation->setToolTip( tr( "How far the whole surface stands off its face, along the normal" ) );
	auto *pApply = new QPushButton( tr( "Apply" ) );
	pApply->setObjectName( QStringLiteral( "mapDispApply" ) );
	pApply->setToolTip( tr( "The power and elevation onto the selected displacements (another power resamples the surface)" ) );
	connect( pApply, &QPushButton::clicked, this, [ this ] { m_Editor.SetDisplacementAttributes( GetDispPower(), m_pElevation->value() ); } );
	pForm->addWidget( new QLabel( tr( "Power" ) ), 0, 0 );
	pForm->addWidget( m_pPower, 0, 1 );
	pForm->addWidget( new QLabel( tr( "Elevation" ) ), 0, 2 );
	pForm->addWidget( m_pElevation, 0, 3 );
	pForm->addWidget( pApply, 0, 4 );
	m_pNoiseMin = new QDoubleSpinBox;
	m_pNoiseMin->setObjectName( QStringLiteral( "mapDispNoiseMin" ) );
	m_pNoiseMin->setRange( -10000, 10000 );
	m_pNoiseMin->setValue( -4.0 );
	m_pNoiseMax = new QDoubleSpinBox;
	m_pNoiseMax->setObjectName( QStringLiteral( "mapDispNoiseMax" ) );
	m_pNoiseMax->setRange( -10000, 10000 );
	m_pNoiseMax->setValue( 4.0 );
	pForm->addWidget( new QLabel( tr( "Noise" ) ), 1, 0 );
	pForm->addWidget( m_pNoiseMin, 1, 1 );
	pForm->addWidget( new QLabel( tr( "to" ) ), 1, 2 );
	pForm->addWidget( m_pNoiseMax, 1, 3 );
	pLayout->addLayout( pForm );

	auto *pPaint = new QGridLayout;
	m_pPaintMode = new QComboBox;
	m_pPaintMode->setObjectName( QStringLiteral( "mapDispPaintMode" ) );
	m_pPaintMode->addItem( tr( "Select faces" ), static_cast<int>( DispPaintMode_t::DISP_PAINT_NONE ) );
	m_pPaintMode->addItem( tr( "Paint geometry" ), static_cast<int>( DispPaintMode_t::DISP_PAINT_GEOMETRY ) );
	m_pPaintMode->addItem( tr( "Paint alpha" ), static_cast<int>( DispPaintMode_t::DISP_PAINT_ALPHA ) );
	m_pPaintMode->setToolTip( tr( "What a click in a 3D view does: the left button raises (or adds alpha), the right lowers" ) );
	m_pEffect = new QComboBox;
	m_pEffect->setObjectName( QStringLiteral( "mapDispEffect" ) );
	m_pEffect->addItem( tr( "Raise/Lower" ), static_cast<int>( xv::map::PaintEffect_t::PAINT_EFFECT_RAISE_LOWER ) );
	m_pEffect->addItem( tr( "Raise To" ), static_cast<int>( xv::map::PaintEffect_t::PAINT_EFFECT_RAISE_TO ) );
	m_pEffect->addItem( tr( "Smooth" ), static_cast<int>( xv::map::PaintEffect_t::PAINT_EFFECT_SMOOTH ) );
	m_pAxis = new QComboBox;
	m_pAxis->setObjectName( QStringLiteral( "mapDispAxis" ) );
	m_pAxis->addItem( tr( "Face normal" ), -1 );
	m_pAxis->addItem( QStringLiteral( "X" ), 0 );
	m_pAxis->addItem( QStringLiteral( "Y" ), 1 );
	m_pAxis->addItem( QStringLiteral( "Z" ), 2 );
	// The panel is narrow: the boxes may shrink below their longest entry.
	for ( QComboBox *pBox : { m_pEffect, m_pAxis } )
	{
		pBox->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
		pBox->setMinimumContentsLength( 8 );
	}
	m_pRadius = new QDoubleSpinBox;
	m_pRadius->setObjectName( QStringLiteral( "mapDispRadius" ) );
	m_pRadius->setRange( 1, 8192 );
	m_pRadius->setValue( 64.0 );
	m_pRadius->setToolTip( tr( "The brush's reach, in units ([ and ] in a 3D view)" ) );
	m_pAmount = new QDoubleSpinBox;
	m_pAmount->setObjectName( QStringLiteral( "mapDispAmount" ) );
	m_pAmount->setRange( -10000, 10000 );
	m_pAmount->setValue( 8.0 );
	m_pAmount->setToolTip( tr( "How far a stroke moves points (Raise/Lower), the height it puts them at (Raise To), or how much it smooths (0 to 1); alpha in 0 to 255" ) );
	pPaint->addWidget( new QLabel( tr( "Mode" ) ), 0, 0 );
	pPaint->addWidget( m_pPaintMode, 0, 1, 1, 3 );
	pPaint->addWidget( new QLabel( tr( "Effect" ) ), 1, 0 );
	pPaint->addWidget( m_pEffect, 1, 1 );
	pPaint->addWidget( new QLabel( tr( "Axis" ) ), 1, 2 );
	pPaint->addWidget( m_pAxis, 1, 3 );
	pPaint->addWidget( new QLabel( tr( "Radius" ) ), 2, 0 );
	pPaint->addWidget( m_pRadius, 2, 1 );
	pPaint->addWidget( new QLabel( tr( "Distance" ) ), 2, 2 );
	pPaint->addWidget( m_pAmount, 2, 3 );
	pLayout->addLayout( pPaint );
	pLayout->addStretch( 1 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
DispPaintMode_t CMapFaceEdit::GetDispPaintMode() const
{
	return static_cast<DispPaintMode_t>( m_pPaintMode->currentData().toInt() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapFaceEdit::SetDispPaintMode( DispPaintMode_t nMode )
{
	m_pPaintMode->setCurrentIndex( m_pPaintMode->findData( static_cast<int>( nMode ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: The axis is a world axis here, or zero for the face's normal (the view fills it in).
//-----------------------------------------------------------------------------
xv::map::DispBrush_t CMapFaceEdit::GetBrush() const
{
	xv::map::DispBrush_t brush;
	brush.m_flRadius = m_pRadius->value();
	brush.m_flAmount = m_pAmount->value();
	brush.m_nEffect = static_cast<xv::map::PaintEffect_t>( m_pEffect->currentData().toInt() );
	brush.m_Axis = {};
	if ( const int nAxis = m_pAxis->currentData().toInt(); nAxis >= 0 )
	{
		brush.m_Axis[ static_cast<std::size_t>( nAxis ) ] = 1.0;
	}
	return brush;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapFaceEdit::IsBrushAlongFaceNormal() const
{
	return m_pAxis->currentData().toInt() < 0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CMapFaceEdit::GetBrushRadius() const
{
	return m_pRadius->value();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapFaceEdit::SetBrushRadius( double flRadius )
{
	m_pRadius->setValue( flRadius );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CMapFaceEdit::GetDispPower() const
{
	return m_pPower->value();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapFaceEdit::ShowTab( int nTab )
{
	m_pTabs->setCurrentIndex( nTab );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
FaceEditMode_t CMapFaceEdit::GetMode() const
{
	return static_cast<FaceEditMode_t>( m_pMode->currentData().toInt() );
}

//-----------------------------------------------------------------------------
// Purpose: A field blank (its "differs" minimum) where the faces don't agree.
//-----------------------------------------------------------------------------
void CMapFaceEdit::Refresh()
{
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	const std::vector<xv::map::FaceRef_t> &faces = m_Editor.GetSelectedFaces();
	m_bFilling = true;
	m_pTexture->setText( m_Editor.GetCurrentMaterial() );
	std::optional<double> flScaleX, flScaleY, flShiftX, flShiftY, flRotation;
	std::optional<int> nLightmap;
	std::optional<bool> bWorld;
	bool bFirst = true;
	auto common = [ & ]( auto &value, auto flNow ) {
		if ( bFirst )
		{
			value = flNow;
		}
		else if ( value && *value != flNow )
		{
			value.reset();
		}
	};
	for ( const xv::map::FaceRef_t &face : faces )
	{
		if ( !pScene || face.m_nSolid >= pScene->m_Map.m_Solids.size() || face.m_nSide >= pScene->m_Map.m_Solids[ face.m_nSolid ].m_Sides.size() )
		{
			continue;
		}
		const xv::map::Side_t &side = pScene->m_Map.m_Solids[ face.m_nSolid ].m_Sides[ face.m_nSide ];
		common( flScaleX, side.m_UAxis.m_flScale );
		common( flScaleY, side.m_VAxis.m_flScale );
		common( flShiftX, side.m_UAxis.m_flShift );
		common( flShiftY, side.m_VAxis.m_flShift );
		common( flRotation, side.m_flRotation );
		common( nLightmap, side.m_nLightmapScale );
		const xv::map::FaceGeometry_t *pFace = pScene->m_Solids[ face.m_nSolid ].FindFace( face.m_nSide );
		common( bWorld, pFace && xv::map::IsWorldAligned( side, pFace->m_Plane.m_Normal ) );
		bFirst = false;
	}
	auto show = []( QDoubleSpinBox *pSpin, const std::optional<double> &value ) { pSpin->setValue( value.value_or( pSpin->minimum() ) ); };
	show( m_pScaleX, flScaleX );
	show( m_pScaleY, flScaleY );
	show( m_pShiftX, flShiftX );
	show( m_pShiftY, flShiftY );
	show( m_pRotation, flRotation );
	m_pLightmap->setValue( nLightmap.value_or( 0 ) );
	m_pAlignWorld->setChecked( bWorld.value_or( false ) );
	m_pAlignFace->setChecked( bWorld && !*bWorld );
	// The Displacement tab shows the selected displacements' power and elevation when they agree.
	std::optional<int> nPower;
	std::optional<double> flElevation;
	bFirst = true;
	for ( const xv::map::FaceRef_t &face : pScene ? xv::map::DisplacementFaces( *pScene, faces ) : std::vector<xv::map::FaceRef_t>{} )
	{
		const xv::map::DispInfo_t &disp = *pScene->m_Map.m_Solids[ face.m_nSolid ].m_Sides[ face.m_nSide ].m_pDisp;
		common( nPower, disp.m_nPower );
		common( flElevation, disp.m_flElevation );
		bFirst = false;
	}
	if ( nPower )
	{
		m_pPower->setValue( *nPower );
	}
	if ( flElevation )
	{
		m_pElevation->setValue( *flElevation );
	}
	m_bFilling = false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapFaceEdit::LiftFrom( const xv::map::Side_t &side )
{
	m_bFilling = true;
	m_pScaleX->setValue( side.m_UAxis.m_flScale );
	m_pScaleY->setValue( side.m_VAxis.m_flScale );
	m_pShiftX->setValue( side.m_UAxis.m_flShift );
	m_pShiftY->setValue( side.m_VAxis.m_flShift );
	m_pRotation->setValue( side.m_flRotation );
	m_pLightmap->setValue( side.m_nLightmapScale );
	m_bFilling = false;
	m_Editor.SetCurrentMaterial( QString::fromUtf8( side.m_szMaterial.data(), static_cast<qsizetype>( side.m_szMaterial.size() ) ) );
	m_pTexture->setText( m_Editor.GetCurrentMaterial() );
}

//-----------------------------------------------------------------------------
// Purpose: The sheet's values (those it has) onto a side.
//-----------------------------------------------------------------------------
void CMapFaceEdit::ApplyValuesTo( xv::map::Side_t &side ) const
{
	auto has = []( const QDoubleSpinBox *pSpin ) { return pSpin->value() > pSpin->minimum(); };
	if ( has( m_pScaleX ) )
	{
		side.m_UAxis.m_flScale = m_pScaleX->value();
	}
	if ( has( m_pScaleY ) )
	{
		side.m_VAxis.m_flScale = m_pScaleY->value();
	}
	if ( has( m_pShiftX ) )
	{
		side.m_UAxis.m_flShift = m_pShiftX->value();
	}
	if ( has( m_pShiftY ) )
	{
		side.m_VAxis.m_flShift = m_pShiftY->value();
	}
	if ( m_pLightmap->value() > 0 )
	{
		side.m_nLightmapScale = m_pLightmap->value();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapFaceEdit::FieldChanged( const QString &szName, const std::function<void( xv::map::Side_t & )> &pfnSet )
{
	m_Editor.EditSelectedFaces( szName, [ pfnSet ]( xv::map::Side_t &side, const xv::map::Point_t &, const std::vector<xv::map::Point_t> & ) { pfnSet( side ); } );
}

//-----------------------------------------------------------------------------
// Purpose: Treated as one, the faces share one extent (a texture fitted across them all);
//          else each is justified on its own.
//-----------------------------------------------------------------------------
void CMapFaceEdit::Justify( xv::map::Justify_t nHow )
{
	const bool bAsOne = m_pTreatAsOne->isChecked();
	const xv::map::CScene *pScene = m_Editor.GetDocument().GetScene();
	std::optional<xv::map::TexelExtent_t> shared;
	if ( bAsOne && pScene )
	{
		// The first face's axes measure them all.
		for ( const xv::map::FaceRef_t &face : m_Editor.GetSelectedFaces() )
		{
			const xv::map::FaceGeometry_t *pFace = face.m_nSolid < pScene->m_Solids.size() ? pScene->m_Solids[ face.m_nSolid ].FindFace( face.m_nSide ) : nullptr;
			if ( !pFace )
			{
				continue;
			}
			const xv::map::Side_t &side = pScene->m_Map.m_Solids[ face.m_nSolid ].m_Sides[ face.m_nSide ];
			const xv::map::TexelExtent_t extent = xv::map::ExtentOf( shared ? pScene->m_Map.m_Solids[ m_Editor.GetSelectedFaces().front().m_nSolid ].m_Sides[ m_Editor.GetSelectedFaces().front().m_nSide ] : side, pFace->m_Points );
			if ( !shared )
			{
				shared = extent;
				continue;
			}
			shared->m_flMinU = std::min( shared->m_flMinU, extent.m_flMinU );
			shared->m_flMaxU = std::max( shared->m_flMaxU, extent.m_flMaxU );
			shared->m_flMinV = std::min( shared->m_flMinV, extent.m_flMinV );
			shared->m_flMaxV = std::max( shared->m_flMaxV, extent.m_flMaxV );
		}
	}
	m_Editor.EditSelectedFaces( tr( "Justify" ), [ this, nHow, shared ]( xv::map::Side_t &side, const xv::map::Point_t &, const std::vector<xv::map::Point_t> &points ) {
		const std::array<int, 2> size = m_Editor.GetMaterialSize( side.m_nMaterial );
		xv::map::Justify( side, nHow, shared.value_or( xv::map::ExtentOf( side, points ) ), size[ 0 ], size[ 1 ] );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapFaceEdit::Align( bool bWorld )
{
	m_Editor.EditSelectedFaces( bWorld ? tr( "Align to World" ) : tr( "Align to Face" ), [ bWorld ]( xv::map::Side_t &side, const xv::map::Point_t &normal, const std::vector<xv::map::Point_t> & ) { xv::map::SetAlignment( side, normal, bWorld ); } );
}
