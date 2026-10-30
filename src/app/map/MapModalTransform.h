//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Keyboard-started map transforms, independent of widgets and undo history.
//
//===========================================================================//
#pragma once

#include <QString>

#include "MapGizmo.h"

class CMapModalTransform
{
public:
	CMapModalTransform( MapGizmoMode_t nMode, xv::map::Point_t pivot, const std::array<xv::map::Point_t, 3> &localAxes, const xv::map::CViewCamera &camera, QPointF from );
	void MovePointer( QPointF at, bool bPrecision );
	void LockAxis( int nAxis, bool bPlane );
	bool Type( QChar ch );
	void Backspace();
	[[nodiscard]] xv::map::Transform_t Evaluate( const xv::map::CViewCamera &camera, double flGrid, double flDegreesStep ) const;
	[[nodiscard]] QString GetDescription() const;
	[[nodiscard]] MapGizmoMode_t GetMode() const { return m_nMode; }
	[[nodiscard]] std::vector<MapGizmoLine_t> GetLockLines( double flLength ) const;

private:
	MapGizmoMode_t m_nMode;
	xv::map::Point_t m_Pivot;
	std::array<xv::map::Point_t, 3> m_LocalAxes;
	xv::map::CViewCamera m_StartCamera;
	QPointF m_From;
	QPointF m_At;
	QPointF m_LastPointer;
	int m_nAxis = -1;
	int m_nSpace = 0; // world, local, then unlocked
	bool m_bPlane = false;
	QString m_szNumber;
	[[nodiscard]] xv::map::Point_t GetAxis( int nAxis ) const;
};
