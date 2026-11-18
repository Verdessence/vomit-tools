//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What a demo put on its players' screens, in time order (D1, D3, D4): chat and the
//          other printed lines (radio, notices, console prints, centre text), the kill feed,
//          HUD messages (game_text and plugins' HudMsg), menus (ShowMenu, VGUIMenu), hints
//          (HintText, KeyHintText), the server's message dialogs (svc_Menu), screen fades,
//          rounds, and who is talking. Read out of CContents; texts that name a language
//          token ("#Cstrike_Chat_All") are put in words through the game's language file.
//          A SourceTV demo has only what went to everyone; a player's own demo has what went
//          to that player too.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "demo/Contents.h"
#include "demo/Players.h"

namespace xv::demo
{

//-----------------------------------------------------------------------------
// Colour codes, as chat and plugins write them in a text
//-----------------------------------------------------------------------------

enum class TextColor_t : std::uint8_t
{
	TEXT_COLOR_DEFAULT, // \x01: the chat's own
	TEXT_COLOR_TEAM,    // \x03: the sender's team's
	TEXT_COLOR_GREEN,   // \x04: the location's
	TEXT_COLOR_OLIVE,   // \x05
	TEXT_COLOR_RGB,     // \x07RRGGBB, \x08RRGGBBAA: a colour of its own
};

struct ColorRun_t
{
	std::string m_szText;
	TextColor_t m_nColor = TextColor_t::TEXT_COLOR_DEFAULT;
	std::uint32_t m_nRgba = 0xffffffff; // TEXT_COLOR_RGB's: red in the top byte, alpha in the lowest
};

// A text cut where its colour codes change; the codes themselves left out.
[[nodiscard]] std::vector<ColorRun_t> SplitColors( std::string_view szText );
// The text without its colour codes.
[[nodiscard]] std::string StripColors( std::string_view szText );

// A language token's text (the name without "#", any case); nullopt for one the files lack.
using Localize_t = std::function<std::optional<std::string>( std::string_view szToken )>;
// A game text as the client shows it: "#Token" put in words (Counter-Strike's "titles" tokens
// tried too), "%s1" to "%s4" replaced by the params (which may be tokens themselves).
[[nodiscard]] std::string FormatGameText( std::string_view szText, std::span<const std::string> params, const Localize_t &pfnLocalize );

//-----------------------------------------------------------------------------
// What was shown
//-----------------------------------------------------------------------------

enum class FeedKind_t : std::uint8_t
{
	FEED_KIND_CHAT,     // SayText2 / SayText: players' chat and plugins' chat lines
	FEED_KIND_RADIO,    // RadioText
	FEED_KIND_NOTICE,   // TextMsg to the notify area or the chat
	FEED_KIND_CONSOLE,  // TextMsg to the console, and the server's prints
	FEED_KIND_CENTER,   // TextMsg to the middle of the screen
	FEED_KIND_HINT,     // HintText: the box low in the middle
	FEED_KIND_KEY_HINT, // KeyHintText: the box at the right
	FEED_KIND_DIALOG,   // svc_Menu's message dialog: a line at the top left
};

struct FeedLine_t
{
	int m_nTick = 0;
	FeedKind_t m_nKind = FeedKind_t::FEED_KIND_CHAT;
	int m_nEntity = 0;   // who sent it (a player's entity); 0 for the server
	std::string m_szText; // in words, colour codes kept
	std::uint32_t m_nRgba = 0xffffffff; // a dialog's own colour
	double m_flSeconds = 0.0; // how long it stays (hints, centre text, dialogs); 0: the panel's own
	int m_nLevel = 0;         // a dialog's level
};

struct Kill_t
{
	int m_nTick = 0;
	int m_nVictim = 0;   // entities; 0 when the demo doesn't say who
	int m_nAttacker = 0; // 0 for the world
	std::string m_szWeapon;
	bool m_bHeadshot = false;
};

// A HUD message (game_text's keyvalues, or HudMsg's fields): where, how and how long.
struct HudText_t
{
	int m_nTick = 0;
	int m_nChannel = 0;
	float m_flX = -1.0f; // 0 to 1 across the screen; -1: in the middle
	float m_flY = -1.0f;
	std::uint32_t m_nColor1 = 0xffffffff; // red in the top byte, alpha in the lowest
	std::uint32_t m_nColor2 = 0xffffffff;
	int m_nEffect = 0; // HUD_EFFECT_*
	float m_flFadeIn = 0.0f;
	float m_flFadeOut = 0.0f;
	float m_flHold = 0.0f;
	float m_flFxTime = 0.0f;
	std::string m_szText;

