//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See SoundPlayback.h.
//
//===========================================================================//
#include "SoundPlayback.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "../util/Numbers.h"
#include "../vfs/Vfs.h"
#include "SoundConstants.h"
#include "SoundLint.h"

namespace xv::sound
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view Trim( std::string_view szText )
{
	const auto nFirst = szText.find_first_not_of( " \t\r\n" );
	if ( nFirst == std::string_view::npos )
	{
		return {};
	}
	return szText.substr( nFirst, szText.find_last_not_of( " \t\r\n" ) - nFirst + 1 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<double> ResolvePart( ValueKind_t nKind, std::string_view szPart )
{
	const auto constant = [ szPart ]( std::span<const Constant_t> table ) -> std::optional<double> {
		if ( const Constant_t *c = FindConstant( table, szPart ) )
		{
			return c->m_flValue;
		}
		return ParseNumber<double>( szPart );
	};
	switch ( nKind )
	{
		case ValueKind_t::VALUE_KIND_VOLUME: return constant( Volumes() );
		case ValueKind_t::VALUE_KIND_PITCH: return constant( Pitches() );
		case ValueKind_t::VALUE_KIND_ATTENUATION: return constant( Attenuations() );
		case ValueKind_t::VALUE_KIND_SOUND_LEVEL: return ParseSoundLevel( szPart );
		default: return ParseNumber<double>( szPart );
	}
}

// The settings a block plays with, read the way its file type reads them.
struct Settings_t
{
	CRange m_Volume{ 1, 1 };
	CRange m_Pitch{ 100, 100 };
	std::optional<int> m_Position;
	bool m_bRandomPosition = false;
	std::optional<double> m_SoundLevel;
	double m_flDelaySeconds = 0;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Settings_t ReadSettings( FileKind_t nKind, const kv1::CNode &block )
{
	const bool bScript = nKind == FileKind_t::FILE_KIND_SOUNDSCRIPT;
	Settings_t settings;
	if ( const kv1::CNode *pVolume = block.Find( "volume" ) )
	{
		settings.m_Volume = ParseRange( bScript ? ValueKind_t::VALUE_KIND_VOLUME : ValueKind_t::VALUE_KIND_NUMBER_RANGE, pVolume->GetValue() ).value_or( CRange{ 1, 1 } );
	}
	if ( const kv1::CNode *pPitch = block.Find( "pitch" ) )
	{
		settings.m_Pitch = ParseRange( bScript ? ValueKind_t::VALUE_KIND_PITCH : ValueKind_t::VALUE_KIND_NUMBER_RANGE, pPitch->GetValue() ).value_or( CRange{ 100, 100 } );
	}
	if ( const kv1::CNode *pLevel = block.Find( "soundlevel" ) )
	{
		if ( const auto range = ParseRange( ValueKind_t::VALUE_KIND_SOUND_LEVEL, pLevel->GetValue() ) )
		{
			settings.m_SoundLevel = range->m_flLow;
		}
	}
	else if ( const kv1::CNode *pAttenuation = block.Find( "attenuation" ) ? block.Find( "attenuation" ) : block.Find( "compatibilityattenuation" ) )
	{
		if ( const auto range = ParseRange( ValueKind_t::VALUE_KIND_ATTENUATION, pAttenuation->GetValue() ) )
		{
			settings.m_SoundLevel = AttenuationToSoundLevel( range->m_flLow );
		}
	}
	if ( const kv1::CNode *pPosition = block.Find( "position" ) )
	{
		if ( kv1::EqualsIgnoreCase( Trim( pPosition->GetValue() ), "random" ) )
		{
			settings.m_bRandomPosition = true;
		}
		else
		{
			settings.m_Position = ParseNumber<int>( Trim( pPosition->GetValue() ) );
		}
	}
	if ( const kv1::CNode *pDelay = block.Find( "delay_msec" ) )
	{
		settings.m_flDelaySeconds = ParseNumber<double>( Trim( pDelay->GetValue() ) ).value_or( 0 ) / 1000.0;
	}
	return settings;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::string> WavesIn( const kv1::CNode &block )
{
	std::vector<std::string> waves;
	for ( const kv1::CNode *pWave : block.FindAll( "wave" ) )
	{
		waves.push_back( pWave->GetValue() );
	}
	if ( const kv1::CNode *pRndwave = block.Find( "rndwave" ); pRndwave && pRndwave->IsBlock() )
	{
		for ( const kv1::CNode *pWave : pRndwave->FindAll( "wave" ) )
		{
			waves.push_back( pWave->GetValue() );
		}
	}
	std::erase_if( waves, []( const std::string &szWave ) { return Trim( szWave ).empty(); } );
	return waves;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
PlayRequest_t MakeRequest( const std::vector<std::string> &waves, const Settings_t &settings, bool bLoop, std::string szOrigin, std::mt19937 &random )
{
	PlayRequest_t request;
	request.m_szWave = waves[ std::uniform_int_distribution<std::size_t>{ 0, waves.size() - 1 }( random ) ];
	request.m_szGamePath = WaveGamePath( request.m_szWave );
	request.m_flVolume = settings.m_Volume.Pick( random );
	request.m_flPitch = settings.m_Pitch.Pick( random );
	request.m_bLoop = bLoop;
	request.m_flDelaySeconds = settings.m_flDelaySeconds;
	request.m_Position = settings.m_Position;
	request.m_bRandomPosition = settings.m_bRandomPosition;
	request.m_SoundLevel = settings.m_SoundLevel;
	request.m_szOrigin = std::move( szOrigin );
	return request;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
double CRange::Pick( std::mt19937 &random ) const
{
	return m_flLow >= m_flHigh ? m_flLow : std::uniform_real_distribution<double>{ m_flLow, m_flHigh }( random );
}

//-----------------------------------------------------------------------------
// Purpose: "0.5", "VOL_NORM", "95, 105", "SNDLVL_80dB"... as a range; nullopt if the value doesn't
//          parse.
//-----------------------------------------------------------------------------
std::optional<CRange> ParseRange( ValueKind_t nKind, std::string_view szText )
{
	std::vector<std::string_view> parts;
	std::size_t nStart = 0;
	while ( true )
	{
		const auto nComma = szText.find( ',', nStart );
		parts.push_back( Trim( szText.substr( nStart, nComma == std::string_view::npos ? std::string_view::npos : nComma - nStart ) ) );
		if ( nComma == std::string_view::npos )
		{
			break;
		}
		nStart = nComma + 1;
	}
	if ( parts.size() > 2 )
	{
		return std::nullopt;
	}
	const auto low = ResolvePart( nKind, parts[ 0 ] );
	const auto high = parts.size() == 2 ? ResolvePart( nKind, parts[ 1 ] ) : low;
	if ( !low || !high )
	{
		return std::nullopt;
	}
	return CRange{ std::min( *low, *high ), std::max( *low, *high ) };
}

//-----------------------------------------------------------------------------
// Purpose: "sound/<path>" for a wave value, flags stripped and normalized; empty for sentences and
//          voice streams.
//-----------------------------------------------------------------------------
std::string WaveGamePath( std::string_view szWave )
{
	for ( std::size_t i = 0; i < 2 && i < szWave.size(); i++ )
	{
		if ( szWave[ i ] == '!' || szWave[ i ] == '?' )
		{
			return {};
		}
	}
	const std::string_view szFile = Trim( StripPathFlags( szWave ) );
	return szFile.empty() ? std::string{} : "sound/" + vfs::NormalizePath( szFile );
}

//-----------------------------------------------------------------------------
// Purpose: One play of a block with its own settings: a soundscript entry, playlooping,
//          playrandom, or an
//          rndwave (which takes its settings from the enclosing block). nullopt if there is
//          nothing to play.
//-----------------------------------------------------------------------------
std::optional<PlayRequest_t> PlayBlock( FileKind_t nKind, const kv1::CNode &block, const kv1::CNode *pParent, std::mt19937 &random )
{
	if ( !block.IsBlock() )
	{
		return std::nullopt;
	}
	const std::string szKey = ToLower( block.GetKey() );
	if ( szKey == "rndwave" )
	{
		// An rndwave plays with the settings of the entry or playrandom around it.
		std::vector<std::string> waves;
		for ( const kv1::CNode *pWave : block.FindAll( "wave" ) )
		{
			if ( !Trim( pWave->GetValue() ).empty() )
			{
				waves.push_back( pWave->GetValue() );
			}
		}
		if ( waves.empty() )
		{
			return std::nullopt;
		}
		const Settings_t settings = pParent ? ReadSettings( nKind, *pParent ) : Settings_t{};
		return MakeRequest( waves, settings, false, pParent ? pParent->GetKey() + " › rndwave" : "rndwave", random );
	}

	const bool bLooping = nKind == FileKind_t::FILE_KIND_SOUNDSCAPE && szKey == "playlooping";
	const bool bEntry = nKind == FileKind_t::FILE_KIND_SOUNDSCRIPT && pParent == nullptr;
	if ( !bLooping && !bEntry && !( nKind == FileKind_t::FILE_KIND_SOUNDSCAPE && szKey == "playrandom" ) )
	{
		return std::nullopt;
	}
	const auto waves = WavesIn( block );
	if ( waves.empty() )
	{
		return std::nullopt;
	}
	return MakeRequest( waves, ReadSettings( nKind, block ), bLooping, block.GetKey(), random );
}

namespace
{

class CPlanner
{
public:
	explicit CPlanner( const SoundscapeLookup_t &pfnLookup )
		: m_pfnLookup( pfnLookup )
	{}

	struct Context_t
	{
		double m_flVolumeScale = 1;
		std::optional<int> m_PositionOverride;
		std::optional<int> m_AmbientPositionOverride;
		std::size_t m_nSource = 0; // the planned soundscape's child block this comes from
	};

	void Add( const kv1::CNode &soundscape, const Context_t &context )
	{
		const std::string szName = ToLower( soundscape.GetKey() );
		if ( std::ranges::find( m_Stack, szName ) != m_Stack.end() )
		{
			m_Plan.m_Problems.push_back( std::format( "playsoundscape loop at \"{}\"; not followed", soundscape.GetKey() ) );
			return;
		}
		if ( m_Stack.size() >= 8 )
		{
			m_Plan.m_Problems.push_back( std::format( "playsoundscape nesting is too deep at \"{}\"", soundscape.GetKey() ) );
			return;
		}
		m_Stack.push_back( szName );
		const bool bTop = m_Stack.size() == 1;
		for ( std::size_t i = 0; i < soundscape.GetChildren().size(); i++ )
		{
			const kv1::CNode &child = soundscape.GetChildren()[ i ];
			if ( !child.IsBlock() )
			{
				continue;
			}
			Context_t here = context;
			if ( bTop )
			{
				here.m_nSource = i;
			}
			const std::string szKey = ToLower( child.GetKey() );
			const std::string szOrigin = soundscape.GetKey() + " › " + child.GetKey();
			if ( szKey == "playlooping" || szKey == "playrandom" )
			{
				AddRule( child, szKey == "playlooping", szOrigin, here );
			}
			else if ( szKey == "playsoundscape" )
			{
				AddNested( child, szOrigin, here );
			}
		}
		m_Stack.pop_back();
	}

	SoundscapePlan_t Take()
	{
		return std::move( m_Plan );
	}

private:
	void AddRule( const kv1::CNode &block, bool bLooping, const std::string &szOrigin, const Context_t &context )
	{
		SoundscapeRule_t rule;
		rule.m_nKind = bLooping ? SoundscapeRule_t::Kind_t::KIND_LOOPING : SoundscapeRule_t::Kind_t::KIND_RANDOM;
		rule.m_Waves = WavesIn( block );
		if ( rule.m_Waves.empty() )
		{
			m_Plan.m_Problems.push_back( szOrigin + " has no sounds" );
			return;
		}
		const Settings_t settings = ReadSettings( FileKind_t::FILE_KIND_SOUNDSCAPE, block );
		rule.m_Volume = { settings.m_Volume.m_flLow * context.m_flVolumeScale, settings.m_Volume.m_flHigh * context.m_flVolumeScale };
		rule.m_Pitch = settings.m_Pitch;
		rule.m_Position = settings.m_Position;
		rule.m_bRandomPosition = settings.m_bRandomPosition;
		rule.m_SoundLevel = settings.m_SoundLevel;
		if ( rule.m_Position && context.m_PositionOverride )
		{
			rule.m_Position = context.m_PositionOverride;
		}
		else if ( !rule.m_Position && !rule.m_bRandomPosition && context.m_AmbientPositionOverride )
		{
			rule.m_Position = context.m_AmbientPositionOverride;
		}
		if ( !bLooping )
		{
			if ( const kv1::CNode *pTime = block.Find( "time" ) )
			{
				rule.m_Interval = ParseRange( ValueKind_t::VALUE_KIND_NUMBER_RANGE, pTime->GetValue() ).value_or( CRange{ 10, 10 } );
			}
		}
		rule.m_szOrigin = szOrigin;
		rule.m_nSource = context.m_nSource;
		m_Plan.m_Rules.push_back( std::move( rule ) );
	}

	void AddNested( const kv1::CNode &block, const std::string &szOrigin, const Context_t &context )
	{
		const kv1::CNode *pName = block.Find( "name" );
		const kv1::CNode *pTarget = pName ? m_pfnLookup( pName->GetValue() ) : nullptr;
		if ( !pTarget )
		{
			m_Plan.m_Problems.push_back( std::format( "{}: no soundscape named \"{}\"", szOrigin, pName ? pName->GetValue() : std::string{} ) );
			return;
		}
		Context_t nested = context;
		if ( const kv1::CNode *pVolume = block.Find( "volume" ) )
		{
			nested.m_flVolumeScale *= ParseNumber<double>( Trim( pVolume->GetValue() ) ).value_or( 1 );
		}
		if ( const kv1::CNode *pPosition = block.Find( "positionoverride" ) )
		{
			nested.m_PositionOverride = ParseNumber<int>( Trim( pPosition->GetValue() ) );
		}
		if ( const kv1::CNode *pPosition = block.Find( "ambientpositionoverride" ) )
		{
			nested.m_AmbientPositionOverride = ParseNumber<int>( Trim( pPosition->GetValue() ) );
		}
		Add( *pTarget, nested );
	}

	const SoundscapeLookup_t &m_pfnLookup;
	std::vector<std::string> m_Stack;
	SoundscapePlan_t m_Plan;
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Every looping and random sound a soundscape plays, nested playsoundscapes included,
//          with their volume scaling and position overrides applied.
//-----------------------------------------------------------------------------
SoundscapePlan_t PlanSoundscape( const kv1::CNode &soundscape, const SoundscapeLookup_t &pfnLookup )
{
	CPlanner planner{ pfnLookup };
	planner.Add( soundscape, {} );
	SoundscapePlan_t plan = planner.Take();
	if ( const kv1::CNode *pDsp = soundscape.Find( "dsp" ) )
	{
		plan.m_nDsp = ParseNumber<int>( Trim( pDsp->GetValue() ) ).value_or( 0 );
	}
	return plan;
}

//-----------------------------------------------------------------------------
// Purpose: Pan follows the sound's angle from straight ahead, so one behind is centred too: a
//          stereo preview can't tell front from back.
//-----------------------------------------------------------------------------
PositionMix_t MixAt( std::optional<double> soundLevel, double flRight, double flAhead )
{
	constexpr double NOMINAL_CLIP_DISTANCE = 1000.0;
	PositionMix_t mix;
	const double flDistance = std::hypot( flRight, flAhead );
	if ( soundLevel )
	{
		mix.m_flGain = std::clamp( 1.0 - flDistance * SoundLevelToAttenuation( *soundLevel ) / NOMINAL_CLIP_DISTANCE, 0.0, 1.0 );
	}
	mix.m_flPan = flDistance > 1e-6 ? std::clamp( flRight / flDistance, -1.0, 1.0 ) : 0.0;
	return mix;
}

//-----------------------------------------------------------------------------
// Purpose: Solo wins over ignore, as on a mixing desk: with anything soloed, only soloed rules play.
//-----------------------------------------------------------------------------
void ApplySoloIgnore( SoundscapePlan_t &plan, const std::set<std::size_t> &ignored, const std::set<std::size_t> &soloed )
{
	std::erase_if( plan.m_Rules, [ & ]( const SoundscapeRule_t &rule ) { return soloed.empty() ? ignored.contains( rule.m_nSource ) : !soloed.contains( rule.m_nSource ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CSoundscapeSimulation::CSoundscapeSimulation( std::vector<SoundscapeRule_t> rules, std::uint32_t nSeed )
	: m_Rules( std::move( rules ) ),
	  m_Due( m_Rules.size(), 0 ),
	  m_Random( nSeed )
{}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
PlayRequest_t CSoundscapeSimulation::Request( const SoundscapeRule_t &rule )
{
	Settings_t settings;
	settings.m_Volume = rule.m_Volume;
	settings.m_Pitch = rule.m_Pitch;
	settings.m_Position = rule.m_Position;
	settings.m_bRandomPosition = rule.m_bRandomPosition;
	settings.m_SoundLevel = rule.m_SoundLevel;
	return MakeRequest( rule.m_Waves, settings, rule.m_nKind == SoundscapeRule_t::Kind_t::KIND_LOOPING, rule.m_szOrigin, m_Random );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<PlayRequest_t> CSoundscapeSimulation::Start()
{
	m_flNow = 0;
	std::vector<PlayRequest_t> loops;
	for ( std::size_t i = 0; i < m_Rules.size(); i++ )
	{
		if ( m_Rules[ i ].m_nKind == SoundscapeRule_t::Kind_t::KIND_LOOPING )
		{
			loops.push_back( Request( m_Rules[ i ] ) );
		}
		else
		{
			m_Due[ i ] = m_Rules[ i ].m_Interval.Pick( m_Random );
		}
	}
	return loops;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<PlayRequest_t> CSoundscapeSimulation::Advance( double flSeconds )
{
	m_flNow += std::max( 0.0, flSeconds );
	std::vector<PlayRequest_t> due;
	for ( std::size_t i = 0; i < m_Rules.size(); i++ )
	{
		if ( m_Rules[ i ].m_nKind != SoundscapeRule_t::Kind_t::KIND_RANDOM )
		{
			continue;
		}
		while ( m_Due[ i ] <= m_flNow )
		{
			due.push_back( Request( m_Rules[ i ] ) );
			// A zero interval would fire forever; the engine can't play faster than its frame rate anyway.
			m_Due[ i ] += std::max( 0.05, m_Rules[ i ].m_Interval.Pick( m_Random ) );
		}
	}
	return due;
}

} // namespace xv::sound
