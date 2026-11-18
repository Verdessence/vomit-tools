//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: VGUI HUD layouts (scripts/HudLayout.res, resource/ui/*.res) as data: which blocks
//          are elements, where an element lands on a screen of a given size, and the
//          scheme (resource/ClientScheme.res): its named colours, settings, fonts and
//          borders. Also what drawing an element as the game does needs: the HUD's icons
//          (scripts/mod_textures.txt), the language file's tokens, and what the well-known
//          elements show. Editing goes through the lossless KeyValues document; this only
//          reads it.
//
//===========================================================================//
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kv1/Kv1.h"

namespace xv::hud
{

// VGUI lays proportional panels out on a 640x480 grid scaled to the screen's height.
constexpr int BASE_TALL = 480;

// What a position or size value is measured from.
enum class Anchor_t
{
	ANCHOR_START,  // "16": from the parent's left or top
	ANCHOR_CENTER, // "c-50": from the parent's middle
	ANCHOR_END,    // "r40": back from the parent's right or bottom
	ANCHOR_FULL,   // "f0" (sizes): the parent's size less the value
	ANCHOR_PARENT, // "p0.5" (sizes): that fraction of the parent's size
};

struct Coord_t
{
	Anchor_t m_nAnchor = Anchor_t::ANCHOR_START;
	double m_flValue = 0.0; // in base units (a fraction for ANCHOR_PARENT)
};

// A "[$WIN32]"-style condition in words: "Windows", "not Xbox 360", "Linux and macOS"
// ($POSIX), parts joined by "and" / "or". Names it doesn't know stay as written.
[[nodiscard]] std::string DescribeCondition( std::string_view szCondition );
// Whether a condition holds where this program runs: the files are read as the game reads
// them on this system ($WIN32 on Windows, $POSIX and $LINUX on Linux, $POSIX and $OSX on
// macOS; never a console's).
[[nodiscard]] bool ConditionHolds( std::string_view szCondition );

[[nodiscard]] Coord_t ParseCoord( std::string_view szText );
// The text for coord, keeping its anchor's prefix ("c-50", "r40", "16").
[[nodiscard]] std::string FormatCoord( const Coord_t &coord );

struct Screen_t
{
	int m_nWide = 640;
	int m_nTall = 480;
	bool m_bProportional = true; // the HUD's elements are

