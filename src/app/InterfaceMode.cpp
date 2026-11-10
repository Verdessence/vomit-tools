//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See InterfaceMode.h.
//
//===========================================================================//
#include "InterfaceMode.h"

#include "Preferences.h"

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
	: m_bSimple( CPreferences::Instance().IsSimpleInterface() )
{
	// Kept in vomit-tools.nut, so an edit there switches it too.
	connect( &CPreferences::Instance(), &CPreferences::SimpleInterfaceChanged, this, &CInterfaceMode::SetSimple );
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
	CPreferences::Instance().SetSimpleInterface( bSimple );
	emit Changed( bSimple );
}
