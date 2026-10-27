//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The map's 3D view (Hammer's camera view): the map drawn wireframe, flat, textured
//          or textured and shaded, flown through with Hammer's camera controls.
//
//===========================================================================//
#pragma once

#include "map/MapView.h"

class CMapView3D : public CMapView
{
	Q_OBJECT

public:
	CMapView3D( CMapEditor &editor, MapViewType_t nType, QWidget *pParent = nullptr );

	[[nodiscard]] MapViewType_t GetType() const override { return m_nType; }
	void SetType( MapViewType_t nType ) override;
	bool Run( MapAction_t nAction ) override;
	void CenterOn( const xv::map::Box_t &box ) override;

protected:
	void paintEvent( QPaintEvent *pEvent ) override;

private:
	MapViewType_t m_nType;
};