	[[nodiscard]] double GetScale() const { return m_bProportional ? static_cast<double>( m_nTall ) / BASE_TALL : 1.0; }
};

// A position along one axis, in screen pixels, for a parent nParent pixels long.
[[nodiscard]] int ResolvePos( const Coord_t &coord, int nParent, double flScale );
// A size along one axis, in screen pixels.
[[nodiscard]] int ResolveSize( const Coord_t &coord, int nParent, double flScale );

struct Rect_t
{
	int m_nX = 0;
	int m_nY = 0;
	int m_nWide = 0;
	int m_nTall = 0;
};

struct Color_t
{
	int m_nR = 255;
	int m_nG = 255;
	int m_nB = 255;
	int m_nA = 255;
};

// "255 176 0 255" (alpha optional).
[[nodiscard]] std::optional<Color_t> ParseColor( std::string_view szText );

// One line of a scheme's Colors or BaseSettings block, as written.
struct SchemeValue_t
{
	std::string m_szName;
	std::string m_szValue;
};

// One of the faces a scheme font may draw with: the first whose "yres" holds the screen's
// height is taken, and one without "yres" fits any screen, scaled as the layout is.
struct FontEntry_t
{
	std::string m_szFace; // "name": the font family
	int m_nTall = 0;
	int m_nWeight = 0;
	bool m_bAntialias = false;
	bool m_bAdditive = false;
	bool m_bItalic = false;
	int m_nBlur = 0;
	int m_nScanlines = 0;
	std::optional<std::pair<int, int>> m_YRes; // screen heights it is for, both ends included
};

struct SchemeFont_t
{
	std::string m_szName;
	std::vector<FontEntry_t> m_Entries;
};

// A border: its name and everything under it, each key by its path ("Left/1/color").
struct SchemeBorder_t
{
	std::string m_szName;
	std::vector<SchemeValue_t> m_Keys;
};

// A scheme font on one screen: the family and its height in screen pixels.
struct Font_t
{
	std::string m_szFace;
	int m_nPixels = 0;
	int m_nWeight = 0;
	bool m_bAntialias = true;
	bool m_bAdditive = false;
	bool m_bItalic = false;
};

// The scheme: its Colors block and BaseSettings' entries (which may name a colour in turn),
// its fonts, borders and the font files it brings.
class CScheme
{
public:
	[[nodiscard]] static CScheme FromDocument( const kv1::CDocument &document );
	// A literal colour, or a name looked up (case-insensitively, as VGUI does).
	[[nodiscard]] std::optional<Color_t> Resolve( std::string_view szValue ) const;
	[[nodiscard]] std::size_t GetCount() const { return m_Colors.size(); }
	// The blocks as written, in the file's order.
	[[nodiscard]] const std::vector<SchemeValue_t> &GetColors() const { return m_ColorList; }
	[[nodiscard]] const std::vector<SchemeValue_t> &GetSettings() const { return m_SettingList; }
	[[nodiscard]] const std::vector<SchemeFont_t> &GetFonts() const { return m_Fonts; }
	[[nodiscard]] const std::vector<SchemeBorder_t> &GetBorders() const { return m_Borders; }
	// CustomFontFiles: the game paths of the font files ("resource/cs.ttf").
	[[nodiscard]] const std::vector<std::string> &GetFontFiles() const { return m_FontFiles; }
	// The face a named font draws with on a screen; nullopt for a name the scheme hasn't.
	[[nodiscard]] std::optional<Font_t> ResolveFont( std::string_view szName, const Screen_t &screen ) const;

private:
	std::map<std::string, std::string> m_Colors; // lower-case name -> value as written
	std::vector<SchemeValue_t> m_ColorList;
	std::vector<SchemeValue_t> m_SettingList;
	std::vector<SchemeFont_t> m_Fonts;
	std::vector<SchemeBorder_t> m_Borders;
	std::vector<std::string> m_FontFiles;
};

// A HUD icon (scripts/mod_textures.txt, scripts/hud_textures.txt): a character of a scheme
// font, or a piece of a sprite sheet.
struct Icon_t
{
	std::string m_szFont;
	std::string m_szCharacter;
	std::string m_szFile; // under materials/, without the extension
	Rect_t m_Rect;        // in the file's pixels
};
// The icons of a textures file, by lower-case name.
[[nodiscard]] std::map<std::string, Icon_t> ReadIcons( const kv1::CDocument &document );
// A language file's text as UTF-8: the files are UTF-16 with a byte order mark (little
// end first), or already UTF-8.
[[nodiscard]] std::string LanguageText( std::string_view szBytes );
// A language file's tokens (resource/<game>_english.txt, as UTF-8), by lower-case name
// without the "#".
[[nodiscard]] std::map<std::string, std::string> ReadTokens( const kv1::CDocument &document );

// What a well-known HUD element shows while playing, to draw a sample of it: the game's code
// fills these in, the layout only places them (icon_xpos, digit_xpos, text_xpos...).
struct Sample_t
{
	std::string_view m_szIcon;    // an icon's name
	std::string_view m_szDigits;
	std::string_view m_szIcon2;
	std::string_view m_szDigits2;
	std::string_view m_szLabel;
};
[[nodiscard]] const Sample_t *SampleFor( std::string_view szElement );

// What scripts/hudanimations.txt leaves each panel's properties at: by lower-case panel, then
// lower-case property ("fgcolor", "textcolor"...), the value its events most often settle on
// (the last "Animate" of the property in an event, by when it ends), fully clear ones aside.
// The game's HUD takes its colours from these more than from the scheme.
using Animations_t = std::map<std::string, std::map<std::string, std::string>>;
[[nodiscard]] Animations_t ReadAnimations( std::string_view szText );

// The variables a text has for the game to fill in: "%mapname%" (a panel's dialog variable)
// and "%s1" (a language string's argument), each once, without the signs.
[[nodiscard]] std::vector<std::string> FindVariables( std::string_view szText );
// The text with those given a value put in; the others stay as written. Names are matched
// case-insensitively (the map's keys are lower-case).
[[nodiscard]] std::string ReplaceVariables( std::string_view szText, const std::map<std::string, std::string> &values );

// One element: a block inside the layout's root block.
struct Element_t
{
	std::size_t m_nIndex = 0; // the block's index among the root's children
	std::string m_szName;     // the block's key
	std::string m_szControl;  // ControlName, when given
	Rect_t m_Rect;
	int m_nZ = 0;
	bool m_bVisible = true;
	std::optional<Color_t> m_BgColor;
	std::optional<Color_t> m_FgColor;
	std::string m_szLabel; // labelText, when given
	// For drawing it as the game does.
	// TextColor and IconColor, when given: what the game's HUD panels draw their caption and
	// icon with; TextFont and NumberFont likewise.
	std::optional<Color_t> m_TextColor;
	std::optional<Color_t> m_IconColor;
	std::string m_szTextFont;
	std::string m_szNumberFont;
	std::string m_szFont;          // "font", when given
	std::string m_szAlignment;     // "textAlignment", lower-case ("west", "center", "north-east"...)
	std::string m_szImage;         // "image", when given: under materials/vgui/
	bool m_bScaleImage = false;    // the image fills the panel
	int m_nPaintBackground = -1;   // PaintBackgroundType (2: rounded corners); -1 when not given
	// Where its parts go, from its top left corner in screen pixels, for the keys it has.
	std::optional<std::pair<int, int>> m_Icon;   // icon_xpos, icon_ypos
	std::optional<std::pair<int, int>> m_Digit;  // digit_xpos, digit_ypos
	std::optional<std::pair<int, int>> m_Icon2;  // icon2_xpos, icon2_ypos
	std::optional<std::pair<int, int>> m_Digit2; // digit2_xpos, digit2_ypos
	std::optional<std::pair<int, int>> m_Text;   // text_xpos, text_ypos
	std::optional<Rect_t> m_Bar;                 // bar_xpos, bar_ypos, bar_width, bar_height (the ammunition's divider)
	// The file's own panel (a .res of resource/ui starts with the dialog its controls are
	// in): the others are placed inside it. m_bMenu: one of the game's full-screen menus
	// (team, class, buy), which the game centres and gives a backdrop of its own.
	bool m_bDialog = false;
	bool m_bMenu = false;
	// A game menu's item (resource/GameMenu.res): placed by the menu, not by keys of its own.
	bool m_bMenuItem = false;
};

// The layout's root: the first block of the file.
[[nodiscard]] const kv1::CNode *LayoutRoot( const kv1::CDocument &document );
[[nodiscard]] kv1::CNode *LayoutRoot( kv1::CDocument &document );
// Whether the document looks like a HUD or panel layout: a root block holding blocks with
// position keys.
[[nodiscard]] bool IsLayout( const kv1::CDocument &document );
// The value of key on an element: the last one written wins, as when VGUI reads it.
[[nodiscard]] std::optional<std::string> GetKey( const kv1::CNode &element, std::string_view szKey );
// resource/GameMenu.res: the main menu's items, a block each with a label and a command.
[[nodiscard]] bool IsGameMenu( const kv1::CDocument &document );
// Its items as the main menu stacks them, bottom left, in the scheme's "MenuLarge" font at
// "MainMenu.MenuItemHeight" a line. bInGame: as the menu is with a map loaded (items marked
// OnlyInGame show; else they are laid out but not visible). Where the menu sits is the game's
// code: these places are an approximation.
[[nodiscard]] std::vector<Element_t> LayOutGameMenu( const kv1::CDocument &document, const Screen_t &screen, const CScheme *pScheme, bool bInGame );

// The root's element blocks, laid out on screen, in file order. In a panel's file the first
// block is the panel itself when its ControlName is no plain control's: the rest are placed
// inside it.
[[nodiscard]] std::vector<Element_t> LayOut( const kv1::CDocument &document, const Screen_t &screen, const CScheme *pScheme = nullptr );

// The xpos and ypos that put an element's top left corner at pixel (nX, nY), keeping the
// anchors its values already have.
struct MovedPos_t
{
	std::string m_szX;
	std::string m_szY;
};
// pParent: the panel the element is in (a panel file's dialog), when it isn't the screen: the
// pixel is then measured from its corner. Anchors still measure the screen, unless the element
// has "proportionalToParent" "1", which measures the panel.
[[nodiscard]] MovedPos_t MoveTo( const kv1::CNode &element, int nX, int nY, const Screen_t &screen, const Rect_t *pParent = nullptr );

} // namespace xv::hud
