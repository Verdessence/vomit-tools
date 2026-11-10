//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Bytecode.h.
//
//===========================================================================//
#include "Bytecode.h"

#include <bit>
#include <cstring>
#include <format>

namespace xv::shader
{

namespace
{

constexpr std::uint32_t CTAB = 'C' | ( 'T' << 8 ) | ( 'A' << 16 ) | ( 'B' << 24 );
constexpr std::uint32_t PIXEL_VERSION = 0xFFFF0000;
constexpr std::uint32_t VERTEX_VERSION = 0xFFFE0000;
// More tokens than this is no shader Source's tools wrote.
constexpr std::size_t MOST_TOKENS = 1u << 20;

struct OpcodeInfo_t
{
	std::string_view m_szName;
	int m_nParameters = 0; // shader model 1's tokens after the instruction (2 and 3 say their own)
	bool m_bDest = true;   // the first is written
};

//-----------------------------------------------------------------------------
// Purpose: An opcode's name, its parameter count in shader model 1 (where the instruction token
//          doesn't hold it), and whether it writes a register.
//-----------------------------------------------------------------------------
OpcodeInfo_t InfoOf( Opcode_t nOpcode, int nMajor, int nMinor )
{
	const bool bTex14 = nMajor == 1 && nMinor >= 4;
	switch ( nOpcode )
	{
		case Opcode_t::OPCODE_NOP: return { "nop", 0, false };
		case Opcode_t::OPCODE_MOV: return { "mov", 2 };
		case Opcode_t::OPCODE_ADD: return { "add", 3 };
		case Opcode_t::OPCODE_SUB: return { "sub", 3 };
		case Opcode_t::OPCODE_MAD: return { "mad", 4 };
		case Opcode_t::OPCODE_MUL: return { "mul", 3 };
		case Opcode_t::OPCODE_RCP: return { "rcp", 2 };
		case Opcode_t::OPCODE_RSQ: return { "rsq", 2 };
		case Opcode_t::OPCODE_DP3: return { "dp3", 3 };
		case Opcode_t::OPCODE_DP4: return { "dp4", 3 };
		case Opcode_t::OPCODE_MIN: return { "min", 3 };
		case Opcode_t::OPCODE_MAX: return { "max", 3 };
		case Opcode_t::OPCODE_SLT: return { "slt", 3 };
		case Opcode_t::OPCODE_SGE: return { "sge", 3 };
		case Opcode_t::OPCODE_EXP: return { "exp", 2 };
		case Opcode_t::OPCODE_LOG: return { "log", 2 };
		case Opcode_t::OPCODE_LIT: return { "lit", 2 };
		case Opcode_t::OPCODE_DST: return { "dst", 3 };
		case Opcode_t::OPCODE_LRP: return { "lrp", 4 };
		case Opcode_t::OPCODE_FRC: return { "frc", 2 };
		case Opcode_t::OPCODE_M4X4: return { "m4x4", 3 };
		case Opcode_t::OPCODE_M4X3: return { "m4x3", 3 };
		case Opcode_t::OPCODE_M3X4: return { "m3x4", 3 };
		case Opcode_t::OPCODE_M3X3: return { "m3x3", 3 };
		case Opcode_t::OPCODE_M3X2: return { "m3x2", 3 };
		case Opcode_t::OPCODE_CALL: return { "call", 1, false };
		case Opcode_t::OPCODE_CALLNZ: return { "callnz", 2, false };
		case Opcode_t::OPCODE_LOOP: return { "loop", 2, false };
		case Opcode_t::OPCODE_RET: return { "ret", 0, false };
		case Opcode_t::OPCODE_ENDLOOP: return { "endloop", 0, false };
		case Opcode_t::OPCODE_LABEL: return { "label", 1, false };
		case Opcode_t::OPCODE_DCL: return { "dcl", 2 };
		case Opcode_t::OPCODE_POW: return { "pow", 3 };
		case Opcode_t::OPCODE_CRS: return { "crs", 3 };
		case Opcode_t::OPCODE_SGN: return { "sgn", 4 };
		case Opcode_t::OPCODE_ABS: return { "abs", 2 };
		case Opcode_t::OPCODE_NRM: return { "nrm", 2 };
		case Opcode_t::OPCODE_SINCOS: return { "sincos", 4 };
		case Opcode_t::OPCODE_REP: return { "rep", 1, false };
		case Opcode_t::OPCODE_ENDREP: return { "endrep", 0, false };
		case Opcode_t::OPCODE_IF: return { "if", 1, false };
		case Opcode_t::OPCODE_IFC: return { "if", 2, false };
		case Opcode_t::OPCODE_ELSE: return { "else", 0, false };
		case Opcode_t::OPCODE_ENDIF: return { "endif", 0, false };
		case Opcode_t::OPCODE_BREAK: return { "break", 0, false };
		case Opcode_t::OPCODE_BREAKC: return { "break", 2, false };
		case Opcode_t::OPCODE_MOVA: return { "mova", 2 };
		case Opcode_t::OPCODE_DEFB: return { "defb", 2 };
		case Opcode_t::OPCODE_DEFI: return { "defi", 5 };
		case Opcode_t::OPCODE_TEXCOORD: return { bTex14 ? "texcrd" : "texcoord", bTex14 ? 2 : 1 };
		case Opcode_t::OPCODE_TEXKILL: return { "texkill", 1 };
		case Opcode_t::OPCODE_TEX: return { nMajor >= 2 || bTex14 ? "texld" : "tex", nMajor >= 2 ? 3 : bTex14 ? 2
																											  : 1 };
		case Opcode_t::OPCODE_TEXBEM: return { "texbem", 2 };
		case Opcode_t::OPCODE_TEXBEML: return { "texbeml", 2 };
		case Opcode_t::OPCODE_TEXREG2AR: return { "texreg2ar", 2 };
		case Opcode_t::OPCODE_TEXREG2GB: return { "texreg2gb", 2 };
		case Opcode_t::OPCODE_TEXM3X2PAD: return { "texm3x2pad", 2 };
		case Opcode_t::OPCODE_TEXM3X2TEX: return { "texm3x2tex", 2 };
		case Opcode_t::OPCODE_TEXM3X3PAD: return { "texm3x3pad", 2 };
		case Opcode_t::OPCODE_TEXM3X3TEX: return { "texm3x3tex", 2 };
		case Opcode_t::OPCODE_TEXM3X3DIFF: return { "texm3x3diff", 2 };
		case Opcode_t::OPCODE_TEXM3X3SPEC: return { "texm3x3spec", 3 };
		case Opcode_t::OPCODE_TEXM3X3VSPEC: return { "texm3x3vspec", 2 };
		case Opcode_t::OPCODE_EXPP: return { "expp", 2 };
		case Opcode_t::OPCODE_LOGP: return { "logp", 2 };
		case Opcode_t::OPCODE_CND: return { "cnd", 4 };
		case Opcode_t::OPCODE_DEF: return { "def", 5 };
		case Opcode_t::OPCODE_TEXREG2RGB: return { "texreg2rgb", 2 };
		case Opcode_t::OPCODE_TEXDP3TEX: return { "texdp3tex", 2 };
		case Opcode_t::OPCODE_TEXM3X2DEPTH: return { "texm3x2depth", 2 };
		case Opcode_t::OPCODE_TEXDP3: return { "texdp3", 2 };
		case Opcode_t::OPCODE_TEXM3X3: return { "texm3x3", 2 };
		case Opcode_t::OPCODE_TEXDEPTH: return { "texdepth", 1 };
		case Opcode_t::OPCODE_CMP: return { "cmp", 4 };
		case Opcode_t::OPCODE_BEM: return { "bem", 3 };
		case Opcode_t::OPCODE_DP2ADD: return { "dp2add", 4 };
		case Opcode_t::OPCODE_DSX: return { "dsx", 2 };
		case Opcode_t::OPCODE_DSY: return { "dsy", 2 };
		case Opcode_t::OPCODE_TEXLDD: return { "texldd", 5 };
		case Opcode_t::OPCODE_SETP: return { "setp", 3 };
		case Opcode_t::OPCODE_TEXLDL: return { "texldl", 3 };
		case Opcode_t::OPCODE_BREAKP: return { "break", 1, false };
		case Opcode_t::OPCODE_PHASE: return { "phase", 0, false };
		default: return { {}, -1, false };
	}
}

//-----------------------------------------------------------------------------
// Purpose: A register's file: three bits low in the token, two more higher up.
//-----------------------------------------------------------------------------
RegisterType_t TypeOf( std::uint32_t nToken )
{
	return static_cast<RegisterType_t>( ( ( nToken >> 28 ) & 0x7 ) | ( ( nToken >> 8 ) & 0x18 ) );
}

//-----------------------------------------------------------------------------
// Purpose: The token reader: one parameter at a time, the relative address's own token with it
//          (shader model 2 and up write one after a register addressed relatively).
//-----------------------------------------------------------------------------
class CTokens
{
public:
	CTokens( std::span<const std::uint32_t> tokens, int nMajor )
		: m_Tokens( tokens ),
		  m_nMajor( nMajor )
	{}

