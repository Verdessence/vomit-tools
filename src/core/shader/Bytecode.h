//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Direct3D 9 shader bytecode (vs_1_1 to ps_3_0, as Source's .vcs files hold it): its
//          tokens read into instructions with their registers, swizzles and modifiers, the
//          constant table the compiler leaves in a comment (CTAB: the names of the constants and
//          samplers), and a disassembly in the usual assembler's words. The token format is
//          Microsoft's published one for Direct3D 9 drivers.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace xv::shader
{

// Opcodes, as numbered in the token stream.
enum class Opcode_t : std::uint16_t
{
	OPCODE_NOP = 0,
	OPCODE_MOV,
	OPCODE_ADD,
	OPCODE_SUB,
	OPCODE_MAD,
	OPCODE_MUL,
	OPCODE_RCP,
	OPCODE_RSQ,
	OPCODE_DP3,
	OPCODE_DP4,
	OPCODE_MIN,
	OPCODE_MAX,
	OPCODE_SLT,
	OPCODE_SGE,
	OPCODE_EXP,
	OPCODE_LOG,
	OPCODE_LIT,
	OPCODE_DST,
	OPCODE_LRP,
	OPCODE_FRC,
	OPCODE_M4X4,
	OPCODE_M4X3,
	OPCODE_M3X4,
	OPCODE_M3X3,
	OPCODE_M3X2,
	OPCODE_CALL,
	OPCODE_CALLNZ,
	OPCODE_LOOP,
	OPCODE_RET,
	OPCODE_ENDLOOP,
	OPCODE_LABEL,
	OPCODE_DCL,
	OPCODE_POW,
	OPCODE_CRS,
	OPCODE_SGN,
	OPCODE_ABS,
	OPCODE_NRM,
	OPCODE_SINCOS,
	OPCODE_REP,
	OPCODE_ENDREP,
	OPCODE_IF,
	OPCODE_IFC,
	OPCODE_ELSE,
	OPCODE_ENDIF,
	OPCODE_BREAK,
	OPCODE_BREAKC,
	OPCODE_MOVA,
	OPCODE_DEFB,
	OPCODE_DEFI,
	OPCODE_TEXCOORD = 64,
	OPCODE_TEXKILL,
	OPCODE_TEX,
	OPCODE_TEXBEM,
	OPCODE_TEXBEML,
	OPCODE_TEXREG2AR,
	OPCODE_TEXREG2GB,
	OPCODE_TEXM3X2PAD,
	OPCODE_TEXM3X2TEX,
	OPCODE_TEXM3X3PAD,
	OPCODE_TEXM3X3TEX,
	OPCODE_TEXM3X3DIFF,
	OPCODE_TEXM3X3SPEC,
	OPCODE_TEXM3X3VSPEC,
	OPCODE_EXPP,
	OPCODE_LOGP,
	OPCODE_CND,
	OPCODE_DEF,
	OPCODE_TEXREG2RGB,
	OPCODE_TEXDP3TEX,
	OPCODE_TEXM3X2DEPTH,
	OPCODE_TEXDP3,
	OPCODE_TEXM3X3,
	OPCODE_TEXDEPTH,
	OPCODE_CMP,
	OPCODE_BEM,
	OPCODE_DP2ADD,
	OPCODE_DSX,
	OPCODE_DSY,
	OPCODE_TEXLDD,
	OPCODE_SETP,
	OPCODE_TEXLDL,
	OPCODE_BREAKP,
	OPCODE_PHASE = 0xFFFD,
	OPCODE_COMMENT = 0xFFFE,
	OPCODE_END = 0xFFFF,
};

// Register files.
enum class RegisterType_t : std::uint8_t
{
	REGISTER_TYPE_TEMP = 0,
	REGISTER_TYPE_INPUT,
	REGISTER_TYPE_CONST,
	REGISTER_TYPE_TEXTURE, // a vertex shader's address register (a0)
	REGISTER_TYPE_RASTOUT,
	REGISTER_TYPE_ATTROUT,
	REGISTER_TYPE_OUTPUT, // texture coordinates out (oT), or vs_3_0's outputs (o)
	REGISTER_TYPE_CONSTINT,
	REGISTER_TYPE_COLOROUT,
	REGISTER_TYPE_DEPTHOUT,
	REGISTER_TYPE_SAMPLER,
	REGISTER_TYPE_CONST2,
	REGISTER_TYPE_CONST3,
	REGISTER_TYPE_CONST4,
	REGISTER_TYPE_CONSTBOOL,
	REGISTER_TYPE_LOOP,
	REGISTER_TYPE_TEMPFLOAT16,
	REGISTER_TYPE_MISCTYPE, // vPos, vFace
	REGISTER_TYPE_LABEL,
	REGISTER_TYPE_PREDICATE,
};

// What a source register is read through.
enum class SourceModifier_t : std::uint8_t
{
	SOURCE_MODIFIER_NONE = 0,
	SOURCE_MODIFIER_NEGATE,
	SOURCE_MODIFIER_BIAS,
	SOURCE_MODIFIER_BIAS_NEGATE,
	SOURCE_MODIFIER_SIGN,
	SOURCE_MODIFIER_SIGN_NEGATE,
	SOURCE_MODIFIER_COMPLEMENT,
	SOURCE_MODIFIER_X2,
	SOURCE_MODIFIER_X2_NEGATE,
	SOURCE_MODIFIER_DIVIDE_Z,
	SOURCE_MODIFIER_DIVIDE_W,
	SOURCE_MODIFIER_ABS,
	SOURCE_MODIFIER_ABS_NEGATE,
	SOURCE_MODIFIER_NOT,
};

struct Register_t
{
	RegisterType_t m_nType = RegisterType_t::REGISTER_TYPE_TEMP;
	std::uint32_t m_nIndex = 0;
	// A destination's components written (bit 0 x ... bit 3 w), a source's swizzle (two bits a
	// component, x first: 0xE4 is .xyzw as it is).
	std::uint8_t m_nMask = 0xF;
	std::uint8_t m_nSwizzle = 0xE4;
	SourceModifier_t m_nModifier = SourceModifier_t::SOURCE_MODIFIER_NONE;
	bool m_bSaturate = false;      // a destination's _sat
	bool m_bPartial = false;       // _pp
	bool m_bCentroid = false;      // _centroid
	int m_nShift = 0;              // ps_1_x's _x2, _x4, _d2... as a power of two
	std::optional<std::pair<RegisterType_t, std::uint32_t>> m_Relative; // [a0.x] or [aL]
	std::uint8_t m_nRelativeComponent = 0;
};

struct Instruction_t
{
	Opcode_t m_nOpcode = Opcode_t::OPCODE_NOP;
	std::uint8_t m_nControls = 0; // comparison (ifc, breakc, setp), or texld's project/bias
	bool m_bCoissue = false;
	bool m_bPredicated = false;
	std::optional<Register_t> m_Dest;
	std::vector<Register_t> m_Sources;
	Register_t m_Predicate;
	// dcl: what the register is (usage and index, or a sampler's texture type); def, defi, defb:
	// the values as written.
	std::uint32_t m_nUsage = 0;
	std::uint32_t m_nUsageIndex = 0;
	std::uint32_t m_nSamplerType = 0; // 2 2D, 3 cube, 4 volume
	std::array<std::uint32_t, 4> m_Values{};
};

// A constant or sampler the constant table names.
struct Constant_t
{
	std::string m_szName;
	int m_nSet = 0; // 0 bool, 1 int4, 2 float4, 3 sampler
	int m_nRegister = 0;
	int m_nCount = 0;
};

struct Program_t
{
	bool m_bPixel = false;
	int m_nMajor = 0;
	int m_nMinor = 0;
	std::vector<Instruction_t> m_Instructions;
	std::vector<Constant_t> m_Constants;
	std::string m_szCreator; // the compiler, as the constant table says
	[[nodiscard]] std::string Profile() const; // "ps_2_b"
	[[nodiscard]] const Constant_t *FindConstant( int nSet, int nRegister ) const;
};

// What a program does and reads, as a shader compiler's summary counts it: instructions (not the
// declarations and defs), texture reads among them, the temporaries, and the float constants and
// samplers its instructions read (by register, each once, in order).
struct ProgramUse_t
{
	int m_nInstructions = 0;
	int m_nTextureReads = 0;
	int m_nTemps = 0;
	std::vector<int> m_Constants;
	std::vector<int> m_Samplers;
};
[[nodiscard]] ProgramUse_t DescribeUse( const Program_t &program );

// The program in tokens (the first the version); nullopt, with why, when they don't make one.
[[nodiscard]] std::optional<Program_t> ReadBytecode( std::span<const std::uint32_t> tokens, std::string *pError = nullptr );
// The assembler's text for it: one instruction a line, the constant table's names first.
[[nodiscard]] std::string Disassemble( const Program_t &program );
[[nodiscard]] std::string_view OpcodeName( Opcode_t nOpcode );

} // namespace xv::shader
