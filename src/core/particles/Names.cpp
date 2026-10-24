//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Names.h.
//
//===========================================================================//
#include "Names.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>

#include "kv1/Kv1.h"
#include "particles/Simulation.h"

namespace xv::particles
{

namespace
{

//-----------------------------------------------------------------------------
// Purpose: ASCII lower case, as the game compares names.
//-----------------------------------------------------------------------------
std::string Lower( std::string_view szText )
{
	std::string szOut{ szText };
	std::ranges::transform( szOut, szOut.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: The first block named name among nodes, or nullptr.
//-----------------------------------------------------------------------------
const kv1::CNode *FindBlock( const std::vector<kv1::CNode> &nodes, std::string_view szName )
{
	for ( const kv1::CNode &node : nodes )
	{
		if ( node.IsBlock() && Lower( node.GetKey() ) == Lower( szName ) )
		{
			return &node;
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
InputKind_t KindOf( std::string_view szKind )
{
	static const std::map<std::string, InputKind_t> kinds{
		{ "share", InputKind_t::INPUT_KIND_SHARE },
		{ "seconds", InputKind_t::INPUT_KIND_SECONDS },
		{ "degrees", InputKind_t::INPUT_KIND_DEGREES },
		{ "degreespersecond", InputKind_t::INPUT_KIND_DEGREES_PER_SECOND },
		{ "persecond", InputKind_t::INPUT_KIND_PER_SECOND },
		{ "alpha", InputKind_t::INPUT_KIND_ALPHA },
		{ "controlpoint", InputKind_t::INPUT_KIND_CONTROL_POINT },
		{ "count", InputKind_t::INPUT_KIND_COUNT },
	};
	const auto found = kinds.find( Lower( szKind ) );
	return found == kinds.end() ? InputKind_t::INPUT_KIND_NUMBER : found->second;
}

//-----------------------------------------------------------------------------
// Purpose: The key a kind goes by in the language file ("unit:seconds").
//-----------------------------------------------------------------------------
std::string_view KindKey( InputKind_t nKind )
{
	switch ( nKind )
	{
		case InputKind_t::INPUT_KIND_SHARE: return "share";
		case InputKind_t::INPUT_KIND_SECONDS: return "seconds";
		case InputKind_t::INPUT_KIND_DEGREES: return "degrees";
		case InputKind_t::INPUT_KIND_DEGREES_PER_SECOND: return "degreespersecond";
		case InputKind_t::INPUT_KIND_PER_SECOND: return "persecond";
		case InputKind_t::INPUT_KIND_ALPHA: return "alpha";
		case InputKind_t::INPUT_KIND_CONTROL_POINT: return "controlpoint";
		case InputKind_t::INPUT_KIND_COUNT: return "count";
		case InputKind_t::INPUT_KIND_NUMBER: break;
	}
	return "number";
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Reads both files.
//-----------------------------------------------------------------------------
CNames CNames::Parse( std::string_view szLanguage, std::string_view szInputs )
{
	CNames names;
	const kv1::CDocument language = kv1::Parse( szLanguage ).m_Document;
	if ( const kv1::CNode *pLang = FindBlock( language.m_Roots, "lang" ) )
	{
		if ( const kv1::CNode *pTokens = FindBlock( pLang->GetChildren(), "Tokens" ) )
		{
			for ( const kv1::CNode &token : pTokens->GetChildren() )
			{
				if ( !token.IsPair() )
				{
					continue;
				}
				const std::string szKey = Lower( token.GetKey() );
				names.m_Tokens[ szKey ] = std::string{ token.GetValue() };
				// "set:<setting>@<value>" and "mat:<parameter>@<value>": one of its named values.
				if ( const auto nAt = szKey.rfind( '@' ); ( szKey.starts_with( "set:" ) || szKey.starts_with( "mat:" ) ) && nAt != std::string::npos )
				{
					const std::string szOriginal{ token.GetKey() };
					names.m_Choices[ szKey.substr( 4, nAt - 4 ) ].push_back( { szOriginal.substr( nAt + 1 ), std::string{ token.GetValue() } } );
				}
			}
		}
	}
	const kv1::CDocument inputs = kv1::Parse( szInputs ).m_Document;
	if ( const kv1::CNode *pInputs = FindBlock( inputs.m_Roots, "particle_inputs" ) )
	{
		for ( const kv1::CNode &input : pInputs->GetChildren() )
		{
			if ( input.IsPair() )
			{
				names.m_Inputs[ Lower( input.GetKey() ) ] = KindOf( input.GetValue() );
			}
		}
	}
	return names;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const std::string *CNames::Token( std::string_view szKey ) const
{
	const auto found = m_Tokens.find( Lower( szKey ) );
	return found == m_Tokens.end() ? nullptr : &found->second;
}

//-----------------------------------------------------------------------------
// Purpose: The list's own entry first (a name in two lists), then the name's.
//-----------------------------------------------------------------------------
PlainName_t CNames::Function( Category_t nCategory, std::string_view szFunction ) const
{
	const std::string szCurrent{ CurrentFunctionName( nCategory, szFunction ) };
	for ( const std::string &szKey : { "fn:" + std::string{ AttributeName( nCategory ) } + "|" + szCurrent, "fn:" + szCurrent } )
	{
		if ( const std::string *pName = Token( szKey ) )
		{
			const std::string *pTip = Token( szKey + "#tip" );
			return { *pName, pTip ? *pTip : std::string{} };
		}
	}
	return { TidyName( szCurrent, true ), {} };
}

//-----------------------------------------------------------------------------
// Purpose: The function's own entry first, then the setting's.
//-----------------------------------------------------------------------------
PlainName_t CNames::Setting( std::string_view szFunction, std::string_view szAttribute ) const
{
	std::string szCurrent{ szFunction };
	for ( Category_t nCategory : CATEGORIES )
	{
		if ( const std::string_view szName = CurrentFunctionName( nCategory, szFunction ); szName != szFunction )
		{
			szCurrent = szName;
			break;
		}
	}
	PlainName_t plain{ TidyName( szAttribute ), {} };
	const std::string szSetting = "set:" + std::string{ szAttribute };
	const std::string szSpecific = "set:" + szCurrent + "|" + std::string{ szAttribute };
	if ( const std::string *pName = szCurrent.empty() ? nullptr : Token( szSpecific ) )
	{
		plain.m_szName = *pName;
	}
	else if ( const std::string *pGeneral = Token( szSetting ) )
	{
		plain.m_szName = *pGeneral;
	}
	if ( const std::string *pTip = szCurrent.empty() ? nullptr : Token( szSpecific + "#tip" ) )
	{
		plain.m_szMeaning = *pTip;
	}
	else if ( const std::string *pGeneralTip = Token( szSetting + "#tip" ) )
	{
		plain.m_szMeaning = *pGeneralTip;
	}
	return plain;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
PlainName_t CNames::MaterialParameter( std::string_view szKey ) const
{
	const std::string szToken = "mat:" + std::string{ szKey };
	const std::string *pName = Token( szToken );
	const std::string *pTip = Token( szToken + "#tip" );
	return { pName ? *pName : TidyMaterialKey( szKey ), pTip ? *pTip : std::string{} };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
PlainName_t CNames::Shader( std::string_view szShader ) const
{
	const std::string szToken = "shader:" + std::string{ szShader };
	const std::string *pName = Token( szToken );
	const std::string *pTip = Token( szToken + "#tip" );
	return { pName ? *pName : TidyName( szShader ), pTip ? *pTip : std::string{} };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CNames::Sound( std::string_view szKey ) const
{
	const std::string *pName = Token( "snd:" + std::string{ szKey } );
	return pName ? *pName : TidyName( szKey );
}

//-----------------------------------------------------------------------------
// Purpose: A texture flag, format or resource ("flag:CLAMPS" is "Clamp S"), and what it does.
//-----------------------------------------------------------------------------
PlainName_t CNames::Texture( std::string_view szKey ) const
{
	const std::string szToken = "vtf:" + std::string{ szKey };
	const std::string *pName = Token( szToken );
	const std::string *pTip = Token( szToken + "#tip" );
	const std::size_t nColon = szKey.rfind( ':' );
	return { pName ? *pName : std::string{ szKey.substr( nColon == std::string_view::npos ? 0 : nColon + 1 ) }, pTip ? *pTip : std::string{} };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const std::vector<NamedValue_t> &CNames::Choices( std::string_view szAttribute ) const
{
	static const std::vector<NamedValue_t> none;
	const auto found = m_Choices.find( Lower( szAttribute ) );
	return found == m_Choices.end() ? none : found->second;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CNames::FieldName( int nField ) const
{
	const std::string *pName = Token( "field:" + std::to_string( nField ) );
	return pName ? *pName : "Field " + std::to_string( nField );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
InputKind_t CNames::Input( std::string_view szAttribute ) const
{
	const auto found = m_Inputs.find( Lower( szAttribute ) );
	return found == m_Inputs.end() ? InputKind_t::INPUT_KIND_NUMBER : found->second;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CNames::Unit( InputKind_t nKind ) const
{
	const std::string *pUnit = Token( "unit:" + std::string{ KindKey( nKind ) } );
	return pUnit ? *pUnit : std::string{};
}

//-----------------------------------------------------------------------------
// Purpose: A key made readable.
//-----------------------------------------------------------------------------
std::string TidyName( std::string_view szKey, bool bFunction )
{
	std::string szText{ szKey };
	if ( bFunction && Lower( szText ).starts_with( "render_" ) )
	{
		szText.erase( 0, 7 );
	}
	// Notes in brackets, "0/1", "0=random 1=..." and "0-2 X/Y/Z" say how to type a value: the
	// simple interface shows them another way.
	static const std::regex notes{ R"(\([^)]*\)|\s0/1\b|\s\d+\s*=.*$|\s0-2 X/Y/Z)", std::regex::icase };
	szText = std::regex_replace( szText, notes, "" );
	std::string szSpaced;
	for ( std::size_t i = 0; i < szText.size(); i++ )
	{
		const char c = szText[ i ];
		if ( c == '#' )
		{
			continue;
		}
		if ( c == '_' )
		{
			szSpaced += ' ';
			continue;
		}
		// camelCase: a capital after a small letter starts a word.
		if ( i > 0 && std::isupper( static_cast<unsigned char>( c ) ) && std::islower( static_cast<unsigned char>( szText[ i - 1 ] ) ) )
		{
			szSpaced += ' ';
		}
		szSpaced += c;
	}
	static const std::set<std::string> small{ "a", "an", "and", "as", "at", "by", "for", "of", "on", "or", "the", "to", "via" };
	std::string szOut;
	std::size_t nWord = 0;
	for ( std::size_t i = 0; i < szSpaced.size(); )
	{
		if ( szSpaced[ i ] == ' ' )
		{
			i++;
			continue;
		}
		const std::size_t nEnd = std::min( szSpaced.find( ' ', i ), szSpaced.size() );
		std::string szWord = szSpaced.substr( i, nEnd - i );
		i = nEnd;
		const bool bHasCapitals = std::any_of( szWord.begin() + 1, szWord.end(), []( unsigned char c ) { return std::isupper( c ); } );
		if ( nWord > 0 && small.contains( Lower( szWord ) ) )
		{
			szWord = Lower( szWord );
		}
		else if ( !bHasCapitals )
		{
			szWord[ 0 ] = static_cast<char>( std::toupper( static_cast<unsigned char>( szWord[ 0 ] ) ) );
		}
		szOut += ( nWord++ > 0 ? " " : "" ) + szWord;
	}
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: Material keys run their words together, so they are split at the words they are
//          usually made of, longest first; underscores split too.
//-----------------------------------------------------------------------------
std::string TidyMaterialKey( std::string_view szKey )
{
	static const std::vector<std::string_view> WORDS = {
		"abovewater", "additive", "albedo", "alpha", "ambient", "amount", "angle", "animated", "base", "blend", "blur",
		"boost", "bottom", "bump", "center", "cheap", "clamp", "color", "compile", "contrast", "control", "coverage",
		"cubemap", "cull", "decal", "depth", "detail", "distance", "dudv", "dynamic", "edge", "end", "entities", "env",
		"expensive", "exponent", "factor", "fade", "flash", "flat", "flow", "fog", "force", "frame", "fresnel", "full",
		"bright", "glow", "half", "lambert", "hdr", "height", "ignore", "illum", "light", "lit", "lod", "map", "mask",
		"material", "max", "min", "mip", "mod", "mode", "model", "noise", "normal", "offset", "opaque", "origin",
		"orientation", "outline", "over", "parallax", "phong", "pos", "prop", "ranges", "range", "reference",
		"reflect", "refract", "render", "rim", "rotate", "saturation", "scale", "scroll", "self", "size", "speed",
		"sphere", "sprite", "srgb", "ssbump", "start", "surface", "test", "texture", "time", "tint", "tool",
		"transform", "translate", "translucent", "two", "vertex", "warp", "water", "write", "allow", "no", "z"
	};
	std::string szLower = Lower( szKey );
	while ( !szLower.empty() && ( szLower.front() == '$' || szLower.front() == '%' ) )
	{
		szLower.erase( 0, 1 );
	}
	std::string szSpaced;
	std::string szLoose; // letters that match no word, kept as one word
	const auto flush = [ & ] {
		if ( !szLoose.empty() )
		{
			szSpaced += ( szSpaced.empty() ? "" : " " ) + szLoose;
			szLoose.clear();
		}
	};
	for ( std::size_t i = 0; i < szLower.size(); )
	{
		if ( szLower[ i ] == '_' || szLower[ i ] == ' ' )
		{
			flush();
			i++;
			continue;
		}
		std::string_view szBest;
		for ( std::string_view szWord : WORDS )
		{
			if ( szWord.size() > szBest.size() && std::string_view{ szLower }.substr( i ).starts_with( szWord ) )
			{
				szBest = szWord;
			}
		}
		if ( szBest.empty() )
		{
			szLoose += szLower[ i++ ];
			continue;
		}
		flush();
		szSpaced += ( szSpaced.empty() ? "" : " " ) + std::string{ szBest };
		i += szBest.size();
	}
	flush();
	return TidyName( szSpaced );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsFieldSetting( std::string_view szAttribute )
{
	const std::string szLower = Lower( szAttribute );
	return szLower == "output field" || szLower == "input field" || szLower == "oscillation field" || szLower == "rotation field" || szLower == "inherited field" ||
		szLower == "rate field";
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsAdvancedSetting( std::string_view szFunction, std::string_view szAttribute )
{
	static const std::set<std::string> advanced{
		"maximum time step",
		"maximum sim tick rate",
		"minimum sim tick rate",
		"minimum rendered frames",
		"time to sleep when not drawn",
		"sort particles",
		"batch particle systems",
		"view model effect",
		"group id",
		"preventnamebasedlookup",
		"cull_cost",
		"max constraint passes",
		"los collision group",
		"los failure scalar",
		"ensure line of sight",
		"maximum trace length",
		"emitter lifetime start time (seconds)",
		"emitter lifetime end time (seconds)",
		"control point to disable rendering if it is the camera",
		"orientation control point",
		"speed_random_exponent",
		"lifetime_random_exponent",
		"radius_random_exponent",
		"alpha_random_exponent",
		"rotation_random_exponent",
		"rotation_speed_random_exponent",
		"yaw_random_exponent",
		"length_random_exponent",
		"fade in time exponent",
		"fade out time exponent",
		"start_fadeout_exponent",
		"end_fadeout_exponent",
	};
	const std::string szLower = Lower( szAttribute );
	(void)szFunction;
	return IsCommonOperatorAttribute( szAttribute ) || szLower.starts_with( "visibility " ) || advanced.contains( szLower );
}

} // namespace xv::particles