	[[nodiscard]] bool AtEnd() const { return m_nAt >= m_Tokens.size(); }
	[[nodiscard]] std::size_t Left() const { return m_Tokens.size() - m_nAt; }
	[[nodiscard]] std::uint32_t Next() { return m_nAt < m_Tokens.size() ? m_Tokens[ m_nAt++ ] : 0; }
	[[nodiscard]] std::size_t At() const { return m_nAt; }
	void Skip( std::size_t n ) { m_nAt = std::min( m_Tokens.size(), m_nAt + n ); }

	Register_t Dest()
	{
		const std::uint32_t nToken = Next();
		Register_t reg = Common( nToken );
		reg.m_nMask = static_cast<std::uint8_t>( ( nToken >> 16 ) & 0xF );
		const std::uint32_t nResult = ( nToken >> 20 ) & 0xF;
		reg.m_bSaturate = ( nResult & 1 ) != 0;
		reg.m_bPartial = ( nResult & 2 ) != 0;
		reg.m_bCentroid = ( nResult & 4 ) != 0;
		const int nShift = static_cast<int>( ( nToken >> 24 ) & 0xF );
		reg.m_nShift = nShift >= 8 ? nShift - 16 : nShift;
		Relative( nToken, reg );
		return reg;
	}

	Register_t Source()
	{
		const std::uint32_t nToken = Next();
		Register_t reg = Common( nToken );
		reg.m_nSwizzle = static_cast<std::uint8_t>( ( nToken >> 16 ) & 0xFF );
		reg.m_nModifier = static_cast<SourceModifier_t>( ( nToken >> 24 ) & 0xF );
		Relative( nToken, reg );
		return reg;
	}

private:
	static Register_t Common( std::uint32_t nToken )
	{
		Register_t reg;
		reg.m_nType = TypeOf( nToken );
		reg.m_nIndex = nToken & 0x7FF;
		// The constant files past the first are numbered on from it.
		if ( reg.m_nType == RegisterType_t::REGISTER_TYPE_CONST2 )
			reg.m_nIndex += 2048;
		else if ( reg.m_nType == RegisterType_t::REGISTER_TYPE_CONST3 )
			reg.m_nIndex += 4096;
		else if ( reg.m_nType == RegisterType_t::REGISTER_TYPE_CONST4 )
			reg.m_nIndex += 6144;
		if ( reg.m_nType == RegisterType_t::REGISTER_TYPE_CONST2 || reg.m_nType == RegisterType_t::REGISTER_TYPE_CONST3 || reg.m_nType == RegisterType_t::REGISTER_TYPE_CONST4 )
			reg.m_nType = RegisterType_t::REGISTER_TYPE_CONST;
		return reg;
	}

