//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Running a pixel shader's bytecode (ps_2_0 to ps_3_0) in software, a pixel at a time:
//          what the card would make of it, for the previews that draw without one. The
//          instructions do what Direct3D 9's documentation says they do; derivatives (dsx, dsy)
//          are 0, as one pixel alone can't know its neighbours'.
//
//===========================================================================//
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "shader/Bytecode.h"

namespace xv::shader
{

using Vec4_t = std::array<float, 4>;

// What a pixel starts with: its texture coordinates (ps_2's t0..t7, ps_3's inputs declared as
// texcoords by their index), its colours (v0, v1) and its place on the screen (vPos).
struct PixelInputs_t
{
	std::array<Vec4_t, 8> m_Texcoords{};
	std::array<Vec4_t, 2> m_Colors{};
	Vec4_t m_Position{};
};

// A sampler's texture read at coordinates (and at a level of detail, for texldl and texldb).
using Sample_t = std::function<Vec4_t( int nSampler, const Vec4_t &coord, float flLod )>;

class CPixelShader
{
public:
	explicit CPixelShader( Program_t program );

	// Whether it can run, and why not (a vertex shader, ps_1_x, flow control that doesn't nest).
	[[nodiscard]] bool CanRun() const { return m_szProblem.empty(); }
	[[nodiscard]] const std::string &GetProblem() const { return m_szProblem; }
	[[nodiscard]] const Program_t &GetProgram() const { return m_Program; }
	// The registers the program reads but doesn't define itself, as the material sets them.
	void SetConstant( int nRegister, const Vec4_t &value );
	void SetInteger( int nRegister, const std::array<int, 4> &value );
	void SetBool( int nRegister, bool bValue );

	// The colour it writes (oC0), or nullopt when the pixel is killed (texkill).
	[[nodiscard]] std::optional<Vec4_t> Run( const PixelInputs_t &inputs, const Sample_t &pfnSample ) const;

	static constexpr int CONSTANTS = 256;

private:
	void Prepare();

	Program_t m_Program;
	std::string m_szProblem;
	std::vector<Vec4_t> m_Constants;
	std::array<std::array<int, 4>, 16> m_Integers{};
	std::array<bool, 16> m_Bools{};
	// Registers the program defines (def, defi, defb): what the material sets doesn't reach them.
	std::vector<bool> m_Defined;
	std::array<bool, 16> m_IntegerDefined{};
	std::array<bool, 16> m_BoolDefined{};
	// Where each flow control instruction goes: if's else (or endif), else's endif, a loop's end
	// and the end's start; a label's instruction by its number.
	std::vector<std::size_t> m_Jumps;
	std::array<std::size_t, 2048> m_Labels{};
	// ps_3_0's input registers, by what they were declared: a texcoord's index, a colour's 8 on.
	std::array<int, 12> m_InputOf{};
};

} // namespace xv::shader
