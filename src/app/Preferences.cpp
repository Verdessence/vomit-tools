//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Preferences.h.
//
//===========================================================================//
#include "Preferences.h"

#include <QSettings>

namespace
{

constexpr auto GRID_ANIMATION_KEY = "particleGridAnimatesAll";
constexpr auto GRAPHICS_KEY = "graphicsApi";

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPreferences &CPreferences::Instance()
{
	static CPreferences preferences;
	return preferences;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPreferences::CPreferences()
	: m_bGridAnimatingAll( QSettings{}.value( GRID_ANIMATION_KEY, false ).toBool() ),
	  m_nGraphicsApi( GraphicsApiFromKey( QSettings{}.value( GRAPHICS_KEY ).toString().toStdString() ).value_or( GraphicsApi_t::GRAPHICS_API_AUTO ) )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetGridAnimatingAll( bool bAll )
{
	if ( bAll == m_bGridAnimatingAll )
	{
		return;
	}
	m_bGridAnimatingAll = bAll;
	QSettings{}.setValue( GRID_ANIMATION_KEY, bAll );
	emit GridAnimationChanged( bAll );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPreferences::SetGraphicsApi( GraphicsApi_t nApi )
{
	m_nGraphicsApi = nApi;
	QSettings{}.setValue( GRAPHICS_KEY, QString::fromLatin1( GraphicsApiKey( nApi ) ) );
}
