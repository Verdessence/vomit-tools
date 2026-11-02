//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The detail props vbsp scatters over faces whose material names a %detailtype, as
//          the map's views preview them: detail.vbsp's types read, the props placed per face
//          (brushes at blend alpha 1, displacements by their own alpha) with its density,
//          groups, amounts and angle limits, and the sprites laid out as the game turns them
//          (fixed, to the screen, or about z), fading out with distance.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "kv1/Kv1.h"
#include "map/Geometry.h"
#include "map/Scene.h"
#include "map/Sprites.h"

namespace xv::map
{

enum class DetailShape_t
{
	DETAIL_SHAPE_SPRITE,
	DETAIL_SHAPE_CROSS, // sprite_shape "cross": two sprites crossing
	DETAIL_SHAPE_TRI,   // sprite_shape "tri": three round the middle
	DETAIL_SHAPE_MODEL, // a model (not drawn here)
};

// One entry of a group: a sprite of the detail material's sheet, or a model.
struct DetailEntry_t
{
	DetailShape_t m_nShape = DetailShape_t::DETAIL_SHAPE_SPRITE;
	std::string m_szModel;
	std::array<float, 4> m_Rect{ 0, 0, 1, 1 };    // the sheet's u v of its top left, then bottom right
	std::array<float, 4> m_Extent{ -10, 20, 10, 0 }; // units from its origin: left, top, right, bottom
	float m_flScaleSpread = 0.0f;                 // spriterandomscale: the scale's standard deviation
	int m_nOrientation = 0;                       // detailOrientation: 0 as placed, 1 to the screen, 2 about z
	bool m_bUpright = false;
	float m_flMinCos = -1.0f;                     // minAngle and maxAngle as cosines of the slope
	float m_flMaxCos = -1.0f;
	float m_flAmount = 1.0f;                      // the group's running total up to and with this one
};

struct DetailGroup_t
{
	float m_flAlpha = 1.0f;
	std::vector<DetailEntry_t> m_Entries;
};

struct DetailType_t
{
	float m_flDensity = 0.0f;              // props per million square units
	std::vector<DetailGroup_t> m_Groups;   // by alpha, lowest first
};

struct CLessIgnoreCase
{
	[[nodiscard]] bool operator()( std::string_view a, std::string_view b ) const;
	using is_transparent = void;
};
using DetailTypes_t = std::map<std::string, DetailType_t, CLessIgnoreCase>;

// detail.vbsp's types (its "detail" block), with vbsp's defaults for what is left out.
[[nodiscard]] DetailTypes_t ReadDetailTypes( const kv1::CDocument &document );

// A prop placed: where, how it is turned, its entry and scale, and the brush it grew on.
struct DetailProp_t
{
	Point_t m_Origin{};
	Point_t m_Right{ 0, -1, 0 };
	Point_t m_Up{ 0, 0, 1 };
	const DetailEntry_t *m_pEntry = nullptr;
	float m_flScale = 1.0f;
	std::uint32_t m_nObject = 0;
};

// How far detail sprites are drawn (cl_detaildist) and over how much of that they fade out
// (cl_detailfade).
struct DetailView_t
{
	float m_flDistance = 1200.0f;
	float m_flFade = 400.0f;
};

class CDetailProps
{
public:
	// The world's and func_detail's faces whose material (by its index in the map's table) has
	// a %detailtype that types knows. The same faces grow the same props whatever else changes.
	CDetailProps( std::shared_ptr<const DetailTypes_t> pTypes, const CScene &scene, const std::function<std::string( std::uint32_t nMaterial )> &pfnDetailType );

	[[nodiscard]] const std::vector<DetailProp_t> &GetProps() const { return m_Props; }
	// The sprites near enough the eye (right and up of unit length), each a quad or two or three
	// of the detail material's sheet, into out.
	void Layout( Point_t eye, Point_t right, Point_t up, const DetailView_t &view, std::uint32_t nMaterial, std::array<int, 2> textureSize, std::vector<ParticleSprite_t> &out ) const;

	// The most props kept: a preview, not the game.
	static constexpr std::size_t MAX_PROPS = 400000;

private:
	std::shared_ptr<const DetailTypes_t> m_pTypes;
	std::vector<DetailProp_t> m_Props;
};

} // namespace xv::map
