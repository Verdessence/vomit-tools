//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See InterfaceMode.h.
//
//===========================================================================//
#include "InterfaceMode.h"

#include <QSettings>

namespace
{

constexpr auto SIMPLE_KEY = "simpleInterface";

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CInterfaceMode &CInterfaceMode::Instance()
{
	static CInterfaceMode mode;
	return mode;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CInterfaceMode::CInterfaceMode()
	: m_bSimple( QSettings{}.value( SIMPLE_KEY, false ).toBool() )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CInterfaceMode::SetSimple( bool bSimple )
{
	if ( bSimple == m_bSimple )
	{
		return;
	}
	m_bSimple = bSimple;
	QSettings{}.setValue( SIMPLE_KEY, bSimple );
	emit Changed( bSimple );
}
