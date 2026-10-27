//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A VMF's values as text, both ways: points, planes, texture axes, colours and plain
//          numbers read as the reader takes them and written as Hammer writes them.
//
//===========================================================================//
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "map/Geometry.h"
#include "map/Map.h"

namespace xv::map
{

// "x y z", with or without brackets or parentheses around it.
[[nodiscard]] std::optional<Point_t> ParsePoint( std::string_view szText );
// A side's plane: "(x y z) (x y z) (x y z)".
[[nodiscard]] std::optional<std::array<Point_t, 3>> ParsePlane( std::string_view szText );
// A texture axis: "[x y z shift] scale".
[[nodiscard]] std::optional<TextureAxis_t> ParseAxis( std::string_view szText );
// "r g b" (anything after the third number, such as a brightness, is ignored).
[[nodiscard]] std::optional<Color_t> ParseColor( std::string_view szText );
// Exactly nCount numbers into out; false otherwise.
[[nodiscard]] bool ParseNumbers( std::string_view szText, std::size_t nCount, std::vector<double> &out );

// A number as Hammer writes one: whole numbers bare, others to six decimals at most, without
// trailing zeros (so what was read and moved by whole units writes back as it was written).
[[nodiscard]] std::string FormatNumber( double flValue );
[[nodiscard]] std::string FormatPoint( const Point_t &p );                  // "x y z"
[[nodiscard]] std::string FormatPlane( const std::array<Point_t, 3> &plane ); // "(x y z) (x y z) (x y z)"
[[nodiscard]] std::string FormatAxis( const TextureAxis_t &axis );         // "[x y z shift] scale"
[[nodiscard]] std::string FormatColor( Color_t color );                    // "r g b"

} // namespace xv::map
