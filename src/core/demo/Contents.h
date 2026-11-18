//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Everything a demo's packets hold besides the players' places (Players.h): every
//          entity followed over the demo (its place, angles, model, visibility, render
//          settings, parent and a few of its class's own values), the game events (decoded by
//          the list the demo carries), the user messages (by the game's table of them), the
//          server's menus, the sounds, the temp entities (by their network tables) and the
//          voice packets with who sent them. Read in one pass, as the game would read them.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "demo/Demo.h"
#include "demo/Network.h"
#include "util/Vec3.h"

namespace xv::demo
{

//-----------------------------------------------------------------------------
// Entities (A1)
//-----------------------------------------------------------------------------

// What is followed of every entity. The EXTRA ones are the class's own (EntityExtras says
// which, by class).
enum EntityField_t
{
	ENTITY_FIELD_ORIGIN,      // three numbers
	ENTITY_FIELD_ANGLES,      // pitch, yaw, roll
	ENTITY_FIELD_MODEL,       // the model precache table's index
	ENTITY_FIELD_EFFECTS,     // m_fEffects (ENTITY_EFFECT_NO_DRAW...)
	ENTITY_FIELD_RENDER,      // render mode, render fx
	ENTITY_FIELD_COLOR,       // m_clrRender: red, green, blue, alpha (a byte each, red lowest)
	ENTITY_FIELD_PARENT,      // the parent's handle (ENTITY_NO_HANDLE: none), its attachment
	ENTITY_FIELD_EXTRA_0,
	ENTITY_FIELD_EXTRA_1,
	ENTITY_FIELD_EXTRA_2,
	ENTITY_FIELD_THERE,       // 1 while the demo has the entity, 0 once it is gone, -1 while it is out of the recorder's sight
	ENTITY_FIELD_COUNT,
};
constexpr int ENTITY_EXTRA_COUNT = 3;
constexpr int ENTITY_EFFECT_NO_DRAW = 0x20;
// A handle names no entity.
constexpr std::uint32_t ENTITY_NO_HANDLE = 0x1fffff;
// A handle's entity number is its low bits.
constexpr std::uint32_t ENTITY_HANDLE_INDEX_MASK = 0x7ff;

// A field's value from a tick on.
struct EntityKey_t
{
	int m_nTick = 0;
	std::array<float, 3> m_flValue{};
};

// A stat's value from a tick on.
struct StatKey_t
{
	int m_nTick = 0;
	float m_flValue = 0.0f;
};

// One entity from the tick it came to the tick it went: a number used again by another
// entity (another class or serial) is another life.
struct EntityLife_t
{
	int m_nEntity = 0;
	int m_nClass = -1;
	int m_nSerial = 0;
	int m_nFrom = 0;
	int m_nTo = -1; // the tick it went; -1 while it is still there at the demo's end
	std::array<std::vector<EntityKey_t>, ENTITY_FIELD_COUNT> m_Fields; // by tick, each
	std::vector<std::vector<StatKey_t>> m_Stats; // by the class's stats (EntityStats), by tick, each

	// A field's value at a tick; zeros before its first.
	[[nodiscard]] std::array<float, 3> ValueAt( EntityField_t nField, int nTick ) const;
	// A stat's value at a tick; nullopt before its first or for a stat the life hasn't.
	[[nodiscard]] std::optional<float> StatAt( int nStat, int nTick ) const;
	[[nodiscard]] bool IsThere( int nTick ) const;
	// Out of the recorder's sight at the tick (it still stands somewhere): the last tick the
	// demo had it. nullopt when it is there, or gone.
	[[nodiscard]] std::optional<int> LastSeen( int nTick ) const;
};

// An entity at a moment between snapshots.
struct EntityState_t
{
	const EntityLife_t *m_pLife = nullptr;
	CVec3 m_vecOrigin;
	CVec3 m_vecAngles;
	int m_nModel = 0;
	int m_nEffects = 0;
	int m_nRenderMode = 0;
	int m_nRenderFx = 0;
	std::uint32_t m_nColor = 0xffffffff;
	int m_nParent = 0; // the parent's entity number, 0 for none
	int m_nParentAttachment = 0;
	std::array<float, ENTITY_EXTRA_COUNT> m_flExtra{};

