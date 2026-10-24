//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Sprite sheets: several pictures packed into one texture, with the VTF's sheet resource
//          saying which part each frame of each sequence shows (what particles animate with).
//          Reads and writes the resource, mksheet's .mks text, and packs pictures into a sheet.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "texture/Image.h"

namespace xv::texture
{

// The sequences of a sprite sheet (the VTF's sheet resource): which part of the texture each frame
// of an animation shows.
struct CSheet
{
	struct Frame_t
	{
		float m_flDuration = 0.0f;
		std::array<float, 4> m_Rect{ 0, 0, 1, 1 }; // left, top, right, bottom, 0-1
		// Version 1 keeps four rects a frame: the first is m_Rect, these the other three (a second
		// picture for multi-texturing, else the same as the first).
		std::array<std::array<float, 4>, 3> m_Others{};
	};
	struct Sequence_t
	{
		int m_nNumber = 0;
		bool m_bClamp = false; // holds the last frame instead of looping
		float m_flTotalTime = 0.0f;
		std::vector<Frame_t> m_Frames;
	};
	int m_nVersion = 0;
	std::vector<Sequence_t> m_Sequences;

	[[nodiscard]] const Sequence_t *Sequence( int nNumber ) const;
};
// Input is untrusted; nullopt if the data doesn't add up.
[[nodiscard]] std::optional<CSheet> ReadSheet( std::span<const std::byte> data );
// The resource's data for a sheet, in its version.
[[nodiscard]] std::vector<std::byte> WriteSheet( const CSheet &sheet );

// mksheet's text: sequences of frames, each frame one or two pictures and a duration.
struct MksFrame_t
{
	std::vector<std::string> m_Files; // one, or two for multi-texturing
	float m_flDuration = 1.0f;
};
struct MksSequence_t
{
	int m_nNumber = 0;
	bool m_bLoop = false;
	std::vector<MksFrame_t> m_Frames;
};
struct CMks
{
	std::vector<MksSequence_t> m_Sequences;
	std::vector<std::string> m_Problems; // "line 7: packmode rgb+a isn't supported"
};
// Lines it doesn't understand are left out and listed in m_Problems.
[[nodiscard]] CMks ParseMks( std::string_view szText );
[[nodiscard]] std::string WriteMks( const CMks &mks );

struct PackedSheet_t
{
	CImage m_Atlas;
	CSheet m_Sheet;
};
// Packs the pictures the sequences name (each once, however often it is used) into one picture
// with power-of-two sides, each with nPadding texels of its own edge around it so filtering doesn't
// bleed from its neighbours, and the sheet saying where each frame is. Version 1 when a frame has
// two pictures. nullopt, with the reason, when a picture is missing or it won't fit in 8192².
[[nodiscard]] std::optional<PackedSheet_t> PackSheet( const CMks &mks, const std::map<std::string, CImage> &pictures, std::string *pError = nullptr, int nPadding = 1 );

} // namespace xv::texture
