//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Number boxes for property panels: they leave the mouse wheel to the panel until
//          clicked into, and floats show without trailing zeros.
//
//===========================================================================//
#pragma once

#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QWheelEvent>

// A spin box that leaves the mouse wheel to the panel until it is clicked into, so scrolling the
// properties doesn't change values.
template <typename Base>
class CFocusSpin : public Base
{
public:
	CFocusSpin()
	{
		this->setFocusPolicy( Qt::StrongFocus );
		this->setKeyboardTracking( false );
	}

protected:
	void wheelEvent( QWheelEvent *pEvent ) override
	{
		if ( this->hasFocus() )
		{
			Base::wheelEvent( pEvent );
		}
		else
		{
			pEvent->ignore();
		}
	}
};

// A number box for floats that shows them without trailing zeros.
class CFloatSpin : public CFocusSpin<QDoubleSpinBox>
{
public:
	CFloatSpin() { setDecimals( 6 ); }

	[[nodiscard]] QString textFromValue( double flValue ) const override
	{
		QString szText = QString::number( flValue, 'f', decimals() );
		if ( szText.contains( u'.' ) )
		{
			while ( szText.endsWith( u'0' ) )
			{
				szText.chop( 1 );
			}
			if ( szText.endsWith( u'.' ) )
			{
				szText.chop( 1 );
			}
		}
		return szText == QStringLiteral( "-0" ) ? QStringLiteral( "0" ) : szText;
	}
};