	[[nodiscard]] bool IsDrawn() const { return ( m_nEffects & ENTITY_EFFECT_NO_DRAW ) == 0; }
	[[nodiscard]] int GetAlpha() const { return static_cast<int>( m_nColor >> 24 ); }
};

// Which properties a class's EXTRA fields follow ("m_bActive" for a particle system); empty
// names for none.
[[nodiscard]] std::array<std::string, ENTITY_EXTRA_COUNT> EntityExtras( std::string_view szClass, std::span<const FlatProp_t> props );

// The numbers a class has for the scoreboard and the HUD (D1, D2), followed beside the fields:
// a player's armour and money, the player resource's scores, deaths and pings, the game
// rules' round clock, a team's score, a weapon's clip. An array's element is named by its
// table and its index ("m_iScore.005"). Empty for a class with none.
[[nodiscard]] std::vector<std::string> EntityStats( std::span<const FlatProp_t> props );
// The name EntityStats gives a property.
[[nodiscard]] std::string StatName( const FlatProp_t &prop );

//-----------------------------------------------------------------------------
// Game events (A2)
//-----------------------------------------------------------------------------

// The values are the protocol's.
enum class EventKeyKind_t : std::uint8_t
{
	EVENT_KEY_KIND_STRING = 1,
	EVENT_KEY_KIND_FLOAT,
	EVENT_KEY_KIND_LONG,
	EVENT_KEY_KIND_SHORT,
	EVENT_KEY_KIND_BYTE,
	EVENT_KEY_KIND_BOOL,
};

struct EventKey_t
{
	std::string m_szName;
	EventKeyKind_t m_nKind = EventKeyKind_t::EVENT_KEY_KIND_BYTE;
};

// A kind of event as the demo's list describes it.
struct EventKind_t
{
	int m_nId = -1;
	std::string m_szName; // "player_death"
	std::vector<EventKey_t> m_Keys;
};

// A number, or a text for a string key.
struct Value_t
{
	double m_flNumber = 0.0;
	std::string m_szText;
};

struct GameEvent_t
{
	int m_nTick = 0;
	int m_nKind = -1; // in GetEventKinds()
	std::vector<Value_t> m_Values; // by the kind's keys
};

//-----------------------------------------------------------------------------
// User messages (A3)
//-----------------------------------------------------------------------------

struct UserField_t
{
	std::string_view m_szName; // "channel"; the layout's, lives as long as the program
	Value_t m_Value;
	bool m_bText = false;
};

struct UserMessage_t
{
	int m_nTick = 0;
	int m_nKind = 0;
	std::string_view m_szKind; // "SayText2"; empty when the game's table doesn't have the number
	bool m_bRead = false;      // decoded to its last bit by the kind's layout
	std::vector<UserField_t> m_Fields;
	std::vector<std::byte> m_Data; // the bytes as sent, for a kind not read

	// A field by name; null when the message has none.
	[[nodiscard]] const Value_t *Find( std::string_view szName ) const;
};

// The game's name for a user message number ("SayText2" for Counter-Strike: Source's 4); empty
// for one it has no name for.
[[nodiscard]] std::string_view UserMessageName( std::string_view szGameDir, int nKind );

//-----------------------------------------------------------------------------
// Menus (A4): svc_Menu, a KeyValues tree for a dialog the server opens.
//-----------------------------------------------------------------------------

// The values are the protocol's.
enum class MenuKind_t : std::uint8_t
{
	MENU_KIND_MESSAGE,
	MENU_KIND_MENU,
	MENU_KIND_TEXT,
	MENU_KIND_ENTRY,
	MENU_KIND_ASK_CONNECT,
};

struct MenuKey_t
{
	std::string m_szName;
	std::string m_szValue; // a number written out; empty for a key with children
	std::vector<MenuKey_t> m_Children;

