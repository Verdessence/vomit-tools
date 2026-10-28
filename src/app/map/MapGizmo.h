//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Hammer++'s gizmo in the 3D views: three axes at the selection's pivot that move,
//          turn or scale it when dragged. The shapes it draws, which axis a pixel is over, and
//          the transform a drag means, all worked out here from the camera alone, so the view
//          only draws and the tests need no widget.
//
//===========================================================================//
#pragma once

#include <array>
#include <optional>
#include <vector>

#include <QPointF>

#include "map/Geometry.h"
#include "map/Instance.h"
#include "map/ViewCamera.h"

enum class MapGizmoMode_t
{
	MAP_GIZMO_MODE_TRANSLATE, // G
	MAP_GIZMO_MODE_ROTATE,    // R
	MAP_GIZMO_MODE_SCALE,     // E
};

// A line of the gizmo's picture, in map units, on one of its axes (0, 1, 2).
struct MapGizmoLine_t
{
	xv::map::Point_t m_From;
	xv::map::Point_t m_To;
	int m_nAxis = 0;
};

class CMapGizmo
{
public:
	// Where it stands and which way its axes point (unit vectors: the world's, or an entity's own).
	void Place( xv::map::Point_t pivot, const std::array<xv::map::Point_t, 3> &axes );
	void SetMode( MapGizmoMode_t nMode ) { m_nMode = nMode; }
	[[nodiscard]] MapGizmoMode_t GetMode() const { return m_nMode; }
	[[nodiscard]] const xv::map::Point_t &GetPivot() const { return m_Pivot; }

	// How long an axis is in map units, so that it is flPixels long on screen where it stands.
	[[nodiscard]] double GetLength( const xv::map::CViewCamera &camera, double flPixels ) const;
	// What to draw: arrows to translate, rings to rotate, axes with a cube at the tip to scale.
	[[nodiscard]] std::vector<MapGizmoLine_t> GetLines( const xv::map::CViewCamera &camera, double flPixels ) const;
	// The axis whose picture is within flSlack pixels of at, or -1.
	[[nodiscard]] int HitAt( const xv::map::CViewCamera &camera, double flPixels, QPointF at, double flSlack ) const;
	// What dragging axis nAxis from pixel from to pixel to does: a move along it (in steps of
	// flGrid when above 0), a turn about it (in steps of flDegreesStep when above 0) or a stretch
	// along it. Identity when the drag can't be read (the axis points straight at the camera).
	[[nodiscard]] xv::map::Transform_t Drag( const xv::map::CViewCamera &camera, int nAxis, QPointF from, QPointF to, double flGrid, double flDegreesStep ) const;

private:
	// Where a pixel's ray meets the axis (the closest point along it, as a distance from the
	// pivot), or the plane the axis is normal to.
	[[nodiscard]] std::optional<double> AlongAxis( const xv::map::CViewCamera &camera, int nAxis, QPointF at ) const;
	[[nodiscard]] std::optional<xv::map::Point_t> OnPlane( const xv::map::CViewCamera &camera, int nAxis, QPointF at ) const;

	xv::map::Point_t m_Pivot{};
	std::array<xv::map::Point_t, 3> m_Axes{ { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
	MapGizmoMode_t m_nMode = MapGizmoMode_t::MAP_GIZMO_MODE_TRANSLATE;
};
