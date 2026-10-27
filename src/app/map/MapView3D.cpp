//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapView3D.h.
//
//===========================================================================//
#include "MapView3D.h"

#include <QPainter>

#include "map/MapConstants.h"

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapView3D::CMapView3D( CMapEditor &editor, MapViewType_t nType, QWidget *pParent )
	: CMapView( editor, pParent ),
	  m_nType( nType )
{
	setAttribute( Qt::WA_OpaquePaintEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::SetType( MapViewType_t nType )
{
	if ( !Is2DView( nType ) )
	{
		m_nType = nType;
		update();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapView3D::Run( MapAction_t )
{
	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::CenterOn( const xv::map::Box_t & )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapView3D::paintEvent( QPaintEvent * )
{
	QPainter painter( this );
	painter.fillRect( rect(), mapstyle::BACKGROUND_3D );
}