	[[nodiscard]] const MenuKey_t *Find( std::string_view szName ) const;
};

struct Menu_t
{
	int m_nTick = 0;
	int m_nKind = 0;
	bool m_bRead = false;
	std::vector<MenuKey_t> m_Keys;
};

[[nodiscard]] std::string_view MenuKindName( int nKind );

//-----------------------------------------------------------------------------
// Sounds (A5)
//-----------------------------------------------------------------------------

// What a sound message asks of the engine besides starting a sound: the first two change
// the volume or pitch of the same sound playing on its entity and channel (a fade, an
// ambient_generic's Volume input), and start it only when it isn't playing.
constexpr int SOUND_FLAG_CHANGE_VOLUME = 1 << 0;
constexpr int SOUND_FLAG_CHANGE_PITCH = 1 << 1;
constexpr int SOUND_FLAG_STOP = 1 << 2;

struct Sound_t
{
	int m_nTick = 0;
	int m_nEntity = 0;
	int m_nSound = 0; // the sound precache table's index, or a sentence's number
	int m_nFlags = 0;
	int m_nChannel = 0;
	int m_nSequence = 0;
	int m_nSoundLevel = 0;
	int m_nPitch = 100;
	int m_nSpecialDsp = 0;
	int m_nSpeaker = -1;
	float m_flVolume = 1.0f;
	float m_flDelay = 0.0f;
	CVec3 m_vecOrigin;
	bool m_bAmbient = false;
	bool m_bSentence = false;
	bool m_bReliable = false;

	[[nodiscard]] bool IsStop() const { return ( m_nFlags & SOUND_FLAG_STOP ) != 0; }
	[[nodiscard]] bool IsChange() const { return !IsStop() && ( m_nFlags & ( SOUND_FLAG_CHANGE_VOLUME | SOUND_FLAG_CHANGE_PITCH ) ) != 0; }
};

//-----------------------------------------------------------------------------
// Temp entities (A6)
//-----------------------------------------------------------------------------

struct TempEntity_t
{
	int m_nTick = 0;
	int m_nClass = -1;
	float m_flDelay = 0.0f;
	std::uint32_t m_nFirst = 0; // its values' first in GetTempValues(); the class has one per property
};

//-----------------------------------------------------------------------------
// Voice (A7)
//-----------------------------------------------------------------------------

struct VoiceCodec_t
{
	std::string m_szCodec; // "steam", "vaudio_speex", "vaudio_celt"...
	int m_nQuality = 0;
	int m_nSampleRate = 0;
};

struct VoicePacket_t
{
	int m_nTick = 0;
	int m_nEntity = 0; // the speaker's (its client number, plus one)
	bool m_bProximity = false;
	std::uint32_t m_nFirst = 0; // its bytes in GetVoiceBytes()
	std::uint32_t m_nBytes = 0;
};

// Someone talking: packets from one player close enough together.
struct Speech_t
{
	int m_nEntity = 0;
	int m_nFrom = 0;
	int m_nTo = 0;
	int m_nPackets = 0;
};

// What a Steam voice packet says of itself (the "steam" codec): the speaker's Steam id, then
// tagged parts (the sample rate, the audio in its codec, silence), then a CRC-32 of the rest.
struct SteamVoice_t
{
	std::uint64_t m_nSteamId = 0;
	int m_nSampleRate = 0;
	int m_nCodec = -1;   // the audio part's tag (STEAM_VOICE_*)
	std::span<const std::byte> m_Audio; // that part's bytes
	int m_nSilence = 0;  // samples of silence, when a part says so
	bool m_bChecked = false; // the CRC matched
};
constexpr int STEAM_VOICE_SILENCE = 0;
constexpr int STEAM_VOICE_OPUS_PLC = 6; // Opus frames, each with its length

constexpr int STEAM_VOICE_SAMPLE_RATE = 11;

[[nodiscard]] std::optional<SteamVoice_t> ParseSteamVoice( std::span<const std::byte> packet );
[[nodiscard]] std::string_view SteamVoiceCodecName( int nCodec );

//-----------------------------------------------------------------------------
// Everything, in one pass
//-----------------------------------------------------------------------------

class CContents
{
public:
	// Reads the whole demo; file is the bytes demo was read from. pfnProgress is told how far
	// along it is (0 to 1) now and then, and stops the reading by answering false.
	[[nodiscard]] static CContents Read( const CDemo &demo, std::span<const std::byte> file, const std::function<bool( double )> &pfnProgress = {} );
	// Contents with nothing but sounds (by tick) and the precache table they name, for
	// trying out what plays them.
	[[nodiscard]] static CContents FromSounds( std::vector<std::string> soundNames, std::vector<Sound_t> sounds, double flTickInterval );

