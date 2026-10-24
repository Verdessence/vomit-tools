//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Making a VTF from pictures, or again from another VTF in a new format: sizing the
//          pictures (games want powers of two), setting the alpha flags the way Valve's tools do,
//          and keeping HDR light values for the float formats.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "texture/Image.h"
#include "texture/Vtf.h"

namespace xv::texture
{

// How the pictures are sized before they go in.
enum class ResizeMode_t
{
	RESIZE_KEEP,    // as they are
	RESIZE_NEAREST, // the nearest power of two on each side
	RESIZE_UP,      // the next power of two up
	RESIZE_DOWN,    // the next power of two down
	RESIZE_MULTIPLE_OF_4, // the nearest multiple of 4 (what DXT needs), not a power of two
	RESIZE_EXACT,         // m_nExactWidth × m_nExactHeight, whatever the pictures are
};

struct BuildOptions_t
{
	VtfWriteOptions_t m_Write;
	int m_nFaces = 1; // 6 for a cube map: rt lf bk ft up dn in each frame
	ResizeMode_t m_nResize = ResizeMode_t::RESIZE_NEAREST;
	int m_nMaxSize = 0;        // largest side after resizing; 0 for no limit
	int m_nExactWidth = 256;   // the size for RESIZE_EXACT
	int m_nExactHeight = 256;
	bool m_bAlphaFlags = true; // set ONEBITALPHA / EIGHTBITALPHA from the pictures
	bool m_bStripAlpha = false; // make every picture solid first
	// A normal map made from the pictures taken as height (brighter is higher), with
	// m_flNormalScale as how steep it gets. Sets NORMAL. Green points down the picture
	// (DirectX style, as Source's normal maps usually are) unless m_bGreenUp.
	bool m_bHeightToNormal = false;
	float m_flNormalScale = 2.0f;
	bool m_bGreenUp = false;
	// Resizing keeps the shape: the longer side is sized, the shorter scaled to match (then rounded
	// the same way).
	bool m_bKeepAspect = false;
	// $distancealpha: alpha becomes how far each texel is from the shape's edge (alpha 128), so
	// edges stay sharp when magnified. m_flDistanceSpread is how many texels of the made texture
	// fade from edge to fully in or out. Works best from pictures several times the size.
	bool m_bDistanceAlpha = false;
	float m_flDistanceSpread = 4.0f;
	// Pictures with see-through parts are made in this format instead (VTF_FORMAT_NONE: the same).
	VtfFormat_t m_nAlphaFormat = VtfFormat_t::VTF_FORMAT_NONE;
};

// One picture going in. m_Light holds light values when there are some (an HDR file, or an HDR
// texture being converted); the float formats are made from those, everything else from m_Image.
struct SourceImage_t
{
	CImage m_Image;
	CFloatImage m_Light;
};

// A height map (brightness) as a normal map; the edges wrap, as tiling textures do. Alpha is kept.
[[nodiscard]] CImage HeightToNormal( const CImage &height, float flScale, bool bGreenUp );

// A side's length after resizing.
[[nodiscard]] int TargetSize( int nSize, ResizeMode_t nResize, int nMaxSize );
// Both sides after resizing, keeping the shape when the options say so.
[[nodiscard]] std::pair<int, int> TargetSizes( int nWidth, int nHeight, const BuildOptions_t &options );
// Alpha as a signed distance field: each texel's distance from the edge between alpha under and
// from 128, scaled so flSpread texels fade from 0 to 255 (128 on the edge).
[[nodiscard]] CImage DistanceAlpha( const CImage &image, float flSpread );
// Whether any picture has a texel that isn't solid.
[[nodiscard]] bool HasClearTexels( std::span<const SourceImage_t> sources );

// Every picture is sized to the first one's target size. The pictures are frames, each with its
// faces, each face with m_Write.m_nDepth slices. A NORMAL texture's mipmaps are made as
// directions. Empty, with the reason, when it can't be made (no pictures, a count that isn't
// whole cube maps, a format that can't be written).
[[nodiscard]] std::vector<std::byte> BuildVtf( std::span<const SourceImage_t> sources, const BuildOptions_t &options, std::string *pError = nullptr );

// The full-size pictures of a VTF, frame after frame with its faces (without the sphere map) and
// slices, to build it again; HDR textures keep their light values.
[[nodiscard]] std::vector<SourceImage_t> SourcesOf( const CVtf &vtf );

// The options a VTF was written with, as far as the file tells: format, flags (without the ones
// BuildVtf sets itself), mipmaps, thumbnail, version, first frame, bump scale, resources.
[[nodiscard]] BuildOptions_t OptionsOf( const CVtf &vtf );

// Sprays (a player's decal): the largest file the game takes for one, and the largest side.
constexpr std::size_t SPRAY_MAX_BYTES = 512 * 1024;
constexpr int SPRAY_MAX_SIZE = 4096;
// What keeps a texture from working as a spray: too big a file or picture, sides that aren't
// powers of two, a cube map or volume, a format decals can't draw. Empty when it would work.
[[nodiscard]] std::vector<std::string> SprayProblems( const CVtf &vtf, std::size_t nFileSize );
// The choices sprays are made with: DXT5 with see-through parts, else DXT1; at most 4096 on a side
// in powers of two, the shape kept; no mipmaps, no LOD, clamped; version 7.2.
[[nodiscard]] BuildOptions_t SprayOptions( bool bAlpha );
// Pictures (frames) made into a spray that fits: smaller, down to 64 on the longer side, then fewer
// frames, until it is under SPRAY_MAX_BYTES. Empty, with the reason, when it can't be.
[[nodiscard]] std::vector<std::byte> BuildSpray( std::span<const SourceImage_t> sources, std::string *pError = nullptr );

} // namespace xv::texture
