//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: OpenEXR files read through tinyexr, kept behind this header so tinyexr's own header
//          is seen by its implementation file only.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>

#include "texture/Image.h"

namespace xv::texture
{

// Whether the bytes start like an OpenEXR file.
[[nodiscard]] bool IsExr( std::span<const std::byte> data );
// A single-part OpenEXR file as light values: its R, G, B and A channels (a file of one channel
// is grey), or nullopt with error set. Input is untrusted; images over 16384 a side are
// refused.
[[nodiscard]] std::optional<CFloatImage> LoadExr( std::span<const std::byte> data, std::string *pError = nullptr );

} // namespace xv::texture
