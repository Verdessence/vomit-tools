//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Glsl.h.
//
//===========================================================================//
#include "Glsl.h"

#include <array>
#include <format>
#include <vector>

namespace xv::shader
{

namespace
{

constexpr std::string_view COMPONENTS = "xyzw";

//-----------------------------------------------------------------------------
// Purpose: What goes before the program's own statements: the inputs, the helpers that read a
//          sampler as the game would, and the registers.
//-----------------------------------------------------------------------------
constexpr std::string_view PROLOGUE = R"(#version 440

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
	vec4 c[224];
	ivec4 i[16];
	vec4 b[16];
	vec4 uScreen;       // width, height, gl_FragCoord's y up (1), the frame's rows bottom up (1)
	vec4 uFrameSampler; // s0..s3: 1 where the sampler is the frame
	vec4 uSrgb;         // s0..s3: 1 where it is read as sRGB (made linear)
	vec4 uMode;         // the blend (0 none, 1 add, 2 colour overlay, 3 alpha), linear write, copy alpha
};

layout(binding = 1) uniform sampler2D s0;
layout(binding = 2) uniform sampler2D s1;
layout(binding = 3) uniform sampler2D s2;
layout(binding = 4) uniform sampler2D s3;
layout(binding = 5) uniform sampler2D uFrame;

vec3 ToLinear(vec3 v) { return mix(v / 12.92, pow((v + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), v)); }
vec3 ToSrgb(vec3 v) { v = clamp(v, 0.0, 1.0); return mix(v * 12.92, 1.055 * pow(v, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), v)); }

// The frame's own picture is read with its rows as the backend keeps them; the game's textures
// as they are. sRGB textures are read made linear.
vec2 Place(vec2 uv, float flFrame) { return (flFrame > 0.5 && uScreen.w > 0.5) ? vec2(uv.x, 1.0 - uv.y) : uv; }
vec4 Read(vec4 v, float flSrgb) { return flSrgb > 0.5 ? vec4(ToLinear(v.rgb), v.a) : v; }
vec4 Sample0(vec2 uv, float flLod, int nKind, vec2 dx, vec2 dy) { vec2 at = Place(uv, uFrameSampler.x); return Read(nKind == 1 ? textureLod(s0, at, flLod) : nKind == 2 ? texture(s0, at, flLod) : nKind == 3 ? textureGrad(s0, at, dx, dy) : texture(s0, at), uSrgb.x); }
vec4 Sample1(vec2 uv, float flLod, int nKind, vec2 dx, vec2 dy) { vec2 at = Place(uv, uFrameSampler.y); return Read(nKind == 1 ? textureLod(s1, at, flLod) : nKind == 2 ? texture(s1, at, flLod) : nKind == 3 ? textureGrad(s1, at, dx, dy) : texture(s1, at), uSrgb.y); }
vec4 Sample2(vec2 uv, float flLod, int nKind, vec2 dx, vec2 dy) { vec2 at = Place(uv, uFrameSampler.z); return Read(nKind == 1 ? textureLod(s2, at, flLod) : nKind == 2 ? texture(s2, at, flLod) : nKind == 3 ? textureGrad(s2, at, dx, dy) : texture(s2, at), uSrgb.z); }
vec4 Sample3(vec2 uv, float flLod, int nKind, vec2 dx, vec2 dy) { vec2 at = Place(uv, uFrameSampler.w); return Read(nKind == 1 ? textureLod(s3, at, flLod) : nKind == 2 ? texture(s3, at, flLod) : nKind == 3 ? textureGrad(s3, at, dx, dy) : texture(s3, at), uSrgb.w); }
float Rcp(float f) { return f == 0.0 ? 3.0e38 : 1.0 / f; }

