//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The demo viewer's HUD and panels (D1 to D4). In the picture, through a player's
//          eyes: the game's own HUD (its HudLayout.res and scheme, drawn by the HUD editor's
//          renderer with the demo's numbers in place of its samples), the chat, the kill
//          feed, HUD messages on their channels, hints, menus, the server's dialogs, centre
//          text, screen fades and who is talking. Beside the picture, the same as logs that
//          follow the playhead (a click on a line puts the playhead there).
//
//===========================================================================//
#pragma once

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QColor>
#include <QTextBrowser>

#include "HudEditor.h"
#include "demo/Feed.h"

class QPainter;

// A text with chat colour codes as HTML: the team colour for \x03, the given colours for the
// rest.
[[nodiscard]] QString DemoColoredHtml( std::string_view szText, const QColor &team, const QColor &normal );

// A HUD message where its x and y put it in rect, each letter in its colour seconds after it
// began, in font.
void DemoPaintHudText( QPainter &painter, const QRect &rect, const xv::demo::HudText_t &text, double flSeconds, const QFont &font );
// The game's font for HUD messages (Trebuchet MS, 24 units tall, heavy) for a screen rect
// tall, when the scheme isn't at hand.
[[nodiscard]] QFont DemoHudTextFont( int nTall );

// A log of lines by tick, showing those up to the playhead.
class CDemoLog : public QTextBrowser
{
	Q_OBJECT
public:
	struct Entry_t
	{
		int m_nTick = 0;
		QString m_szHtml;
	};
	explicit CDemoLog( QWidget *pParent = nullptr );
	// The lines, in tick order; szTime makes each one's time.
	void SetEntries( std::vector<Entry_t> entries, std::function<QString( int )> pfnTime );
	// Shows the lines up to a tick, kept scrolled to the newest unless scrolled back.
	void ShowUpTo( int nTick );
	[[nodiscard]] int GetShownCount() const { return m_nShown; }

signals:
	void TickPicked( int nTick );
	// A line's own link ("item:N" in its HTML) was clicked.
	void ItemPicked( int nItem );

private:
	[[nodiscard]] QString LineHtml( const Entry_t &entry ) const;

	std::vector<Entry_t> m_Entries;
	std::function<QString( int )> m_pfnTime;
	int m_nShown = 0;
};

// What the HUD shows of the player it is for, at a moment.
struct DemoHudMoment_t
{
	double m_flTick = 0.0;
	int m_nPlayer = 0; // the entity seen through; 0 for none
	int m_nTeam = 0;
	bool m_bAlive = false;
	int m_nHealth = 0;
	std::optional<int> m_nArmor;
	bool m_bHelmet = false;
	std::optional<int> m_nMoney;
	std::optional<int> m_nClip;
	std::optional<int> m_nReserve;
	std::string m_szAmmoIcon; // mod_textures' name ("ammo_556")
	std::optional<double> m_flRoundLeft; // seconds on the round's clock
	// What went to this player alone (a player's own demo, through the recorder's eyes, or a
	// SourceTV demo, where all of it went to everyone).
	bool m_bOwnMessages = false;
	std::vector<int> m_Speakers; // talking now
	std::vector<int> m_MutedSpeakers;
	bool m_bCrosshair = true;    // seen through the player's eyes
};

// One player's line on the scoreboard.
struct DemoScoreRow_t
{
	int m_nEntity = 0;
	QString m_szName;
	int m_nTeam = 0;
	std::optional<int> m_nScore, m_nDeaths, m_nPing;
	bool m_bAlive = false;
	bool m_bTalking = false;
	bool m_bMuted = false;
	bool m_bFollowed = false; // the player the view follows or looks through
};

// The scoreboard (Tab) at a moment.
struct DemoScoreboard_t
{
	QString m_szServer;
	QString m_szMap;
	std::array<QString, 4> m_TeamNames;            // by team number
	std::array<std::optional<int>, 4> m_TeamScores; // likewise
	std::optional<double> m_flRoundLeft;
	std::vector<DemoScoreRow_t> m_Rows; // everyone there, on a team or not
};

class CDemoHud
{
public:
	CDemoHud();
	~CDemoHud();

	// The game's files: HudLayout.res, the scheme, fonts, icons, the language. Null for none;
	// the HUD then draws its own plain way.
	void SetFiles( CHudEditor::ReadFile_t pfnRead );
	[[nodiscard]] bool HasGameHud() const { return m_pLayout != nullptr; }
	// A language token in words; nullopt when the files haven't it.
	[[nodiscard]] std::optional<std::string> Localize( std::string_view szToken ) const;

	// Everything over the picture, for one player at a moment. pfnName names a player's
	// entity at the moment; pfnTeam gives its team.
	void Paint( QPainter &painter, const QRect &rect, const xv::demo::CFeed &feed, const DemoHudMoment_t &moment, const std::function<QString( int )> &pfnName, const std::function<int( int )> &pfnTeam );
	// The scoreboard over rect: laid out by the game's resource/ui/scoreboard.res when it has
	// one (its backdrop, headings and labels; the players in rows down from its first row's
	// cells, CTPlayerName0 and the others), else plainly. Each team's players by score, then
	// fewer deaths, as the game sorts them.
	void PaintScoreboard( QPainter &painter, const QRect &rect, const DemoScoreboard_t &board );
	[[nodiscard]] bool HasGameScoreboard() const { return m_pScoreboard != nullptr; }

private:
	[[nodiscard]] QFont FontFor( std::initializer_list<const char *> names, double flScale ) const;
	void PaintHudTexts( QPainter &painter, const QRect &rect, const xv::demo::CFeed &feed, double flTick, double flScale ) const;
	void PaintChat( QPainter &painter, const QRect &rect, const xv::demo::CFeed &feed, const DemoHudMoment_t &moment, double flScale, const std::function<int( int )> &pfnTeam ) const;
	void PaintKills( QPainter &painter, const QRect &rect, const xv::demo::CFeed &feed, double flTick, double flScale, const std::function<QString( int )> &pfnName, const std::function<int( int )> &pfnTeam ) const;
	void PaintBoxes( QPainter &painter, const QRect &rect, const xv::demo::CFeed &feed, const DemoHudMoment_t &moment, double flScale ) const;
	void PaintPlainHud( QPainter &painter, const QRect &rect, const DemoHudMoment_t &moment, double flScale ) const;
	void PaintPlainScoreboard( QPainter &painter, const QRect &rect, const DemoScoreboard_t &board, double flScale ) const;
	// A variable of the scoreboard's labels, set only when it changes (setting one redoes the
	// layout's variable table).
	void SetScoreboardVariable( const char *pszName, const QString &szValue );

	std::unique_ptr<CHudEditor> m_pLayout; // the game's HudLayout.res, never shown
	CHudEditor::ReadFile_t m_pfnRead;
	std::map<std::string, std::string> m_Tokens; // lower-case
	QSize m_ScreenFor; // the size the layout was laid out for
	std::unique_ptr<CHudEditor> m_pScoreboard; // resource/ui/scoreboard.res, never shown
	QSize m_ScoreboardFor;
	std::map<std::string, QString> m_ScoreboardVariables;
};
