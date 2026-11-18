//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Contents.h. The layouts here were checked against Verd's SourceTV demos: a
//          layout that leaves bits over, or runs out of them, is counted as a misread.
//
//===========================================================================//
#include "Contents.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "Bits.h"

namespace xv::demo
{

namespace
{

constexpr int EVENT_ID_BITS = 9;
constexpr int EVENT_KEY_KIND_BITS = 3;
constexpr int ENTITY_NUMBER_BITS = 11;
constexpr int SOUND_INDEX_BITS = 14;
constexpr int SOUND_FLAG_BITS = 11;
constexpr int SOUND_CHANNEL_BITS = 3;
constexpr int SOUND_SEQUENCE_BITS = 10;
constexpr int SOUND_LEVEL_BITS = 9;
constexpr int SOUND_DELAY_BITS = 13;
// Sound origins are sent in eighths of a unit's multiples of 8, so many bits signed.
constexpr int SOUND_ORIGIN_BITS = 12;
constexpr float SOUND_ORIGIN_SCALE = 8.0f;
constexpr float SOUND_DELAY_OFFSET = 0.1f;
constexpr int SOUND_LEVEL_NORMAL = 75;
constexpr int SOUND_CHANNEL_STATIC = 6;
constexpr int PROGRESS_EVERY = 256;
constexpr std::size_t MISREADS_KEPT = 32;
// Voice packets from one player further apart than this are two times talking.
constexpr double SPEECH_GAP_SECONDS = 0.4;
constexpr double FALLBACK_TICK_INTERVAL = 0.015;
// As fast as anything moves (units a second), and some slack: further than that between two
// snapshots and the entity was put there, not moved.
constexpr double FASTEST_SPEED = 3500.0;
constexpr double TELEPORT_SLACK = 16.0;
constexpr int MENU_DEPTH = 32;

// A binary KeyValues tree's value kinds.
enum KeyKind_t
{
	KEY_KIND_NONE,
	KEY_KIND_STRING,
	KEY_KIND_INT,
	KEY_KIND_FLOAT,
	KEY_KIND_POINTER,
	KEY_KIND_WIDE_STRING,
	KEY_KIND_COLOR,
	KEY_KIND_UINT64,
	KEY_KIND_END,
};

//-----------------------------------------------------------------------------
// User message layouts
//-----------------------------------------------------------------------------

enum UserFieldKind_t
{
	USER_FIELD_BYTE,
	USER_FIELD_CHAR,
	USER_FIELD_SHORT,
	USER_FIELD_WORD,
	USER_FIELD_LONG,
	USER_FIELD_FLOAT,
	USER_FIELD_STRING,
	USER_FIELD_STRING_OPTIONAL, // there when bits are left
	USER_FIELD_STRINGS,         // a byte saying how many, then that many strings
	USER_FIELD_PAIRS,           // a byte saying how many, then that many name and value strings
};

struct UserFieldLayout_t
{
	std::string_view m_szName;
	UserFieldKind_t m_nKind = USER_FIELD_BYTE;
};

struct UserLayout_t
{
	std::string_view m_szName;
	std::vector<UserFieldLayout_t> m_Fields; // empty with m_bKnown false: not decoded
	bool m_bKnown = true;
};

//-----------------------------------------------------------------------------
// Purpose: Counter-Strike: Source's user messages, by the numbers the game registers them in
//          (the order is the game's, which the demo doesn't carry). The layouts are those of
//          the messages a demo is worth reading for: chat, prints, the HUD's texts and menus,
//          the screen's fades and shakes. Their order of fields was settled by reading Verd's
//          demos to the last bit, which also bears out the numbers of the kinds read (Train,
//          SayText2 to HudMsg, ResetHUD, ShowMenu, Fade to Rumble, SendAudio, RadioText to
//          KeyHintText). The names of the rest are the game's order as known, not checked;
//          the game has more than these (45 and 46 turn up), which go unnamed.
//-----------------------------------------------------------------------------
const std::vector<UserLayout_t> &CounterStrikeLayouts()
{
	static const std::vector<UserLayout_t> LAYOUTS = {
		{ "Geiger", { { "range", USER_FIELD_BYTE } } },
		{ "Train", { { "position", USER_FIELD_BYTE } } },
		{ "HudText", { { "text", USER_FIELD_STRING } } },
		{ "SayText", { { "entity", USER_FIELD_BYTE }, { "text", USER_FIELD_STRING }, { "chat", USER_FIELD_BYTE } } },
		{ "SayText2", { { "entity", USER_FIELD_BYTE }, { "chat", USER_FIELD_BYTE }, { "text", USER_FIELD_STRING }, { "param1", USER_FIELD_STRING_OPTIONAL }, { "param2", USER_FIELD_STRING_OPTIONAL }, { "param3", USER_FIELD_STRING_OPTIONAL }, { "param4", USER_FIELD_STRING_OPTIONAL } } },
		{ "TextMsg", { { "destination", USER_FIELD_BYTE }, { "text", USER_FIELD_STRING }, { "param1", USER_FIELD_STRING_OPTIONAL }, { "param2", USER_FIELD_STRING_OPTIONAL }, { "param3", USER_FIELD_STRING_OPTIONAL }, { "param4", USER_FIELD_STRING_OPTIONAL } } },
		{ "HudMsg", { { "channel", USER_FIELD_BYTE }, { "x", USER_FIELD_FLOAT }, { "y", USER_FIELD_FLOAT }, { "r1", USER_FIELD_BYTE }, { "g1", USER_FIELD_BYTE }, { "b1", USER_FIELD_BYTE }, { "a1", USER_FIELD_BYTE }, { "r2", USER_FIELD_BYTE }, { "g2", USER_FIELD_BYTE }, { "b2", USER_FIELD_BYTE }, { "a2", USER_FIELD_BYTE }, { "effect", USER_FIELD_BYTE }, { "fadein", USER_FIELD_FLOAT }, { "fadeout", USER_FIELD_FLOAT }, { "holdtime", USER_FIELD_FLOAT }, { "fxtime", USER_FIELD_FLOAT }, { "text", USER_FIELD_STRING } } },
		{ "ResetHUD", { { "reset", USER_FIELD_BYTE } } },
		{ "GameTitle", {} },
		{ "ItemPickup", { { "item", USER_FIELD_STRING } } },
		{ "ShowMenu", { { "slots", USER_FIELD_WORD }, { "time", USER_FIELD_CHAR }, { "more", USER_FIELD_BYTE }, { "text", USER_FIELD_STRING } } },
		{ "Shake", { { "command", USER_FIELD_BYTE }, { "amplitude", USER_FIELD_FLOAT }, { "frequency", USER_FIELD_FLOAT }, { "duration", USER_FIELD_FLOAT } } },
		{ "Fade", { { "duration", USER_FIELD_WORD }, { "hold", USER_FIELD_WORD }, { "flags", USER_FIELD_WORD }, { "r", USER_FIELD_BYTE }, { "g", USER_FIELD_BYTE }, { "b", USER_FIELD_BYTE }, { "a", USER_FIELD_BYTE } } },
		{ "VGUIMenu", { { "name", USER_FIELD_STRING }, { "show", USER_FIELD_BYTE }, { "key", USER_FIELD_PAIRS } } },
		{ "Rumble", { { "index", USER_FIELD_BYTE }, { "data", USER_FIELD_BYTE }, { "flags", USER_FIELD_BYTE } } },
		{ "CloseCaption", {}, false },
		{ "SendAudio", { { "sound", USER_FIELD_STRING } } },
		{ "RawAudio", {}, false },
		{ "VoiceMask", {}, false },
		{ "RequestState", {} },
		{ "BarTime", { { "duration", USER_FIELD_SHORT } } },
		{ "Damage", {}, false },
		{ "RadioText", { { "destination", USER_FIELD_BYTE }, { "entity", USER_FIELD_BYTE }, { "text", USER_FIELD_STRING }, { "param1", USER_FIELD_STRING_OPTIONAL }, { "param2", USER_FIELD_STRING_OPTIONAL }, { "param3", USER_FIELD_STRING_OPTIONAL }, { "param4", USER_FIELD_STRING_OPTIONAL } } },
		{ "HintText", { { "text", USER_FIELD_STRING } } },
		{ "KeyHintText", { { "text", USER_FIELD_STRINGS } } },
		{ "ReloadEffect", {}, false },
		{ "PlayerAnimEvent", {}, false },
		{ "AmmoDenied", {}, false },
		{ "UpdateRadar", {}, false },
		{ "KillCam", {}, false },
		{ "MarkAchievement", {}, false },
		{ "CallVoteFailed", {}, false },
		{ "VoteStart", {}, false },
		{ "VotePass", {}, false },
		{ "VoteFailed", {}, false },
		{ "VoteSetup", {}, false },
		{ "SPHapWeapEvent", {}, false },
		{ "HapDmg", {}, false },
		{ "HapPunch", {}, false },
		{ "HapSetDrag", {}, false },
		{ "HapSetConst", {}, false },
		{ "HapMeleeContact", {}, false },
	};
	return LAYOUTS;
}

//-----------------------------------------------------------------------------
// Purpose: The layout for a user message number in a game; null for one not known.
//-----------------------------------------------------------------------------
const UserLayout_t *FindUserLayout( std::string_view szGameDir, int nKind )
{
	if ( szGameDir != "cstrike" )
	{
		return nullptr;
	}
	const std::vector<UserLayout_t> &layouts = CounterStrikeLayouts();
	return nKind >= 0 && static_cast<std::size_t>( nKind ) < layouts.size() ? &layouts[ static_cast<std::size_t>( nKind ) ] : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: A user message's fields by its layout; false when the bits don't fit it.
//-----------------------------------------------------------------------------
bool ReadUserFields( const UserLayout_t &layout, CBitReader &reader, std::vector<UserField_t> &fields )
{
	const auto number = [ & ]( std::string_view szName, double flValue ) {
		UserField_t field;
		field.m_szName = szName;
		field.m_Value.m_flNumber = flValue;
		fields.push_back( std::move( field ) );
	};
	const auto text = [ & ]( std::string_view szName, std::string szText ) {
		UserField_t field;
		field.m_szName = szName;
		field.m_Value.m_szText = std::move( szText );
		field.m_bText = true;
		fields.push_back( std::move( field ) );
	};
	for ( const UserFieldLayout_t &field : layout.m_Fields )
	{
		switch ( field.m_nKind )
		{
			case USER_FIELD_BYTE:
				number( field.m_szName, reader.ReadBits( 8 ) );
				break;
			case USER_FIELD_CHAR:
				number( field.m_szName, reader.ReadSignedBits( 8 ) );
				break;
			case USER_FIELD_SHORT:
				number( field.m_szName, reader.ReadSignedBits( 16 ) );
				break;
			case USER_FIELD_WORD:
				number( field.m_szName, reader.ReadBits( 16 ) );
				break;
			case USER_FIELD_LONG:
				number( field.m_szName, static_cast<std::int32_t>( reader.ReadBits( 32 ) ) );
				break;
			case USER_FIELD_FLOAT:
				number( field.m_szName, reader.ReadFloat() );
				break;
			case USER_FIELD_STRING:
				text( field.m_szName, reader.ReadString() );
				break;
			case USER_FIELD_STRING_OPTIONAL:
				if ( reader.GetLeft() >= 8 )
				{
					text( field.m_szName, reader.ReadString() );
				}
				break;
			case USER_FIELD_STRINGS:
			{
				const int nCount = static_cast<int>( reader.ReadBits( 8 ) );
				for ( int i = 0; i < nCount && !reader.IsOverflowed(); i++ )
				{
					text( field.m_szName, reader.ReadString() );
				}
				break;
			}
			case USER_FIELD_PAIRS:
			{
				const int nCount = static_cast<int>( reader.ReadBits( 8 ) );
				for ( int i = 0; i < nCount && !reader.IsOverflowed(); i++ )
				{
					std::string szName = reader.ReadString();
					UserField_t pair;
					pair.m_szName = field.m_szName;
					pair.m_Value.m_szText = reader.ReadString();
					pair.m_Value.m_flNumber = 0.0;
					pair.m_bText = true;
					// The pair's name goes first, as a field of its own named "name".
					text( "name", std::move( szName ) );
					fields.push_back( std::move( pair ) );
				}
				break;
			}
		}
		if ( reader.IsOverflowed() )
		{
			return false;
		}
	}
	return !reader.IsOverflowed() && reader.GetLeft() == 0;
}

//-----------------------------------------------------------------------------
// Purpose: Bytes from a buffer, little-endian, with a flag for running out.
//-----------------------------------------------------------------------------
class CByteReader
{
public:
	explicit CByteReader( std::span<const std::byte> bytes )
		: m_Bytes( bytes )
	{}

	[[nodiscard]] bool IsOverflowed() const { return m_bOverflow; }
	[[nodiscard]] std::size_t GetLeft() const { return m_Bytes.size() - m_nAt; }
	[[nodiscard]] std::size_t GetAt() const { return m_nAt; }

	[[nodiscard]] std::uint64_t ReadNumber( int nBytes )
	{
		if ( GetLeft() < static_cast<std::size_t>( nBytes ) )
		{
			m_bOverflow = true;
			m_nAt = m_Bytes.size();
			return 0;
		}
		std::uint64_t nValue = 0;
		for ( int i = 0; i < nBytes; i++ )
		{
			nValue |= static_cast<std::uint64_t>( static_cast<std::uint8_t>( m_Bytes[ m_nAt + static_cast<std::size_t>( i ) ] ) ) << ( 8 * i );
		}
		m_nAt += static_cast<std::size_t>( nBytes );
		return nValue;
	}

	[[nodiscard]] std::string ReadString()
	{
		std::string szText;
		while ( !m_bOverflow )
		{
			const char ch = static_cast<char>( ReadNumber( 1 ) );
			if ( ch == '\0' )
			{
				break;
			}
			szText.push_back( ch );
		}
		return szText;
	}

	[[nodiscard]] std::span<const std::byte> ReadSpan( std::size_t nCount )
	{
		if ( GetLeft() < nCount )
		{
			m_bOverflow = true;
			m_nAt = m_Bytes.size();
			return {};
		}
		const std::span<const std::byte> part = m_Bytes.subspan( m_nAt, nCount );
		m_nAt += nCount;
		return part;
	}

private:
	std::span<const std::byte> m_Bytes;
	std::size_t m_nAt = 0;
	bool m_bOverflow = false;
};

//-----------------------------------------------------------------------------
// Purpose: A binary KeyValues block: each key's kind, its name, then its value (a key with
//          children has a block of its own), until the end mark.
//-----------------------------------------------------------------------------
bool ReadBinaryKeys( CByteReader &reader, std::vector<MenuKey_t> &keys, int nDepth )
{
	if ( nDepth > MENU_DEPTH )
	{
		return false;
	}
	for ( ;; )
	{
		const int nKind = static_cast<int>( reader.ReadNumber( 1 ) );
		if ( reader.IsOverflowed() )
		{
			return false;
		}
		if ( nKind == KEY_KIND_END )
		{
			return true;
		}
		MenuKey_t key;
		key.m_szName = reader.ReadString();
		switch ( nKind )
		{
			case KEY_KIND_NONE:
				if ( !ReadBinaryKeys( reader, key.m_Children, nDepth + 1 ) )
				{
					return false;
				}
				break;
			case KEY_KIND_STRING:
				key.m_szValue = reader.ReadString();
				break;
			case KEY_KIND_INT:
			case KEY_KIND_POINTER:
				key.m_szValue = std::to_string( static_cast<std::int32_t>( reader.ReadNumber( 4 ) ) );
				break;
			case KEY_KIND_FLOAT:
			{
				const std::uint32_t nBits = static_cast<std::uint32_t>( reader.ReadNumber( 4 ) );
				float flValue = 0.0f;
				std::memcpy( &flValue, &nBits, sizeof( flValue ) );
				key.m_szValue = std::to_string( flValue );
				break;
			}
			case KEY_KIND_COLOR:
			{
				const std::uint64_t nColor = reader.ReadNumber( 4 );
				key.m_szValue = std::to_string( nColor & 0xff ) + " " + std::to_string( ( nColor >> 8 ) & 0xff ) + " " + std::to_string( ( nColor >> 16 ) & 0xff ) + " " + std::to_string( nColor >> 24 );
				break;
			}
			case KEY_KIND_UINT64:
				key.m_szValue = std::to_string( reader.ReadNumber( 8 ) );
				break;
			default:
				// Wide strings aren't written by the engine's own writer.
				return false;
		}
		if ( reader.IsOverflowed() )
		{
			return false;
		}
		keys.push_back( std::move( key ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The standard CRC-32 (zlib's), as Steam's voice packets end with.
//-----------------------------------------------------------------------------
std::uint32_t Crc32( std::span<const std::byte> bytes )
{
	static const std::array<std::uint32_t, 256> TABLE = [] {
		std::array<std::uint32_t, 256> table{};
		for ( std::uint32_t i = 0; i < 256; i++ )
		{
			std::uint32_t nValue = i;
			for ( int nBit = 0; nBit < 8; nBit++ )
			{
				nValue = ( nValue & 1 ) ? 0xedb88320u ^ ( nValue >> 1 ) : nValue >> 1;
			}
			table[ i ] = nValue;
		}
		return table;
	}();
	std::uint32_t nCrc = 0xffffffffu;
	for ( const std::byte b : bytes )
	{
		nCrc = TABLE[ ( nCrc ^ static_cast<std::uint8_t>( b ) ) & 0xff ] ^ ( nCrc >> 8 );
	}
	return nCrc ^ 0xffffffffu;
}

//-----------------------------------------------------------------------------
// Purpose: An angle a to b by the short way round.
//-----------------------------------------------------------------------------
float LerpAngle( float flFrom, float flTo, float flPart )
{
	return flFrom + std::remainder( flTo - flFrom, 360.0f ) * flPart;
}

// The slots CNetworkState keeps for an entity: one per field but ENTITY_FIELD_THERE, then the
// class's stats three to a slot.
constexpr int ENTITY_SLOT_COUNT = ENTITY_FIELD_THERE;
constexpr int STAT_MOST = 360; // the player resource's five arrays of 65 are the most
constexpr int STAT_SLOT_MOST = ( STAT_MOST + 2 ) / 3;

// The player resource's arrays (one element per player) and the single values of the other
// classes that EntityStats follows.
constexpr std::string_view STAT_ARRAYS[] = { "m_iPing", "m_iScore", "m_iDeaths", "m_iMVPs", "m_bHasDefuser", "m_iAmmo", "m_hControlPointEnts" };
constexpr std::string_view STAT_SINGLES[] = {
	// A player.
	"m_ArmorValue",
	"m_bHasHelmet",
	"m_iAccount",
	"m_bHasDefuser",
	"m_flFlashDuration",
	"m_flFlashMaxAlpha",
	"m_iObserverMode",
	"m_bIsScoped",
	"m_hObserverTarget",
	// A weapon.
	"m_iClip1",
	"m_iPrimaryAmmoType",
	"m_iState",
	// A recorder's view model.
	"m_nViewModelIndex",
	"m_nAnimationParity",
	"m_hOwner",
	// The game rules.
	"m_bFreezePeriod",
	"m_iRoundTime",
	"m_fRoundStartTime",
	"m_flGameStartTime",
	"m_bBombPlanted",
	// A team.
	"m_iTeamNum",
	"m_iScore",
	// An animating prop (a prop_dynamic): the game works its cycle out from when its
	// sequence (or the parity that starts it again) last changed, at its playback rate; the
	// cycle itself is sent only for one the server animates.
	"m_flPlaybackRate",
	"m_nNewSequenceParity",
	"m_flCycle",
	// A planted bomb.
	"m_flC4Blow",
	"m_bBombTicking",
	// An env_fog_controller.
	"m_fog.enable",
	"m_fog.blend",
	"m_fog.radial",
	"m_fog.colorPrimary",
	"m_fog.colorSecondary",
	"m_fog.start",
	"m_fog.end",
	"m_fog.maxdensity",
	// A player's own data (only the player's own demo has it): the fog controller it sees, and
	// the 3D skybox's camera (the sky_camera it was given) and fog.
	"m_PlayerFog.m_hCtrl",
	"m_skybox3d.scale",
	"m_skybox3d.area",
	"m_skybox3d.fog.enable",
	"m_skybox3d.fog.blend",
	"m_skybox3d.fog.radial",
	"m_skybox3d.fog.colorPrimary",
	"m_skybox3d.fog.colorSecondary",
	"m_skybox3d.fog.start",
	"m_skybox3d.fog.end",
	"m_skybox3d.fog.maxdensity",
};
// Vectors kept as stats, three in a row ("[0]" to "[2]" after the name), starting a slot.
constexpr std::string_view STAT_VECTORS[] = { "m_fog.dirPrimary", "m_skybox3d.origin", "m_skybox3d.fog.dirPrimary" };

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::array<std::string, ENTITY_EXTRA_COUNT> EntityExtras( std::string_view, std::span<const FlatProp_t> props )
{
	const auto has = [ & ]( std::string_view szTable ) { return std::ranges::any_of( props, [ & ]( const FlatProp_t &prop ) { return prop.m_szTable == szTable; } ); };
	const auto hasProp = [ & ]( std::string_view szName ) { return std::ranges::any_of( props, [ & ]( const FlatProp_t &prop ) { return prop.m_szName == szName; } ); };
	if ( has( "DT_BaseViewModel" ) || has( "DT_PredictedViewModel" ) )
	{
		return { "m_nSequence", "m_nSkin", "m_nBody" };
	}
	if ( has( "DT_ParticleSystem" ) )
	{
		return { "m_bActive", "m_iEffectIndex", "m_flStartTime" };
	}
	if ( has( "DT_Sprite" ) )
	{
		return { hasProp( "m_flBrightness" ) ? "m_flBrightness" : "m_nBrightness", "m_flSpriteScale", "m_flFrame" };
	}
	// A weapon: its model is what its holder sees (the view model) while held; the world
	// model is what others see in its hand.
	if ( has( "DT_BaseCombatWeapon" ) && hasProp( "m_iWorldModelIndex" ) )
	{
		return { "m_iWorldModelIndex", "m_nSkin", "m_nBody" };
	}
	if ( has( "DT_BaseAnimating" ) && !has( "DT_BasePlayer" ) )
	{
		return { "m_nSequence", "m_nSkin", "m_nBody" };
	}
	// A player: the weapon it holds (a handle), and the skin its model wears.
	if ( has( "DT_BasePlayer" ) && hasProp( "m_hActiveWeapon" ) )
	{
		return { "m_hActiveWeapon", "m_nSkin", "" };
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: An array's element ("000" in the table "m_iScore") by its table and index.
//-----------------------------------------------------------------------------
std::string StatName( const FlatProp_t &prop )
{
	const bool bElement = !prop.m_szName.empty() && std::ranges::all_of( prop.m_szName, []( char ch ) { return ch >= '0' && ch <= '9'; } );
	return bElement ? prop.m_szTable + "." + prop.m_szName : prop.m_szName;
}

//-----------------------------------------------------------------------------
// Purpose: The listed singles a class has, then the listed arrays' elements; numbers only.
//-----------------------------------------------------------------------------
std::vector<std::string> EntityStats( std::span<const FlatProp_t> props )
{
	std::vector<std::string> stats;
	const bool bViewModel = std::ranges::any_of( props, []( const FlatProp_t &field ) { return field.m_szName == "m_nViewModelIndex"; } );
	const auto keepable = []( const FlatProp_t &prop ) { return prop.m_nKind == PropKind_t::PROP_KIND_INT || prop.m_nKind == PropKind_t::PROP_KIND_FLOAT; };
	for ( const FlatProp_t &prop : props )
	{
		// Every entity has a team number: only a team's is wanted.
		const bool bTeamOnly = prop.m_szName == "m_iTeamNum" && prop.m_szTable != "DT_Team";
		const bool bOtherOwner = !bViewModel && prop.m_szName == "m_hOwner";
		if ( keepable( prop ) && !bTeamOnly && !bOtherOwner && std::ranges::find( STAT_SINGLES, std::string_view{ prop.m_szName } ) != std::end( STAT_SINGLES ) && std::ranges::find( stats, prop.m_szName ) == stats.end() )
		{
			stats.push_back( prop.m_szName );
		}
	}
	for ( const FlatProp_t &prop : props )
	{
		if ( prop.m_nKind == PropKind_t::PROP_KIND_VECTOR && std::ranges::find( STAT_VECTORS, std::string_view{ prop.m_szName } ) != std::end( STAT_VECTORS ) && std::ranges::find( stats, prop.m_szName + "[0]" ) == stats.end() )
		{
			while ( stats.size() % 3 != 0 )
			{
				stats.emplace_back();
			}
			for ( const char *pszPart : { "[0]", "[1]", "[2]" } )
			{
				stats.push_back( prop.m_szName + pszPart );
			}
		}
	}
	for ( const FlatProp_t &prop : props )
	{
		if ( keepable( prop ) && std::ranges::find( STAT_ARRAYS, std::string_view{ prop.m_szTable } ) != std::end( STAT_ARRAYS ) && static_cast<int>( stats.size() ) < STAT_MOST )
		{
			std::string szName = StatName( prop );
			if ( szName != prop.m_szName && std::ranges::find( stats, szName ) == stats.end() )
			{
				stats.push_back( std::move( szName ) );
			}
		}
	}
	return stats;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<float> EntityLife_t::StatAt( int nStat, int nTick ) const
{
	if ( nStat < 0 || static_cast<std::size_t>( nStat ) >= m_Stats.size() )
	{
		return std::nullopt;
	}
	const std::vector<StatKey_t> &keys = m_Stats[ static_cast<std::size_t>( nStat ) ];
	const auto after = std::upper_bound( keys.begin(), keys.end(), nTick, []( int nAt, const StatKey_t &key ) { return nAt < key.m_nTick; } );
	return after == keys.begin() ? std::nullopt : std::optional<float>( ( after - 1 )->m_flValue );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::array<float, 3> EntityLife_t::ValueAt( EntityField_t nField, int nTick ) const
{
	const std::vector<EntityKey_t> &keys = m_Fields[ nField ];
	const auto after = std::upper_bound( keys.begin(), keys.end(), nTick, []( int nAt, const EntityKey_t &key ) { return nAt < key.m_nTick; } );
	return after == keys.begin() ? std::array<float, 3>{} : ( after - 1 )->m_flValue;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool EntityLife_t::IsThere( int nTick ) const
{
	return nTick >= m_nFrom && ( m_nTo < 0 || nTick < m_nTo ) && ValueAt( ENTITY_FIELD_THERE, nTick )[ 0 ] > 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: The key that took it out of sight, and the tick before it.
//-----------------------------------------------------------------------------
std::optional<int> EntityLife_t::LastSeen( int nTick ) const
{
	if ( nTick < m_nFrom || ( m_nTo >= 0 && nTick >= m_nTo ) )
		return std::nullopt;
	const std::vector<EntityKey_t> &keys = m_Fields[ ENTITY_FIELD_THERE ];
	const auto after = std::upper_bound( keys.begin(), keys.end(), nTick, []( int nAt, const EntityKey_t &key ) { return nAt < key.m_nTick; } );
	if ( after == keys.begin() || std::prev( after )->m_flValue[ 0 ] >= 0.0f )
		return std::nullopt;
	return std::max( m_nFrom, std::prev( after )->m_nTick - 1 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const Value_t *UserMessage_t::Find( std::string_view szName ) const
{
	const auto found = std::ranges::find( m_Fields, szName, &UserField_t::m_szName );
	return found == m_Fields.end() ? nullptr : &found->m_Value;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view UserMessageName( std::string_view szGameDir, int nKind )
{
	const UserLayout_t *pLayout = FindUserLayout( szGameDir, nKind );
	return pLayout ? pLayout->m_szName : std::string_view{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const MenuKey_t *MenuKey_t::Find( std::string_view szName ) const
{
	const auto found = std::ranges::find( m_Children, szName, &MenuKey_t::m_szName );
	return found == m_Children.end() ? nullptr : &*found;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view MenuKindName( int nKind )
{
	static constexpr std::string_view NAMES[] = { "message", "menu", "text", "entry", "ask connect" };
	return nKind >= 0 && nKind < static_cast<int>( std::size( NAMES ) ) ? NAMES[ nKind ] : std::string_view( "unknown" );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view MessageKindName( int nKind )
{
	static constexpr std::string_view NAMES[] = {
		"net_NOP",
		"net_Disconnect",
		"net_File",
		"net_Tick",
		"net_StringCmd",
		"net_SetConVar",
		"net_SignonState",
		"svc_Print",
		"svc_ServerInfo",
		"svc_SendTable",
		"svc_ClassInfo",
		"svc_SetPause",
		"svc_CreateStringTable",
		"svc_UpdateStringTable",
		"svc_VoiceInit",
		"svc_VoiceData",
		"",
		"svc_Sounds",
		"svc_SetView",
		"svc_FixAngle",
		"svc_CrosshairAngle",
		"svc_BSPDecal",
		"",
		"svc_UserMessage",
		"svc_EntityMessage",
		"svc_GameEvent",
		"svc_PacketEntities",
		"svc_TempEntities",
		"svc_Prefetch",
		"svc_Menu",
		"svc_GameEventList",
		"svc_GetCvarValue",
		"svc_CmdKeyValues",
		"svc_SetPauseTimed",
	};
	return nKind >= 0 && nKind < static_cast<int>( std::size( NAMES ) ) ? NAMES[ nKind ] : std::string_view{};
}

//-----------------------------------------------------------------------------
// Purpose: The tagged parts after the Steam id, up to the CRC: the sample rate (two bytes),
//          silence (two bytes: how many samples), or audio (two bytes of length, then it).
//          Only the tags Verd's demos have are taken; any other makes the packet unreadable
//          rather than guessed at.
//-----------------------------------------------------------------------------
std::optional<SteamVoice_t> ParseSteamVoice( std::span<const std::byte> packet )
{
	if ( packet.size() < 8 + 4 )
	{
		return std::nullopt;
	}
	SteamVoice_t voice;
	const std::span<const std::byte> body = packet.first( packet.size() - 4 );
	CByteReader reader( body );
	voice.m_nSteamId = reader.ReadNumber( 8 );
	while ( reader.GetLeft() > 0 )
	{
		const int nTag = static_cast<int>( reader.ReadNumber( 1 ) );
		switch ( nTag )
		{
			case STEAM_VOICE_SAMPLE_RATE:
				voice.m_nSampleRate = static_cast<int>( reader.ReadNumber( 2 ) );
				break;
			case STEAM_VOICE_SILENCE:
				voice.m_nSilence += static_cast<int>( reader.ReadNumber( 2 ) );
				break;
			case STEAM_VOICE_OPUS_PLC:
			{
				const std::size_t nBytes = static_cast<std::size_t>( reader.ReadNumber( 2 ) );
				voice.m_nCodec = nTag;
				voice.m_Audio = reader.ReadSpan( nBytes );
				break;
			}
			default:
				return std::nullopt;
		}
		if ( reader.IsOverflowed() )
		{
			return std::nullopt;
		}
	}
	std::uint32_t nCrc = 0;
	std::memcpy( &nCrc, packet.data() + body.size(), 4 );
	voice.m_bChecked = Crc32( body ) == nCrc;
	return voice;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view SteamVoiceCodecName( int nCodec )
{
	switch ( nCodec )
	{
		case STEAM_VOICE_OPUS_PLC:
			return "Opus";
		case -1:
			return "none";
		default:
			return "unknown";
	}
}

//-----------------------------------------------------------------------------
// Purpose: Reads a demo into a CContents: the network state's messages as they come, and the
//          entities after each frame.
//-----------------------------------------------------------------------------
class CContentsReader final : public IMessageListener
{
public:
	explicit CContentsReader( CContents &contents );

	void Read( const CDemo &demo, std::span<const std::byte> file, const std::function<bool( double )> &pfnProgress );

	void OnMessage( int nKind ) override { m_Contents.m_MessageCounts[ nKind ]++; }
	void OnPrint( const std::string &szText ) override { m_Contents.m_Prints.emplace_back( m_nTick, szText ); }
	void OnStringCommand( const std::string &szText ) override { m_Contents.m_ServerCommands.emplace_back( m_nTick, szText ); }
	void OnVoiceInit( const std::string &szCodec, int nQuality, int nSampleRate ) override;
	void OnVoiceData( int nClient, bool bProximity, CBitReader data ) override;
	void OnSounds( bool bReliable, int nCount, CBitReader data ) override;
	void OnUserMessage( int nKind, CBitReader data ) override;
	void OnEntityMessage( int nEntity, int nClass, CBitReader data ) override;
	void OnGameEventList( int nCount, CBitReader data ) override;
	void OnGameEvent( CBitReader data ) override;
	void OnTempEntities( int nCount, CBitReader data ) override;
	void OnMenu( int nKind, std::span<const std::byte> data ) override;

private:
	std::vector<int> Watch( const ServerClass_t &serverClass, std::span<const FlatProp_t> props );
	void Misread( std::string szWhat );
	void NoteEntities( int nTick );
	void NoteKey( EntityLife_t &life, EntityField_t nField, const std::array<float, 3> &value, int nTick );
	void Finish( const CDemo &demo );

	CContents &m_Contents;
	CNetworkState m_State;
	int m_nTick = 0;
	int m_nLastSnapshot = 0; // the tick of the snapshot before this one
	std::uint64_t m_nSnapshots = 0;
	std::vector<int> m_EventByKindId;
	std::vector<std::vector<EntityLife_t>> m_LivesByEntity;
	std::vector<std::uint8_t> m_Seen; // per entity, this frame: touched already
	std::vector<std::vector<char>> m_StatColors; // by class, by stat: a colour (24 bits kept)
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CContentsReader::CContentsReader( CContents &contents )
	: m_Contents( contents ),
	  m_State( [ this ]( const ServerClass_t &serverClass, std::span<const FlatProp_t> props ) { return Watch( serverClass, props ); }, ENTITY_SLOT_COUNT, ENTITY_SLOT_COUNT + STAT_SLOT_MOST ),
	  m_EventByKindId( std::size_t{ 1 } << EVENT_ID_BITS, -1 ),
	  m_LivesByEntity( MAX_ENTITIES ),
	  m_Seen( MAX_ENTITIES, 0 )
{
	m_State.SetListener( this );
}

//-----------------------------------------------------------------------------
// Purpose: Where each followed property of a class goes. The place and angles, model, effects,
//          render settings, colour and parent are any entity's (DT_BaseEntity's); a player's
//          angles are its eyes'; the EXTRA fields are the class's own.
//-----------------------------------------------------------------------------
std::vector<int> CContentsReader::Watch( const ServerClass_t &serverClass, std::span<const FlatProp_t> props )
{
	const std::array<std::string, ENTITY_EXTRA_COUNT> extras = EntityExtras( serverClass.m_szName, props );
	const int nClass = m_Contents.FindClass( serverClass.m_szName );
	if ( nClass >= 0 )
	{
		if ( m_Contents.m_Extras.size() <= static_cast<std::size_t>( nClass ) )
		{
			m_Contents.m_Extras.resize( static_cast<std::size_t>( nClass ) + 1 );
		}
		m_Contents.m_Extras[ static_cast<std::size_t>( nClass ) ] = extras;
		if ( m_Contents.m_Stats.size() <= static_cast<std::size_t>( nClass ) )
		{
			m_Contents.m_Stats.resize( static_cast<std::size_t>( nClass ) + 1 );
		}
		m_Contents.m_Stats[ static_cast<std::size_t>( nClass ) ] = EntityStats( props );
		// Colours are 32 bits, more than a float holds whole: their red, green and blue kept.
		std::vector<char> colors;
		for ( const std::string &szStat : m_Contents.m_Stats[ static_cast<std::size_t>( nClass ) ] )
		{
			colors.push_back( szStat.find( "color" ) != std::string::npos ? 1 : 0 );
		}
		if ( m_StatColors.size() <= static_cast<std::size_t>( nClass ) )
		{
			m_StatColors.resize( static_cast<std::size_t>( nClass ) + 1 );
		}
		m_StatColors[ static_cast<std::size_t>( nClass ) ] = std::move( colors );
	}
	const std::vector<std::string> noStats;
	const std::vector<std::string> &stats = nClass >= 0 ? m_Contents.m_Stats[ static_cast<std::size_t>( nClass ) ] : noStats;
	const bool bPlayer = std::ranges::any_of( props, []( const FlatProp_t &prop ) { return prop.m_szTable == "DT_BasePlayer"; } );
	std::vector<int> watch( props.size(), -1 );
	for ( std::size_t i = 0; i < props.size(); i++ )
	{
		const FlatProp_t &prop = props[ i ];
		const std::string &szName = prop.m_szName;
		const bool bVector = prop.m_nKind == PropKind_t::PROP_KIND_VECTOR || prop.m_nKind == PropKind_t::PROP_KIND_VECTOR_XY;
		int nCode = -1;
		if ( szName == "m_vecOrigin" && bVector )
		{
			nCode = ENTITY_FIELD_ORIGIN * 4;
		}
		else if ( szName == "m_vecOrigin[2]" && prop.m_nKind == PropKind_t::PROP_KIND_FLOAT )
		{
			nCode = ENTITY_FIELD_ORIGIN * 4 + 2;
		}
		else if ( !bPlayer && szName == "m_angRotation" && bVector )
		{
			nCode = ENTITY_FIELD_ANGLES * 4;
		}
		else if ( bPlayer && ( szName == "m_angEyeAngles[0]" || ( szName == "m_angEyeAngles" && bVector ) ) )
		{
			nCode = ENTITY_FIELD_ANGLES * 4;
		}
		else if ( bPlayer && szName == "m_angEyeAngles[1]" )
		{
			nCode = ENTITY_FIELD_ANGLES * 4 + 1;
		}
		else if ( szName == "m_nModelIndex" )
		{
			nCode = ENTITY_FIELD_MODEL * 4;
		}
		else if ( szName == "m_fEffects" )
		{
			nCode = ENTITY_FIELD_EFFECTS * 4;
		}
		else if ( szName == "m_nRenderMode" )
		{
			nCode = ENTITY_FIELD_RENDER * 4;
		}
		else if ( szName == "m_nRenderFX" )
		{
			nCode = ENTITY_FIELD_RENDER * 4 + 1;
		}
		else if ( szName == "m_clrRender" )
		{
			nCode = ENTITY_FIELD_COLOR * 4;
		}
		else if ( szName == "moveparent" )
		{
			nCode = ENTITY_FIELD_PARENT * 4;
		}
		else if ( szName == "m_iParentAttachment" )
		{
			nCode = ENTITY_FIELD_PARENT * 4 + 1;
		}
		else
		{
			for ( int n = 0; n < ENTITY_EXTRA_COUNT; n++ )
			{
				if ( !extras[ static_cast<std::size_t>( n ) ].empty() && szName == extras[ static_cast<std::size_t>( n ) ] && !bVector )
				{
					nCode = ( ENTITY_FIELD_EXTRA_0 + n ) * 4;
				}
			}
		}
		// A vector kept as stats fills its three, from the first.
		if ( nCode < 0 && bVector && prop.m_nKind == PropKind_t::PROP_KIND_VECTOR )
		{
			const auto found = std::ranges::find( stats, szName + "[0]" );
			if ( found != stats.end() )
			{
				const int nStat = static_cast<int>( found - stats.begin() );
				nCode = ( ENTITY_SLOT_COUNT + nStat / 3 ) * 4;
			}
		}
		// A property both an EXTRA field and a stat (a player's m_bHasDefuser is only a stat)
		// is kept as the field: a property has one place.
		if ( nCode < 0 && !stats.empty() && !bVector )
		{
			const auto found = std::ranges::find( stats, StatName( prop ) );
			if ( found != stats.end() )
			{
				const int nStat = static_cast<int>( found - stats.begin() );
				nCode = ( ENTITY_SLOT_COUNT + nStat / 3 ) * 4 + nStat % 3;
			}
		}
		watch[ i ] = nCode;
	}
	return watch;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CContentsReader::Misread( std::string szWhat )
{
	m_Contents.m_nMisreads++;
	if ( m_Contents.m_Misreads.size() < MISREADS_KEPT )
	{
		m_Contents.m_Misreads.push_back( "tick " + std::to_string( m_nTick ) + ": " + std::move( szWhat ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CContentsReader::OnVoiceInit( const std::string &szCodec, int nQuality, int nSampleRate )
{
	m_Contents.m_VoiceCodec = { szCodec, nQuality, nSampleRate };
}

//-----------------------------------------------------------------------------
// Purpose: A voice packet's bytes are kept for whoever decodes them; the sender is its client
//          number (its entity less one).
//-----------------------------------------------------------------------------
void CContentsReader::OnVoiceData( int nClient, bool bProximity, CBitReader data )
{
	VoicePacket_t packet;
	packet.m_nTick = m_nTick;
	packet.m_nEntity = nClient + 1;
	packet.m_bProximity = bProximity;
	packet.m_nFirst = static_cast<std::uint32_t>( m_Contents.m_VoiceBytes.size() );
	const std::size_t nBytes = data.GetLeft() / 8;
	packet.m_nBytes = static_cast<std::uint32_t>( nBytes );
	m_Contents.m_VoiceBytes.resize( m_Contents.m_VoiceBytes.size() + nBytes );
	data.ReadBytes( m_Contents.m_VoiceBytes.data() + packet.m_nFirst, nBytes );
	if ( data.GetLeft() != 0 )
	{
		Misread( "svc_VoiceData: not whole bytes" );
	}
	m_Contents.m_VoicePackets.push_back( packet );
}

//-----------------------------------------------------------------------------
// Purpose: Sounds, each written against the one before (the first against the defaults): a
//          bit before each field says whether it changed.
//-----------------------------------------------------------------------------
void CContentsReader::OnSounds( bool bReliable, int nCount, CBitReader data )
{
	Sound_t sound;
	sound.m_nSoundLevel = SOUND_LEVEL_NORMAL;
	sound.m_nChannel = SOUND_CHANNEL_STATIC;
	for ( int i = 0; i < nCount; i++ )
	{
		Sound_t next = sound;
		next.m_nTick = m_nTick;
		next.m_bReliable = bReliable;
		if ( data.ReadBit() )
		{
			next.m_nEntity = static_cast<int>( data.ReadBit() ? data.ReadBits( 5 ) : data.ReadBits( ENTITY_NUMBER_BITS ) );
		}
		if ( data.ReadBit() )
		{
			next.m_nSound = static_cast<int>( data.ReadBits( SOUND_INDEX_BITS ) );
		}
		if ( data.ReadBit() )
		{
			next.m_nFlags = static_cast<int>( data.ReadBits( SOUND_FLAG_BITS ) );
		}
		if ( data.ReadBit() )
		{
			next.m_nChannel = static_cast<int>( data.ReadBits( SOUND_CHANNEL_BITS ) );
		}
		next.m_bAmbient = data.ReadBit();
		next.m_bSentence = data.ReadBit();
		// Only a bare stop leaves the rest out: one with other flags sends it all.
		if ( next.m_nFlags != SOUND_FLAG_STOP )
		{
			if ( data.ReadBit() )
			{
				// The same sequence number as the last.
			}
			else if ( data.ReadBit() )
			{
				next.m_nSequence = sound.m_nSequence + 1;
			}
			else
			{
				next.m_nSequence = static_cast<int>( data.ReadBits( SOUND_SEQUENCE_BITS ) );
			}
			if ( data.ReadBit() )
			{
				next.m_flVolume = static_cast<float>( data.ReadBits( 7 ) ) / 127.0f;
			}
			if ( data.ReadBit() )
			{
				next.m_nSoundLevel = static_cast<int>( data.ReadBits( SOUND_LEVEL_BITS ) );
			}
			if ( data.ReadBit() )
			{
				next.m_nPitch = static_cast<int>( data.ReadBits( 8 ) );
			}
			if ( data.ReadBit() )
			{
				next.m_nSpecialDsp = static_cast<int>( data.ReadBits( 8 ) );
			}
			if ( data.ReadBit() )
			{
				// Milliseconds; a negative one in tens of them.
				float flDelay = static_cast<float>( data.ReadSignedBits( SOUND_DELAY_BITS ) ) / 1000.0f;
				if ( flDelay < 0.0f )
				{
					flDelay *= 10.0f;
				}
				next.m_flDelay = flDelay - SOUND_DELAY_OFFSET;
			}
			for ( float *pflAxis : { &next.m_vecOrigin.m_flX, &next.m_vecOrigin.m_flY, &next.m_vecOrigin.m_flZ } )
			{
				if ( data.ReadBit() )
				{
					*pflAxis = SOUND_ORIGIN_SCALE * static_cast<float>( data.ReadSignedBits( SOUND_ORIGIN_BITS ) );
				}
			}
			if ( data.ReadBit() )
			{
				next.m_nSpeaker = data.ReadSignedBits( ENTITY_NUMBER_BITS + 1 );
			}
		}
		else
		{
			// A stop has nothing past its channel; what it leaves is what a stop clears to.
			next.m_flVolume = 0.0f;
			next.m_nSoundLevel = 0;
			next.m_nPitch = 100;
			next.m_nSpecialDsp = 0;
			next.m_flDelay = 0.0f;
			next.m_nSequence = 0;
			next.m_vecOrigin = {};
			next.m_nSpeaker = -1;
		}
		if ( data.IsOverflowed() )
		{
			Misread( "svc_Sounds: runs past its bits" );
			return;
		}
		m_Contents.m_Sounds.push_back( next );
		sound = next;
	}
	if ( data.GetLeft() != 0 )
	{
		Misread( "svc_Sounds: " + std::to_string( data.GetLeft() ) + " bits left over" );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CContentsReader::OnUserMessage( int nKind, CBitReader data )
{
	m_Contents.m_UserMessageCounts[ nKind ]++;
	UserMessage_t message;
	message.m_nTick = m_nTick;
	message.m_nKind = nKind;
	const std::string szGameDir = m_State.GetServerInfo() ? m_State.GetServerInfo()->m_szGameDir : std::string();
	const UserLayout_t *pLayout = FindUserLayout( szGameDir, nKind );
	if ( pLayout )
	{
		message.m_szKind = pLayout->m_szName;
	}
	if ( pLayout && pLayout->m_bKnown )
	{
		CBitReader fields = data;
		message.m_bRead = ReadUserFields( *pLayout, fields, message.m_Fields );
		if ( !message.m_bRead )
		{
			Misread( std::string( pLayout->m_szName ) + ": " + ( fields.IsOverflowed() ? "runs past its bits" : std::to_string( fields.GetLeft() ) + " bits left over" ) );
			message.m_Fields.clear();
		}
	}
	if ( !message.m_bRead )
	{
		message.m_Data.resize( ( data.GetLeft() + 7 ) / 8 );
		for ( std::byte &b : message.m_Data )
		{
			b = static_cast<std::byte>( data.ReadBits( static_cast<int>( std::min<std::size_t>( 8, data.GetLeft() ) ) ) );
		}
	}
	m_Contents.m_UserMessages.push_back( std::move( message ) );
}

//-----------------------------------------------------------------------------
// Purpose: Entity messages are each class's own (a breakable's gibs...): counted, not read.
//-----------------------------------------------------------------------------
void CContentsReader::OnEntityMessage( int, int nClass, CBitReader )
{
	m_Contents.m_EntityMessageCounts[ nClass ]++;
}

//-----------------------------------------------------------------------------
// Purpose: The game's events as the server describes them: each one's number and name, then
//          its keys (a kind and a name each, until a kind of 0).
//-----------------------------------------------------------------------------
void CContentsReader::OnGameEventList( int nCount, CBitReader data )
{
	m_Contents.m_EventKinds.clear();
	std::ranges::fill( m_EventByKindId, -1 );
	for ( int i = 0; i < nCount && !data.IsOverflowed(); i++ )
	{
		EventKind_t kind;
		kind.m_nId = static_cast<int>( data.ReadBits( EVENT_ID_BITS ) );
		kind.m_szName = data.ReadString();
		for ( ;; )
		{
			const int nKeyKind = static_cast<int>( data.ReadBits( EVENT_KEY_KIND_BITS ) );
			if ( nKeyKind == 0 || data.IsOverflowed() )
			{
				break;
			}
			EventKey_t key;
			key.m_nKind = static_cast<EventKeyKind_t>( nKeyKind );
			key.m_szName = data.ReadString();
			kind.m_Keys.push_back( std::move( key ) );
		}
		m_EventByKindId[ static_cast<std::size_t>( kind.m_nId ) ] = static_cast<int>( m_Contents.m_EventKinds.size() );
		m_Contents.m_EventKinds.push_back( std::move( kind ) );
	}
	if ( data.IsOverflowed() || data.GetLeft() != 0 )
	{
		Misread( "svc_GameEventList: doesn't fit its bits" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: An event: its kind's number, then each key's value as its kind is written.
//-----------------------------------------------------------------------------
void CContentsReader::OnGameEvent( CBitReader data )
{
	GameEvent_t event;
	event.m_nTick = m_nTick;
	const int nId = static_cast<int>( data.ReadBits( EVENT_ID_BITS ) );
	event.m_nKind = m_EventByKindId[ static_cast<std::size_t>( nId ) ];
	if ( event.m_nKind < 0 )
	{
		Misread( "svc_GameEvent: an event the list doesn't have (" + std::to_string( nId ) + ")" );
		return;
	}
	const EventKind_t &kind = m_Contents.m_EventKinds[ static_cast<std::size_t>( event.m_nKind ) ];
	event.m_Values.resize( kind.m_Keys.size() );
	for ( std::size_t i = 0; i < kind.m_Keys.size(); i++ )
	{
		Value_t &value = event.m_Values[ i ];
		switch ( kind.m_Keys[ i ].m_nKind )
		{
			case EventKeyKind_t::EVENT_KEY_KIND_STRING:
				value.m_szText = data.ReadString();
				break;
			case EventKeyKind_t::EVENT_KEY_KIND_FLOAT:
				value.m_flNumber = data.ReadFloat();
				break;
			case EventKeyKind_t::EVENT_KEY_KIND_LONG:
				value.m_flNumber = static_cast<std::int32_t>( data.ReadBits( 32 ) );
				break;
			case EventKeyKind_t::EVENT_KEY_KIND_SHORT:
				value.m_flNumber = data.ReadSignedBits( 16 );
				break;
			case EventKeyKind_t::EVENT_KEY_KIND_BYTE:
				value.m_flNumber = data.ReadBits( 8 );
				break;
			case EventKeyKind_t::EVENT_KEY_KIND_BOOL:
				value.m_flNumber = data.ReadBit() ? 1.0 : 0.0;
				break;
		}
	}
	if ( data.IsOverflowed() || data.GetLeft() != 0 )
	{
		Misread( kind.m_szName + ": doesn't fit its bits" );
	}
	m_Contents.m_Events.push_back( std::move( event ) );
}

//-----------------------------------------------------------------------------
// Purpose: Temp entities: each may be put off (a hundredth of a second a step), and names its
//          class (one more than its number) or is of the last one's, whose values it starts
//          from; its properties then come as an entity's do.
//-----------------------------------------------------------------------------
void CContentsReader::OnTempEntities( int nCount, CBitReader data )
{
	int nClass = -1;
	std::vector<PropValue_t> values;
	for ( int i = 0; i < nCount; i++ )
	{
		TempEntity_t tempEntity;
		tempEntity.m_nTick = m_nTick;
		if ( data.ReadBit() )
		{
			tempEntity.m_flDelay = static_cast<float>( data.ReadBits( 8 ) ) / 100.0f;
		}
		if ( data.ReadBit() )
		{
			nClass = static_cast<int>( data.ReadBits( m_State.GetClassBits() ) ) - 1;
			values.clear();
		}
		const std::vector<FlatProp_t> *pProps = m_State.GetProps( nClass );
		if ( !pProps )
		{
			Misread( "svc_TempEntities: a class the demo doesn't describe (" + std::to_string( nClass ) + ")" );
			return;
		}
		values.resize( pProps->size() );
		if ( !m_State.ReadClassProps( nClass, data, values ) )
		{
			Misread( "svc_TempEntities: " + m_Contents.m_Classes[ static_cast<std::size_t>( nClass ) ].m_szName + " doesn't read" );
			return;
		}
		std::vector<std::vector<FlatProp_t>> &props = m_Contents.m_TempProps;
		if ( props.size() <= static_cast<std::size_t>( nClass ) )
		{
			props.resize( static_cast<std::size_t>( nClass ) + 1 );
		}
		if ( props[ static_cast<std::size_t>( nClass ) ].empty() )
		{
			props[ static_cast<std::size_t>( nClass ) ] = *pProps;
		}
		tempEntity.m_nClass = nClass;
		tempEntity.m_nFirst = static_cast<std::uint32_t>( m_Contents.m_TempValues.size() );
		m_Contents.m_TempValues.insert( m_Contents.m_TempValues.end(), values.begin(), values.end() );
		m_Contents.m_TempEntities.push_back( tempEntity );
	}
	if ( data.GetLeft() != 0 )
	{
		Misread( "svc_TempEntities: " + std::to_string( data.GetLeft() ) + " bits left over" );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CContentsReader::OnMenu( int nKind, std::span<const std::byte> data )
{
	Menu_t menu;
	menu.m_nTick = m_nTick;
	menu.m_nKind = nKind;
	CByteReader reader( data );
	menu.m_bRead = ReadBinaryKeys( reader, menu.m_Keys, 0 ) && reader.GetLeft() == 0;
	if ( !menu.m_bRead )
	{
		Misread( "svc_Menu: doesn't read as KeyValues" );
	}
	m_Contents.m_Menus.push_back( std::move( menu ) );
}

//-----------------------------------------------------------------------------
// Purpose: A field's key, when its value differs from the last one's. The place and angles
//          also get the last value again at the snapshot before, when it had stood since an
//          earlier one: a move starts from there, not from when it last moved.
//-----------------------------------------------------------------------------
void CContentsReader::NoteKey( EntityLife_t &life, EntityField_t nField, const std::array<float, 3> &value, int nTick )
{
	std::vector<EntityKey_t> &keys = life.m_Fields[ nField ];
	if ( !keys.empty() && keys.back().m_flValue == value )
	{
		return;
	}
	if ( ( nField == ENTITY_FIELD_ORIGIN || nField == ENTITY_FIELD_ANGLES ) && !keys.empty() && keys.back().m_nTick < m_nLastSnapshot && m_nLastSnapshot < nTick )
	{
		keys.push_back( { m_nLastSnapshot, keys.back().m_flValue } );
	}
	// A tick noted twice (a packet split in two) keeps the later word.
	if ( !keys.empty() && keys.back().m_nTick >= nTick )
	{
		keys.back().m_flValue = value;
	}
	else
	{
		keys.push_back( { nTick, value } );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The entities the frame's entity messages touched: each one's open life gets keys
//          for what changed, a life ends when its number is taken by another class or serial,
//          and one gone from sight is marked so (it may come back, as the same).
//-----------------------------------------------------------------------------
void CContentsReader::NoteEntities( int nTick )
{
	// A snapshot that changed nothing still says everything stood where it was.
	const bool bSnapshot = m_State.GetSnapshotCount() != m_nSnapshots;
	m_nSnapshots = m_State.GetSnapshotCount();
	const std::span<const int> changed = m_State.GetChanged();
	if ( changed.empty() )
	{
		if ( bSnapshot )
		{
			m_nLastSnapshot = nTick;
		}
		return;
	}
	const std::span<const Entity_t> entities = m_State.GetEntities();
	for ( const int nEntity : changed )
	{
		const std::size_t n = static_cast<std::size_t>( nEntity );
		if ( m_Seen[ n ] )
		{
			continue;
		}
		m_Seen[ n ] = 1;
		const Entity_t &entity = entities[ n ];
		std::vector<EntityLife_t> &lives = m_LivesByEntity[ n ];
		EntityLife_t *pLife = lives.empty() || lives.back().m_nTo >= 0 ? nullptr : &lives.back();
		if ( entity.m_nClass < 0 )
		{
			if ( pLife )
			{
				NoteKey( *pLife, ENTITY_FIELD_THERE, { entity.m_bOutOfSight ? -1.0f : 0.0f, 0.0f, 0.0f }, nTick );
			}
			continue;
		}
		if ( pLife && ( pLife->m_nClass != entity.m_nClass || pLife->m_nSerial != entity.m_nSerial ) )
		{
			// It went when it was last marked gone, or now.
			const std::vector<EntityKey_t> &there = pLife->m_Fields[ ENTITY_FIELD_THERE ];
			pLife->m_nTo = !there.empty() && there.back().m_flValue[ 0 ] <= 0.0f ? there.back().m_nTick : nTick;
			pLife = nullptr;
		}
		if ( !pLife )
		{
			EntityLife_t life;
			life.m_nEntity = nEntity;
			life.m_nClass = entity.m_nClass;
			life.m_nSerial = entity.m_nSerial;
			life.m_nFrom = nTick;
			lives.push_back( std::move( life ) );
			pLife = &lives.back();
		}
		NoteKey( *pLife, ENTITY_FIELD_THERE, { 1.0f, 0.0f, 0.0f }, nTick );
		if ( entity.m_Values.size() < ENTITY_SLOT_COUNT )
		{
			continue;
		}
		for ( int nField = 0; nField < ENTITY_SLOT_COUNT; nField++ )
		{
			const PropValue_t &value = entity.m_Values[ static_cast<std::size_t>( nField ) ];
			std::array<float, 3> key = { static_cast<float>( value[ 0 ] ), static_cast<float>( value[ 1 ] ), static_cast<float>( value[ 2 ] ) };
			if ( nField == ENTITY_FIELD_COLOR )
			{
				// A float holds 24 bits whole: the colour, then its alpha apart.
				const std::uint32_t nColor = static_cast<std::uint32_t>( value[ 0 ] );
				key = { static_cast<float>( nColor & 0xffffff ), static_cast<float>( nColor >> 24 ), 0.0f };
			}
			NoteKey( *pLife, static_cast<EntityField_t>( nField ), key, nTick );
		}
		const std::size_t nClass = static_cast<std::size_t>( entity.m_nClass );
		const std::size_t nStats = nClass < m_Contents.m_Stats.size() ? m_Contents.m_Stats[ nClass ].size() : 0;
		if ( nStats > 0 && pLife->m_Stats.size() < nStats )
		{
			pLife->m_Stats.resize( nStats );
		}
		for ( std::size_t nStat = 0; nStat < nStats; nStat++ )
		{
			const std::size_t nSlot = ENTITY_SLOT_COUNT + nStat / 3;
			if ( nSlot >= entity.m_Values.size() )
			{
				break;
			}
			double flRaw = entity.m_Values[ nSlot ][ nStat % 3 ];
			if ( nClass < m_StatColors.size() && nStat < m_StatColors[ nClass ].size() && m_StatColors[ nClass ][ nStat ] )
			{
				flRaw = static_cast<double>( static_cast<std::uint32_t>( static_cast<std::int64_t>( flRaw ) ) & 0xffffffu );
			}
			const float flValue = static_cast<float>( flRaw );
			std::vector<StatKey_t> &keys = pLife->m_Stats[ nStat ];
			if ( !keys.empty() && keys.back().m_flValue == flValue )
			{
				continue;
			}
			if ( !keys.empty() && keys.back().m_nTick >= nTick )
			{
				keys.back().m_flValue = flValue;
			}
			else
			{
				keys.push_back( { nTick, flValue } );
			}
		}
	}
	for ( const int nEntity : changed )
	{
		m_Seen[ static_cast<std::size_t>( nEntity ) ] = 0;
	}
	m_State.ClearChanged();
	m_nLastSnapshot = nTick;
}

//-----------------------------------------------------------------------------
// Purpose: Every frame in order, as CPlayers reads them.
//-----------------------------------------------------------------------------
void CContentsReader::Read( const CDemo &demo, std::span<const std::byte> file, const std::function<bool( double )> &pfnProgress )
{
	const std::vector<Frame_t> &frames = demo.GetFrames();
	int nLastTick = 0;
	for ( std::size_t i = 0; i < frames.size(); i++ )
	{
		const Frame_t &frame = frames[ i ];
		if ( pfnProgress && i % PROGRESS_EVERY == 0 && !pfnProgress( static_cast<double>( i ) / static_cast<double>( frames.size() ) ) )
		{
			m_Contents.m_szWarning = "Reading the demo was stopped.";
			break;
		}
		m_nTick = frame.m_nKind == FrameKind_t::FRAME_KIND_PACKET ? std::max( frame.m_nTick, nLastTick ) : nLastTick;
		bool bRead = true;
		switch ( frame.m_nKind )
		{
			case FrameKind_t::FRAME_KIND_SIGNON:
			case FrameKind_t::FRAME_KIND_PACKET:
				bRead = m_State.ReadPacket( CDemo::FrameBlock( frame, file ) );
				break;
			case FrameKind_t::FRAME_KIND_DATA_TABLES:
				bRead = m_State.ReadDataTables( CDemo::FrameBlock( frame, file ) );
				m_Contents.m_Classes = m_State.GetClasses();
				break;
			case FrameKind_t::FRAME_KIND_STRING_TABLES:
				bRead = m_State.ReadStringTables( CDemo::FrameBlock( frame, file ) );
				break;
			default:
				break;
		}
		if ( !bRead )
		{
			m_Contents.m_szWarning = "The demo isn't read past tick " + std::to_string( nLastTick ) + ": " + m_State.GetError() + ".";
			break;
		}
		nLastTick = m_nTick;
		NoteEntities( m_nTick );
		if ( const int nServer = m_State.GetServerTick(); nServer > 0 && frame.m_nKind == FrameKind_t::FRAME_KIND_PACKET )
		{
			std::vector<std::pair<int, int>> &ticks = m_Contents.m_ServerTicks;
			const int nOffset = nServer - m_nTick;
			if ( ticks.empty() || ticks.back().second != nOffset )
			{
				ticks.emplace_back( m_nTick, nOffset );
			}
		}
	}
	Finish( demo );
}

//-----------------------------------------------------------------------------
// Purpose: The lives in one list by entity number, the precache tables' names, and the voice
//          packets put together into times talking.
//-----------------------------------------------------------------------------
void CContentsReader::Finish( const CDemo &demo )
{
	CContents &contents = m_Contents;
	contents.m_ServerInfo = m_State.GetServerInfo();
	contents.m_Classes = m_State.GetClasses();
	contents.m_flTickInterval = contents.m_ServerInfo && contents.m_ServerInfo->m_flTickInterval > 0.0f ? contents.m_ServerInfo->m_flTickInterval : demo.GetTickInterval();
	if ( contents.m_flTickInterval <= 0.0 )
	{
		contents.m_flTickInterval = FALLBACK_TICK_INTERVAL;
	}

	contents.m_FirstLife.assign( MAX_ENTITIES + 1, 0 );
	for ( std::size_t n = 0; n < m_LivesByEntity.size(); n++ )
	{
		contents.m_FirstLife[ n ] = static_cast<std::uint32_t>( contents.m_Lives.size() );
		for ( EntityLife_t &life : m_LivesByEntity[ n ] )
		{
			const std::vector<EntityKey_t> &there = life.m_Fields[ ENTITY_FIELD_THERE ];
			if ( life.m_nTo < 0 && !there.empty() && there.back().m_flValue[ 0 ] == 0.0f )
			{
				life.m_nTo = there.back().m_nTick;
			}
			for ( std::vector<EntityKey_t> &keys : life.m_Fields )
			{
				keys.shrink_to_fit();
			}
			contents.m_Lives.push_back( std::move( life ) );
		}
	}
	contents.m_FirstLife[ MAX_ENTITIES ] = static_cast<std::uint32_t>( contents.m_Lives.size() );
	m_LivesByEntity.clear();

	const auto names = [ & ]( std::string_view szTable, std::vector<std::string> &out ) {
		if ( const StringTable_t *pTable = m_State.FindTable( szTable ) )
		{
			for ( const StringEntry_t &entry : pTable->m_Entries )
			{
				out.push_back( entry.m_szText );
			}
		}
	};
	names( "soundprecache", contents.m_SoundNames );
	names( "modelprecache", contents.m_ModelNames );
	names( "ParticleEffectNames", contents.m_ParticleNames );
	names( "EffectDispatch", contents.m_EffectNames );
	for ( const char *pszTable : { "downloadables", "modelprecache", "genericprecache", "soundprecache", "decalprecache", "instancebaseline", "lightstyles", "userinfo", "server_query_info", "ParticleEffectNames", "EffectDispatch", "VguiScreen", "Materials", "InfoPanel", "Scenes" } )
	{
		if ( const StringTable_t *pTable = m_State.FindTable( pszTable ) )
		{
			contents.m_StringTables.emplace_back( pTable->m_szName, static_cast<int>( pTable->m_Entries.size() ) );
		}
	}

	const int nGap = std::max( 1, static_cast<int>( std::lround( SPEECH_GAP_SECONDS / contents.m_flTickInterval ) ) );
	std::vector<int> open( MAX_ENTITIES + 1, -1 );
	for ( const VoicePacket_t &packet : contents.m_VoicePackets )
	{
		if ( packet.m_nEntity < 0 || packet.m_nEntity > MAX_ENTITIES )
		{
			continue;
		}
		int &nOpen = open[ static_cast<std::size_t>( packet.m_nEntity ) ];
		if ( nOpen >= 0 && packet.m_nTick - contents.m_Speech[ static_cast<std::size_t>( nOpen ) ].m_nTo <= nGap )
		{
			Speech_t &speech = contents.m_Speech[ static_cast<std::size_t>( nOpen ) ];
			speech.m_nTo = packet.m_nTick;
			speech.m_nPackets++;
			continue;
		}
		nOpen = static_cast<int>( contents.m_Speech.size() );
		contents.m_Speech.push_back( { packet.m_nEntity, packet.m_nTick, packet.m_nTick, 1 } );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CContents CContents::Read( const CDemo &demo, std::span<const std::byte> file, const std::function<bool( double )> &pfnProgress )
{
	CContents contents;
	CContentsReader reader( contents );
	reader.Read( demo, file, pfnProgress );
	return contents;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view CContents::ClassName( int nClass ) const
{
	return nClass >= 0 && static_cast<std::size_t>( nClass ) < m_Classes.size() ? std::string_view( m_Classes[ static_cast<std::size_t>( nClass ) ].m_szName ) : std::string_view{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CContents::FindClass( std::string_view szName ) const
{
	const auto found = std::ranges::find( m_Classes, szName, &ServerClass_t::m_szName );
	return found == m_Classes.end() ? -1 : static_cast<int>( found - m_Classes.begin() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<const EntityLife_t *> CContents::LivesOf( int nEntity ) const
{
	std::vector<const EntityLife_t *> lives;
	if ( nEntity < 0 || nEntity >= MAX_ENTITIES || m_FirstLife.size() != MAX_ENTITIES + 1 )
	{
		return lives;
	}
	for ( std::uint32_t i = m_FirstLife[ static_cast<std::size_t>( nEntity ) ]; i < m_FirstLife[ static_cast<std::size_t>( nEntity ) + 1 ]; i++ )
	{
		lives.push_back( &m_Lives[ i ] );
	}
	return lives;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const EntityLife_t *CContents::LifeAt( int nEntity, int nTick ) const
{
	for ( const EntityLife_t *pLife : LivesOf( nEntity ) )
	{
		if ( pLife->IsThere( nTick ) )
		{
			return pLife;
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The keys at or before the tick; the place and angles moved towards the next keys by
//          how far between them the tick is, unless the move is too far to be one.
//-----------------------------------------------------------------------------
std::optional<EntityState_t> CContents::StateOf( const EntityLife_t &life, double flTick ) const
{
	const int nTick = static_cast<int>( std::floor( flTick ) );
	if ( !life.IsThere( nTick ) )
	{
		return std::nullopt;
	}
	EntityState_t state;
	state.m_pLife = &life;
	const auto lerp = [ & ]( EntityField_t nField, bool bAngles ) -> CVec3 {
		const std::vector<EntityKey_t> &keys = life.m_Fields[ nField ];
		const auto after = std::upper_bound( keys.begin(), keys.end(), flTick, []( double flAt, const EntityKey_t &key ) { return flAt < key.m_nTick; } );
		if ( after == keys.begin() )
		{
			return {};
		}
		const EntityKey_t &from = *( after - 1 );
		CVec3 vecValue{ from.m_flValue[ 0 ], from.m_flValue[ 1 ], from.m_flValue[ 2 ] };
		if ( after == keys.end() || after->m_nTick <= from.m_nTick )
		{
			return vecValue;
		}
		const EntityKey_t &to = *after;
		const CVec3 vecTo{ to.m_flValue[ 0 ], to.m_flValue[ 1 ], to.m_flValue[ 2 ] };
		const double flTicks = to.m_nTick - from.m_nTick;
		const float flPart = static_cast<float>( ( flTick - from.m_nTick ) / flTicks );
		if ( bAngles )
		{
			return { LerpAngle( vecValue.m_flX, vecTo.m_flX, flPart ), LerpAngle( vecValue.m_flY, vecTo.m_flY, flPart ), LerpAngle( vecValue.m_flZ, vecTo.m_flZ, flPart ) };
		}
		if ( Length( vecTo - vecValue ) > FASTEST_SPEED * flTicks * m_flTickInterval + TELEPORT_SLACK )
		{
			return vecValue;
		}
		return vecValue + ( vecTo - vecValue ) * flPart;
	};
	state.m_vecOrigin = lerp( ENTITY_FIELD_ORIGIN, false );
	state.m_vecAngles = lerp( ENTITY_FIELD_ANGLES, true );
	state.m_nModel = static_cast<int>( life.ValueAt( ENTITY_FIELD_MODEL, nTick )[ 0 ] );
	state.m_nEffects = static_cast<int>( life.ValueAt( ENTITY_FIELD_EFFECTS, nTick )[ 0 ] );
	const std::array<float, 3> render = life.ValueAt( ENTITY_FIELD_RENDER, nTick );
	state.m_nRenderMode = static_cast<int>( render[ 0 ] );
	state.m_nRenderFx = static_cast<int>( render[ 1 ] );
	const std::array<float, 3> color = life.ValueAt( ENTITY_FIELD_COLOR, nTick );
	state.m_nColor = life.m_Fields[ ENTITY_FIELD_COLOR ].empty() ? 0xffffffffu : static_cast<std::uint32_t>( color[ 0 ] ) | static_cast<std::uint32_t>( color[ 1 ] ) << 24;
	const std::array<float, 3> parent = life.ValueAt( ENTITY_FIELD_PARENT, nTick );
	const std::uint32_t nHandle = static_cast<std::uint32_t>( parent[ 0 ] );
	state.m_nParent = life.m_Fields[ ENTITY_FIELD_PARENT ].empty() || nHandle == ENTITY_NO_HANDLE ? 0 : static_cast<int>( nHandle & ENTITY_HANDLE_INDEX_MASK );
	state.m_nParentAttachment = static_cast<int>( parent[ 1 ] );
	for ( int n = 0; n < ENTITY_EXTRA_COUNT; n++ )
	{
		state.m_flExtra[ static_cast<std::size_t>( n ) ] = life.ValueAt( static_cast<EntityField_t>( ENTITY_FIELD_EXTRA_0 + n ), nTick )[ 0 ];
	}
	return state;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<EntityState_t> CContents::At( double flTick ) const
{
	std::vector<EntityState_t> states;
	for ( const EntityLife_t &life : m_Lives )
	{
		if ( auto state = StateOf( life, flTick ) )
		{
			states.push_back( *state );
		}
	}
	return states;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const std::array<std::string, ENTITY_EXTRA_COUNT> &CContents::GetExtras( int nClass ) const
{
	static const std::array<std::string, ENTITY_EXTRA_COUNT> NONE;
	return nClass >= 0 && static_cast<std::size_t>( nClass ) < m_Extras.size() ? m_Extras[ static_cast<std::size_t>( nClass ) ] : NONE;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const std::string> CContents::GetStats( int nClass ) const
{
	return nClass >= 0 && static_cast<std::size_t>( nClass ) < m_Stats.size() ? std::span<const std::string>( m_Stats[ static_cast<std::size_t>( nClass ) ] ) : std::span<const std::string>{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CContents::FindStat( int nClass, std::string_view szName ) const
{
	const std::span<const std::string> stats = GetStats( nClass );
	const auto found = std::ranges::find( stats, szName );
	return found == stats.end() ? -1 : static_cast<int>( found - stats.begin() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<float> CContents::StatOf( const EntityLife_t &life, std::string_view szName, int nTick ) const
{
	return life.StatAt( FindStat( life.m_nClass, szName ), nTick );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const EntityLife_t *CContents::FindLife( std::string_view szClass, int nTick ) const
{
	const int nClass = FindClass( szClass );
	if ( nClass < 0 )
	{
		return nullptr;
	}
	const EntityLife_t *pAny = nullptr;
	for ( const EntityLife_t &life : m_Lives )
	{
		if ( life.m_nClass == nClass && nTick >= life.m_nFrom && ( life.m_nTo < 0 || nTick < life.m_nTo ) )
		{
			if ( life.IsThere( nTick ) )
			{
				return &life;
			}
			pAny = pAny ? pAny : &life;
		}
	}
	return pAny;
}

//-----------------------------------------------------------------------------
// Purpose: Before the first net_Tick, the first one's offset.
//-----------------------------------------------------------------------------
double CContents::ServerTimeAt( double flTick ) const
{
	int nOffset = 0;
	if ( !m_ServerTicks.empty() )
	{
		const auto after = std::upper_bound( m_ServerTicks.begin(), m_ServerTicks.end(), flTick, []( double flAt, const std::pair<int, int> &entry ) { return flAt < entry.first; } );
		nOffset = ( after == m_ServerTicks.begin() ? after : after - 1 )->second;
	}
	return ( flTick + nOffset ) * m_flTickInterval;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CContents::GetEntityBytes() const
{
	std::size_t nBytes = m_Lives.capacity() * sizeof( EntityLife_t );
	for ( const EntityLife_t &life : m_Lives )
	{
		for ( const std::vector<EntityKey_t> &keys : life.m_Fields )
		{
			nBytes += keys.capacity() * sizeof( EntityKey_t );
		}
	}
	return nBytes;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CContents::FindEventKind( std::string_view szName ) const
{
	const auto found = std::ranges::find( m_EventKinds, szName, &EventKind_t::m_szName );
	return found == m_EventKinds.end() ? -1 : static_cast<int>( found - m_EventKinds.begin() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const Value_t *CContents::EventValue( const GameEvent_t &event, std::string_view szKey ) const
{
	if ( event.m_nKind < 0 || static_cast<std::size_t>( event.m_nKind ) >= m_EventKinds.size() )
	{
		return nullptr;
	}
	const std::vector<EventKey_t> &keys = m_EventKinds[ static_cast<std::size_t>( event.m_nKind ) ].m_Keys;
	const auto found = std::ranges::find( keys, szKey, &EventKey_t::m_szName );
	if ( found == keys.end() )
	{
		return nullptr;
	}
	const std::size_t n = static_cast<std::size_t>( found - keys.begin() );
	return n < event.m_Values.size() ? &event.m_Values[ n ] : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CContents CContents::FromSounds( std::vector<std::string> soundNames, std::vector<Sound_t> sounds, double flTickInterval )
{
	CContents contents;
	contents.m_SoundNames = std::move( soundNames );
	contents.m_Sounds = std::move( sounds );
	contents.m_flTickInterval = flTickInterval;
	return contents;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CContents::SoundName( const Sound_t &sound ) const
{
	if ( sound.m_bSentence )
	{
		return "sentence " + std::to_string( sound.m_nSound );
	}
	return sound.m_nSound >= 0 && static_cast<std::size_t>( sound.m_nSound ) < m_SoundNames.size() ? m_SoundNames[ static_cast<std::size_t>( sound.m_nSound ) ] : std::string();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view CContents::ParticleName( int nIndex ) const
{
	return nIndex >= 0 && static_cast<std::size_t>( nIndex ) < m_ParticleNames.size() ? std::string_view( m_ParticleNames[ static_cast<std::size_t>( nIndex ) ] ) : std::string_view{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view CContents::EffectName( int nIndex ) const
{
	return nIndex >= 0 && static_cast<std::size_t>( nIndex ) < m_EffectNames.size() ? std::string_view( m_EffectNames[ static_cast<std::size_t>( nIndex ) ] ) : std::string_view{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view CContents::ModelName( int nModel ) const
{
	return nModel >= 0 && static_cast<std::size_t>( nModel ) < m_ModelNames.size() ? std::string_view( m_ModelNames[ static_cast<std::size_t>( nModel ) ] ) : std::string_view{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const PropValue_t> CContents::GetTempValues( const TempEntity_t &tempEntity ) const
{
	const std::span<const FlatProp_t> props = GetTempProps( tempEntity.m_nClass );
	if ( tempEntity.m_nFirst + props.size() > m_TempValues.size() )
	{
		return {};
	}
	return std::span( m_TempValues ).subspan( tempEntity.m_nFirst, props.size() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const FlatProp_t> CContents::GetTempProps( int nClass ) const
{
	return nClass >= 0 && static_cast<std::size_t>( nClass ) < m_TempProps.size() ? std::span<const FlatProp_t>( m_TempProps[ static_cast<std::size_t>( nClass ) ] ) : std::span<const FlatProp_t>{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const PropValue_t *CContents::TempValue( const TempEntity_t &tempEntity, std::string_view szProp ) const
{
	const std::span<const FlatProp_t> props = GetTempProps( tempEntity.m_nClass );
	const std::span<const PropValue_t> values = GetTempValues( tempEntity );
	for ( std::size_t i = 0; i < props.size() && i < values.size(); i++ )
	{
		if ( props[ i ].m_szName == szProp )
		{
			return &values[ i ];
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const std::byte> CContents::GetVoiceBytes( const VoicePacket_t &packet ) const
{
	if ( static_cast<std::size_t>( packet.m_nFirst ) + packet.m_nBytes > m_VoiceBytes.size() )
	{
		return {};
	}
	return std::span( m_VoiceBytes ).subspan( packet.m_nFirst, packet.m_nBytes );
}

} // namespace xv::demo