void main() {
	// The pixel's place from the top left, as Direct3D has it, and across the screen from 0 to 1.
	vec2 pixel = vec2(gl_FragCoord.x, uScreen.z > 0.5 ? uScreen.y - gl_FragCoord.y : gl_FragCoord.y);
	vec2 uv = pixel / uScreen.xy;
	vec4 frame = texture(uFrame, Place(uv, 1.0));
	vec4 r[32];
	for (int k = 0; k < 32; k++) r[k] = vec4(0.0);
	vec4 t[8];
	for (int k = 0; k < 8; k++) t[k] = vec4(0.0);
	t[0] = vec4(uv, 0.0, 1.0);
	t[2] = vec4(uv, 0.0, 1.0);
	vec4 o0 = vec4(0.0);
	bvec4 p0 = bvec4(false);
	int aL = 0;
	bool bKilled = false;
	vec4 vPos = vec4(floor(pixel), 0.0, 1.0);
)";

constexpr std::string_view EPILOGUE = R"(
	// Onto the frame, as the material blends it.
	vec4 result = clamp(o0, 0.0, 1.0);
	bool bLinear = uMode.y > 0.5;
	vec3 behind = bLinear ? frame.rgb : ToLinear(frame.rgb);
	vec3 colour = result.rgb;
	int nBlend = int(uMode.x + 0.5);
	if (nBlend == 1) colour = colour + behind;
	else if (nBlend == 2) colour = colour + behind * (1.0 - result.a);
	else if (nBlend == 3) colour = colour * result.a + behind * (1.0 - result.a);
	colour = bLinear ? clamp(colour, 0.0, 1.0) : ToSrgb(colour);
	fragColor = bKilled ? vec4(frame.rgb, 1.0) : vec4(colour, 1.0);
}
)";

//-----------------------------------------------------------------------------
// Purpose: The writer: each instruction as statements, the blocks' nesting kept.
//-----------------------------------------------------------------------------
class CWriter
{
public:
	explicit CWriter( const Program_t &program )
		: m_Program( program )
	{}

	bool Write( std::string &szOut, std::string &szError )
	{
		m_pOut = &szOut;
		std::vector<int> loops; // how deep each open loop's aL save is, for its end
		for ( const Instruction_t &instruction : m_Program.m_Instructions )
		{
			if ( !Statement( instruction, loops, szError ) )
				return false;
		}
		return true;
	}

private:
	//-----------------------------------------------------------------------------
	// A register read as a vec4, before its swizzle and modifier.
	//-----------------------------------------------------------------------------
	[[nodiscard]] std::string Base( const Register_t &reg ) const
	{
		const std::string szIndex = reg.m_Relative ? std::format( "{} + aL", reg.m_nIndex ) : std::format( "{}", reg.m_nIndex );
		switch ( reg.m_nType )
		{
			case RegisterType_t::REGISTER_TYPE_TEMP: return std::format( "r[{}]", reg.m_nIndex );
			case RegisterType_t::REGISTER_TYPE_CONST: return std::format( "c[{}]", szIndex );
			case RegisterType_t::REGISTER_TYPE_TEXTURE: return std::format( "t[{}]", reg.m_nIndex & 7 );
			case RegisterType_t::REGISTER_TYPE_INPUT:
				if ( m_Program.m_nMajor >= 3 )
				{
					// ps_3_0's inputs by what they were declared: texcoords as ps_2's t#, colours white.
					for ( const Instruction_t &instruction : m_Program.m_Instructions )
						if ( instruction.m_nOpcode == Opcode_t::OPCODE_DCL && instruction.m_Dest && instruction.m_Dest->m_nType == RegisterType_t::REGISTER_TYPE_INPUT && instruction.m_Dest->m_nIndex == reg.m_nIndex && instruction.m_nUsage == 5 )
							return std::format( "t[{}]", instruction.m_nUsageIndex & 7 );
				}
				return "vec4(1.0)";
			case RegisterType_t::REGISTER_TYPE_CONSTINT: return std::format( "vec4(i[{}])", reg.m_nIndex & 15 );
			case RegisterType_t::REGISTER_TYPE_CONSTBOOL: return std::format( "vec4(b[{}].x != 0.0 ? 1.0 : 0.0)", reg.m_nIndex & 15 );
			case RegisterType_t::REGISTER_TYPE_MISCTYPE: return reg.m_nIndex == 0 ? "vPos" : "vec4(1.0)";
			case RegisterType_t::REGISTER_TYPE_LOOP: return "vec4(float(aL))";
			case RegisterType_t::REGISTER_TYPE_PREDICATE: return "vec4(p0)";
			case RegisterType_t::REGISTER_TYPE_COLOROUT: return "o0";
			default: return "vec4(0.0)";
		}
	}

