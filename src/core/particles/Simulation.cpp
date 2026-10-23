//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Simulation.h.
//
//===========================================================================//
#include "Simulation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <numbers>
#include <set>

namespace xv::particles
{

using dmx::Value_t;

namespace
{

constexpr float DEGREES = std::numbers::pi_v<float> / 180.0f;
constexpr int MAX_CHILD_DEPTH = 8; // child systems that spawn each other stop here

// The particle fields operators read and write by number ("output field" and so on), as Valve's
// particle editor lists them.
enum Field_t
{
	FIELD_POSITION = 0,
	FIELD_LIFETIME = 1,
	FIELD_RADIUS = 3,
	FIELD_ROTATION = 4,
	FIELD_ROTATION_SPEED = 5,
	FIELD_COLOR = 6,
	FIELD_ALPHA = 7,
	FIELD_CREATION_TIME = 8,
	FIELD_SEQUENCE = 9,
	FIELD_TRAIL_LENGTH = 10,
	FIELD_YAW = 12,
	FIELD_SEQUENCE2 = 13,
	FIELD_ALPHA2 = 16,
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::uint64_t Mix( std::uint64_t x )
{
	x += 0x9e3779b97f4a7c15ULL;
	x = ( x ^ ( x >> 30 ) ) * 0xbf58476d1ce4e5b9ULL;
	x = ( x ^ ( x >> 27 ) ) * 0x94d049bb133111ebULL;
	return x ^ ( x >> 31 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float Unit( std::uint64_t nBits )
{
	return static_cast<float>( nBits >> 40 ) / static_cast<float>( 1ULL << 24 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float Lerp( float a, float b, float t )
{
	return a + ( b - a ) * t;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 Lerp( CVec3 a, CVec3 b, CVec3 t )
{
	return { Lerp( a.m_flX, b.m_flX, t.m_flX ), Lerp( a.m_flY, b.m_flY, t.m_flY ), Lerp( a.m_flZ, b.m_flZ, t.m_flZ ) };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float Clamp01( float t )
{
	return std::clamp( t, 0.0f, 1.0f );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float Smooth( float t )
{
	return t * t * ( 3.0f - 2.0f * t );
}

//-----------------------------------------------------------------------------
// Purpose: Schlick's bias: 0.5 is a straight line, lower values hold low longer.
//-----------------------------------------------------------------------------
float Bias( float t, float flAmount )
{
	flAmount = std::clamp( flAmount, 0.001f, 0.999f );
	return t / ( ( 1.0f / flAmount - 2.0f ) * ( 1.0f - t ) + 1.0f );
}

//-----------------------------------------------------------------------------
// Purpose: A value between min and max, skewed towards min by exponents above 1.
//-----------------------------------------------------------------------------
float RandomRange( float flMin, float flMax, float r, float flExponent = 1.0f )
{
	return Lerp( flMin, flMax, flExponent == 1.0f ? r : std::pow( r, flExponent ) );
}

//-----------------------------------------------------------------------------
// Purpose: Fields reset to their initial value every step, so fades and scales apply to where they
//          started.
//-----------------------------------------------------------------------------
bool ResetsEachStep( int nField )
{
	return nField == FIELD_RADIUS || nField == FIELD_ALPHA || nField == FIELD_COLOR;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float GetField( const Particle_t &p, int nField )
{
	switch ( nField )
	{
		case FIELD_LIFETIME: return p.m_flLifetime;
		case FIELD_RADIUS: return p.m_flRadius;
		case FIELD_ROTATION: return p.m_flRotation;
		case FIELD_ROTATION_SPEED: return p.m_flRotationSpeed;
		case FIELD_ALPHA: return p.m_flAlpha;
		case FIELD_CREATION_TIME: return p.m_flCreationTime;
		case FIELD_SEQUENCE: return static_cast<float>( p.m_nSequence );
		case FIELD_TRAIL_LENGTH: return p.m_flTrailLength;
		case FIELD_YAW: return p.m_flYaw;
		case FIELD_SEQUENCE2: return static_cast<float>( p.m_nSequence2 );
		case FIELD_ALPHA2: return p.m_flAlpha2;
		default: return 0.0f;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Sets a scalar field; at creation the starting value is set too.
//-----------------------------------------------------------------------------
void SetField( Particle_t &p, int nField, float flValue, bool bInitial )
{
	switch ( nField )
	{
		case FIELD_LIFETIME: p.m_flLifetime = flValue; break;
		case FIELD_RADIUS:
			p.m_flRadius = flValue;
			if ( bInitial )
			{
				p.m_flInitialRadius = flValue;
			}
			break;
		case FIELD_ROTATION: p.m_flRotation = flValue; break;
		case FIELD_ROTATION_SPEED: p.m_flRotationSpeed = flValue; break;
		case FIELD_ALPHA:
			p.m_flAlpha = flValue;
			if ( bInitial )
			{
				p.m_flInitialAlpha = flValue;
			}
			break;
		case FIELD_CREATION_TIME: p.m_flCreationTime = flValue; break;
		case FIELD_SEQUENCE: p.m_nSequence = static_cast<int>( flValue ); break;
		case FIELD_TRAIL_LENGTH: p.m_flTrailLength = flValue; break;
		case FIELD_YAW: p.m_flYaw = flValue; break;
		case FIELD_SEQUENCE2: p.m_nSequence2 = static_cast<int>( flValue ); break;
		case FIELD_ALPHA2: p.m_flAlpha2 = flValue; break;
		default: break;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 GetVectorField( const Particle_t &p, int nField )
{
	if ( nField == FIELD_COLOR )
	{
		return { p.m_vecColor[ 0 ], p.m_vecColor[ 1 ], p.m_vecColor[ 2 ] };
	}
	return p.m_vecPosition;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void SetVectorField( Particle_t &p, int nField, CVec3 vecValue, bool bInitial )
{
	if ( nField == FIELD_COLOR )
	{
		p.m_vecColor = { vecValue.m_flX, vecValue.m_flY, vecValue.m_flZ };
		if ( bInitial )
		{
			p.m_vecInitialColor = p.m_vecColor;
		}
	}
	else if ( nField == FIELD_POSITION )
	{
		p.m_vecPosition = vecValue;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::array<float, 4> ColorOf( const dmx::CColor &c )
{
	return { c.m_nR / 255.0f, c.m_nG / 255.0f, c.m_nB / 255.0f, c.m_nA / 255.0f };
}

//-----------------------------------------------------------------------------
// Purpose: The functions the preview runs, by category.
//-----------------------------------------------------------------------------
const std::set<std::pair<Category_t, std::string_view>> &SimulatedFunctions()
{
	static const std::set<std::pair<Category_t, std::string_view>> functions{
		{ Category_t::CATEGORY_EMITTERS, "emit_continuously" },
		{ Category_t::CATEGORY_EMITTERS, "emit_instantaneously" },
		{ Category_t::CATEGORY_INITIALIZERS, "Position Within Sphere Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Position Within Box Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Position Modify Offset Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Lifetime Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Lifetime Pre-Age Noise" },
		{ Category_t::CATEGORY_INITIALIZERS, "lifetime from sequence" },
		{ Category_t::CATEGORY_INITIALIZERS, "Radius Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Color Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Alpha Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Rotation Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Rotation Yaw Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Rotation Yaw Flip Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Sequence Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Sequence Two Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Trail Length Random" },
		{ Category_t::CATEGORY_INITIALIZERS, "Velocity Noise" },
		{ Category_t::CATEGORY_INITIALIZERS, "Velocity Inherit from Control Point" },
		{ Category_t::CATEGORY_INITIALIZERS, "Remap Noise to Scalar" },
		{ Category_t::CATEGORY_INITIALIZERS, "remap initial scalar" },
		{ Category_t::CATEGORY_INITIALIZERS, "random_number" },
		{ Category_t::CATEGORY_OPERATORS, "Lifespan Decay" },
		{ Category_t::CATEGORY_OPERATORS, "Movement Basic" },
		{ Category_t::CATEGORY_OPERATORS, "Alpha Fade In Random" },
		{ Category_t::CATEGORY_OPERATORS, "Alpha Fade Out Random" },
		{ Category_t::CATEGORY_OPERATORS, "Alpha Fade and Decay" },
		{ Category_t::CATEGORY_OPERATORS, "Color Fade" },
		{ Category_t::CATEGORY_OPERATORS, "Radius Scale" },
		{ Category_t::CATEGORY_OPERATORS, "Rotation Spin Roll" },
		{ Category_t::CATEGORY_OPERATORS, "Rotation Spin Yaw" },
		{ Category_t::CATEGORY_OPERATORS, "Oscillate Scalar" },
		{ Category_t::CATEGORY_OPERATORS, "Oscillate Vector" },
		{ Category_t::CATEGORY_OPERATORS, "Movement Lock to Control Point" },
		{ Category_t::CATEGORY_OPERATORS, "Movement Dampen Relative to Control Point" },
		{ Category_t::CATEGORY_OPERATORS, "Cull Random" },
		{ Category_t::CATEGORY_OPERATORS, "Cull when crossing plane" },
		{ Category_t::CATEGORY_OPERATORS, "Noise Vector" },
		{ Category_t::CATEGORY_OPERATORS, "Remap Distance to Control Point to Scalar" },
		{ Category_t::CATEGORY_OPERATORS, "Set Control Point Positions" },
		{ Category_t::CATEGORY_RENDERERS, "render_animated_sprites" },
		{ Category_t::CATEGORY_RENDERERS, "render_sprite_trail" },
		{ Category_t::CATEGORY_RENDERERS, "render_rope" },
		{ Category_t::CATEGORY_FORCES, "random force" },
		{ Category_t::CATEGORY_FORCES, "twist around axis" },
		{ Category_t::CATEGORY_FORCES, "Pull towards control point" },
		{ Category_t::CATEGORY_CONSTRAINTS, "Constrain distance to control point" },
		{ Category_t::CATEGORY_CONSTRAINTS, "Prevent passing through a plane" },
	};
	return functions;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Compiling

//-----------------------------------------------------------------------------
// Purpose: ---------------------------------------------------------------------------------------
//          ------
//          Compiling
//-----------------------------------------------------------------------------
float CCompiledOperator::Number( std::string_view szName, float flFallback ) const
{
	const auto it = m_Values.find( szName );
	if ( it == m_Values.end() )
	{
		return flFallback;
	}
	if ( const auto *f = std::get_if<float>( &it->second ) )
	{
		return *f;
	}
	if ( const auto *i = std::get_if<std::int32_t>( &it->second ) )
	{
		return static_cast<float>( *i );
	}
	if ( const auto *b = std::get_if<bool>( &it->second ) )
	{
		return *b ? 1.0f : 0.0f;
	}
	return flFallback;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CCompiledOperator::Integer( std::string_view szName, int nFallback ) const
{
	return static_cast<int>( std::lround( Number( szName, static_cast<float>( nFallback ) ) ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CCompiledOperator::Flag( std::string_view szName, bool bFallback ) const
{
	return Number( szName, bFallback ? 1.0f : 0.0f ) != 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 CCompiledOperator::Vector( std::string_view szName, CVec3 vecFallback ) const
{
	const auto it = m_Values.find( szName );
	const auto *v = it == m_Values.end() ? nullptr : std::get_if<dmx::Vec3_t>( &it->second );
	return v ? CVec3{ ( *v )[ 0 ], ( *v )[ 1 ], ( *v )[ 2 ] } : vecFallback;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::array<float, 4> CCompiledOperator::Color( std::string_view szName, std::array<float, 4> fallback ) const
{
	const auto it = m_Values.find( szName );
	const auto *c = it == m_Values.end() ? nullptr : std::get_if<dmx::CColor>( &it->second );
	return c ? ColorOf( *c ) : fallback;
}

//-----------------------------------------------------------------------------
// Purpose: How strongly it applies at a system time: 0 outside its window.
//-----------------------------------------------------------------------------
float CCompiledOperator::Weight( float flTime ) const
{
	if ( m_flStartFadeIn == 0.0f && m_flEndFadeIn == 0.0f && m_flStartFadeOut == 0.0f && m_flEndFadeOut == 0.0f )
	{
		return 1.0f;
	}
	float flIn = 1.0f;
	if ( m_flEndFadeIn > 0.0f || m_flStartFadeIn > 0.0f )
	{
		flIn = m_flEndFadeIn > m_flStartFadeIn ? Clamp01( ( flTime - m_flStartFadeIn ) / ( m_flEndFadeIn - m_flStartFadeIn ) ) : ( flTime >= m_flStartFadeIn ? 1.0f : 0.0f );
	}
	float flOut = 1.0f;
	if ( m_flEndFadeOut > 0.0f || m_flStartFadeOut > 0.0f )
	{
		flOut = m_flEndFadeOut > m_flStartFadeOut ? 1.0f - Clamp01( ( flTime - m_flStartFadeOut ) / ( m_flEndFadeOut - m_flStartFadeOut ) ) : ( flTime < m_flStartFadeOut ? 1.0f : 0.0f );
	}
	return flIn * flOut;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<const CCompiledOperator *> CDefinition::Of( Category_t nCategory ) const
{
	std::vector<const CCompiledOperator *> out;
	for ( const CCompiledOperator &op : operators )
	{
		if ( op.m_nCategory == nCategory )
		{
			out.push_back( &op );
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: The control points its operators use (settings named "control point ..."), plus 0.
//-----------------------------------------------------------------------------
std::vector<int> CDefinition::GetControlPoints() const
{
	std::set<int> used{ 0 };
	for ( const CCompiledOperator &op : operators )
	{
		for ( const auto &[ name, value ] : op.m_Values )
		{
			std::string szLowerName = name;
			std::ranges::transform( szLowerName, szLowerName.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
			const bool bControlPoint = szLowerName.find( "control point" ) != std::string::npos || szLowerName.find( "control_point" ) != std::string::npos;
			const auto *pNumber = std::get_if<std::int32_t>( &value );
			// "... Number", "control_point_number", "control point": an index; the rest are settings about one.
			if ( bControlPoint && pNumber && *pNumber >= 0 && *pNumber < CSimulation::CONTROL_POINTS && szLowerName.find( "scale" ) == std::string::npos && szLowerName.find( "field" ) == std::string::npos && szLowerName.find( "set" ) == std::string::npos )
			{
				used.insert( *pNumber );
			}
		}
	}
	return { used.begin(), used.end() };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::size_t> CLibrary::Find( std::string_view szName ) const
{
	for ( std::size_t i = 0; i < m_Definitions.size(); i++ )
	{
		if ( m_Definitions[ i ].m_szName == szName )
		{
			return i;
		}
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: Whether the preview runs this function. Others are left out (and shown as such).
//-----------------------------------------------------------------------------
bool IsSimulated( Category_t nCategory, std::string_view szFunction )
{
	return SimulatedFunctions().contains( { nCategory, szFunction } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CLibrary Compile( const dmx::Document_t &document )
{
	CLibrary library;
	const std::vector<std::int32_t> all = Systems( document );
	std::map<std::int32_t, std::size_t> indexOf;
	for ( std::int32_t nSystem : all )
	{
		indexOf[ nSystem ] = library.m_Definitions.size();
		library.m_Definitions.emplace_back();
	}
	for ( std::int32_t nSystem : all )
	{
		const dmx::CElement &element = document.m_Elements[ static_cast<std::size_t>( nSystem ) ];
		CDefinition &definition = library.m_Definitions[ indexOf[ nSystem ] ];
		definition.m_szName = element.m_szName;
		definition.m_nElement = nSystem;
		const auto get = [ &element ]( std::string_view szName ) -> const Value_t * {
			const dmx::Attribute_t *pAttribute = element.Find( szName );
			return pAttribute ? &pAttribute->m_Value : nullptr;
		};
		if ( const auto *v = get( "max_particles" ); v && std::holds_alternative<std::int32_t>( *v ) )
		{
			definition.m_nMaxParticles = std::max( 0, std::get<std::int32_t>( *v ) );
		}
		if ( const auto *v = get( "initial_particles" ); v && std::holds_alternative<std::int32_t>( *v ) )
		{
			definition.m_nInitialParticles = std::max( 0, std::get<std::int32_t>( *v ) );
		}
		if ( const auto *v = get( "radius" ); v && std::holds_alternative<float>( *v ) )
		{
			definition.m_flRadius = std::get<float>( *v );
		}
		if ( const auto *v = get( "color" ); v && std::holds_alternative<dmx::CColor>( *v ) )
		{
			definition.m_Color = ColorOf( std::get<dmx::CColor>( *v ) );
		}
		if ( const auto *v = get( "rotation" ); v && std::holds_alternative<float>( *v ) )
		{
			definition.m_flRotation = std::get<float>( *v ) * DEGREES;
		}
		if ( const auto *v = get( "rotation_speed" ); v && std::holds_alternative<float>( *v ) )
		{
			definition.m_flRotationSpeed = std::get<float>( *v ) * DEGREES;
		}
		if ( const auto *v = get( "sequence_number" ); v && std::holds_alternative<std::int32_t>( *v ) )
		{
			definition.m_nSequence = std::get<std::int32_t>( *v );
		}
		if ( const auto *v = get( "material" ); v && std::holds_alternative<std::string>( *v ) )
		{
			definition.m_szMaterial = std::get<std::string>( *v );
		}
		if ( const auto *v = get( "bounding_box_min" ); v && std::holds_alternative<dmx::Vec3_t>( *v ) )
		{
			const auto &b = std::get<dmx::Vec3_t>( *v );
			definition.m_vecBoundsMin = { b[ 0 ], b[ 1 ], b[ 2 ] };
		}
		if ( const auto *v = get( "bounding_box_max" ); v && std::holds_alternative<dmx::Vec3_t>( *v ) )
		{
			const auto &b = std::get<dmx::Vec3_t>( *v );
			definition.m_vecBoundsMax = { b[ 0 ], b[ 1 ], b[ 2 ] };
		}
		if ( const auto *v = get( "bounding_box_control_point" ); v && std::holds_alternative<std::int32_t>( *v ) )
		{
			definition.m_nBoundsControlPoint = std::get<std::int32_t>( *v );
		}
		if ( const auto *v = get( "Sort particles" ); v && std::holds_alternative<bool>( *v ) )
		{
			definition.m_bSort = std::get<bool>( *v );
		}
		for ( Category_t nCategory : CATEGORIES )
		{
			for ( std::int32_t nOp : References( element, AttributeName( nCategory ) ) )
			{
				const dmx::CElement &opElement = document.m_Elements[ static_cast<std::size_t>( nOp ) ];
				if ( opElement.m_szType != OPERATOR_TYPE )
				{
					continue;
				}
				CCompiledOperator compiled;
				compiled.m_nCategory = nCategory;
				compiled.m_szFunction = std::string{ FunctionName( opElement ) };
				compiled.m_nElement = nOp;
				compiled.m_bSimulated = IsSimulated( nCategory, compiled.m_szFunction );
				for ( const dmx::Attribute_t &attribute : opElement.m_Attributes )
				{
					compiled.m_Values.emplace( attribute.m_szName, attribute.m_Value );
				}
				compiled.m_flStartFadeIn = compiled.Number( "operator start fadein" );
				compiled.m_flEndFadeIn = compiled.Number( "operator end fadein" );
				compiled.m_flStartFadeOut = compiled.Number( "operator start fadeout" );
				compiled.m_flEndFadeOut = compiled.Number( "operator end fadeout" );
				definition.operators.push_back( std::move( compiled ) );
			}
		}
		for ( const ChildLink_t &link : Children( document, nSystem ) )
		{
			if ( link.m_nSystem >= 0 && indexOf.contains( link.m_nSystem ) )
			{
				definition.m_Children.push_back( { indexOf[ link.m_nSystem ], link.m_flDelay } );
			}
		}
	}
	return library;
}

// ---------------------------------------------------------------------------------------------
// Simulating

//-----------------------------------------------------------------------------
// Purpose: A value from 0 to 1 that is fixed for one particle and one use (the operator and what
//          for).
//-----------------------------------------------------------------------------
float ParticleRandom( std::uint32_t nParticle, std::size_t nOp, std::uint32_t nSalt )
{
	return Unit( Mix( ( static_cast<std::uint64_t>( nParticle ) << 32 ) ^ ( static_cast<std::uint64_t>( nOp ) << 8 ) ^ nSalt ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSimulation::CSimulation( std::shared_ptr<const CLibrary> pLibrary, std::size_t nDefinition, std::uint32_t nSeed )
	: m_pLibrary( std::move( pLibrary ) ),
	  m_nDefinition( nDefinition ),
	  m_nSeed( nSeed ),
	  m_nState( nSeed )
{
	Restart();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSimulation::SetControlPoint( int nIndex, CVec3 vecPosition )
{
	if ( nIndex >= 0 && nIndex < CONTROL_POINTS )
	{
		m_UserControlPoints[ static_cast<std::size_t>( nIndex ) ] = vecPosition;
		m_ControlPoints[ static_cast<std::size_t>( nIndex ) ] = vecPosition;
		// m_PreviousControlPoints keeps the old place, so what is locked to it follows on the next step.
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 CSimulation::ControlPoint( int nIndex ) const
{
	return nIndex >= 0 && nIndex < CONTROL_POINTS ? m_ControlPoints[ static_cast<std::size_t>( nIndex ) ] : CVec3{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float CSimulation::Random()
{
	m_nState = Mix( m_nState );
	return Unit( m_nState );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSimulation::Instance_t CSimulation::MakeInstance( std::size_t nDefinition, float flDelay ) const
{
	Instance_t instance;
	instance.m_pDefinition = &m_pLibrary->m_Definitions[ nDefinition ];
	instance.m_flTime = -flDelay;
	const auto emitters = instance.m_pDefinition->Of( Category_t::CATEGORY_EMITTERS );
	instance.m_Carry.assign( emitters.size(), 0.0f );
	instance.m_Done.assign( emitters.size(), false );
	return instance;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSimulation::Restart()
{
	m_flTime = 0.0f;
	m_flPending = 0.0f;
	m_nNextId = 0;
	m_nState = m_nSeed;
	m_ControlPoints = m_UserControlPoints;
	m_PreviousControlPoints = m_ControlPoints;
	m_Root = {};
	if ( m_nDefinition >= m_pLibrary->m_Definitions.size() )
	{
		return;
	}
	// Child systems start with the parent, after their delay; children of children too.
	std::function<void( Instance_t &, int )> pfnAddChildren = [ & ]( Instance_t &instance, int nDepth ) {
		if ( nDepth >= MAX_CHILD_DEPTH )
		{
			return;
		}
		for ( const CDefinition::Child_t &child : instance.m_pDefinition->m_Children )
		{
			instance.m_Children.push_back( MakeInstance( child.m_nDefinition, child.m_flDelay ) );
			pfnAddChildren( instance.m_Children.back(), nDepth + 1 );
		}
	};
	m_Root = MakeInstance( m_nDefinition, 0.0f );
	pfnAddChildren( m_Root, 1 );
}

//-----------------------------------------------------------------------------
// Purpose: Advances by seconds, in steps of STEP (the remainder carries over to the next call).
//-----------------------------------------------------------------------------
void CSimulation::Advance( float flSeconds )
{
	m_flPending += std::max( 0.0f, flSeconds );
	while ( m_flPending >= STEP )
	{
		m_flPending -= STEP;
		m_flTime += STEP;
		std::function<void( Instance_t & )> pfnStepAll = [ & ]( Instance_t &instance ) {
			Step( instance, STEP );
			for ( Instance_t &child : instance.m_Children )
			{
				pfnStepAll( child );
			}
		};
		if ( m_Root.m_pDefinition )
		{
			pfnStepAll( m_Root );
		}
		m_PreviousControlPoints = m_ControlPoints;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CSimulation::GetParticleCount() const
{
	std::function<std::size_t( const Instance_t & )> pfnCount = [ & ]( const Instance_t &instance ) {
		std::size_t n = instance.m_Particles.size();
		for ( const Instance_t &child : instance.m_Children )
		{
			n += pfnCount( child );
		}
		return n;
	};
	return m_Root.m_pDefinition ? pfnCount( m_Root ) : 0;
}

//-----------------------------------------------------------------------------
// Purpose: Nothing alive, and nothing left to emit.
//-----------------------------------------------------------------------------
bool CSimulation::GetFinished() const
{
	std::function<bool( const Instance_t & )> pfnOver = [ & ]( const Instance_t &instance ) {
		if ( instance.m_flTime < 0.0f || !instance.m_Particles.empty() || !instance.m_bInitialEmitted )
		{
			return false;
		}
		if ( !std::ranges::all_of( instance.m_Done, std::identity{} ) )
		{
			return false;
		}
		return std::ranges::all_of( instance.m_Children, pfnOver );
	};
	return !m_Root.m_pDefinition || pfnOver( m_Root );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSimulation::Spawn( Instance_t &instance, int nCount )
{
	const CDefinition &definition = *instance.m_pDefinition;
	nCount = std::min( nCount, definition.m_nMaxParticles - static_cast<int>( instance.m_Particles.size() ) );
	const auto initializers = definition.Of( Category_t::CATEGORY_INITIALIZERS );
	for ( int i = 0; i < nCount; i++ )
	{
		Particle_t p;
		p.m_nId = static_cast<std::uint32_t>( Mix( m_nSeed ^ ( static_cast<std::uint64_t>( m_nNextId++ ) << 20 ) ) );
		p.m_vecPosition = ControlPoint( 0 );
		p.m_flRadius = p.m_flInitialRadius = definition.m_flRadius;
		p.m_vecColor = p.m_vecInitialColor = { definition.m_Color[ 0 ], definition.m_Color[ 1 ], definition.m_Color[ 2 ] };
		p.m_flAlpha = p.m_flInitialAlpha = definition.m_Color[ 3 ];
		p.m_flRotation = definition.m_flRotation;
		p.m_flRotationSpeed = definition.m_flRotationSpeed;
		p.m_nSequence = definition.m_nSequence;
		p.m_flCreationTime = instance.m_flTime;
		for ( std::size_t nOp = 0; nOp < definition.operators.size(); nOp++ )
		{
			const CCompiledOperator &compiled = definition.operators[ nOp ];
			if ( compiled.m_nCategory == Category_t::CATEGORY_INITIALIZERS && compiled.m_bSimulated && compiled.Weight( instance.m_flTime ) > 0.0f )
			{
				Initialize( instance, p, compiled, nOp );
			}
		}
		instance.m_Particles.push_back( p );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSimulation::Initialize( Instance_t &instance, Particle_t &p, const CCompiledOperator &op, std::size_t nOpIndex )
{
	const std::string &f = op.m_szFunction;
	const auto r = [ &p, nOpIndex ]( std::uint32_t nSalt ) { return ParticleRandom( p.m_nId, nOpIndex, nSalt ); };
	const auto randomVector = [ & ]( CVec3 vecMin, CVec3 vecMax, std::uint32_t nSalt ) {
		return Lerp( vecMin, vecMax, CVec3{ r( nSalt ), r( nSalt + 1 ), r( nSalt + 2 ) } );
	};

	if ( f == "Position Within Sphere Random" )
	{
		const CVec3 vecCenter = ControlPoint( op.Integer( "control_point_number" ) );
		// A random direction, squashed by the bias (0 on an axis flattens the sphere onto a plane).
		const float z = 2.0f * r( 1 ) - 1.0f;
		const float flAngle = 2.0f * std::numbers::pi_v<float> * r( 2 );
		const float flFlat = std::sqrt( std::max( 0.0f, 1.0f - z * z ) );
		CVec3 vecDirection{ flFlat * std::cos( flAngle ), flFlat * std::sin( flAngle ), z };
		const CVec3 vecBias = op.Vector( "distance_bias", { 1, 1, 1 } );
		const CVec3 vecAbsolute = op.Vector( "distance_bias_absolute_value" );
		vecDirection = { vecDirection.m_flX * vecBias.m_flX, vecDirection.m_flY * vecBias.m_flY, vecDirection.m_flZ * vecBias.m_flZ };
		if ( vecAbsolute.m_flX != 0.0f )
		{
			vecDirection.m_flX = std::abs( vecDirection.m_flX );
		}
		if ( vecAbsolute.m_flY != 0.0f )
		{
			vecDirection.m_flY = std::abs( vecDirection.m_flY );
		}
		if ( vecAbsolute.m_flZ != 0.0f )
		{
			vecDirection.m_flZ = std::abs( vecDirection.m_flZ );
		}
		const float flDistance = Lerp( op.Number( "distance_min" ), op.Number( "distance_max" ), r( 3 ) );
		p.m_vecPosition = vecCenter + vecDirection * flDistance;
		const float flSpeed = RandomRange( op.Number( "speed_min" ), op.Number( "speed_max" ), r( 4 ), op.Number( "speed_random_exponent", 1.0f ) );
		p.m_vecVelocity = Normalized( vecDirection ) * flSpeed + randomVector( op.Vector( "speed_in_local_coordinate_system_min" ), op.Vector( "speed_in_local_coordinate_system_max" ), 5 );
	}
	else if ( f == "Position Within Box Random" )
	{
		p.m_vecPosition = ControlPoint( op.Integer( "control point number" ) ) + randomVector( op.Vector( "min" ), op.Vector( "max" ), 1 );
	}
	else if ( f == "Position Modify Offset Random" )
	{
		CVec3 vecOffset = randomVector( op.Vector( "offset min" ), op.Vector( "offset max" ), 1 );
		if ( op.Flag( "offset proportional to radius 0/1" ) )
		{
			vecOffset *= p.m_flRadius;
		}
		p.m_vecPosition += vecOffset;
	}
	else if ( f == "Lifetime Random" )
	{
		p.m_flLifetime = RandomRange( op.Number( "lifetime_min" ), op.Number( "lifetime_max" ), r( 1 ), op.Number( "lifetime_random_exponent", 1.0f ) );
	}
	else if ( f == "lifetime from sequence" )
	{
		const auto nDefinition = static_cast<std::size_t>( instance.m_pDefinition - m_pLibrary->m_Definitions.data() );
		const int nFrames = m_pfnSequenceFrames ? m_pfnSequenceFrames( nDefinition, p.m_nSequence ) : 0;
		const float flRate = op.Number( "Frames Per Second", 30.0f );
		if ( nFrames > 0 && flRate > 0.0f )
		{
			p.m_flLifetime = static_cast<float>( nFrames ) / flRate;
		}
	}
	else if ( f == "Lifetime Pre-Age Noise" )
	{
		p.m_flAge = Lerp( op.Number( "start age minimum" ), op.Number( "start age maximum" ), r( 1 ) ) * p.m_flLifetime;
	}
	else if ( f == "Radius Random" )
	{
		p.m_flRadius = p.m_flInitialRadius = RandomRange( op.Number( "radius_min" ), op.Number( "radius_max" ), r( 1 ), op.Number( "radius_random_exponent", 1.0f ) );
	}
	else if ( f == "Color Random" )
	{
		const auto a = op.Color( "color1" );
		const auto b = op.Color( "color2" );
		const float t = r( 1 );
		p.m_vecColor = p.m_vecInitialColor = { Lerp( a[ 0 ], b[ 0 ], t ), Lerp( a[ 1 ], b[ 1 ], t ), Lerp( a[ 2 ], b[ 2 ], t ) };
	}
	else if ( f == "Alpha Random" )
	{
		p.m_flAlpha = p.m_flInitialAlpha = RandomRange( op.Number( "alpha_min", 255.0f ), op.Number( "alpha_max", 255.0f ), r( 1 ), op.Number( "alpha_random_exponent", 1.0f ) ) / 255.0f;
	}
	else if ( f == "Rotation Random" )
	{
		p.m_flRotation = ( op.Number( "rotation_initial" ) + RandomRange( op.Number( "rotation_offset_min" ), op.Number( "rotation_offset_max", 360.0f ), r( 1 ), op.Number( "rotation_random_exponent", 1.0f ) ) ) * DEGREES;
	}
	else if ( f == "Rotation Yaw Random" )
	{
		p.m_flYaw = ( op.Number( "yaw_initial" ) + RandomRange( op.Number( "yaw_offset_min" ), op.Number( "yaw_offset_max" ), r( 1 ), op.Number( "yaw_random_exponent", 1.0f ) ) ) * DEGREES;
	}
	else if ( f == "Rotation Yaw Flip Random" )
	{
		if ( r( 1 ) < op.Number( "Flip Percentage", 0.5f ) )
		{
			p.m_flYaw += std::numbers::pi_v<float>;
		}
	}
	else if ( f == "Sequence Random" || f == "Sequence Two Random" )
	{
		const int nMin = op.Integer( "sequence_min" );
		const int nMax = std::max( nMin, op.Integer( "sequence_max" ) );
		const int nValue = nMin + std::min( nMax - nMin, static_cast<int>( r( 1 ) * static_cast<float>( nMax - nMin + 1 ) ) );
		( f == "Sequence Random" ? p.m_nSequence : p.m_nSequence2 ) = nValue;
	}
	else if ( f == "Trail Length Random" )
	{
		p.m_flTrailLength = RandomRange( op.Number( "length_min" ), op.Number( "length_max" ), r( 1 ), op.Number( "length_random_exponent", 1.0f ) );
	}
	else if ( f == "Velocity Noise" )
	{
		// Noise over space and time in the game; a fixed random value per particle here.
		p.m_vecVelocity += randomVector( op.Vector( "output minimum" ), op.Vector( "output maximum" ), 1 );
	}
	else if ( f == "Remap Noise to Scalar" )
	{
		const int nField = op.Integer( "output field" );
		SetField( p, nField, Lerp( op.Number( "output minimum" ), op.Number( "output maximum" ), r( 1 ) ), true );
	}
	else if ( f == "remap initial scalar" )
	{
		const float flInMin = op.Number( "input minimum" );
		const float flInMax = op.Number( "input maximum", 1.0f );
		const float flInput = GetField( p, op.Integer( "input field" ) );
		if ( op.Flag( "only active within specified input range" ) && ( flInput < std::min( flInMin, flInMax ) || flInput > std::max( flInMin, flInMax ) ) )
		{
			return;
		}
		const float t = flInMax != flInMin ? Clamp01( ( flInput - flInMin ) / ( flInMax - flInMin ) ) : 0.0f;
		const float flValue = Lerp( op.Number( "output minimum" ), op.Number( "output maximum" ), t );
		const int nField = op.Integer( "output field" );
		SetField( p, nField, op.Flag( "output is scalar of initial random range" ) ? GetField( p, nField ) * flValue : flValue, true );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSimulation::Operate( Instance_t &instance, const CCompiledOperator &op, std::size_t nOpIndex, float flDt )
{
	const std::string &f = op.m_szFunction;
	auto &particles = instance.m_Particles;
	const auto r = [ nOpIndex ]( const Particle_t &p, std::uint32_t nSalt ) { return ParticleRandom( p.m_nId, nOpIndex, nSalt ); };
	const auto life = []( const Particle_t &p ) { return p.m_flLifetime > 0.0f ? Clamp01( p.m_flAge / p.m_flLifetime ) : 1.0f; };

	if ( f == "Lifespan Decay" )
	{
		for ( Particle_t &p : particles )
		{
			if ( p.m_flAge >= p.m_flLifetime )
			{
				p.m_bAlive = false;
			}
		}
	}
	else if ( f == "Movement Basic" )
	{
		const CVec3 vecGravity = op.Vector( "gravity" );
		// Drag takes away that share of the speed per 30th of a second.
		const float flKeep = std::pow( 1.0f - std::clamp( op.Number( "drag" ), 0.0f, 0.999f ), flDt * 30.0f );
		for ( Particle_t &p : particles )
		{
			p.m_vecVelocity = ( p.m_vecVelocity + vecGravity * flDt ) * flKeep;
			p.m_vecPosition += p.m_vecVelocity * flDt;
		}
	}
	else if ( f == "Alpha Fade In Random" || f == "Alpha Fade Out Random" )
	{
		const bool bIn = f == "Alpha Fade In Random";
		const char *pszMinKey = bIn ? "fade in time min" : "fade out time min";
		const char *pszMaxKey = bIn ? "fade in time max" : "fade out time max";
		const char *pszExpKey = bIn ? "fade in time exponent" : "fade out time exponent";
		const bool bProportional = op.Flag( "proportional 0/1", true );
		const bool bEase = op.Flag( "ease in and out" );
		for ( Particle_t &p : particles )
		{
			float flSpan = RandomRange( op.Number( pszMinKey ), op.Number( pszMaxKey ), r( p, 1 ), op.Number( pszExpKey, 1.0f ) );
			if ( bProportional )
			{
				flSpan *= p.m_flLifetime;
			}
			if ( flSpan <= 0.0f )
			{
				continue;
			}
			float t = bIn ? Clamp01( p.m_flAge / flSpan ) : Clamp01( ( p.m_flLifetime - p.m_flAge ) / flSpan );
			if ( bEase )
			{
				t = Smooth( t );
			}
			p.m_flAlpha *= t;
		}
	}
	else if ( f == "Alpha Fade and Decay" )
	{
		const float flStartAlpha = op.Number( "start_alpha", 1.0f );
		const float flEndAlpha = op.Number( "end_alpha" );
		const float flInStart = op.Number( "start_fade_in_time" );
		const float flInEnd = op.Number( "end_fade_in_time", 0.5f );
		const float flOutStart = op.Number( "start_fade_out_time", 0.5f );
		const float flOutEnd = op.Number( "end_fade_out_time", 1.0f );
		for ( Particle_t &p : particles )
		{
			const float t = life( p );
			float flAlpha = flStartAlpha;
			if ( t < flInEnd && flInEnd > flInStart )
			{
				flAlpha *= Clamp01( ( t - flInStart ) / ( flInEnd - flInStart ) );
			}
			if ( t > flOutStart )
			{
				flAlpha = flOutEnd > flOutStart ? Lerp( flStartAlpha, flEndAlpha, Clamp01( ( t - flOutStart ) / ( flOutEnd - flOutStart ) ) ) : flEndAlpha;
			}
			p.m_flAlpha *= flAlpha;
			if ( t >= 1.0f )
			{
				p.m_bAlive = false;
			}
		}
	}
	else if ( f == "Color Fade" )
	{
		const auto target = op.Color( "color_fade" );
		const float flStart = op.Number( "fade_start_time" );
		const float flEnd = op.Number( "fade_end_time", 1.0f );
		const bool bEase = op.Flag( "ease_in_and_out" );
		for ( Particle_t &p : particles )
		{
			float t = flEnd > flStart ? Clamp01( ( life( p ) - flStart ) / ( flEnd - flStart ) ) : ( life( p ) >= flStart ? 1.0f : 0.0f );
			if ( bEase )
			{
				t = Smooth( t );
			}
			for ( int c = 0; c < 3; c++ )
			{
				p.m_vecColor[ static_cast<std::size_t>( c ) ] = Lerp( p.m_vecColor[ static_cast<std::size_t>( c ) ], target[ static_cast<std::size_t>( c ) ], t );
			}
		}
	}
	else if ( f == "Radius Scale" )
	{
		const float flStart = op.Number( "start_time" );
		const float flEnd = op.Number( "end_time", 1.0f );
		const float flStartScale = op.Number( "radius_start_scale", 1.0f );
		const float flEndScale = op.Number( "radius_end_scale", 1.0f );
		const float flAmount = op.Number( "scale_bias", 0.5f );
		const bool bEase = op.Flag( "ease_in_and_out" );
		for ( Particle_t &p : particles )
		{
			float t = flEnd > flStart ? Clamp01( ( life( p ) - flStart ) / ( flEnd - flStart ) ) : ( life( p ) >= flStart ? 1.0f : 0.0f );
			t = Bias( t, flAmount );
			if ( bEase )
			{
				t = Smooth( t );
			}
			p.m_flRadius *= Lerp( flStartScale, flEndScale, t );
		}
	}
	else if ( f == "Rotation Spin Roll" || f == "Rotation Spin Yaw" )
	{
		const bool bRoll = f == "Rotation Spin Roll";
		const float flRate = op.Number( bRoll ? "spin_rate_degrees" : "yaw_rate_degrees" );
		const float flMinRate = op.Number( bRoll ? "spin_rate_min" : "yaw_rate_min" );
		const float flStop = op.Number( bRoll ? "spin_stop_time" : "yaw_stop_time" );
		for ( Particle_t &p : particles )
		{
			float flSpeed = flRate;
			if ( flStop > 0.0f )
			{
				// Slows down to the minimum rate by the stop time (a share of the lifetime).
				flSpeed = Lerp( flRate, flMinRate, Clamp01( life( p ) / flStop ) );
			}
			( bRoll ? p.m_flRotation : p.m_flYaw ) += ( flSpeed * DEGREES + ( bRoll ? p.m_flRotationSpeed : 0.0f ) ) * flDt;
		}
	}
	else if ( f == "Oscillate Scalar" || f == "Oscillate Vector" )
	{
		const bool bVector = f == "Oscillate Vector";
		const int nField = op.Integer( "oscillation field" );
		const bool bProportional = op.Flag( "proportional 0/1", true );
		const float flMultiplier = op.Number( "oscillation multiplier", 2.0f );
		const float flPhase = op.Number( "oscillation start phase", 0.5f );
		for ( Particle_t &p : particles )
		{
			float flStart = Lerp( op.Number( "start time min" ), op.Number( "start time max" ), r( p, 1 ) );
			float flEnd = Lerp( op.Number( "end time min", 1.0f ), op.Number( "end time max", 1.0f ), r( p, 2 ) );
			const float flTime = bProportional ? life( p ) : p.m_flAge;
			if ( flTime < flStart || flTime > flEnd )
			{
				continue;
			}
			const float flCycle = 2.0f * std::numbers::pi_v<float> * ( p.m_flAge + flPhase );
			if ( bVector )
			{
				const CVec3 vecRate = Lerp( op.Vector( "oscillation rate min" ), op.Vector( "oscillation rate max" ), CVec3{ r( p, 3 ), r( p, 4 ), r( p, 5 ) } );
				const CVec3 vecFrequency = Lerp( op.Vector( "oscillation frequency min", { 1, 1, 1 } ), op.Vector( "oscillation frequency max", { 1, 1, 1 } ), CVec3{ r( p, 6 ), r( p, 7 ), r( p, 8 ) } );
				const CVec3 vecWave{ std::sin( flCycle * vecFrequency.m_flX ), std::sin( flCycle * vecFrequency.m_flY ), std::sin( flCycle * vecFrequency.m_flZ ) };
				const CVec3 vecChange = CVec3{ vecRate.m_flX * vecWave.m_flX, vecRate.m_flY * vecWave.m_flY, vecRate.m_flZ * vecWave.m_flZ } * ( flMultiplier * flDt );
				SetVectorField( p, nField, GetVectorField( p, nField ) + vecChange, false );
			}
			else if ( !ResetsEachStep( nField ) )
			{
				const float flRate = Lerp( op.Number( "oscillation rate min" ), op.Number( "oscillation rate max" ), r( p, 3 ) );
				const float flFrequency = Lerp( op.Number( "oscillation frequency min", 1.0f ), op.Number( "oscillation frequency max", 1.0f ), r( p, 4 ) );
				SetField( p, nField, GetField( p, nField ) + flRate * std::sin( flCycle * flFrequency ) * flMultiplier * flDt, false );
			}
		}
	}
	else if ( f == "Movement Dampen Relative to Control Point" )
	{
		const CVec3 vecCenter = ControlPoint( op.Integer( "control_point_number" ) );
		const float flRange = op.Number( "falloff range", 4.0f );
		const float flScale = op.Number( "dampen scale", 1.0f );
		for ( Particle_t &p : particles )
		{
			const float flDistance = Length( p.m_vecPosition - vecCenter );
			if ( flRange > 0.0f && flDistance < flRange )
			{
				p.m_vecVelocity *= 1.0f - Clamp01( flScale * ( 1.0f - flDistance / flRange ) );
			}
		}
	}
	else if ( f == "Cull Random" )
	{
		const float flPercentage = op.Number( "Cull Percentage", 0.5f );
		const float flStart = op.Number( "Cull Start Time" );
		const float flEnd = op.Number( "Cull End Time", 1.0f );
		for ( Particle_t &p : particles )
		{
			if ( r( p, 1 ) < flPercentage && life( p ) >= Lerp( flStart, flEnd, std::pow( r( p, 2 ), op.Number( "Cull Time Exponent", 1.0f ) ) ) )
			{
				p.m_bAlive = false;
			}
		}
	}
	else if ( f == "Cull when crossing plane" )
	{
		const CVec3 vecNormal = Normalized( op.Vector( "Plane Normal", { 0, 0, 1 } ) );
		const CVec3 vecPoint = ControlPoint( op.Integer( "Control Point for point on plane" ) ) + vecNormal * op.Number( "Cull plane offset" );
		for ( Particle_t &p : particles )
		{
			if ( Dot( p.m_vecPosition - vecPoint, vecNormal ) < 0.0f )
			{
				p.m_bAlive = false;
			}
		}
	}
	else if ( f == "Noise Vector" )
	{
		const int nField = op.Integer( "output field", FIELD_COLOR );
		for ( Particle_t &p : particles )
		{
			SetVectorField( p, nField, Lerp( op.Vector( "output minimum" ), op.Vector( "output maximum" ), CVec3{ r( p, 1 ), r( p, 2 ), r( p, 3 ) } ), false );
		}
	}
	else if ( f == "Remap Distance to Control Point to Scalar" )
	{
		const CVec3 vecCenter = ControlPoint( op.Integer( "control point", 1 ) );
		const float flDMin = op.Number( "distance minimum" );
		const float flDMax = op.Number( "distance maximum", 128.0f );
		const int nField = op.Integer( "output field", FIELD_ALPHA );
		const bool bScalar = op.Flag( "output is scalar of initial random range" );
		for ( Particle_t &p : particles )
		{
			const float t = flDMax != flDMin ? Clamp01( ( Length( p.m_vecPosition - vecCenter ) - flDMin ) / ( flDMax - flDMin ) ) : 0.0f;
			const float flValue = Lerp( op.Number( "output minimum" ), op.Number( "output maximum", 1.0f ), t );
			SetField( p, nField, bScalar ? GetField( p, nField ) * flValue : flValue, false );
		}
	}
	else if ( f == "Set Control Point Positions" )
	{
		static constexpr std::array<std::string_view, 4> ORDER{ "First", "Second", "Third", "Fourth" };
		for ( std::string_view szWhich : ORDER )
		{
			const int nNumber = op.Integer( std::string{ szWhich } + " Control Point Number", -1 );
			const int nParent = op.Integer( std::string{ szWhich } + " Control Point Parent", 0 );
			if ( nNumber > 0 && nNumber < CONTROL_POINTS )
			{
				m_ControlPoints[ static_cast<std::size_t>( nNumber ) ] = ControlPoint( nParent ) + op.Vector( std::string{ szWhich } + " Control Point Location" );
			}
		}
	}
	else if ( f == "random force" )
	{
		for ( Particle_t &p : particles )
		{
			p.m_vecVelocity += Lerp( op.Vector( "min force" ), op.Vector( "max force" ), CVec3{ Random(), Random(), Random() } ) * flDt;
		}
	}
	else if ( f == "twist around axis" )
	{
		const CVec3 vecAxis = Normalized( op.Vector( "twist axis", { 0, 0, 1 } ) );
		const CVec3 vecCenter = ControlPoint( 0 );
		const float flAmount = op.Number( "amount of force", 10.0f );
		for ( Particle_t &p : particles )
		{
			p.m_vecVelocity += Normalized( Cross( vecAxis, p.m_vecPosition - vecCenter ) ) * ( flAmount * flDt );
		}
	}
	else if ( f == "Pull towards control point" )
	{
		const CVec3 vecCenter = ControlPoint( op.Integer( "control point number" ) );
		const float flAmount = op.Number( "amount of force", 5.0f );
		const float flFalloff = op.Number( "falloff power" );
		for ( Particle_t &p : particles )
		{
			const CVec3 vecToward = vecCenter - p.m_vecPosition;
			const float flDistance = std::max( 1.0f, Length( vecToward ) );
			p.m_vecVelocity += Normalized( vecToward ) * ( flAmount * flDt / std::pow( flDistance, flFalloff ) );
		}
	}
	else if ( f == "Constrain distance to control point" )
	{
		const CVec3 vecCenter = ControlPoint( op.Integer( "control point number" ) ) + op.Vector( "offset of center" );
		const float flMinimum = op.Number( "minimum distance" );
		const float flMaximum = op.Number( "maximum distance", 100.0f );
		for ( Particle_t &p : particles )
		{
			const CVec3 vecOffset = p.m_vecPosition - vecCenter;
			const float flDistance = Length( vecOffset );
			if ( flDistance > 0.0f && ( flDistance < flMinimum || flDistance > flMaximum ) )
			{
				p.m_vecPosition = vecCenter + vecOffset * ( std::clamp( flDistance, flMinimum, flMaximum ) / flDistance );
			}
		}
	}
	else if ( f == "Prevent passing through a plane" )
	{
		const CVec3 vecNormal = Normalized( op.Vector( "plane normal", { 0, 0, 1 } ) );
		const CVec3 vecPoint = op.Flag( "global origin" ) ? op.Vector( "plane point" ) : ControlPoint( op.Integer( "control point number" ) ) + op.Vector( "plane point" );
		for ( Particle_t &p : particles )
		{
			const float flSide = Dot( p.m_vecPosition - vecPoint, vecNormal );
			if ( flSide < 0.0f )
			{
				p.m_vecPosition -= vecNormal * flSide;
				const float flInto = Dot( p.m_vecVelocity, vecNormal );
				if ( flInto < 0.0f )
				{
					p.m_vecVelocity -= vecNormal * flInto;
				}
			}
		}
	}
	if ( f == "Movement Lock to Control Point" )
	{
		// Carried along by however far the control point moved since the last step.
		const int nIndex = std::clamp( op.Integer( "control_point_number" ), 0, CONTROL_POINTS - 1 );
		const CVec3 vecMoved = m_ControlPoints[ static_cast<std::size_t>( nIndex ) ] - m_PreviousControlPoints[ static_cast<std::size_t>( nIndex ) ];
		for ( Particle_t &p : particles )
		{
			p.m_vecPosition += vecMoved;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CSimulation::Step( Instance_t &instance, float flDt )
{
	instance.m_flTime += flDt;
	if ( instance.m_flTime < 0.0f )
	{
		return;
	}
	const CDefinition &definition = *instance.m_pDefinition;

	// Emit
	if ( !instance.m_bInitialEmitted )
	{
		instance.m_bInitialEmitted = true;
		Spawn( instance, definition.m_nInitialParticles );
	}
	const auto emitters = definition.Of( Category_t::CATEGORY_EMITTERS );
	for ( std::size_t i = 0; i < emitters.size(); i++ )
	{
		const CCompiledOperator &op = *emitters[ i ];
		if ( instance.m_Done[ i ] )
		{
			continue;
		}
		if ( !op.m_bSimulated )
		{
			instance.m_Done[ i ] = true;
			continue;
		}
		const float flStart = op.Number( "emission_start_time" );
		if ( instance.m_flTime < flStart || op.Weight( instance.m_flTime ) <= 0.0f )
		{
			continue;
		}
		if ( op.m_szFunction == "emit_continuously" )
		{
			const float flDuration = op.Number( "emission_duration" );
			if ( flDuration > 0.0f && instance.m_flTime >= flStart + flDuration )
			{
				instance.m_Done[ i ] = true;
				continue;
			}
			instance.m_Carry[ i ] += op.Number( "emission_rate", 100.0f ) * flDt;
			const int nCount = static_cast<int>( instance.m_Carry[ i ] );
			instance.m_Carry[ i ] -= static_cast<float>( nCount );
			Spawn( instance, nCount );
		}
		else
		{
			if ( instance.m_Carry[ i ] == 0.0f )
			{
				const int nMaximum = op.Integer( "num_to_emit", 100 );
				const int nMinimum = op.Integer( "num_to_emit_minimum", -1 );
				instance.m_Carry[ i ] = static_cast<float>( nMinimum >= 0 && nMinimum < nMaximum ? nMinimum + static_cast<int>( Random() * static_cast<float>( nMaximum - nMinimum + 1 ) ) : nMaximum );
				instance.m_Carry[ i ] = std::max( instance.m_Carry[ i ], 0.5f ); // owed something, even if 0
			}
			const int nPerFrame = op.Integer( "maximum emission per frame", -1 );
			int nCount = static_cast<int>( instance.m_Carry[ i ] );
			if ( nPerFrame > 0 )
			{
				nCount = std::min( nCount, nPerFrame );
			}
			Spawn( instance, nCount );
			instance.m_Carry[ i ] -= static_cast<float>( nCount );
			if ( instance.m_Carry[ i ] < 1.0f )
			{
				instance.m_Done[ i ] = true;
			}
		}
	}

	// Age, reset what fades and scales work from, then run the operators: forces push, operators
	// move and fade, constraints have the last word.
	for ( Particle_t &p : instance.m_Particles )
	{
		p.m_flAge += flDt;
		p.m_flRadius = p.m_flInitialRadius;
		p.m_flAlpha = p.m_flInitialAlpha;
		p.m_vecColor = p.m_vecInitialColor;
	}
	for ( Category_t nCategory : { Category_t::CATEGORY_FORCES, Category_t::CATEGORY_OPERATORS, Category_t::CATEGORY_CONSTRAINTS } )
	{
		for ( std::size_t i = 0; i < definition.operators.size(); i++ )
		{
			const CCompiledOperator &op = definition.operators[ i ];
			if ( op.m_nCategory == nCategory && op.m_bSimulated && op.Weight( instance.m_flTime ) > 0.0f )
			{
				Operate( instance, op, i, flDt );
			}
		}
	}
	std::erase_if( instance.m_Particles, []( const Particle_t &p ) { return !p.m_bAlive; } );
}

} // namespace xv::particles