	// Why the contents stop short of the demo's end; empty when it was all read.
	[[nodiscard]] const std::string &GetWarning() const { return m_szWarning; }
	[[nodiscard]] const std::optional<ServerInfo_t> &GetServerInfo() const { return m_ServerInfo; }
	[[nodiscard]] const std::vector<ServerClass_t> &GetClasses() const { return m_Classes; }
	[[nodiscard]] std::string_view ClassName( int nClass ) const;
	[[nodiscard]] int FindClass( std::string_view szName ) const;

	// Entities.
	[[nodiscard]] const std::vector<EntityLife_t> &GetLives() const { return m_Lives; }
	// The lives an entity number had, in order.
	[[nodiscard]] std::vector<const EntityLife_t *> LivesOf( int nEntity ) const;
	[[nodiscard]] const EntityLife_t *LifeAt( int nEntity, int nTick ) const;
	// Every entity there at a tick, moved smoothly between snapshots.
	[[nodiscard]] std::vector<EntityState_t> At( double flTick ) const;
	[[nodiscard]] std::optional<EntityState_t> StateOf( const EntityLife_t &life, double flTick ) const;
	// What a class's EXTRA fields follow.
	[[nodiscard]] const std::array<std::string, ENTITY_EXTRA_COUNT> &GetExtras( int nClass ) const;
	// A class's stats, in their lives' m_Stats order, and one by name (-1 when it hasn't it).
	[[nodiscard]] std::span<const std::string> GetStats( int nClass ) const;
	[[nodiscard]] int FindStat( int nClass, std::string_view szName ) const;
	// A stat of an entity's life at a tick, by name; nullopt when it hasn't one then.
	[[nodiscard]] std::optional<float> StatOf( const EntityLife_t &life, std::string_view szName, int nTick ) const;
	// The first life of a class there at a tick (the game rules, the player resource); null
	// for none.
	[[nodiscard]] const EntityLife_t *FindLife( std::string_view szClass, int nTick ) const;
	// The server's clock at a demo tick (its tick count, as net_Tick says): game times the
	// entities hold (a round's start) are this times the tick interval.
	[[nodiscard]] double ServerTimeAt( double flTick ) const;
	// The bytes the entity lives' keys take.
	[[nodiscard]] std::size_t GetEntityBytes() const;

	// Game events.
	[[nodiscard]] const std::vector<EventKind_t> &GetEventKinds() const { return m_EventKinds; }
	[[nodiscard]] const std::vector<GameEvent_t> &GetEvents() const { return m_Events; }
	[[nodiscard]] int FindEventKind( std::string_view szName ) const;
	// A key's value in an event; null when its kind has no such key.
	[[nodiscard]] const Value_t *EventValue( const GameEvent_t &event, std::string_view szKey ) const;

	[[nodiscard]] const std::vector<UserMessage_t> &GetUserMessages() const { return m_UserMessages; }
	[[nodiscard]] const std::vector<Menu_t> &GetMenus() const { return m_Menus; }

	[[nodiscard]] const std::vector<Sound_t> &GetSounds() const { return m_Sounds; }
	// A sound's name from the precache table ("weapons/knife/knife_hit1.wav"); "sentence N"
	// for a sentence; empty when the table doesn't have it.
	[[nodiscard]] std::string SoundName( const Sound_t &sound ) const;
	// A model's name from the model precache table ("models/props/crate.mdl", "*12" for a
	// brush model of the map).
	[[nodiscard]] std::string_view ModelName( int nModel ) const;
	// A particle system's name by its index in the ParticleEffectNames table (what
	// info_particle_system's m_iEffectIndex and a dispatched effect's m_nHitBox number), and
	// a dispatched effect's kind by EffectDispatch's ("ParticleEffect"...); empty for none.
	[[nodiscard]] std::string_view ParticleName( int nIndex ) const;
	[[nodiscard]] std::string_view EffectName( int nIndex ) const;

	[[nodiscard]] const std::vector<TempEntity_t> &GetTempEntities() const { return m_TempEntities; }
	[[nodiscard]] std::span<const PropValue_t> GetTempValues( const TempEntity_t &tempEntity ) const;
	// A temp entity class's properties, in its values' order; empty for one not seen.
	[[nodiscard]] std::span<const FlatProp_t> GetTempProps( int nClass ) const;
	// A property's value in a temp entity; null when its class has none of that name.
	[[nodiscard]] const PropValue_t *TempValue( const TempEntity_t &tempEntity, std::string_view szProp ) const;