	//-----------------------------------------------------------------------------
	// A source as a vec4: swizzled, then modified.
	//-----------------------------------------------------------------------------
	[[nodiscard]] std::string Source( const Register_t &reg ) const
	{
		std::string sz = Base( reg );
		if ( reg.m_nSwizzle != 0xE4 )
		{
			sz = "(" + sz + ").";
			for ( int i = 0; i < 4; i++ )
				sz += COMPONENTS[ ( reg.m_nSwizzle >> ( i * 2 ) ) & 3 ];
		}
		switch ( reg.m_nModifier )
		{
			case SourceModifier_t::SOURCE_MODIFIER_NEGATE: return "(-" + sz + ")";
			case SourceModifier_t::SOURCE_MODIFIER_BIAS: return "(" + sz + " - 0.5)";
			case SourceModifier_t::SOURCE_MODIFIER_BIAS_NEGATE: return "(0.5 - " + sz + ")";
			case SourceModifier_t::SOURCE_MODIFIER_SIGN: return "(2.0 * " + sz + " - 1.0)";
			case SourceModifier_t::SOURCE_MODIFIER_SIGN_NEGATE: return "(1.0 - 2.0 * " + sz + ")";
			case SourceModifier_t::SOURCE_MODIFIER_COMPLEMENT: return "(1.0 - " + sz + ")";
			case SourceModifier_t::SOURCE_MODIFIER_X2: return "(2.0 * " + sz + ")";
			case SourceModifier_t::SOURCE_MODIFIER_X2_NEGATE: return "(-2.0 * " + sz + ")";
			case SourceModifier_t::SOURCE_MODIFIER_ABS: return "abs(" + sz + ")";
			case SourceModifier_t::SOURCE_MODIFIER_ABS_NEGATE: return "(-abs(" + sz + "))";
			default: return sz;
		}
	}

	[[nodiscard]] std::string Src( const Instruction_t &instruction, std::size_t n ) const { return n < instruction.m_Sources.size() ? Source( instruction.m_Sources[ n ] ) : std::string{ "vec4(0.0)" }; }

	//-----------------------------------------------------------------------------
	// The comparison ifc, breakc and setp name, between two scalars (or vectors: bvec4).
	//-----------------------------------------------------------------------------
	[[nodiscard]] static std::string Compare( std::uint8_t nComparison, const std::string &a, const std::string &b, bool bVector )
	{
		static constexpr std::array<std::string_view, 7> SCALAR{ "false", ">", "==", ">=", "<", "!=", "<=" };
		static constexpr std::array<std::string_view, 7> VECTOR{ "", "greaterThan", "equal", "greaterThanEqual", "lessThan", "notEqual", "lessThanEqual" };
		if ( nComparison == 0 || nComparison >= SCALAR.size() )
			return bVector ? "bvec4(false)" : "false";
		if ( bVector )
			return std::format( "{}({}, {})", VECTOR[ nComparison ], a, b );
		return std::format( "({} {} {})", a, SCALAR[ nComparison ], b );
	}

