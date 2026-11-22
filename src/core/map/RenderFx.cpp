//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See RenderFx.h.
//
//===========================================================================//
#include "map/RenderFx.h"

#include <algorithm>
#include <cmath>

#include "map/VmfValues.h"
#include "util/Numbers.h"

namespace xv::map
{

namespace
{

// rendermode: normal ignores renderamt, none draws nothing; the rest are see-through by it.
constexpr int RENDER_MODE_NORMAL = 0;
constexpr int RENDER_MODE_NONE = 10;

// renderfx, by their numbers in the FGDs' lists.
enum RenderFx_t
{
	RENDER_FX_NONE = 0,
	RENDER_FX_PULSE_SLOW = 1,
	RENDER_FX_PULSE_FAST = 2,
	RENDER_FX_PULSE_SLOW_WIDE = 3,
	RENDER_FX_PULSE_FAST_WIDE = 4,
	RENDER_FX_FADE_SLOW = 5,
	RENDER_FX_FADE_FAST = 6,
	RENDER_FX_SOLID_SLOW = 7,
	RENDER_FX_SOLID_FAST = 8,
	RENDER_FX_STROBE_SLOW = 9,
	RENDER_FX_STROBE_FAST = 10,
	RENDER_FX_STROBE_FASTER = 11,
	RENDER_FX_FLICKER_SLOW = 12,
	RENDER_FX_FLICKER_FAST = 13,
	RENDER_FX_DISTORT = 15,
	RENDER_FX_HOLOGRAM = 16,
	RENDER_FX_PULSE_FAST_WIDER = 24,
};

//-----------------------------------------------------------------------------
// Purpose: A number in 0-1 that wanders about, the same for the same moment and seed.
//-----------------------------------------------------------------------------
double Noise( double flTime, std::uint32_t nSeed )
{
	std::uint32_t n = static_cast<std::uint32_t>( std::floor( flTime * 30.0 ) ) * 747796405u + nSeed * 2891336453u;
	n ^= n >> 16;
	n *= 2246822519u;
	n ^= n >> 13;
	return ( n & 0xffffu ) / 65535.0;
}

//-----------------------------------------------------------------------------
// Purpose: The opacity (0-255) an effect gives renderamt at that moment.
//-----------------------------------------------------------------------------
double FxAlpha( int nFx, double flAmount, double flTime, double flPhase, std::uint32_t nSeed )
{
	// Pulses swing the opacity a little (16) or a lot (64) about renderamt, about every three
	// seconds (slow) or in under one (fast); the widest goes all the way, fastest of all.
	// Strobes and flickers are on (renderamt) or off; fades and becoming solid run from one end
	// to the other, shown here over and over (four seconds slow, one fast).
	const auto wave = [ & ]( double flRate ) { return std::sin( flTime * flRate + flPhase ); };
	const auto ramp = [ & ]( double flSeconds ) { return std::fmod( flTime + flPhase, flSeconds ) / flSeconds; };
	switch ( nFx )
	{
		case RENDER_FX_PULSE_SLOW: return flAmount + 16.0 * wave( 2.0 );
		case RENDER_FX_PULSE_FAST: return flAmount + 16.0 * wave( 8.0 );
		case RENDER_FX_PULSE_SLOW_WIDE: return flAmount + 64.0 * wave( 2.0 );
		case RENDER_FX_PULSE_FAST_WIDE: return flAmount + 64.0 * wave( 8.0 );
		case RENDER_FX_PULSE_FAST_WIDER: return 255.0 * std::abs( wave( 12.0 ) );
		case RENDER_FX_FADE_SLOW: return flAmount * ( 1.0 - ramp( 4.0 ) );
		case RENDER_FX_FADE_FAST: return flAmount * ( 1.0 - ramp( 1.0 ) );
		case RENDER_FX_SOLID_SLOW: return flAmount + ( 255.0 - flAmount ) * ramp( 4.0 );
		case RENDER_FX_SOLID_FAST: return flAmount + ( 255.0 - flAmount ) * ramp( 1.0 );
		case RENDER_FX_STROBE_SLOW: return wave( 4.0 ) > 0.0 ? flAmount : 0.0;
		case RENDER_FX_STROBE_FAST: return wave( 16.0 ) > 0.0 ? flAmount : 0.0;
		case RENDER_FX_STROBE_FASTER: return wave( 36.0 ) > 0.0 ? flAmount : 0.0;
		case RENDER_FX_FLICKER_SLOW: return std::sin( flTime * 2.0 ) + std::sin( flTime * 17.0 + flPhase ) > 0.0 ? flAmount : 0.0;
		case RENDER_FX_FLICKER_FAST: return std::sin( flTime * 16.0 ) + std::sin( flTime * 23.0 + flPhase ) > 0.0 ? flAmount : 0.0;
		case RENDER_FX_DISTORT:
		case RENDER_FX_HOLOGRAM: return flAmount * ( 0.6 + 0.4 * Noise( flTime, nSeed ) );
		default: return flAmount;
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
RenderLook_t RenderLookOf( const Entity_t &entity, double flTime, std::uint32_t nSeed )
{
	RenderLook_t look;
	if ( entity.HasKey( "rendercolor" ) )
	{
		if ( const std::optional<Color_t> color = ParseColor( entity.GetValue( "rendercolor" ) ) )
			look.m_Tint = { color->m_nR, color->m_nG, color->m_nB, 255 };
	}
	const int nMode = ParseNumber<int>( entity.GetValue( "rendermode" ) ).value_or( RENDER_MODE_NORMAL );
	if ( nMode == RENDER_MODE_NORMAL )
		return look;
	if ( nMode == RENDER_MODE_NONE )
	{
		look.m_Tint[ 3 ] = 0;
		return look;
	}
	const double flAmount = std::clamp( ParseNumber<double>( entity.GetValue( "renderamt" ) ).value_or( 255.0 ), 0.0, 255.0 );
	const int nFx = ParseNumber<int>( entity.GetValue( "renderfx" ) ).value_or( RENDER_FX_NONE );
	const double flAlpha = FxAlpha( nFx, flAmount, flTime, ( nSeed % 1000u ) * 0.0063, nSeed );
	look.m_bAnimated = ( nFx >= RENDER_FX_PULSE_SLOW && nFx <= RENDER_FX_FLICKER_FAST ) || nFx == RENDER_FX_DISTORT || nFx == RENDER_FX_HOLOGRAM || nFx == RENDER_FX_PULSE_FAST_WIDER;
	look.m_Tint[ 3 ] = static_cast<std::uint8_t>( std::clamp( std::lround( flAlpha ), 0L, 255L ) );
	return look;
}

} // namespace xv::map
