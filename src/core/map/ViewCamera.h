//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The 3D view's camera: where it is, which way it looks (Source's yaw and pitch), and
//          the projection both renderers and picking share, so a click lands where the picture
//          shows.
//
//===========================================================================//
#pragma once

#include <array>
#include <optional>
#include <utility>

#include "map/Geometry.h"

namespace xv::map
{

class CViewCamera
{
public:
	Point_t m_Eye{};
	double m_flYaw = 90.0;    // degrees about +Z; 0 looks along +X (east), 90 along +Y (north)
	double m_flPitch = 0.0;   // degrees; positive looks down, as in Source
	double m_flFov = 90.0;    // horizontal, in degrees
	double m_flNear = 4.0;
	double m_flFar = 32768.0;
	int m_nWidth = 1;
	int m_nHeight = 1;
	// Above 0: an orthographic view (a 2D view's), this many pixels a unit, looking from m_Eye.
	double m_flOrthoScale = 0.0;

	[[nodiscard]] bool IsOrthographic() const { return m_flOrthoScale > 0.0; }
	// A view straight along an axis: centre in the middle of the image, the whole map's depth
	// (up to flDepth either side of it) in front.
	void LookAlongAxis( Point_t centre, Point_t forward, double flScale, double flDepth );

	[[nodiscard]] Point_t GetForward() const;
	[[nodiscard]] Point_t GetRight() const;
	[[nodiscard]] Point_t GetUp() const;
	// Turns to face target from eye.
	void LookAt( Point_t eye, Point_t target );

	// View then projection, column-major, in OpenGL's clip space (depth -1 to 1, y up). A GPU
	// renderer multiplies in its backend's correction; the software one uses it as it is.
	[[nodiscard]] std::array<float, 16> GetViewProjection() const;
	// A map point's place in the image: x and y in pixels (y down), and depth from 0 at the near
	// plane to 1 at the far one. nullopt behind the near plane.
	[[nodiscard]] std::optional<std::array<double, 3>> Project( Point_t p ) const;
	// The ray from the eye through a pixel (centre at x + 0.5, y + 0.5): start and unit direction.
	[[nodiscard]] std::pair<Point_t, Point_t> Ray( double x, double y ) const;
	// Whether any of box can be in view (between the near and far planes, inside the sides).
	[[nodiscard]] bool CanSee( const Box_t &box ) const;

private:
	[[nodiscard]] std::array<double, 2> GetFocal() const; // the projection's x and y scale
};

} // namespace xv::map