	//-----------------------------------------------------------------------------
	// The result (a vec4) into the destination's masked components: saturated, predicated.
	//-----------------------------------------------------------------------------
	void Assign( const Instruction_t &instruction, const std::string &szValue )
	{
		const Register_t &dest = *instruction.m_Dest;
		std::string szTarget;
		if ( dest.m_nType == RegisterType_t::REGISTER_TYPE_TEMP )
			szTarget = std::format( "r[{}]", dest.m_nIndex & 31 );
		else if ( dest.m_nType == RegisterType_t::REGISTER_TYPE_COLOROUT && dest.m_nIndex == 0 )
			szTarget = "o0";
		else if ( dest.m_nType == RegisterType_t::REGISTER_TYPE_PREDICATE )
		{
			// setp's destination: written by its own case.
			return;
		}
		else
			return; // other targets and depth aren't kept
		std::string szMask;
		for ( int i = 0; i < 4; i++ )
			if ( dest.m_nMask & ( 1 << i ) )
				szMask += COMPONENTS[ static_cast<std::size_t>( i ) ];
		if ( szMask.empty() )
			return;
		std::string szResult = szValue;
		if ( dest.m_nShift )
			szResult = std::format( "({} * {})", szResult, dest.m_nShift > 0 ? 1 << dest.m_nShift : 1 );
		if ( dest.m_bSaturate )
			szResult = "clamp(" + szResult + ", 0.0, 1.0)";
		if ( instruction.m_bPredicated )
		{
			// Where the predicate (swizzled, perhaps negated) is true.
			std::string szPredicate = "p0";
			if ( instruction.m_Predicate.m_nSwizzle != 0xE4 )
			{
				szPredicate += ".";
				for ( int i = 0; i < 4; i++ )
					szPredicate += COMPONENTS[ ( instruction.m_Predicate.m_nSwizzle >> ( i * 2 ) ) & 3 ];
			}
			if ( instruction.m_Predicate.m_nModifier == SourceModifier_t::SOURCE_MODIFIER_NOT )
				szPredicate = "not(" + szPredicate + ")";
			szResult = std::format( "mix({}, {}, {})", szTarget, szResult, szPredicate );
		}
		Line( std::format( "{}.{} = ({}).{};", szTarget, szMask, szResult, szMask ) );
	}

	void Line( const std::string &sz )
	{
		*m_pOut += std::string( static_cast<std::size_t>( 1 + m_nDepth ), '\t' ) + sz + "\n";
	}

