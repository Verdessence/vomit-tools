//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: VGUI HUD layouts (scripts/HudLayout.res, resource/ui/*.res) as data: which blocks
//          are elements, where an element lands on a screen of a given size, and the
//          scheme's named colours (resource/ClientScheme.res). Editing goes through the
//          lossless KeyValues document; this only reads it.
//
//===========================================================================//
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kv1/Kv1.h"

namespace xv::hud
{

// VGUI lays proportional panels out on a 640x480 grid scaled to the screen's height.
constexpr int BASE_TALL = 480;

// What a position or size value is measured from.
enum class Anchor_t
{
	ANCHOR_START,  // "16": from the parent's left or top
	ANCHOR_CENTER, // "c-50": from the parent's middle
	ANCHOR_END,    // "r40": back from the parent's right or bottom
	ANCHOR_FULL,   // "f0" (sizes): the parent's size less the value
	ANCHOR_PARENT, // "p0.5" (sizes): that fraction of the parent's size
};

struct Coord_t
{
	Anchor_t m_nAnchor = Anchor_t::ANCHOR_START;
	double m_flValue = 0.0; // in base units (a fraction for ANCHOR_PARENT)
};

[[nodiscard]] Coord_t ParseCoord( std::string_view szText );
// The text for coord, keeping its anchor's prefix ("c-50", "r40", "16").
[[nodiscard]] std::string FormatCoord( const Coord_t &coord );

struct Screen_t
{
	int m_nWide = 640;
	int m_nTall = 480;
	bool m_bProportional = true; // the HUD's elements are

	[[nodiscard]] double GetScale() const { return m_bProportional ? static_cast<double>( m_nTall ) / BASE_TALL : 1.0; }
};

// A position along one axis, in screen pixels, for a parent nParent pixels long.
[[nodiscard]] int ResolvePos( const Coord_t &coord, int nParent, double flScale );
// A size along one axis, in screen pixels.
[[nodiscard]] int ResolveSize( const Coord_t &coord, int nParent, double flScale );

struct Rect_t
{
	int m_nX = 0;
	int m_nY = 0;
	int m_nWide = 0;
	int m_nTall = 0;
};

struct Color_t
{
	int m_nR = 255;
	int m_nG = 255;
	int m_nB = 255;
	int m_nA = 255;
};

// "255 176 0 255" (alpha optional).
[[nodiscard]] std::optional<Color_t> ParseColor( std::string_view szText );

// The scheme's named colours: its Colors block, and BaseSettings' entries (which may name a
// colour in turn).
class CScheme
{
public:
	[[nodiscard]] static CScheme FromDocument( const kv1::CDocument &document );
	// A literal colour, or a name looked up (case-insensitively, as VGUI does).
	[[nodiscard]] std::optional<Color_t> Resolve( std::string_view szValue ) const;
	[[nodiscard]] std::size_t GetCount() const { return m_Colors.size(); }

private:
	std::map<std::string, std::string> m_Colors; // lower-case name -> value as written
};

// One element: a block inside the layout's root block.
struct Element_t
{
	std::size_t m_nIndex = 0; // the block's index among the root's children
	std::string m_szName;     // the block's key
	std::string m_szControl;  // ControlName, when given
	Rect_t m_Rect;
	int m_nZ = 0;
	bool m_bVisible = true;
	std::optional<Color_t> m_BgColor;
	std::optional<Color_t> m_FgColor;
	std::string m_szLabel; // labelText, when given
};

// The layout's root: the first block of the file.
[[nodiscard]] const kv1::CNode *LayoutRoot( const kv1::CDocument &document );
[[nodiscard]] kv1::CNode *LayoutRoot( kv1::CDocument &document );
// Whether the document looks like a HUD or panel layout: a root block holding blocks with
// position keys.
[[nodiscard]] bool IsLayout( const kv1::CDocument &document );
// The value of key on an element: the last one written wins, as when VGUI reads it.
[[nodiscard]] std::optional<std::string> GetKey( const kv1::CNode &element, std::string_view szKey );
// The root's element blocks, laid out on screen, in file order.
[[nodiscard]] std::vector<Element_t> LayOut( const kv1::CDocument &document, const Screen_t &screen, const CScheme *pScheme = nullptr );

// The xpos and ypos that put an element's top left corner at pixel (nX, nY), keeping the
// anchors its values already have.
struct MovedPos_t
{
	std::string m_szX;
	std::string m_szY;
};
[[nodiscard]] MovedPos_t MoveTo( const kv1::CNode &element, int nX, int nY, const Screen_t &screen );

} // namespace xv::hud