	[[nodiscard]] const VoiceCodec_t &GetVoiceCodec() const { return m_VoiceCodec; }
	[[nodiscard]] const std::vector<VoicePacket_t> &GetVoicePackets() const { return m_VoicePackets; }
	[[nodiscard]] std::span<const std::byte> GetVoiceBytes( const VoicePacket_t &packet ) const;
	// Who talked when.
	[[nodiscard]] const std::vector<Speech_t> &GetSpeech() const { return m_Speech; }

	// Server prints (console text sent to everyone).
	[[nodiscard]] const std::vector<std::pair<int, std::string>> &GetPrints() const { return m_Prints; }
	// Commands the server ran in the recorder's console (a player's own demo; SourceTV's are
	// its own), each at its tick.
	[[nodiscard]] const std::vector<std::pair<int, std::string>> &GetServerCommands() const { return m_ServerCommands; }
	// How many messages of each kind (by number), user messages by number, entity messages and
	// temp entities by class: the inventory.
	[[nodiscard]] const std::map<int, int> &GetMessageCounts() const { return m_MessageCounts; }
	[[nodiscard]] const std::map<int, int> &GetUserMessageCounts() const { return m_UserMessageCounts; }
	[[nodiscard]] const std::map<int, int> &GetEntityMessageCounts() const { return m_EntityMessageCounts; }
	// Messages whose layout didn't read to the bit (the first few kept, as "kind: why").
	[[nodiscard]] const std::vector<std::string> &GetMisreads() const { return m_Misreads; }
	[[nodiscard]] int GetMisreadCount() const { return m_nMisreads; }
	// The string tables as they stood at the end: names and entry counts.
	[[nodiscard]] const std::vector<std::pair<std::string, int>> &GetStringTables() const { return m_StringTables; }

	[[nodiscard]] double GetTickInterval() const { return m_flTickInterval; }

private:
	friend class CContentsReader;

	std::string m_szWarning;
	std::optional<ServerInfo_t> m_ServerInfo;
	std::vector<ServerClass_t> m_Classes;
	double m_flTickInterval = 0.0;

	std::vector<EntityLife_t> m_Lives;              // by entity number, then by tick
	std::vector<std::uint32_t> m_FirstLife;         // per entity number: its first in m_Lives (MAX_ENTITIES + 1 of them)
	std::vector<std::array<std::string, ENTITY_EXTRA_COUNT>> m_Extras; // by class
	std::vector<std::vector<std::string>> m_Stats;                      // by class
	std::vector<std::pair<int, int>> m_ServerTicks;                    // (demo tick, the server's tick less it), from each change

	std::vector<EventKind_t> m_EventKinds;
	std::vector<GameEvent_t> m_Events;
	std::vector<UserMessage_t> m_UserMessages;
	std::vector<Menu_t> m_Menus;
	std::vector<Sound_t> m_Sounds;
	std::vector<std::string> m_SoundNames;
	std::vector<std::string> m_ModelNames;
	std::vector<std::string> m_ParticleNames;
	std::vector<std::string> m_EffectNames;
	std::vector<TempEntity_t> m_TempEntities;
	std::vector<PropValue_t> m_TempValues;
	std::vector<std::vector<FlatProp_t>> m_TempProps; // by class
	VoiceCodec_t m_VoiceCodec;
	std::vector<VoicePacket_t> m_VoicePackets;
	std::vector<std::byte> m_VoiceBytes;
	std::vector<Speech_t> m_Speech;
	std::vector<std::pair<int, std::string>> m_Prints;
	std::vector<std::pair<int, std::string>> m_ServerCommands;
	std::map<int, int> m_MessageCounts;
	std::map<int, int> m_UserMessageCounts;
	std::map<int, int> m_EntityMessageCounts;
	std::vector<std::string> m_Misreads;
	int m_nMisreads = 0;
	std::vector<std::pair<std::string, int>> m_StringTables;
};

// "svc_PacketEntities" for 26...: the protocol's message names.
[[nodiscard]] std::string_view MessageKindName( int nKind );

} // namespace xv::demo
