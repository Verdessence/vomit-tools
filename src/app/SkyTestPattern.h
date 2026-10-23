//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A labelled face for checking a sky in game: the face's name and direction in the
//          middle, and at each edge the name of the face that should meet it there. If the table
//          in sky/Sky.cpp is right, every seam in game shows two labels naming each other, and
//          "setang 0 0 0" faces RT.
//
//===========================================================================//
#pragma once

#include "sky/Sky.h"
#include "texture/Image.h"

[[nodiscard]] xv::texture::CImage SkyTestPattern( xv::sky::Face_t nFace, int nSize );
