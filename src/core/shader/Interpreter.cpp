//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Interpreter.h.
//
//===========================================================================//
#include "Interpreter.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>

namespace xv::shader
{

namespace
{

constexpr std::size_t NO_JUMP = ~std::size_t{ 0 };
constexpr int TEMPS = 32;
// A loop runs at most this many times, whatever its counter says (as the card's do: 255).
constexpr int MOST_ITERATIONS = 255;
// Instructions a pixel may run before it is given up on (a loop that never ends).
constexpr int MOST_STEPS = 1 << 16;

//-----------------------------------------------------------------------------
// Purpose: The state of one pixel's run.
//-----------------------------------------------------------------------------
struct Machine_t
{
	std::array<Vec4_t, TEMPS> m_Temps{};
	Vec4_t m_Out{};
	std::array<bool, 4> m_Predicate{};
	int m_nLoop = 0; // aL
	bool m_bKilled = false;
};

struct Loop_t
{
	std::size_t m_nStart = 0; // the loop's first instruction after its own
	int m_nLeft = 0;
	int m_nStep = 0;
	int m_nSavedLoop = 0;
	bool m_bLoop = false; // loop (with aL) rather than rep
};

//-----------------------------------------------------------------------------
// Purpose: A source read through its swizzle, then its modifier.
//-----------------------------------------------------------------------------
Vec4_t Modified( const Vec4_t &value, const Register_t &reg )
{
	Vec4_t v{};
	for ( int i = 0; i < 4; i++ )
		v[ static_cast<std::size_t>( i ) ] = value[ ( reg.m_nSwizzle >> ( i * 2 ) ) & 3 ];
	for ( float &f : v )
	{
		switch ( reg.m_nModifier )
		{
			case SourceModifier_t::SOURCE_MODIFIER_NEGATE: f = -f; break;
			case SourceModifier_t::SOURCE_MODIFIER_BIAS: f = f - 0.5f; break;
			case SourceModifier_t::SOURCE_MODIFIER_BIAS_NEGATE: f = -( f - 0.5f ); break;
			case SourceModifier_t::SOURCE_MODIFIER_SIGN: f = 2.0f * f - 1.0f; break;
			case SourceModifier_t::SOURCE_MODIFIER_SIGN_NEGATE: f = -( 2.0f * f - 1.0f ); break;
			case SourceModifier_t::SOURCE_MODIFIER_COMPLEMENT: f = 1.0f - f; break;
			case SourceModifier_t::SOURCE_MODIFIER_X2: f = 2.0f * f; break;
			case SourceModifier_t::SOURCE_MODIFIER_X2_NEGATE: f = -2.0f * f; break;
			case SourceModifier_t::SOURCE_MODIFIER_ABS: f = std::abs( f ); break;
			case SourceModifier_t::SOURCE_MODIFIER_ABS_NEGATE: f = -std::abs( f ); break;
			default: break;
		}
	}
	return v;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float Dot( const Vec4_t &a, const Vec4_t &b, int nCount )
{
	float fl = 0.0f;
	for ( int i = 0; i < nCount; i++ )
		fl += a[ static_cast<std::size_t>( i ) ] * b[ static_cast<std::size_t>( i ) ];
	return fl;
}

//-----------------------------------------------------------------------------
// Purpose: A comparison as ifc, breakc and setp name it.
//-----------------------------------------------------------------------------
bool Compare( std::uint8_t nComparison, float a, float b )
{
	switch ( nComparison )
	{
		case 1: return a > b;
		case 2: return a == b;
		case 3: return a >= b;
		case 4: return a < b;
		case 5: return a != b;
		case 6: return a <= b;
		default: return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: A float as the card's: what isn't finite is made so, as its arithmetic would clamp.
//-----------------------------------------------------------------------------
float Finite( float f )
{
	if ( std::isnan( f ) )
		return 0.0f;
	return std::clamp( f, -3.0e38f, 3.0e38f );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CPixelShader::CPixelShader( Program_t program )
	: m_Program( std::move( program ) ),
	  m_Constants( CONSTANTS, Vec4_t{} )
{
	Prepare();
}

//-----------------------------------------------------------------------------
// Purpose: What it can't run; where its flow control goes; its inputs' meanings.
//-----------------------------------------------------------------------------
void CPixelShader::Prepare()
{
	if ( !m_Program.m_bPixel )
	{
		m_szProblem = "a vertex shader: only pixel shaders run";
		return;
	}
	if ( m_Program.m_nMajor < 2 )
	{
		m_szProblem = std::format( "{} isn't run: its texture instructions work otherwise than ps_2_0's and later", m_Program.Profile() );
		return;
	}
	m_InputOf.fill( -1 );
	m_Labels.fill( NO_JUMP );
	const std::vector<Instruction_t> &code = m_Program.m_Instructions;
	m_Jumps.assign( code.size(), NO_JUMP );
	std::vector<std::size_t> open;
	for ( std::size_t i = 0; i < code.size(); i++ )
	{
		const Instruction_t &instruction = code[ i ];
		switch ( instruction.m_nOpcode )
		{
			case Opcode_t::OPCODE_IF:
			case Opcode_t::OPCODE_IFC:
			case Opcode_t::OPCODE_REP:
			case Opcode_t::OPCODE_LOOP: open.push_back( i ); break;
			case Opcode_t::OPCODE_ELSE:
				if ( open.empty() )
				{
					m_szProblem = "an else without its if";
					return;
				}
				m_Jumps[ open.back() ] = i;
				open.back() = i;
				break;
			case Opcode_t::OPCODE_ENDIF:
			case Opcode_t::OPCODE_ENDREP:
			case Opcode_t::OPCODE_ENDLOOP:
				if ( open.empty() )
				{
					m_szProblem = "a block's end without its start";
					return;
				}
				m_Jumps[ open.back() ] = i;
				m_Jumps[ i ] = open.back();
				open.pop_back();
				break;
			case Opcode_t::OPCODE_LABEL:
				if ( !instruction.m_Sources.empty() && instruction.m_Sources[ 0 ].m_nIndex < m_Labels.size() )
					m_Labels[ instruction.m_Sources[ 0 ].m_nIndex ] = i;
				break;
			case Opcode_t::OPCODE_DCL:
				if ( instruction.m_Dest && instruction.m_Dest->m_nType == RegisterType_t::REGISTER_TYPE_INPUT && instruction.m_Dest->m_nIndex < m_InputOf.size() )
				{
					// Texcoords by their index; colours after them.
					constexpr std::uint32_t USAGE_COLOR = 10;
					constexpr std::uint32_t USAGE_TEXCOORD = 5;
					if ( instruction.m_nUsage == USAGE_TEXCOORD )
						m_InputOf[ instruction.m_Dest->m_nIndex ] = static_cast<int>( instruction.m_nUsageIndex );
					else if ( instruction.m_nUsage == USAGE_COLOR )
						m_InputOf[ instruction.m_Dest->m_nIndex ] = 8 + static_cast<int>( instruction.m_nUsageIndex );
				}
				break;
			default: break;
		}
	}
	if ( !open.empty() )
		m_szProblem = "a block that doesn't end";
	// def, defi and defb take their registers' places over what the material sets, as the card's do.
	m_Defined.assign( CONSTANTS, false );
	for ( const Instruction_t &instruction : code )
	{
		if ( !instruction.m_Dest )
			continue;
		const std::uint32_t n = instruction.m_Dest->m_nIndex;
		if ( instruction.m_nOpcode == Opcode_t::OPCODE_DEF && n < m_Constants.size() )
		{
			for ( std::size_t i = 0; i < 4; i++ )
				m_Constants[ n ][ i ] = std::bit_cast<float>( instruction.m_Values[ i ] );
			m_Defined[ n ] = true;
		}
		else if ( instruction.m_nOpcode == Opcode_t::OPCODE_DEFI && n < m_Integers.size() )
		{
			for ( std::size_t i = 0; i < 4; i++ )
				m_Integers[ n ][ i ] = static_cast<std::int32_t>( instruction.m_Values[ i ] );
			m_IntegerDefined[ n ] = true;
		}
		else if ( instruction.m_nOpcode == Opcode_t::OPCODE_DEFB && n < m_Bools.size() )
		{
			m_Bools[ n ] = instruction.m_Values[ 0 ] != 0;
			m_BoolDefined[ n ] = true;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPixelShader::SetConstant( int nRegister, const Vec4_t &value )
{
	if ( nRegister >= 0 && nRegister < CONSTANTS && !( static_cast<std::size_t>( nRegister ) < m_Defined.size() && m_Defined[ static_cast<std::size_t>( nRegister ) ] ) )
		m_Constants[ static_cast<std::size_t>( nRegister ) ] = value;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPixelShader::SetInteger( int nRegister, const std::array<int, 4> &value )
{
	if ( nRegister >= 0 && nRegister < static_cast<int>( m_Integers.size() ) && !m_IntegerDefined[ static_cast<std::size_t>( nRegister ) ] )
		m_Integers[ static_cast<std::size_t>( nRegister ) ] = value;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CPixelShader::SetBool( int nRegister, bool bValue )
{
	if ( nRegister >= 0 && nRegister < static_cast<int>( m_Bools.size() ) && !m_BoolDefined[ static_cast<std::size_t>( nRegister ) ] )
		m_Bools[ static_cast<std::size_t>( nRegister ) ] = bValue;
}

//-----------------------------------------------------------------------------
// Purpose: The instructions in order, flow control followed.
//-----------------------------------------------------------------------------
std::optional<Vec4_t> CPixelShader::Run( const PixelInputs_t &inputs, const Sample_t &pfnSample ) const
{
	if ( !CanRun() )
		return std::nullopt;
	const std::vector<Instruction_t> &code = m_Program.m_Instructions;
	Machine_t machine;
	const std::vector<Vec4_t> &constants = m_Constants;
	const std::array<std::array<int, 4>, 16> &integers = m_Integers;
	const std::array<bool, 16> &bools = m_Bools;
	const bool bShader3 = m_Program.m_nMajor >= 3;
	const auto read = [ & ]( const Register_t &reg ) -> Vec4_t {
		Vec4_t value{};
		std::uint32_t nIndex = reg.m_nIndex;
		if ( reg.m_Relative )
			nIndex = static_cast<std::uint32_t>( static_cast<int>( nIndex ) + machine.m_nLoop );
		switch ( reg.m_nType )
		{
			case RegisterType_t::REGISTER_TYPE_TEMP: value = nIndex < TEMPS ? machine.m_Temps[ nIndex ] : Vec4_t{}; break;
			case RegisterType_t::REGISTER_TYPE_CONST: value = nIndex < constants.size() ? constants[ nIndex ] : Vec4_t{}; break;
			case RegisterType_t::REGISTER_TYPE_TEXTURE: value = nIndex < inputs.m_Texcoords.size() ? inputs.m_Texcoords[ nIndex ] : Vec4_t{}; break;
			case RegisterType_t::REGISTER_TYPE_INPUT:
				if ( bShader3 )
				{
					const int nInput = nIndex < m_InputOf.size() ? m_InputOf[ nIndex ] : -1;
					value = nInput >= 8 ? inputs.m_Colors[ static_cast<std::size_t>( std::min( nInput - 8, 1 ) ) ] : nInput >= 0 ? inputs.m_Texcoords[ static_cast<std::size_t>( nInput ) ]
																																 : Vec4_t{};
				}
				else
				{
					value = nIndex < inputs.m_Colors.size() ? inputs.m_Colors[ nIndex ] : Vec4_t{};
				}
				break;
			case RegisterType_t::REGISTER_TYPE_CONSTINT:
				if ( nIndex < integers.size() )
					for ( std::size_t i = 0; i < 4; i++ )
						value[ i ] = static_cast<float>( integers[ nIndex ][ i ] );
				break;
			case RegisterType_t::REGISTER_TYPE_CONSTBOOL: value.fill( nIndex < bools.size() && bools[ nIndex ] ? 1.0f : 0.0f ); break;
			case RegisterType_t::REGISTER_TYPE_MISCTYPE: value = nIndex == 0 ? inputs.m_Position : Vec4_t{ 1, 1, 1, 1 }; break; // vFace: the front
			case RegisterType_t::REGISTER_TYPE_LOOP: value.fill( static_cast<float>( machine.m_nLoop ) ); break;
			case RegisterType_t::REGISTER_TYPE_PREDICATE:
				for ( std::size_t i = 0; i < 4; i++ )
					value[ i ] = machine.m_Predicate[ i ] ? 1.0f : 0.0f;
				break;
			case RegisterType_t::REGISTER_TYPE_COLOROUT: value = machine.m_Out; break;
			default: break;
		}
		return Modified( value, reg );
	};
	const auto write = [ & ]( const Instruction_t &instruction, const Vec4_t &result ) {
		const Register_t &dest = *instruction.m_Dest;
		Vec4_t *pTarget = nullptr;
		if ( dest.m_nType == RegisterType_t::REGISTER_TYPE_TEMP && dest.m_nIndex < TEMPS )
			pTarget = &machine.m_Temps[ dest.m_nIndex ];
		else if ( dest.m_nType == RegisterType_t::REGISTER_TYPE_COLOROUT && dest.m_nIndex == 0 )
			pTarget = &machine.m_Out;
		else if ( dest.m_nType == RegisterType_t::REGISTER_TYPE_PREDICATE )
		{
			for ( std::size_t i = 0; i < 4; i++ )
				if ( dest.m_nMask & ( 1 << i ) )
					machine.m_Predicate[ i ] = result[ i ] != 0.0f;
			return;
		}
		if ( !pTarget )
			return; // other render targets and oDepth aren't kept
		const float flScale = dest.m_nShift ? std::ldexp( 1.0f, dest.m_nShift ) : 1.0f;
		for ( std::size_t i = 0; i < 4; i++ )
		{
			if ( ( dest.m_nMask & ( 1 << i ) ) == 0 )
				continue;
			if ( instruction.m_bPredicated )
			{
				const std::size_t nComponent = ( instruction.m_Predicate.m_nSwizzle >> ( i * 2 ) ) & 3;
				const bool bOn = machine.m_Predicate[ nComponent ] != ( instruction.m_Predicate.m_nModifier == SourceModifier_t::SOURCE_MODIFIER_NOT );
				if ( !bOn )
					continue;
			}
			float f = Finite( result[ i ] * flScale );
			if ( dest.m_bSaturate )
				f = std::clamp( f, 0.0f, 1.0f );
			( *pTarget )[ i ] = f;
		}
	};
	const auto source = [ & ]( const Instruction_t &instruction, std::size_t n ) { return n < instruction.m_Sources.size() ? read( instruction.m_Sources[ n ] ) : Vec4_t{}; };
	const auto splat = []( float f ) { return Vec4_t{ f, f, f, f }; };
	std::vector<Loop_t> loops;
	std::vector<std::size_t> calls;
	int nSteps = 0;
	for ( std::size_t pc = 0; pc < code.size(); pc++ )
	{
		if ( ++nSteps > MOST_STEPS )
			break;
		const Instruction_t &instruction = code[ pc ];
		const auto componentwise = [ & ]( auto pfnOp ) {
			const Vec4_t a = source( instruction, 0 );
			const Vec4_t b = source( instruction, 1 );
			const Vec4_t c = source( instruction, 2 );
			Vec4_t result{};
			for ( std::size_t i = 0; i < 4; i++ )
				result[ i ] = pfnOp( a[ i ], b[ i ], c[ i ] );
			write( instruction, result );
		};
		const auto matrix = [ & ]( int nRows, int nColumns ) {
			const Vec4_t v = source( instruction, 0 );
			Vec4_t result{};
			Register_t row = instruction.m_Sources.size() > 1 ? instruction.m_Sources[ 1 ] : Register_t{};
			for ( int r = 0; r < nColumns; r++ )
			{
				result[ static_cast<std::size_t>( r ) ] = Dot( v, read( row ), nRows );
				row.m_nIndex++;
			}
			write( instruction, result );
		};
		switch ( instruction.m_nOpcode )
		{
			case Opcode_t::OPCODE_MOV:
			case Opcode_t::OPCODE_MOVA: write( instruction, source( instruction, 0 ) ); break;
			case Opcode_t::OPCODE_ADD: componentwise( []( float a, float b, float ) { return a + b; } ); break;
			case Opcode_t::OPCODE_SUB: componentwise( []( float a, float b, float ) { return a - b; } ); break;
			case Opcode_t::OPCODE_MUL: componentwise( []( float a, float b, float ) { return a * b; } ); break;
			case Opcode_t::OPCODE_MAD: componentwise( []( float a, float b, float c ) { return a * b + c; } ); break;
			case Opcode_t::OPCODE_MIN: componentwise( []( float a, float b, float ) { return std::min( a, b ); } ); break;
			case Opcode_t::OPCODE_MAX: componentwise( []( float a, float b, float ) { return std::max( a, b ); } ); break;
			case Opcode_t::OPCODE_SLT: componentwise( []( float a, float b, float ) { return a < b ? 1.0f : 0.0f; } ); break;
			case Opcode_t::OPCODE_SGE: componentwise( []( float a, float b, float ) { return a >= b ? 1.0f : 0.0f; } ); break;
			case Opcode_t::OPCODE_LRP: componentwise( []( float a, float b, float c ) { return a * b + ( 1.0f - a ) * c; } ); break;
			case Opcode_t::OPCODE_CMP: componentwise( []( float a, float b, float c ) { return a >= 0.0f ? b : c; } ); break;
			case Opcode_t::OPCODE_CND: componentwise( []( float a, float b, float c ) { return a > 0.5f ? b : c; } ); break;
			case Opcode_t::OPCODE_FRC: componentwise( []( float a, float, float ) { return a - std::floor( a ); } ); break;
			case Opcode_t::OPCODE_ABS: componentwise( []( float a, float, float ) { return std::abs( a ); } ); break;
			case Opcode_t::OPCODE_SGN: componentwise( []( float a, float, float ) { return a > 0.0f ? 1.0f : a < 0.0f ? -1.0f
																													  : 0.0f; } ); break;
			case Opcode_t::OPCODE_RCP:
			{
				const float f = source( instruction, 0 )[ 0 ];
				write( instruction, splat( f == 0.0f ? 3.0e38f : 1.0f / f ) );
				break;
			}
			case Opcode_t::OPCODE_RSQ:
			{
				const float f = std::abs( source( instruction, 0 )[ 0 ] );
				write( instruction, splat( f == 0.0f ? 3.0e38f : 1.0f / std::sqrt( f ) ) );
				break;
			}
			case Opcode_t::OPCODE_EXP:
			case Opcode_t::OPCODE_EXPP: write( instruction, splat( std::exp2( source( instruction, 0 )[ 0 ] ) ) ); break;
			case Opcode_t::OPCODE_LOG:
			case Opcode_t::OPCODE_LOGP:
			{
				const float f = std::abs( source( instruction, 0 )[ 0 ] );
				write( instruction, splat( f == 0.0f ? -3.0e38f : std::log2( f ) ) );
				break;
			}
			case Opcode_t::OPCODE_POW: write( instruction, splat( std::pow( std::abs( source( instruction, 0 )[ 0 ] ), source( instruction, 1 )[ 0 ] ) ) ); break;
			case Opcode_t::OPCODE_DP3: write( instruction, splat( Dot( source( instruction, 0 ), source( instruction, 1 ), 3 ) ) ); break;
			case Opcode_t::OPCODE_DP4: write( instruction, splat( Dot( source( instruction, 0 ), source( instruction, 1 ), 4 ) ) ); break;
			case Opcode_t::OPCODE_DP2ADD:
			{
				const Vec4_t a = source( instruction, 0 );
				const Vec4_t b = source( instruction, 1 );
				write( instruction, splat( a[ 0 ] * b[ 0 ] + a[ 1 ] * b[ 1 ] + source( instruction, 2 )[ 0 ] ) );
				break;
			}
			case Opcode_t::OPCODE_CRS:
			{
				const Vec4_t a = source( instruction, 0 );
				const Vec4_t b = source( instruction, 1 );
				write( instruction, { a[ 1 ] * b[ 2 ] - a[ 2 ] * b[ 1 ], a[ 2 ] * b[ 0 ] - a[ 0 ] * b[ 2 ], a[ 0 ] * b[ 1 ] - a[ 1 ] * b[ 0 ], 0.0f } );
				break;
			}
			case Opcode_t::OPCODE_NRM:
			{
				const Vec4_t a = source( instruction, 0 );
				const float flLength = std::sqrt( Dot( a, a, 3 ) );
				const float flScale = flLength > 0.0f ? 1.0f / flLength : 3.0e38f;
				write( instruction, { a[ 0 ] * flScale, a[ 1 ] * flScale, a[ 2 ] * flScale, a[ 3 ] * flScale } );
				break;
			}
			case Opcode_t::OPCODE_SINCOS:
			{
				// Its source names one component (replicated): the first after the swizzle.
				const float f = source( instruction, 0 )[ 0 ];
				write( instruction, { std::cos( f ), std::sin( f ), 0.0f, 0.0f } );
				break;
			}
			case Opcode_t::OPCODE_LIT:
			{
				const Vec4_t a = source( instruction, 0 );
				const float flPower = std::clamp( a[ 3 ], -127.9961f, 127.9961f );
				write( instruction, { 1.0f, std::max( a[ 0 ], 0.0f ), a[ 0 ] > 0.0f && a[ 1 ] > 0.0f ? std::pow( a[ 1 ], flPower ) : 0.0f, 1.0f } );
				break;
			}
			case Opcode_t::OPCODE_DST:
			{
				const Vec4_t a = source( instruction, 0 );
				const Vec4_t b = source( instruction, 1 );
				write( instruction, { 1.0f, a[ 1 ] * b[ 1 ], a[ 2 ], b[ 3 ] } );
				break;
			}
			case Opcode_t::OPCODE_M4X4: matrix( 4, 4 ); break;
			case Opcode_t::OPCODE_M4X3: matrix( 4, 3 ); break;
			case Opcode_t::OPCODE_M3X4: matrix( 3, 4 ); break;
			case Opcode_t::OPCODE_M3X3: matrix( 3, 3 ); break;
			case Opcode_t::OPCODE_M3X2: matrix( 3, 2 ); break;
			case Opcode_t::OPCODE_DSX:
			case Opcode_t::OPCODE_DSY: write( instruction, Vec4_t{} ); break;
			case Opcode_t::OPCODE_TEX:
			case Opcode_t::OPCODE_TEXLDL:
			case Opcode_t::OPCODE_TEXLDD:
			{
				Vec4_t coord = source( instruction, 0 );
				const int nSampler = instruction.m_Sources.size() > 1 ? static_cast<int>( instruction.m_Sources[ 1 ].m_nIndex ) : 0;
				float flLod = 0.0f;
				if ( instruction.m_nOpcode == Opcode_t::OPCODE_TEXLDL || ( instruction.m_nOpcode == Opcode_t::OPCODE_TEX && instruction.m_nControls == 2 ) )
					flLod = coord[ 3 ];
				else if ( instruction.m_nOpcode == Opcode_t::OPCODE_TEX && instruction.m_nControls == 1 && coord[ 3 ] != 0.0f )
					for ( std::size_t i = 0; i < 3; i++ )
						coord[ i ] /= coord[ 3 ];
				Vec4_t sampled = pfnSample ? pfnSample( nSampler, coord, flLod ) : Vec4_t{};
				// The sampler's swizzle (ps_2_x and up) picks the components.
				if ( instruction.m_Sources.size() > 1 )
				{
					Register_t sampler = instruction.m_Sources[ 1 ];
					sampler.m_nModifier = SourceModifier_t::SOURCE_MODIFIER_NONE;
					sampled = Modified( sampled, sampler );
				}
				write( instruction, sampled );
				break;
			}
			case Opcode_t::OPCODE_TEXKILL:
			{
				// The destination token names what is tested: any of its masked components below 0.
				if ( instruction.m_Dest )
				{
					Register_t tested = *instruction.m_Dest;
					tested.m_nSwizzle = 0xE4;
					const Vec4_t value = read( tested );
					for ( std::size_t i = 0; i < 3; i++ )
						if ( ( instruction.m_Dest->m_nMask & ( 1 << i ) ) && value[ i ] < 0.0f )
							machine.m_bKilled = true;
				}
				break;
			}
			case Opcode_t::OPCODE_SETP:
			{
				const Vec4_t a = source( instruction, 0 );
				const Vec4_t b = source( instruction, 1 );
				Vec4_t result{};
				for ( std::size_t i = 0; i < 4; i++ )
					result[ i ] = Compare( instruction.m_nControls, a[ i ], b[ i ] ) ? 1.0f : 0.0f;
				write( instruction, result );
				break;
			}
			case Opcode_t::OPCODE_IF:
			case Opcode_t::OPCODE_IFC:
			{
				const bool bTaken = instruction.m_nOpcode == Opcode_t::OPCODE_IF ? source( instruction, 0 )[ 0 ] != 0.0f : Compare( instruction.m_nControls, source( instruction, 0 )[ 0 ], source( instruction, 1 )[ 0 ] );
				if ( !bTaken && m_Jumps[ pc ] != NO_JUMP )
					pc = m_Jumps[ pc ]; // to else (whose next runs) or endif
				break;
			}
			case Opcode_t::OPCODE_ELSE:
				// Reached from the if's own part: past the else's to its endif.
				if ( m_Jumps[ pc ] != NO_JUMP )
					pc = m_Jumps[ pc ];
				break;
			case Opcode_t::OPCODE_REP:
			case Opcode_t::OPCODE_LOOP:
			{
				const bool bLoop = instruction.m_nOpcode == Opcode_t::OPCODE_LOOP;
				// rep i#: i.x times; loop aL, i#: i.x times, aL from i.y by i.z.
				const Register_t &counter = instruction.m_Sources.empty() ? Register_t{} : instruction.m_Sources[ bLoop && instruction.m_Sources.size() > 1 ? 1 : 0 ];
				const std::uint32_t nIndex = counter.m_nIndex;
				const std::array<int, 4> value = nIndex < integers.size() ? integers[ nIndex ] : std::array<int, 4>{};
				const int nCount = std::clamp( value[ 0 ], 0, MOST_ITERATIONS );
				if ( nCount == 0 )
				{
					pc = m_Jumps[ pc ];
					break;
				}
				loops.push_back( { pc + 1, nCount, value[ 2 ], machine.m_nLoop, bLoop } );
				if ( bLoop )
					machine.m_nLoop = value[ 1 ];
				break;
			}
			case Opcode_t::OPCODE_ENDREP:
			case Opcode_t::OPCODE_ENDLOOP:
			{
				if ( loops.empty() )
					break;
				Loop_t &loop = loops.back();
				if ( --loop.m_nLeft > 0 )
				{
					if ( loop.m_bLoop )
						machine.m_nLoop += loop.m_nStep;
					pc = loop.m_nStart - 1;
				}
				else
				{
					machine.m_nLoop = loop.m_nSavedLoop;
					loops.pop_back();
				}
				break;
			}
			case Opcode_t::OPCODE_BREAK:
			case Opcode_t::OPCODE_BREAKC:
			case Opcode_t::OPCODE_BREAKP:
			{
				bool bBreak = true;
				if ( instruction.m_nOpcode == Opcode_t::OPCODE_BREAKC )
					bBreak = Compare( instruction.m_nControls, source( instruction, 0 )[ 0 ], source( instruction, 1 )[ 0 ] );
				else if ( instruction.m_nOpcode == Opcode_t::OPCODE_BREAKP )
					bBreak = source( instruction, 0 )[ 0 ] != 0.0f;
				if ( bBreak && !loops.empty() )
				{
					// To the end of the loop it is in: the end's own jump goes to its start.
					std::size_t nEnd = loops.back().m_nStart - 1;
					nEnd = m_Jumps[ nEnd ];
					machine.m_nLoop = loops.back().m_nSavedLoop;
					loops.pop_back();
					pc = nEnd;
				}
				break;
			}
			case Opcode_t::OPCODE_CALL:
			case Opcode_t::OPCODE_CALLNZ:
			{
				const bool bCall = instruction.m_nOpcode == Opcode_t::OPCODE_CALL || ( instruction.m_Sources.size() > 1 && read( instruction.m_Sources[ 1 ] )[ 0 ] != 0.0f );
				const std::uint32_t nLabel = instruction.m_Sources.empty() ? 0 : instruction.m_Sources[ 0 ].m_nIndex;
				if ( bCall && nLabel < m_Labels.size() && m_Labels[ nLabel ] != NO_JUMP && calls.size() < 16 )
				{
					calls.push_back( pc );
					pc = m_Labels[ nLabel ];
				}
				break;
			}
			case Opcode_t::OPCODE_RET:
				if ( calls.empty() )
					pc = code.size(); // the main program's end
				else
				{
					pc = calls.back();
					calls.pop_back();
				}
				break;
			case Opcode_t::OPCODE_LABEL:
				// The main program ends where the subroutines start.
				if ( calls.empty() )
					pc = code.size();
				break;
			default: break; // dcl, def, nop and the like do nothing as it runs
		}
	}
	if ( machine.m_bKilled )
		return std::nullopt;
	return machine.m_Out;
}

} // namespace xv::shader