	void Relative( std::uint32_t nToken, Register_t &reg )
	{
		if ( ( nToken & ( 1u << 13 ) ) == 0 )
			return;
		if ( m_nMajor >= 2 )
		{
			const std::uint32_t nAddress = Next();
			reg.m_Relative = std::pair{ TypeOf( nAddress ), nAddress & 0x7FF };
			reg.m_nRelativeComponent = static_cast<std::uint8_t>( ( nAddress >> 16 ) & 0x3 );
		}
		else
		{
			reg.m_Relative = std::pair{ RegisterType_t::REGISTER_TYPE_TEXTURE, 0u }; // vs_1_1's a0.x
		}
	}

	std::span<const std::uint32_t> m_Tokens;
	std::size_t m_nAt = 0;
	int m_nMajor = 0;
};

//-----------------------------------------------------------------------------
// Purpose: The names of the constants and samplers, from the compiler's table in a comment.
//-----------------------------------------------------------------------------
void ReadConstantTable( std::span<const std::uint32_t> comment, Program_t &program )
{
	if ( comment.size() < 8 || comment[ 0 ] != CTAB )
		return;
	const auto *pBytes = reinterpret_cast<const char *>( comment.data() + 1 );
	const std::size_t nBytes = ( comment.size() - 1 ) * 4;
	const auto u32 = [ & ]( std::size_t nAt ) -> std::uint32_t {
		std::uint32_t n = 0;
		if ( nAt + 4 <= nBytes )
			std::memcpy( &n, pBytes + nAt, 4 );
		return n;
	};
	const auto u16 = [ & ]( std::size_t nAt ) -> std::uint16_t {
		std::uint16_t n = 0;
		if ( nAt + 2 <= nBytes )
			std::memcpy( &n, pBytes + nAt, 2 );
		return n;
	};
	const auto text = [ & ]( std::size_t nAt ) {
		std::string sz;
		for ( std::size_t i = nAt; i < nBytes && pBytes[ i ] != '\0' && sz.size() < 256; i++ )
			sz += pBytes[ i ];
		return sz;
	};
	program.m_szCreator = text( u32( 4 ) );
	const std::uint32_t nCount = std::min( u32( 12 ), 4096u );
	const std::uint32_t nInfo = u32( 16 );
	for ( std::uint32_t i = 0; i < nCount; i++ )
	{
		const std::size_t nAt = nInfo + i * 20;
		if ( nAt + 20 > nBytes )
			break;
		program.m_Constants.push_back( { text( u32( nAt ) ), u16( nAt + 4 ), u16( nAt + 6 ), u16( nAt + 8 ) } );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A register as the assembler writes it.
//-----------------------------------------------------------------------------
std::string RegisterName( RegisterType_t nType, std::uint32_t nIndex, bool bPixel, int nMajor )
{
	switch ( nType )
	{
		case RegisterType_t::REGISTER_TYPE_TEMP: return std::format( "r{}", nIndex );
		case RegisterType_t::REGISTER_TYPE_INPUT: return std::format( "v{}", nIndex );
		case RegisterType_t::REGISTER_TYPE_CONST: return std::format( "c{}", nIndex );
		case RegisterType_t::REGISTER_TYPE_TEXTURE: return bPixel ? std::format( "t{}", nIndex ) : std::format( "a{}", nIndex );
		case RegisterType_t::REGISTER_TYPE_RASTOUT: return nIndex == 0 ? "oPos" : nIndex == 1 ? "oFog"
																							  : "oPts";
		case RegisterType_t::REGISTER_TYPE_ATTROUT: return std::format( "oD{}", nIndex );
		case RegisterType_t::REGISTER_TYPE_OUTPUT: return nMajor >= 3 ? std::format( "o{}", nIndex ) : std::format( "oT{}", nIndex );
		case RegisterType_t::REGISTER_TYPE_CONSTINT: return std::format( "i{}", nIndex );
		case RegisterType_t::REGISTER_TYPE_COLOROUT: return std::format( "oC{}", nIndex );
		case RegisterType_t::REGISTER_TYPE_DEPTHOUT: return "oDepth";
		case RegisterType_t::REGISTER_TYPE_SAMPLER: return std::format( "s{}", nIndex );
		case RegisterType_t::REGISTER_TYPE_CONSTBOOL: return std::format( "b{}", nIndex );
		case RegisterType_t::REGISTER_TYPE_LOOP: return "aL";
		case RegisterType_t::REGISTER_TYPE_TEMPFLOAT16: return std::format( "half{}", nIndex );
		case RegisterType_t::REGISTER_TYPE_MISCTYPE: return nIndex == 0 ? "vPos" : "vFace";
		case RegisterType_t::REGISTER_TYPE_LABEL: return std::format( "l{}", nIndex );
		case RegisterType_t::REGISTER_TYPE_PREDICATE: return std::format( "p{}", nIndex );
		default: return std::format( "?{}", nIndex );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A source with its modifier and swizzle (left out when it is .xyzw), or a
//          destination with its mask.
//-----------------------------------------------------------------------------
std::string SourceText( const Register_t &reg, bool bPixel, int nMajor )
{
	std::string szName = RegisterName( reg.m_nType, reg.m_nIndex, bPixel, nMajor );
	if ( reg.m_Relative )
	{
		// c[a0.x + 5]: the file's letters, the address register, then the base.
		const std::string szFile = szName.substr( 0, szName.find_first_of( "0123456789" ) );
		const bool bLoop = reg.m_Relative->first == RegisterType_t::REGISTER_TYPE_LOOP;
		const std::string szAddress = RegisterName( reg.m_Relative->first, reg.m_Relative->second, bPixel, nMajor ) + ( bLoop ? std::string{} : std::string{ "." } + "xyzw"[ reg.m_nRelativeComponent ] );
		szName = std::format( "{}[{} + {}]", szFile, szAddress, reg.m_nIndex );
	}
	std::string szSwizzle;
	if ( reg.m_nSwizzle != 0xE4 )
	{
		std::array<char, 4> parts{};
		for ( int i = 0; i < 4; i++ )
			parts[ static_cast<std::size_t>( i ) ] = "xyzw"[ ( reg.m_nSwizzle >> ( i * 2 ) ) & 3 ];
		// Repeated last components are left off, as the assembler writes .xyy as .xy and .xxxx as .x.
		int nLength = 4;
		while ( nLength > 1 && parts[ static_cast<std::size_t>( nLength - 1 ) ] == parts[ static_cast<std::size_t>( nLength - 2 ) ] )
			nLength--;
		szSwizzle = "." + std::string( parts.data(), static_cast<std::size_t>( nLength ) );
	}
	const std::string szBody = szName + szSwizzle;
	switch ( reg.m_nModifier )
	{
		case SourceModifier_t::SOURCE_MODIFIER_NEGATE: return "-" + szBody;
		case SourceModifier_t::SOURCE_MODIFIER_BIAS: return szName + "_bias" + szSwizzle;
		case SourceModifier_t::SOURCE_MODIFIER_BIAS_NEGATE: return "-" + szName + "_bias" + szSwizzle;
		case SourceModifier_t::SOURCE_MODIFIER_SIGN: return szName + "_bx2" + szSwizzle;
		case SourceModifier_t::SOURCE_MODIFIER_SIGN_NEGATE: return "-" + szName + "_bx2" + szSwizzle;
		case SourceModifier_t::SOURCE_MODIFIER_COMPLEMENT: return "1 - " + szBody;
		case SourceModifier_t::SOURCE_MODIFIER_X2: return szName + "_x2" + szSwizzle;
		case SourceModifier_t::SOURCE_MODIFIER_X2_NEGATE: return "-" + szName + "_x2" + szSwizzle;
		case SourceModifier_t::SOURCE_MODIFIER_DIVIDE_Z: return szName + "_dz" + szSwizzle;
		case SourceModifier_t::SOURCE_MODIFIER_DIVIDE_W: return szName + "_dw" + szSwizzle;
		case SourceModifier_t::SOURCE_MODIFIER_ABS: return szName + "_abs" + szSwizzle;
		case SourceModifier_t::SOURCE_MODIFIER_ABS_NEGATE: return "-" + szName + "_abs" + szSwizzle;
		case SourceModifier_t::SOURCE_MODIFIER_NOT: return "!" + szBody;
		default: return szBody;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string DestText( const Register_t &reg, bool bPixel, int nMajor )
{
	std::string sz = RegisterName( reg.m_nType, reg.m_nIndex, bPixel, nMajor );
	if ( reg.m_nMask != 0xF && reg.m_nMask != 0 )
	{
		sz += '.';
		for ( int i = 0; i < 4; i++ )
			if ( reg.m_nMask & ( 1 << i ) )
				sz += "xyzw"[ i ];
	}
	return sz;
}

//-----------------------------------------------------------------------------
// Purpose: A dcl's usage, as its name and index (texcoord0 is dcl_texcoord0, written texcoord).
//-----------------------------------------------------------------------------
std::string UsageName( std::uint32_t nUsage, std::uint32_t nIndex )
{
	static constexpr std::array<std::string_view, 14> NAMES{ "position", "blendweight", "blendindices", "normal", "psize", "texcoord", "tangent", "binormal", "tessfactor", "positiont", "color", "fog", "depth", "sample" };
	const std::string_view szName = nUsage < NAMES.size() ? NAMES[ nUsage ] : std::string_view{ "usage" };
	return nIndex ? std::format( "{}{}", szName, nIndex ) : std::string{ szName };
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view OpcodeName( Opcode_t nOpcode )
{
	return InfoOf( nOpcode, 3, 0 ).m_szName;
}

//-----------------------------------------------------------------------------
// Purpose: ps_2_a and ps_2_b are both written 2.1; Source's files call that ps20b.
//-----------------------------------------------------------------------------
std::string Program_t::Profile() const
{
	const char *pszKind = m_bPixel ? "ps" : "vs";
	if ( m_nMajor == 2 && m_nMinor == 1 )
		return std::format( "{}_2_b", pszKind );
	return std::format( "{}_{}_{}", pszKind, m_nMajor, m_nMinor );
}

//-----------------------------------------------------------------------------
// Purpose: The constant table's entry that covers a register.
//-----------------------------------------------------------------------------
const Constant_t *Program_t::FindConstant( int nSet, int nRegister ) const
{
	for ( const Constant_t &constant : m_Constants )
	{
		if ( constant.m_nSet == nSet && nRegister >= constant.m_nRegister && nRegister < constant.m_nRegister + std::max( 1, constant.m_nCount ) )
			return &constant;
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The version token, then instructions until the end token; comments hold the
//          constant table.
//-----------------------------------------------------------------------------
std::optional<Program_t> ReadBytecode( std::span<const std::uint32_t> tokens, std::string *pError )
{
	const auto fail = [ pError ]( std::string szWhy ) -> std::optional<Program_t> {
		if ( pError )
			*pError = std::move( szWhy );
		return std::nullopt;
	};
	if ( tokens.empty() || tokens.size() > MOST_TOKENS )
		return fail( "no bytecode" );
	const std::uint32_t nVersion = tokens[ 0 ];
	Program_t program;
	if ( ( nVersion & 0xFFFF0000 ) == PIXEL_VERSION )
		program.m_bPixel = true;
	else if ( ( nVersion & 0xFFFF0000 ) != VERTEX_VERSION )
		return fail( std::format( "{:#010x} isn't a shader's version", nVersion ) );
	program.m_nMajor = static_cast<int>( ( nVersion >> 8 ) & 0xFF );
	program.m_nMinor = static_cast<int>( nVersion & 0xFF );
	if ( program.m_nMajor < 1 || program.m_nMajor > 3 )
		return fail( std::format( "shader model {}.{} isn't Direct3D 9's", program.m_nMajor, program.m_nMinor ) );
	CTokens reader( tokens.subspan( 1 ), program.m_nMajor );
	bool bEnded = false;
	while ( !reader.AtEnd() )
	{
		const std::uint32_t nToken = reader.Next();
		const auto nOpcode = static_cast<Opcode_t>( nToken & 0xFFFF );
		if ( nOpcode == Opcode_t::OPCODE_END )
		{
			bEnded = true;
			break;
		}
		if ( nOpcode == Opcode_t::OPCODE_COMMENT )
		{
			const std::size_t nLength = ( nToken >> 16 ) & 0x7FFF;
			if ( nLength > reader.Left() )
				return fail( "a comment runs past the end" );
			ReadConstantTable( tokens.subspan( 1 + reader.At(), nLength ), program );
			reader.Skip( nLength );
			continue;
		}
		const OpcodeInfo_t info = InfoOf( nOpcode, program.m_nMajor, program.m_nMinor );
		if ( info.m_nParameters < 0 )
			return fail( std::format( "unknown opcode {} at token {}", static_cast<int>( nOpcode ), reader.At() ) );
		Instruction_t instruction;
		instruction.m_nOpcode = nOpcode;
		instruction.m_nControls = static_cast<std::uint8_t>( ( nToken >> 16 ) & 0xFF );
		instruction.m_bPredicated = ( nToken & ( 1u << 28 ) ) != 0;
		instruction.m_bCoissue = ( nToken & ( 1u << 30 ) ) != 0;
		// Shader model 2 and up say how many tokens follow; 1 has them by opcode.
		const std::size_t nLength = program.m_nMajor >= 2 ? ( ( nToken >> 24 ) & 0xF ) : static_cast<std::size_t>( info.m_nParameters );
		if ( nLength > reader.Left() )
			return fail( std::format( "{} runs past the end", info.m_szName ) );
		const std::size_t nEnd = reader.At() + nLength;
		switch ( nOpcode )
		{
			case Opcode_t::OPCODE_DCL:
			{
				const std::uint32_t nUsage = reader.Next();
				instruction.m_nUsage = nUsage & 0x1F;
				instruction.m_nUsageIndex = ( nUsage >> 16 ) & 0xF;
				instruction.m_nSamplerType = ( nUsage >> 27 ) & 0xF;
				instruction.m_Dest = reader.Dest();
				break;
			}
			case Opcode_t::OPCODE_DEF:
			case Opcode_t::OPCODE_DEFI:
			case Opcode_t::OPCODE_DEFB:
			{
				instruction.m_Dest = reader.Dest();
				for ( std::size_t i = 0; i < 4 && reader.At() < nEnd; i++ )
					instruction.m_Values[ i ] = reader.Next();
				break;
			}
			default:
			{
				if ( info.m_bDest && reader.At() < nEnd )
					instruction.m_Dest = reader.Dest();
				// The predicate a predicated instruction is written with comes before its sources.
				if ( instruction.m_bPredicated && reader.At() < nEnd )
					instruction.m_Predicate = reader.Source();
				while ( reader.At() < nEnd )
					instruction.m_Sources.push_back( reader.Source() );
				break;
			}
		}
		if ( reader.At() != nEnd )
			return fail( std::format( "{}'s registers don't fill its tokens", info.m_szName ) );
		program.m_Instructions.push_back( std::move( instruction ) );
	}
	if ( !bEnded )
		return fail( "no end token" );
	return program;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Disassemble( const Program_t &program )
{
	std::string szOut;
	if ( !program.m_szCreator.empty() )
		szOut += "// " + program.m_szCreator + "\n";
	if ( !program.m_Constants.empty() )
	{
		static constexpr std::array<std::string_view, 4> SETS{ "b", "i", "c", "s" };
		szOut += "//\n// Parameters:\n";
		for ( const Constant_t &constant : program.m_Constants )
		{
			const std::string_view szSet = constant.m_nSet >= 0 && constant.m_nSet < 4 ? SETS[ static_cast<std::size_t>( constant.m_nSet ) ] : std::string_view{ "?" };
			szOut += std::format( "//   {:<24} {}{}{}\n", constant.m_szName, szSet, constant.m_nRegister, constant.m_nCount > 1 ? std::format( " ({} registers)", constant.m_nCount ) : std::string{} );
		}
		szOut += "//\n";
	}
	szOut += program.Profile() + "\n";
	int nDepth = 0;
	for ( const Instruction_t &instruction : program.m_Instructions )
	{
		const OpcodeInfo_t info = InfoOf( instruction.m_nOpcode, program.m_nMajor, program.m_nMinor );
		if ( instruction.m_nOpcode == Opcode_t::OPCODE_ELSE || instruction.m_nOpcode == Opcode_t::OPCODE_ENDIF || instruction.m_nOpcode == Opcode_t::OPCODE_ENDLOOP || instruction.m_nOpcode == Opcode_t::OPCODE_ENDREP )
			nDepth = std::max( 0, nDepth - 1 );
		std::string szLine( static_cast<std::size_t>( 4 + nDepth * 2 ), ' ' );
		if ( instruction.m_bCoissue )
			szLine += "+";
		if ( instruction.m_bPredicated )
			szLine += "(" + SourceText( instruction.m_Predicate, program.m_bPixel, program.m_nMajor ) + ") ";
		std::string szName{ info.m_szName };
		if ( instruction.m_nOpcode == Opcode_t::OPCODE_DCL && instruction.m_Dest )
		{
			const RegisterType_t nType = instruction.m_Dest->m_nType;
			if ( nType == RegisterType_t::REGISTER_TYPE_SAMPLER )
				szName += instruction.m_nSamplerType == 3 ? "_cube" : instruction.m_nSamplerType == 4 ? "_volume"
																									  : "_2d";
			else if ( !program.m_bPixel || program.m_nMajor >= 3 )
				szName += "_" + UsageName( instruction.m_nUsage, instruction.m_nUsageIndex );
		}
		static constexpr std::array<std::string_view, 7> COMPARISONS{ "", "_gt", "_eq", "_ge", "_lt", "_ne", "_le" };
		if ( ( instruction.m_nOpcode == Opcode_t::OPCODE_IFC || instruction.m_nOpcode == Opcode_t::OPCODE_BREAKC || instruction.m_nOpcode == Opcode_t::OPCODE_SETP ) && instruction.m_nControls < COMPARISONS.size() )
			szName += COMPARISONS[ instruction.m_nControls ];
		if ( instruction.m_nOpcode == Opcode_t::OPCODE_TEX && program.m_nMajor >= 2 )
			szName += instruction.m_nControls == 1 ? "p" : instruction.m_nControls == 2 ? "b"
																						: "";
		if ( instruction.m_Dest && instruction.m_nOpcode != Opcode_t::OPCODE_DCL )
		{
			const Register_t &dest = *instruction.m_Dest;
			if ( dest.m_nShift )
				szName += dest.m_nShift > 0 ? std::format( "_x{}", 1 << dest.m_nShift ) : std::format( "_d{}", 1 << -dest.m_nShift );
			if ( dest.m_bSaturate )
				szName += "_sat";
			if ( dest.m_bPartial )
				szName += "_pp";
			if ( dest.m_bCentroid )
				szName += "_centroid";
		}
		szLine += szName;
		std::vector<std::string> parts;
		if ( instruction.m_Dest )
			parts.push_back( DestText( *instruction.m_Dest, program.m_bPixel, program.m_nMajor ) );
		if ( instruction.m_nOpcode == Opcode_t::OPCODE_DEF )
		{
			for ( const std::uint32_t nValue : instruction.m_Values )
				parts.push_back( std::format( "{}", std::bit_cast<float>( nValue ) ) );
		}
		else if ( instruction.m_nOpcode == Opcode_t::OPCODE_DEFI )
		{
			for ( const std::uint32_t nValue : instruction.m_Values )
				parts.push_back( std::format( "{}", static_cast<std::int32_t>( nValue ) ) );
		}
		else if ( instruction.m_nOpcode == Opcode_t::OPCODE_DEFB )
		{
			parts.push_back( instruction.m_Values[ 0 ] ? "true" : "false" );
		}
		for ( const Register_t &source : instruction.m_Sources )
			parts.push_back( SourceText( source, program.m_bPixel, program.m_nMajor ) );
		for ( std::size_t i = 0; i < parts.size(); i++ )
			szLine += ( i == 0 ? " " : ", " ) + parts[ i ];
		// Which named constant or sampler a register is, where the table says.
		std::string szNote;
		for ( const Register_t &source : instruction.m_Sources )
		{
			const int nSet = source.m_nType == RegisterType_t::REGISTER_TYPE_SAMPLER ? 3 : source.m_nType == RegisterType_t::REGISTER_TYPE_CONST ? 2
																																				 : -1;
			if ( const Constant_t *pConstant = nSet >= 0 ? program.FindConstant( nSet, static_cast<int>( source.m_nIndex ) ) : nullptr )
				szNote += ( szNote.empty() ? "  // " : ", " ) + pConstant->m_szName;
		}
		szOut += szLine + szNote + "\n";
		if ( instruction.m_nOpcode == Opcode_t::OPCODE_IF || instruction.m_nOpcode == Opcode_t::OPCODE_IFC || instruction.m_nOpcode == Opcode_t::OPCODE_ELSE || instruction.m_nOpcode == Opcode_t::OPCODE_LOOP || instruction.m_nOpcode == Opcode_t::OPCODE_REP )
			nDepth++;
	}
	return szOut;
}

} // namespace xv::shader
