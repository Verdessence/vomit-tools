//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer's Transform and Paste Special dialogs.
//
//===========================================================================//
#include "MapDialogs.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace
{

//-----------------------------------------------------------------------------
// Purpose: A row of three spin boxes labelled X, Y and Z.
//-----------------------------------------------------------------------------
QWidget *MakeTriple( std::array<QDoubleSpinBox *, 3> &boxes, const QString &szName, double flMin, double flMax, double flDefault )
{
	auto *pRow = new QWidget;
	auto *pLayout = new QHBoxLayout( pRow );
	pLayout->setContentsMargins( 0, 0, 0, 0 );
	const char *labels[] = { "X", "Y", "Z" };
	for ( std::size_t a = 0; a < 3; a++ )
	{
		boxes[ a ] = new QDoubleSpinBox;
		boxes[ a ]->setObjectName( szName + QLatin1String( labels[ a ] ) );
		boxes[ a ]->setRange( flMin, flMax );
		boxes[ a ]->setDecimals( 3 );
		boxes[ a ]->setValue( flDefault );
		boxes[ a ]->setKeyboardTracking( false );
		pLayout->addWidget( new QLabel( QLatin1String( labels[ a ] ) ) );
		pLayout->addWidget( boxes[ a ], 1 );
	}
	return pRow;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapTransformDialog::CMapTransformDialog( QWidget *pParent )
	: QDialog( pParent )
{
	setWindowTitle( tr( "Transform" ) );
	setObjectName( QStringLiteral( "mapTransformDialog" ) );
	auto *pLayout = new QVBoxLayout( this );
	auto *pModes = new QGroupBox( tr( "Mode" ) );
	auto *pModeLayout = new QHBoxLayout( pModes );
	m_pModes = new QButtonGroup( this );
	const struct
	{
		TransformMode_t m_nMode;
		QString m_szText;
		QString m_szName;
	} modes[] = {
		{ TransformMode_t::TRANSFORM_MODE_ROTATE, tr( "Rotate" ), QStringLiteral( "mapTransformRotate" ) },
		{ TransformMode_t::TRANSFORM_MODE_SCALE, tr( "Scale" ), QStringLiteral( "mapTransformScale" ) },
		{ TransformMode_t::TRANSFORM_MODE_MOVE, tr( "Move" ), QStringLiteral( "mapTransformMove" ) },
	};
	for ( const auto &mode : modes )
	{
		auto *pRadio = new QRadioButton( mode.m_szText );
		pRadio->setObjectName( mode.m_szName );
		m_pModes->addButton( pRadio, static_cast<int>( mode.m_nMode ) );
		pModeLayout->addWidget( pRadio );
	}
	pLayout->addWidget( pModes );
	auto *pValues = new QGroupBox( tr( "Values" ) );
	auto *pValueLayout = new QVBoxLayout( pValues );
	pValueLayout->addWidget( MakeTriple( m_Values, QStringLiteral( "mapTransformValue" ), -1e6, 1e6, 0.0 ) );
	auto *pHint = new QLabel( tr( "Rotate: degrees about each axis, through the selection's centre. Scale: a factor for each axis (a negative one mirrors). Move: units." ) );
	pHint->setWordWrap( true );
	pValueLayout->addWidget( pHint );
	pLayout->addWidget( pValues );
	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	connect( pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
	pLayout->addWidget( pButtons );
	connect( m_pModes, &QButtonGroup::idClicked, this, [ this ] { FillDefaults(); } );
	m_pModes->button( static_cast<int>( TransformMode_t::TRANSFORM_MODE_ROTATE ) )->setChecked( true );
}

//-----------------------------------------------------------------------------
// Purpose: A scale of 0 would flatten the selection, so a fresh Scale shows 1s; the others 0s.
//-----------------------------------------------------------------------------
void CMapTransformDialog::FillDefaults()
{
	const bool bScale = m_pModes->checkedId() == static_cast<int>( TransformMode_t::TRANSFORM_MODE_SCALE );
	for ( QDoubleSpinBox *pBox : m_Values )
	{
		pBox->setValue( bScale ? 1.0 : 0.0 );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
TransformValues_t CMapTransformDialog::GetValues() const
{
	TransformValues_t values;
	values.m_nMode = static_cast<TransformMode_t>( m_pModes->checkedId() );
	for ( std::size_t a = 0; a < 3; a++ )
	{
		values.m_Values[ a ] = m_Values[ a ]->value();
	}
	return values;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapTransformDialog::SetValues( const TransformValues_t &values )
{
	m_pModes->button( static_cast<int>( values.m_nMode ) )->setChecked( true );
	for ( std::size_t a = 0; a < 3; a++ )
	{
		m_Values[ a ]->setValue( values.m_Values[ a ] );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapPasteSpecialDialog::CMapPasteSpecialDialog( QWidget *pParent )
	: QDialog( pParent )
{
	setWindowTitle( tr( "Paste Special" ) );
	setObjectName( QStringLiteral( "mapPasteSpecialDialog" ) );
	auto *pLayout = new QVBoxLayout( this );
	auto *pForm = new QFormLayout;
	m_pCopies = new QSpinBox;
	m_pCopies->setObjectName( QStringLiteral( "mapPasteCopies" ) );
	m_pCopies->setRange( 1, 1000 );
	m_pCopies->setValue( 1 );
	pForm->addRow( tr( "Number of copies" ), m_pCopies );
	auto *pStart = new QWidget;
	auto *pStartLayout = new QVBoxLayout( pStart );
	pStartLayout->setContentsMargins( 0, 0, 0, 0 );
	m_pStart = new QButtonGroup( this );
	auto *pOriginal = new QRadioButton( tr( "Where the original was" ) );
	pOriginal->setObjectName( QStringLiteral( "mapPasteStartOriginal" ) );
	auto *pView = new QRadioButton( tr( "The centre of the 2D view" ) );
	pView->setObjectName( QStringLiteral( "mapPasteStartView" ) );
	m_pStart->addButton( pOriginal, 0 );
	m_pStart->addButton( pView, 1 );
	pOriginal->setChecked( true );
	pStartLayout->addWidget( pOriginal );
	pStartLayout->addWidget( pView );
	pForm->addRow( tr( "Start at" ), pStart );
	pForm->addRow( tr( "Offset each copy" ), MakeTriple( m_Offset, QStringLiteral( "mapPasteOffset" ), -1e6, 1e6, 0.0 ) );
	pForm->addRow( tr( "Rotate each copy" ), MakeTriple( m_Angles, QStringLiteral( "mapPasteAngle" ), -3600.0, 3600.0, 0.0 ) );
	m_pGroup = new QCheckBox( tr( "Group each copy" ) );
	m_pGroup->setObjectName( QStringLiteral( "mapPasteGroup" ) );
	m_pUniqueNames = new QCheckBox( tr( "Make entity names unique" ) );
	m_pUniqueNames->setObjectName( QStringLiteral( "mapPasteUniqueNames" ) );
	m_pUniqueNames->setToolTip( tr( "A copy's entities get names the map doesn't have yet (a number appended, or a trailing number raised), and its outputs follow" ) );
	pForm->addRow( QString(), m_pGroup );
	pForm->addRow( QString(), m_pUniqueNames );
	pLayout->addLayout( pForm );
	auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	connect( pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept );
	connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
	pLayout->addWidget( pButtons );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::PasteSpecial_t CMapPasteSpecialDialog::GetValues() const
{
	xv::map::PasteSpecial_t how;
	how.m_nCopies = m_pCopies->value();
	for ( std::size_t a = 0; a < 3; a++ )
	{
		how.m_Offset[ a ] = m_Offset[ a ]->value();
		how.m_Angles[ a ] = m_Angles[ a ]->value();
	}
	how.m_bGroup = m_pGroup->isChecked();
	how.m_bUniqueNames = m_pUniqueNames->isChecked();
	return how;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapPasteSpecialDialog::IsStartAtView() const
{
	return m_pStart->checkedId() == 1;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapPasteSpecialDialog::SetValues( const xv::map::PasteSpecial_t &how, bool bStartAtView )
{
	m_pCopies->setValue( how.m_nCopies );
	for ( std::size_t a = 0; a < 3; a++ )
	{
		m_Offset[ a ]->setValue( how.m_Offset[ a ] );
		m_Angles[ a ]->setValue( how.m_Angles[ a ] );
	}
	m_pGroup->setChecked( how.m_bGroup );
	m_pUniqueNames->setChecked( how.m_bUniqueNames );
	m_pStart->button( bStartAtView ? 1 : 0 )->setChecked( true );
}