	//-----------------------------------------------------------------------------
	// One instruction.
	//-----------------------------------------------------------------------------
	bool Statement( const Instruction_t &instruction, std::vector<int> &loops, std::string &szError )
	{
		const auto a = [ & ] { return Src( instruction, 0 ); };
		const auto b = [ & ] { return Src( instruction, 1 ); };
		const auto c = [ & ] { return Src( instruction, 2 ); };
		const auto assign = [ & ]( const std::string &sz ) {
			if ( instruction.m_Dest )
				Assign( instruction, sz );
		};
		const auto matrix = [ & ]( int nRows, int nColumns ) {
			if ( instruction.m_Sources.size() < 2 )
				return;
			std::vector<std::string> parts;
			for ( int k = 0; k < nColumns; k++ )
			{
				Register_t row = instruction.m_Sources[ 1 ];
				row.m_nIndex += static_cast<std::uint32_t>( k );
				parts.push_back( nRows == 4 ? std::format( "dot({}, {})", a(), Source( row ) ) : std::format( "dot(({}).xyz, ({}).xyz)", a(), Source( row ) ) );
			}
			while ( parts.size() < 4 )
				parts.push_back( "0.0" );
			assign( std::format( "vec4({}, {}, {}, {})", parts[ 0 ], parts[ 1 ], parts[ 2 ], parts[ 3 ] ) );
		};
		switch ( instruction.m_nOpcode )
		{
			case Opcode_t::OPCODE_NOP:
			case Opcode_t::OPCODE_DCL:
			case Opcode_t::OPCODE_DEF:
			case Opcode_t::OPCODE_DEFI:
			case Opcode_t::OPCODE_DEFB:
			case Opcode_t::OPCODE_PHASE: return true;
			case Opcode_t::OPCODE_MOV:
			case Opcode_t::OPCODE_MOVA: assign( a() ); return true;
			case Opcode_t::OPCODE_ADD: assign( a() + " + " + b() ); return true;
			case Opcode_t::OPCODE_SUB: assign( a() + " - " + b() ); return true;
			case Opcode_t::OPCODE_MUL: assign( a() + " * " + b() ); return true;
			case Opcode_t::OPCODE_MAD: assign( a() + " * " + b() + " + " + c() ); return true;
			case Opcode_t::OPCODE_MIN: assign( "min(" + a() + ", " + b() + ")" ); return true;
			case Opcode_t::OPCODE_MAX: assign( "max(" + a() + ", " + b() + ")" ); return true;
			case Opcode_t::OPCODE_SLT: assign( "vec4(lessThan(" + a() + ", " + b() + "))" ); return true;
			case Opcode_t::OPCODE_SGE: assign( "vec4(greaterThanEqual(" + a() + ", " + b() + "))" ); return true;
			case Opcode_t::OPCODE_LRP: assign( "mix(" + c() + ", " + b() + ", " + a() + ")" ); return true;
			case Opcode_t::OPCODE_CMP: assign( "mix(" + c() + ", " + b() + ", vec4(greaterThanEqual(" + a() + ", vec4(0.0))))" ); return true;
			case Opcode_t::OPCODE_CND: assign( "mix(" + c() + ", " + b() + ", vec4(greaterThan(" + a() + ", vec4(0.5))))" ); return true;
			case Opcode_t::OPCODE_FRC: assign( "fract(" + a() + ")" ); return true;
			case Opcode_t::OPCODE_ABS: assign( "abs(" + a() + ")" ); return true;
			case Opcode_t::OPCODE_SGN: assign( "sign(" + a() + ")" ); return true;
			case Opcode_t::OPCODE_RCP: assign( "vec4(Rcp((" + a() + ").x))" ); return true;
			case Opcode_t::OPCODE_RSQ: assign( "vec4(Rcp(sqrt(abs((" + a() + ").x))))" ); return true;
			case Opcode_t::OPCODE_EXP:
			case Opcode_t::OPCODE_EXPP: assign( "vec4(exp2((" + a() + ").x))" ); return true;
			case Opcode_t::OPCODE_LOG:
			case Opcode_t::OPCODE_LOGP: assign( "vec4(log2(max(abs((" + a() + ").x), 1.0e-38)))" ); return true;
			case Opcode_t::OPCODE_POW: assign( "vec4(pow(abs((" + a() + ").x), (" + b() + ").x))" ); return true;
			case Opcode_t::OPCODE_DP3: assign( "vec4(dot((" + a() + ").xyz, (" + b() + ").xyz))" ); return true;
			case Opcode_t::OPCODE_DP4: assign( "vec4(dot(" + a() + ", " + b() + "))" ); return true;
			case Opcode_t::OPCODE_DP2ADD: assign( "vec4(dot((" + a() + ").xy, (" + b() + ").xy) + (" + c() + ").x)" ); return true;
			case Opcode_t::OPCODE_CRS: assign( "vec4(cross((" + a() + ").xyz, (" + b() + ").xyz), 0.0)" ); return true;
			case Opcode_t::OPCODE_NRM: assign( "(" + a() + " * Rcp(length((" + a() + ").xyz)))" ); return true;
			case Opcode_t::OPCODE_SINCOS: assign( "vec4(cos((" + a() + ").x), sin((" + a() + ").x), 0.0, 0.0)" ); return true;
			case Opcode_t::OPCODE_LIT: assign( "vec4(1.0, max((" + a() + ").x, 0.0), ((" + a() + ").x > 0.0 && (" + a() + ").y > 0.0) ? pow((" + a() + ").y, clamp((" + a() + ").w, -127.9961, 127.9961)) : 0.0, 1.0)" ); return true;
			case Opcode_t::OPCODE_DST: assign( "vec4(1.0, (" + a() + ").y * (" + b() + ").y, (" + a() + ").z, (" + b() + ").w)" ); return true;
			case Opcode_t::OPCODE_M4X4: matrix( 4, 4 ); return true;
			case Opcode_t::OPCODE_M4X3: matrix( 4, 3 ); return true;
			case Opcode_t::OPCODE_M3X4: matrix( 3, 4 ); return true;
			case Opcode_t::OPCODE_M3X3: matrix( 3, 3 ); return true;
			case Opcode_t::OPCODE_M3X2: matrix( 3, 2 ); return true;
			case Opcode_t::OPCODE_DSX: assign( "dFdx(" + a() + ")" ); return true;
			case Opcode_t::OPCODE_DSY: assign( "dFdy(" + a() + ")" ); return true;
			case Opcode_t::OPCODE_TEX:
			case Opcode_t::OPCODE_TEXLDL:
			case Opcode_t::OPCODE_TEXLDD:
			{
				if ( instruction.m_Sources.size() < 2 )
				{
					szError = "a texture read without its sampler";
					return false;
				}
				const std::string szCoord = a();
				const int nSampler = static_cast<int>( instruction.m_Sources[ 1 ].m_nIndex );
				if ( nSampler > 3 )
				{
					szError = std::format( "sampler s{} (screen effects have s0 to s3)", nSampler );
					return false;
				}
				std::string szUv = "(" + szCoord + ").xy";
				int nKind = 0;
				std::string szDx = "vec2(0.0)";
				std::string szDy = "vec2(0.0)";
				if ( instruction.m_nOpcode == Opcode_t::OPCODE_TEXLDL )
					nKind = 1;
				else if ( instruction.m_nOpcode == Opcode_t::OPCODE_TEXLDD )
				{
					nKind = 3;
					szDx = "(" + c() + ").xy";
					szDy = "(" + Src( instruction, 3 ) + ").xy";
				}
				else if ( instruction.m_nControls == 1 )
					szUv = "((" + szCoord + ").xy / (" + szCoord + ").w)";
				else if ( instruction.m_nControls == 2 )
					nKind = 2;
				std::string szRead = std::format( "Sample{}({}, ({}).w, {}, {}, {})", nSampler, szUv, szCoord, nKind, szDx, szDy );
				// The sampler's swizzle picks the components.
				const std::uint8_t nSwizzle = instruction.m_Sources[ 1 ].m_nSwizzle;
				if ( nSwizzle != 0xE4 )
				{
					szRead = "(" + szRead + ").";
					for ( int i = 0; i < 4; i++ )
						szRead += COMPONENTS[ ( nSwizzle >> ( i * 2 ) ) & 3 ];
				}
				assign( szRead );
				return true;
			}
			case Opcode_t::OPCODE_TEXKILL:
			{
				if ( !instruction.m_Dest )
					return true;
				Register_t tested = *instruction.m_Dest;
				tested.m_nSwizzle = 0xE4;
				tested.m_nModifier = SourceModifier_t::SOURCE_MODIFIER_NONE;
				for ( int i = 0; i < 3; i++ )
					if ( instruction.m_Dest->m_nMask & ( 1 << i ) )
						Line( std::format( "if (({}).{} < 0.0) bKilled = true;", Base( tested ), COMPONENTS[ static_cast<std::size_t>( i ) ] ) );
				return true;
			}
			case Opcode_t::OPCODE_SETP:
			{
				if ( !instruction.m_Dest )
					return true;
				std::string szMask;
				for ( int i = 0; i < 4; i++ )
					if ( instruction.m_Dest->m_nMask & ( 1 << i ) )
						szMask += COMPONENTS[ static_cast<std::size_t>( i ) ];
				if ( !szMask.empty() )
					Line( std::format( "p0.{} = ({}).{};", szMask, Compare( instruction.m_nControls, a(), b(), true ), szMask ) );
				return true;
			}
			case Opcode_t::OPCODE_IF:
				Line( "if ((" + a() + ").x != 0.0) {" );
				m_nDepth++;
				return true;
			case Opcode_t::OPCODE_IFC:
				Line( "if " + Compare( instruction.m_nControls, "(" + a() + ").x", "(" + b() + ").x", false ) + " {" );
				m_nDepth++;
				return true;
			case Opcode_t::OPCODE_ELSE:
				m_nDepth = std::max( 0, m_nDepth - 1 );
				Line( "} else {" );
				m_nDepth++;
				return true;
			case Opcode_t::OPCODE_ENDIF:
				m_nDepth = std::max( 0, m_nDepth - 1 );
				Line( "}" );
				return true;
			case Opcode_t::OPCODE_REP:
			{
				const std::uint32_t nCounter = instruction.m_Sources.empty() ? 0 : instruction.m_Sources[ 0 ].m_nIndex & 15;
				Line( std::format( "for (int k{} = 0; k{} < min(i[{}].x, 255); k{}++) {{", m_nLoops, m_nLoops, nCounter, m_nLoops ) );
				m_nLoops++;
				m_nDepth++;
				loops.push_back( -1 );
				return true;
			}
			case Opcode_t::OPCODE_LOOP:
			{
				const std::uint32_t nCounter = instruction.m_Sources.size() > 1 ? instruction.m_Sources[ 1 ].m_nIndex & 15 : 0;
				const int nLoop = m_nLoops++;
				Line( std::format( "int aL{} = aL;", nLoop ) );
				Line( std::format( "aL = i[{}].y;", nCounter ) );
				Line( std::format( "for (int k{} = 0; k{} < min(i[{}].x, 255); k{}++, aL += i[{}].z) {{", nLoop, nLoop, nCounter, nLoop, nCounter ) );
				m_nDepth++;
				loops.push_back( nLoop );
				return true;
			}
			case Opcode_t::OPCODE_ENDREP:
			case Opcode_t::OPCODE_ENDLOOP:
				m_nDepth = std::max( 0, m_nDepth - 1 );
				Line( "}" );
				if ( !loops.empty() )
				{
					if ( loops.back() >= 0 )
						Line( std::format( "aL = aL{};", loops.back() ) );
					loops.pop_back();
				}
				return true;
			case Opcode_t::OPCODE_BREAK: Line( "break;" ); return true;
			case Opcode_t::OPCODE_BREAKC: Line( "if " + Compare( instruction.m_nControls, "(" + a() + ").x", "(" + b() + ").x", false ) + " break;" ); return true;
			case Opcode_t::OPCODE_BREAKP: Line( "if ((" + a() + ").x != 0.0) break;" ); return true;
			case Opcode_t::OPCODE_CALL:
			case Opcode_t::OPCODE_CALLNZ:
			case Opcode_t::OPCODE_LABEL:
			case Opcode_t::OPCODE_RET: szError = "it has subroutines (call, label), which aren't written for the card"; return false;
			default: szError = std::format( "{} isn't written for the card", OpcodeName( instruction.m_nOpcode ) ); return false;
		}
	}

	const Program_t &m_Program;
	std::string *m_pOut = nullptr;
	int m_nDepth = 0;
	int m_nLoops = 0;
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::string> TranslateToGlsl( const Program_t &program, std::string *pError )
{
	const auto fail = [ pError ]( std::string szWhy ) -> std::optional<std::string> {
		if ( pError )
			*pError = std::move( szWhy );
		return std::nullopt;
	};
	if ( !program.m_bPixel )
		return fail( "a vertex shader: only pixel shaders are written for the card" );
	if ( program.m_nMajor < 2 )
		return fail( std::format( "{} isn't written for the card: its texture instructions work otherwise", program.Profile() ) );
	std::string szOut{ PROLOGUE };
	std::string szError;
	CWriter writer( program );
	if ( !writer.Write( szOut, szError ) )
		return fail( szError );
	szOut += EPILOGUE;
	return szOut;
}

} // namespace xv::shader
