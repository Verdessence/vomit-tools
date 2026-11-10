//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Brush modeling operations and their repeatable, view-independent parameters.
//===========================================================================//
#pragma once
#include "map/BrushOperations.h"
namespace xv::map
{
enum class BrushModelOperation_t
{
	BRUSH_MODEL_OPERATION_EXTRUDE,
	BRUSH_MODEL_OPERATION_BEVEL,
	BRUSH_MODEL_OPERATION_LOOP,
	BRUSH_MODEL_OPERATION_KNIFE,
	BRUSH_MODEL_OPERATION_DISSOLVE,
	BRUSH_MODEL_OPERATION_HULL,
	BRUSH_MODEL_OPERATION_INTERSECT,
	BRUSH_MODEL_OPERATION_BRIDGE,
	BRUSH_MODEL_OPERATION_MIRROR,
	BRUSH_MODEL_OPERATION_ARRAY,
	BRUSH_MODEL_OPERATION_SPIN,
	BRUSH_MODEL_OPERATION_FLATTEN,
	BRUSH_MODEL_OPERATION_ALIGN_FACE,
	BRUSH_MODEL_OPERATION_INSET,
};
struct BrushModelParams_t
{
	double m_flAmount = 16;
	double m_flDepth = 0;
	double m_flAngle = 45;
	double m_flSlide = 0;
	int m_nCount = 1;
	int m_nAxis = 2;
	Point_t m_Offset{ 64, 0, 0 };
	Point_t m_Pivot{};
	Plane_t m_Plane{ { 1, 0, 0 }, 0 };
	bool m_bUsePivot = false;
	bool m_bNewBrush = false;
	bool m_bRegion = false;
	bool m_bSquareCorners = false;
	bool m_bBestPlane = true;
	bool m_bAllEdges = false;
	BrushConcave_t m_nConcave = BrushConcave_t::BRUSH_CONCAVE_SPLIT;
	bool operator==( const BrushModelParams_t &other ) const
	{
		return m_flAmount == other.m_flAmount && m_flDepth == other.m_flDepth && m_flAngle == other.m_flAngle && m_flSlide == other.m_flSlide && m_nCount == other.m_nCount && m_nAxis == other.m_nAxis && m_Offset == other.m_Offset && m_Pivot == other.m_Pivot && m_Plane.m_Normal == other.m_Plane.m_Normal && m_Plane.m_flDistance == other.m_Plane.m_flDistance && m_bUsePivot == other.m_bUsePivot && m_bNewBrush == other.m_bNewBrush && m_bRegion == other.m_bRegion && m_bSquareCorners == other.m_bSquareCorners && m_bBestPlane == other.m_bBestPlane && m_bAllEdges == other.m_bAllEdges && m_nConcave == other.m_nConcave;
	}
};
struct BrushModelResult_t
{
	MapEdit_t m_Edit;
	std::vector<int> m_SelectedSides;
	std::string m_szProblem;
};
struct BrushRingEdge_t
{
	std::uint32_t m_nEdge = 0;
	bool m_bReverse = false;
};
[[nodiscard]] std::vector<BrushRingEdge_t> FindBrushEdgeRing( const CBrushMesh &mesh, std::uint32_t nEdge );
[[nodiscard]] bool LoopCutBrush( CBrushMesh &mesh, std::uint32_t nEdge, int nCount, double flSlide );
[[nodiscard]] BrushModelResult_t ModelBrushes( const CScene &scene, const std::vector<std::uint32_t> &objects, std::span<const BrushPart_t> parts, BrushModelOperation_t nOperation, const BrushModelParams_t &params );
} // namespace xv::map