	// How long it shows from its start: fading in (letter by letter for the scan-out), held,
	// fading out.
	[[nodiscard]] double GetDuration() const;
};
constexpr int HUD_EFFECT_FADE = 0;     // fades in and out whole
constexpr int HUD_EFFECT_FLICKER = 1;  // flickers between its colours as it shows
constexpr int HUD_EFFECT_SCAN_OUT = 2; // written out a letter at a time, each first in colour 2
// Channels a client keeps apart: a new message on one replaces the one there.
constexpr int HUD_CHANNEL_COUNT = 6;

// A game_text entity's message as a HUD message, from its keyvalues (pfnValue gives a key's
// value, empty when it has none): "\n" in its message is a line break, as the game reads it.
[[nodiscard]] HudText_t HudTextFromGameText( const std::function<std::string_view( std::string_view )> &pfnValue );
// The moment a HUD message is all in, held: what a still picture of it shows.
[[nodiscard]] double HudTextHeldAt( const HudText_t &text );

// One letter of a HUD message at a moment: its colour (alpha included); alpha 0 when unseen.
[[nodiscard]] std::uint32_t HudLetterColor( const HudText_t &text, int nLetter, double flSeconds );

// A menu (ShowMenu, joined over its parts): a numbered list the player picks from.
struct MenuShown_t
{
	int m_nTick = 0;
	int m_nSlots = 0;        // the keys that pick (bit 0 is key 1)
	double m_flSeconds = -1; // how long it stays; -1 until something replaces it
	int m_nPicked = -1;      // the tick the recorder picked from it ("menuselect"); -1 if not
	std::string m_szText;
};

// A VGUI panel the server opened or closed (the team menu, the MOTD).
struct PanelShown_t
{
	int m_nTick = 0;
	std::string m_szName;
	bool m_bShow = false;
	std::vector<std::pair<std::string, std::string>> m_Keys;
};

// A screen fade (the Fade message): to a colour or back from one.
struct Fade_t
{
	int m_nTick = 0;
	double m_flDuration = 0.0;
	double m_flHold = 0.0;
	int m_nFlags = 0; // FADE_FLAG_*
	std::uint32_t m_nRgba = 0x000000ff;

	// How much of the colour covers the screen (0 to 1) at a time after it began.
	[[nodiscard]] double CoverAt( double flSeconds ) const;
};
constexpr int FADE_FLAG_IN = 0x1;    // from the colour to clear
constexpr int FADE_FLAG_OUT = 0x2;   // from clear to the colour
constexpr int FADE_FLAG_MODULATE = 0x4;
constexpr int FADE_FLAG_STAY_OUT = 0x8; // stays on the colour until another fade
constexpr int FADE_FLAG_PURGE = 0x10;   // ends the fades before it

struct Round_t
{
	int m_nStart = 0; // round_start's tick
	int m_nEnd = -1;  // round_end's; -1 for a round the demo doesn't see end
	int m_nWinner = 0;
	std::string m_szMessage; // in words ("Terrorists Win!")
};

class CFeed
{
public:
	// commands: the recorder's console commands (a player's own demo), whose "menuselect"
	// closes the menu it picks from.
	[[nodiscard]] static CFeed Build( const CContents &contents, const CPlayers &players, const Localize_t &pfnLocalize, std::span<const Command_t> commands = {} );

	[[nodiscard]] const std::vector<FeedLine_t> &GetLines() const { return m_Lines; }
	[[nodiscard]] const std::vector<Kill_t> &GetKills() const { return m_Kills; }
	[[nodiscard]] const std::vector<HudText_t> &GetHudTexts() const { return m_HudTexts; }
	[[nodiscard]] const std::vector<MenuShown_t> &GetMenus() const { return m_Menus; }
	[[nodiscard]] const std::vector<PanelShown_t> &GetPanels() const { return m_Panels; }
	[[nodiscard]] const std::vector<Fade_t> &GetFades() const { return m_Fades; }
	[[nodiscard]] const std::vector<Round_t> &GetRounds() const { return m_Rounds; }

	// How many of a list's items begin at or before a tick (they are in tick order).
	template <typename T>
	[[nodiscard]] static std::size_t CountUpTo( const std::vector<T> &items, int nTick )
	{
		std::size_t nLow = 0, nHigh = items.size();
		while ( nLow < nHigh )
		{
			const std::size_t nMid = ( nLow + nHigh ) / 2;
			if ( items[ nMid ].m_nTick <= nTick )
				nLow = nMid + 1;
			else
				nHigh = nMid;
		}
		return nLow;
	}

	// What shows at a moment (seconds are the demo's tick interval): the HUD message on each
	// channel (null for none), with the seconds since it began.
	struct ShownText_t
	{
		const HudText_t *m_pText = nullptr;
		double m_flSeconds = 0.0;
	};
	[[nodiscard]] std::vector<ShownText_t> HudTextsAt( double flTick ) const;
	// The menu up at a moment; null for none.
	[[nodiscard]] const MenuShown_t *MenuAt( double flTick ) const;
	// The latest line of a kind still up at a moment (a hint, centre text, a dialog), when it
	// lasts a time of its own.
	[[nodiscard]] const FeedLine_t *LineAt( FeedKind_t nKind, double flTick ) const;
	// The fade over the screen at a moment: its colour with the cover as alpha; 0 for none.
	[[nodiscard]] std::uint32_t FadeAt( double flTick ) const;
	// The round going on at a tick; null before the first the demo sees.
	[[nodiscard]] const Round_t *RoundAt( int nTick ) const;

	[[nodiscard]] double GetTickInterval() const { return m_flTickInterval; }

private:
	std::vector<FeedLine_t> m_Lines;
	std::vector<Kill_t> m_Kills;
	std::vector<HudText_t> m_HudTexts;
	std::vector<MenuShown_t> m_Menus;
	std::vector<PanelShown_t> m_Panels;
	std::vector<Fade_t> m_Fades;
	std::vector<Round_t> m_Rounds;
	double m_flTickInterval = 0.015;
};

// The player whose user id a game event names, at a tick: its entity, 0 when no one had it.
[[nodiscard]] int EntityOfUserId( const CPlayers &players, int nUserId, int nTick );
// Who is talking at a tick (their entities), by the voice packets' senders.
[[nodiscard]] std::vector<int> SpeakersAt( const CContents &contents, int nTick );

} // namespace xv::demo
