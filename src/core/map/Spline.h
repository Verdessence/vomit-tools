//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The spline tool's paths: a smooth curve through points, and a strip of displacement
//          pieces swept along it (roads, paths, rivers' beds). The curve is centripetal
//          Catmull-Rom, which passes through every point without loops or cusps however
//          unevenly they are spaced. Each piece is a flat four-sided brush whose top face is a
//          displacement with every grid point moved onto the curve; neighbours compute their
//          shared edge from the same numbers, so the strip has no cracks.
//
//          A spline stays editable: the map keeps it in a "vomit_spline" block (which Hammer
//          ignores, and drops on its own saves; the pieces are ordinary displacements either
//          way), naming the solids it made, so moving a point makes its pieces again.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "map/Edit.h"
#include "map/Geometry.h"
#include "map/Map.h"
#include "map/Scene.h"

namespace xv::map
{

// A point on the curve: where, which way it runs, which way is across it (level: a road stays
// flat across), how wide it is there, and how far along.
struct SplineFrame_t
{
	Point_t m_Position{};
	Point_t m_Tangent{ 1, 0, 0 };
	Point_t m_Right{ 0, -1, 0 };
	double m_flWidth = 0.0;
	double m_flDistance = 0.0;
};

// The curve, sampled densely enough to measure along.
class CSplineCurve
{
public:
	explicit CSplineCurve( const Spline_t &spline );

	[[nodiscard]] bool IsValid() const { return m_Samples.size() >= 2; }
	[[nodiscard]] double GetLength() const { return m_Samples.empty() ? 0.0 : m_Samples.back().m_flDistance; }
	// The frame at a distance along (clamped to the ends).
	[[nodiscard]] SplineFrame_t FrameAt( double flDistance ) const;
	// Across the curve at a distance: 0 its left edge, 1 its right.
	[[nodiscard]] Point_t PointAt( double flDistance, double flAcross ) const;
	// The samples, for drawing the curve.
	[[nodiscard]] const std::vector<SplineFrame_t> &Samples() const { return m_Samples; }
	// How far along each of the spline's points is.
	[[nodiscard]] const std::vector<double> &PointDistances() const { return m_PointDistances; }

private:
	std::vector<SplineFrame_t> m_Samples;
	std::vector<double> m_PointDistances;
};

// A point on the curve between two of its points, i the first and t 0-1 between them
// (centripetal Catmull-Rom across the ground, the ends carrying on straight; the height a
// monotone cubic, never above or below both its points).
[[nodiscard]] Point_t EvaluateSpline( const Spline_t &spline, std::size_t i, double t );

// The pieces as solids (no ids yet; AddObjects gives them), their top faces the spline's
// material with displacements on the curve, the rest nodraw. nullopt with pError when it can't
// be swept: under two points, or a bend too tight for the pieces' length (a piece's corners
// would cross).
[[nodiscard]] std::optional<std::vector<Solid_t>> SweepSpline( const CMap &map, const Spline_t &spline, std::string *pError = nullptr );

// A new spline's pieces added and the spline kept (with the next free id), as one edit; its
// pieces' object numbers into pAdded.
[[nodiscard]] std::optional<MapEdit_t> AddSpline( const CScene &scene, Spline_t spline, std::vector<std::uint32_t> *pAdded, std::string *pError = nullptr );
// A spline changed (by its id): its old pieces deleted, new ones made, in one edit.
[[nodiscard]] std::optional<MapEdit_t> UpdateSpline( const CScene &scene, Spline_t spline, std::vector<std::uint32_t> *pAdded, std::string *pError = nullptr );
// A spline and its pieces taken out.
[[nodiscard]] MapEdit_t DeleteSpline( const CScene &scene, int nId );
// The spline a solid is a piece of (by the solid's id), if any.
[[nodiscard]] const Spline_t *SplineOfSolid( const CMap &map, int nSolidId );
[[nodiscard]] const Spline_t *FindSpline( const CMap &map, int nId );

} // namespace xv::map
