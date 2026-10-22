// SPDX-License-Identifier: GPL-3.0-or-later
// The stb implementations, compiled once in their own target (vomit_tools_stb) so our warning
// flags don't apply to them.

// Faces bigger than this are refused rather than decoded; skies top out at 4096.
#define STBI_MAX_DIMENSIONS 16384
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"
