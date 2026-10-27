//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Fgd.h.
//
//===========================================================================//
#include "Fgd.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>

#include "util/Numbers.h"
#include "vfs/Vfs.h"

namespace xv::map
{

namespace
{

constexpr int MAX_INCLUDE_DEPTH = 16;
constexpr int MAX_BASE_DEPTH = 32;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Lower( std::string_view szText )
{
	std::string sz{ szText };
	for ( char &ch : sz )
	{
		if ( ch >= 'A' && ch <= 'Z' )
		{
			ch = static_cast<char>( ch - 'A' + 'a' );
		}
	}
	return sz;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsBlank( char ch )
{
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\v' || ch == '\f';
}

//-----------------------------------------------------------------------------
// Purpose: Characters that stand alone in an FGD.
//-----------------------------------------------------------------------------
bool IsPunctuation( char ch )
{
	return ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == ',' || ch == ':' || ch == '=' || ch == '@' || ch == '"';
}

enum class FgdTokenKind_t
{
	FGD_TOKEN_KIND_END,
	FGD_TOKEN_KIND_WORD,
	FGD_TOKEN_KIND_STRING,
	FGD_TOKEN_KIND_DIRECTIVE, // "@pointclass", without the '@'
	FGD_TOKEN_KIND_PUNCTUATION,
};

struct FgdToken_t
{
	FgdTokenKind_t m_nKind = FgdTokenKind_t::FGD_TOKEN_KIND_END;
	std::string_view m_szText;
	std::size_t m_nLine = 0;

	[[nodiscard]] bool Is( char ch ) const { return m_nKind == FgdTokenKind_t::FGD_TOKEN_KIND_PUNCTUATION && m_szText.size() == 1 && m_szText[ 0 ] == ch; }
	[[nodiscard]] bool IsValue() const { return m_nKind == FgdTokenKind_t::FGD_TOKEN_KIND_WORD || m_nKind == FgdTokenKind_t::FGD_TOKEN_KIND_STRING; }
};

class CFgdLexer
{
public:
	explicit CFgdLexer( std::string_view szText )
		: m_szText( szText )
	{
		if ( m_szText.starts_with( "\xEF\xBB\xBF" ) )
		{
			m_nPos = 3;
		}
		m_Next = Read();
	}

	[[nodiscard]] const FgdToken_t &Peek() const { return m_Next; }
	FgdToken_t Take()
	{
		FgdToken_t token = m_Next;
		m_Next = Read();
		return token;
	}

private:
	FgdToken_t Read()
	{
		while ( m_nPos < m_szText.size() )
		{
			const char ch = m_szText[ m_nPos ];
			if ( ch == '\n' )
			{
				m_nLine++;
				m_nPos++;
			}
			else if ( IsBlank( ch ) )
			{
				m_nPos++;
			}
			else if ( ch == '/' && m_nPos + 1 < m_szText.size() && m_szText[ m_nPos + 1 ] == '/' )
			{
				while ( m_nPos < m_szText.size() && m_szText[ m_nPos ] != '\n' )
				{
					m_nPos++;
				}
			}
			else
			{
				break;
			}
		}
		FgdToken_t token;
		token.m_nLine = m_nLine;
		if ( m_nPos >= m_szText.size() )
		{
			return token;
		}
		const std::size_t nStart = m_nPos;
		const char ch = m_szText[ m_nPos ];
		if ( ch == '"' )
		{
			// No escapes: an FGD string ends at the next quote.
			const std::size_t nEnd = m_szText.find( '"', m_nPos + 1 );
			const std::size_t nClose = nEnd == std::string_view::npos ? m_szText.size() : nEnd;
			token.m_nKind = FgdTokenKind_t::FGD_TOKEN_KIND_STRING;
			token.m_szText = m_szText.substr( nStart + 1, nClose - nStart - 1 );
			m_nLine += static_cast<std::size_t>( std::count( token.m_szText.begin(), token.m_szText.end(), '\n' ) );
			m_nPos = nEnd == std::string_view::npos ? m_szText.size() : nEnd + 1;
			return token;
		}
		if ( ch == '@' )
		{
			m_nPos++;
			while ( m_nPos < m_szText.size() && !IsBlank( m_szText[ m_nPos ] ) && !IsPunctuation( m_szText[ m_nPos ] ) )
			{
				m_nPos++;
			}
			token.m_nKind = FgdTokenKind_t::FGD_TOKEN_KIND_DIRECTIVE;
			token.m_szText = m_szText.substr( nStart + 1, m_nPos - nStart - 1 );
			return token;
		}
		// A '+' joins strings, unless a number follows it.
		if ( IsPunctuation( ch ) || ( ch == '+' && !( m_nPos + 1 < m_szText.size() && m_szText[ m_nPos + 1 ] >= '0' && m_szText[ m_nPos + 1 ] <= '9' ) ) )
		{
			m_nPos++;
			token.m_nKind = FgdTokenKind_t::FGD_TOKEN_KIND_PUNCTUATION;
			token.m_szText = m_szText.substr( nStart, 1 );
			return token;
		}
		while ( m_nPos < m_szText.size() && !IsBlank( m_szText[ m_nPos ] ) && !IsPunctuation( m_szText[ m_nPos ] ) && !( m_szText[ m_nPos ] == '/' && m_nPos + 1 < m_szText.size() && m_szText[ m_nPos + 1 ] == '/' ) )
		{
			m_nPos++;
		}
		token.m_nKind = FgdTokenKind_t::FGD_TOKEN_KIND_WORD;
		token.m_szText = m_szText.substr( nStart, m_nPos - nStart );
		return token;
	}

	std::string_view m_szText;
	std::size_t m_nPos = 0;
	std::size_t m_nLine = 1;
	FgdToken_t m_Next;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Trim( std::string sz )
{
	while ( !sz.empty() && IsBlank( sz.back() ) )
	{
		sz.pop_back();
	}
	std::size_t nStart = 0;
	while ( nStart < sz.size() && IsBlank( sz[ nStart ] ) )
	{
		nStart++;
	}
	return sz.substr( nStart );
}

//-----------------------------------------------------------------------------
// Purpose: The file's bytes, or nullopt.
//-----------------------------------------------------------------------------
std::optional<std::string> ReadText( const std::filesystem::path &path )
{
	std::ifstream stream{ path, std::ios::binary };
	if ( !stream )
	{
		return std::nullopt;
	}
	return std::string{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
}

} // namespace

class CFgdParser
{
public:
	CFgdParser( CFgd &fgd, std::string_view szText, const std::string &szFile, const CFgd::Includer_t &pfnInclude )
		: m_Fgd( fgd ),
		  m_Lexer( szText ),
		  m_szFile( szFile ),
		  m_pfnInclude( pfnInclude )
	{
	}

	void Parse();

private:
	void Error( const FgdToken_t &token, std::string szMessage, bool bError = true )
	{
		m_Fgd.AddDiagnostic( m_szFile, token.m_nLine, bError, std::move( szMessage ) );
	}
	bool Expect( char ch, std::string_view szWhere );
	std::string ReadString();
	std::optional<std::string> ReadField();
	void SkipTo( char ch );
	void SkipBlock();
	void SkipToDirective();
	void ParseClass( FgdClassKind_t nKind, const FgdToken_t &start );
	bool ParseBody( FgdClass_t &fgdClass );
	bool ParseChoices( FgdKey_t &key );
	std::vector<std::string> ParseArguments();

	CFgd &m_Fgd;
	CFgdLexer m_Lexer;
	std::string m_szFile;
	const CFgd::Includer_t &m_pfnInclude;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CFgdParser::Expect( char ch, std::string_view szWhere )
{
	if ( m_Lexer.Peek().Is( ch ) )
	{
		m_Lexer.Take();
		return true;
	}
	Error( m_Lexer.Peek(), std::string{ "expected '" } + ch + "' " + std::string{ szWhere } + ", found '" + std::string{ m_Lexer.Peek().m_szText } + "'" );
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: A value: a string (joined to following strings with '+') or a word.
//-----------------------------------------------------------------------------
std::string CFgdParser::ReadString()
{
	const FgdToken_t token = m_Lexer.Take();
	std::string sz{ token.m_szText };
	if ( token.m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_STRING )
	{
		return sz;
	}
	while ( m_Lexer.Peek().Is( '+' ) )
	{
		m_Lexer.Take();
		if ( m_Lexer.Peek().m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_STRING )
		{
			Error( m_Lexer.Peek(), "'+' without a string after it" );
			break;
		}
		sz += m_Lexer.Take().m_szText;
	}
	return sz;
}

//-----------------------------------------------------------------------------
// Purpose: After a ':', the field's value; empty when the field is left out (": :"). nullopt
//          when no ':' comes next.
//-----------------------------------------------------------------------------
std::optional<std::string> CFgdParser::ReadField()
{
	if ( !m_Lexer.Peek().Is( ':' ) )
	{
		return std::nullopt;
	}
	m_Lexer.Take();
	if ( m_Lexer.Peek().IsValue() )
	{
		return ReadString();
	}
	return std::string{};
}

//-----------------------------------------------------------------------------
// Purpose: Skips past the next ch at this level.
//-----------------------------------------------------------------------------
void CFgdParser::SkipTo( char ch )
{
	while ( m_Lexer.Peek().m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_END && m_Lexer.Peek().m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_DIRECTIVE )
	{
		if ( m_Lexer.Take().Is( ch ) )
		{
			return;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Skips a [ ... ] (the '[' already taken), nested ones included.
//-----------------------------------------------------------------------------
void CFgdParser::SkipBlock()
{
	int nLevel = 1;
	while ( m_Lexer.Peek().m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_END && m_Lexer.Peek().m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_DIRECTIVE )
	{
		const FgdToken_t token = m_Lexer.Take();
		if ( token.Is( '[' ) )
		{
			nLevel++;
		}
		else if ( token.Is( ']' ) && --nLevel == 0 )
		{
			return;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Recovers from an error by moving on to the next '@'.
//-----------------------------------------------------------------------------
void CFgdParser::SkipToDirective()
{
	while ( m_Lexer.Peek().m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_END && m_Lexer.Peek().m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_DIRECTIVE )
	{
		m_Lexer.Take();
	}
}

//-----------------------------------------------------------------------------
// Purpose: A helper's arguments, from '(' to ')': split at commas, the words between joined by
//          spaces.
//-----------------------------------------------------------------------------
std::vector<std::string> CFgdParser::ParseArguments()
{
	std::vector<std::string> arguments;
	m_Lexer.Take(); // '('
	std::string szCurrent;
	bool bAny = false;
	while ( true )
	{
		const FgdToken_t &next = m_Lexer.Peek();
		if ( next.m_nKind == FgdTokenKind_t::FGD_TOKEN_KIND_END || next.m_nKind == FgdTokenKind_t::FGD_TOKEN_KIND_DIRECTIVE || next.Is( '=' ) || next.Is( '[' ) )
		{
			Error( next, "a helper's '(' is never closed" );
			break;
		}
		const FgdToken_t token = m_Lexer.Take();
		if ( token.Is( ')' ) )
		{
			break;
		}
		if ( token.Is( ',' ) )
		{
			arguments.push_back( Trim( szCurrent ) );
			szCurrent.clear();
			bAny = true;
			continue;
		}
		if ( !szCurrent.empty() )
		{
			szCurrent += ' ';
		}
		szCurrent += token.m_szText;
		bAny = true;
	}
	if ( bAny )
	{
		arguments.push_back( Trim( szCurrent ) );
	}
	return arguments;
}

//-----------------------------------------------------------------------------
// Purpose: A choices or flags list, from '[' to ']'. False on a syntax error.
//-----------------------------------------------------------------------------
bool CFgdParser::ParseChoices( FgdKey_t &key )
{
	if ( !Expect( '[', "to open a list of choices" ) )
	{
		return false;
	}
	while ( !m_Lexer.Peek().Is( ']' ) )
	{
		if ( !m_Lexer.Peek().IsValue() )
		{
			Error( m_Lexer.Peek(), "expected a choice's value, found '" + std::string{ m_Lexer.Peek().m_szText } + "'" );
			SkipBlock();
			return false;
		}
		FgdChoice_t choice;
		choice.m_szValue = ReadString();
		choice.m_szName = ReadField().value_or( choice.m_szValue );
		if ( const auto szDefault = ReadField() )
		{
			if ( key.m_szType == "flags" )
			{
				choice.m_bDefault = ParseNumber<int>( *szDefault ).value_or( 0 ) != 0;
			}
			else
			{
				choice.m_szDescription = *szDefault;
			}
		}
		if ( const auto szDescription = ReadField() )
		{
			choice.m_szDescription = *szDescription;
		}
		key.m_Choices.push_back( std::move( choice ) );
	}
	m_Lexer.Take();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A class's body, from '[' to ']': keys, flags, inputs and outputs. False when a
//          syntax error cut it short.
//-----------------------------------------------------------------------------
bool CFgdParser::ParseBody( FgdClass_t &fgdClass )
{
	m_Lexer.Take(); // '['
	while ( true )
	{
		const FgdToken_t token = m_Lexer.Peek();
		if ( token.Is( ']' ) )
		{
			m_Lexer.Take();
			return true;
		}
		if ( token.m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_WORD )
		{
			Error( token, token.m_nKind == FgdTokenKind_t::FGD_TOKEN_KIND_END || token.m_nKind == FgdTokenKind_t::FGD_TOKEN_KIND_DIRECTIVE ? "class body is never closed" : "expected a key, input or output, found '" + std::string{ token.m_szText } + "'" );
			SkipBlock();
			return false;
		}
		m_Lexer.Take();
		const std::string szWord = Lower( token.m_szText );
		if ( ( szWord == "input" || szWord == "output" ) && m_Lexer.Peek().m_nKind == FgdTokenKind_t::FGD_TOKEN_KIND_WORD )
		{
			FgdIo_t io;
			io.m_szName = std::string{ m_Lexer.Take().m_szText };
			if ( !Expect( '(', "after an input or output's name" ) || m_Lexer.Peek().m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_WORD )
			{
				SkipBlock();
				return false;
			}
			io.m_szType = Lower( m_Lexer.Take().m_szText );
			if ( !Expect( ')', "after an input or output's type" ) )
			{
				SkipBlock();
				return false;
			}
			io.m_szDescription = ReadField().value_or( "" );
			( szWord == "input" ? fgdClass.m_Inputs : fgdClass.m_Outputs ).push_back( std::move( io ) );
			continue;
		}
		FgdKey_t key;
		key.m_szName = std::string{ token.m_szText };
		if ( szWord == "key" && m_Lexer.Peek().m_nKind == FgdTokenKind_t::FGD_TOKEN_KIND_WORD )
		{
			key.m_szName = std::string{ m_Lexer.Take().m_szText };
		}
		if ( !Expect( '(', "after a key's name" ) || m_Lexer.Peek().m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_WORD )
		{
			SkipBlock();
			return false;
		}
		key.m_szType = Lower( m_Lexer.Take().m_szText );
		if ( key.m_szType.starts_with( '*' ) )
		{
			key.m_bReport = true;
			key.m_szType.erase( 0, 1 );
		}
		if ( !Expect( ')', "after a key's type" ) )
		{
			SkipBlock();
			return false;
		}
		while ( m_Lexer.Peek().m_nKind == FgdTokenKind_t::FGD_TOKEN_KIND_WORD )
		{
			const std::string szModifier = Lower( m_Lexer.Peek().m_szText );
			if ( szModifier != "readonly" && szModifier != "report" )
			{
				break;
			}
			m_Lexer.Take();
			( szModifier == "readonly" ? key.m_bReadOnly : key.m_bReport ) = true;
		}
		key.m_szDisplayName = ReadField().value_or( "" );
		key.m_szDefault = ReadField().value_or( "" );
		key.m_szDescription = ReadField().value_or( "" );
		if ( m_Lexer.Peek().Is( '=' ) )
		{
			m_Lexer.Take();
			if ( !ParseChoices( key ) )
			{
				fgdClass.m_Keys.push_back( std::move( key ) );
				SkipBlock();
				return false;
			}
		}
		fgdClass.m_Keys.push_back( std::move( key ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: "@XClass helpers = name : "description" : "url" [ body ]".
//-----------------------------------------------------------------------------
void CFgdParser::ParseClass( FgdClassKind_t nKind, const FgdToken_t &start )
{
	FgdClass_t fgdClass;
	fgdClass.m_nKind = nKind;
	fgdClass.m_szFile = m_szFile;
	fgdClass.m_nLine = start.m_nLine;
	while ( !m_Lexer.Peek().Is( '=' ) )
	{
		const FgdToken_t token = m_Lexer.Peek();
		if ( token.m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_WORD )
		{
			Error( token, "expected a helper or '=' in a class's definition, found '" + std::string{ token.m_szText } + "'" );
			SkipToDirective();
			return;
		}
		m_Lexer.Take();
		FgdHelper_t helper;
		helper.m_szName = Lower( token.m_szText );
		if ( m_Lexer.Peek().Is( '(' ) )
		{
			helper.m_Arguments = ParseArguments();
		}
		if ( helper.m_szName == "base" )
		{
			fgdClass.m_Bases.insert( fgdClass.m_Bases.end(), helper.m_Arguments.begin(), helper.m_Arguments.end() );
		}
		else
		{
			fgdClass.m_Helpers.push_back( std::move( helper ) );
		}
	}
	m_Lexer.Take(); // '='
	if ( m_Lexer.Peek().m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_WORD )
	{
		Error( m_Lexer.Peek(), "expected a class name after '='" );
		SkipToDirective();
		return;
	}
	fgdClass.m_szName = std::string{ m_Lexer.Take().m_szText };
	fgdClass.m_szDescription = ReadField().value_or( "" );
	(void)ReadField(); // a link to documentation, which some editors show
	bool bComplete = true;
	if ( m_Lexer.Peek().Is( '[' ) )
	{
		bComplete = ParseBody( fgdClass );
	}
	else
	{
		Error( m_Lexer.Peek(), "class " + fgdClass.m_szName + " has no body", false );
	}
	if ( !bComplete )
	{
		SkipToDirective();
	}
	if ( nKind == FgdClassKind_t::FGD_CLASS_KIND_EXTEND )
	{
		m_Fgd.m_Extensions.push_back( std::move( fgdClass ) );
	}
	else
	{
		const std::string szKey = Lower( fgdClass.m_szName );
		m_Fgd.m_Classes.insert_or_assign( szKey, std::move( fgdClass ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: The top level: a list of '@' directives.
//-----------------------------------------------------------------------------
void CFgdParser::Parse()
{
	static const std::map<std::string, FgdClassKind_t, std::less<>> CLASS_KINDS = {
		{ "baseclass", FgdClassKind_t::FGD_CLASS_KIND_BASE },
		{ "pointclass", FgdClassKind_t::FGD_CLASS_KIND_POINT },
		{ "solidclass", FgdClassKind_t::FGD_CLASS_KIND_SOLID },
		{ "npcclass", FgdClassKind_t::FGD_CLASS_KIND_NPC },
		{ "keyframeclass", FgdClassKind_t::FGD_CLASS_KIND_KEYFRAME },
		{ "moveclass", FgdClassKind_t::FGD_CLASS_KIND_MOVE },
		{ "filterclass", FgdClassKind_t::FGD_CLASS_KIND_FILTER },
		{ "extendclass", FgdClassKind_t::FGD_CLASS_KIND_EXTEND },
	};
	while ( m_Lexer.Peek().m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_END )
	{
		const FgdToken_t token = m_Lexer.Take();
		if ( token.m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_DIRECTIVE )
		{
			Error( token, "expected '@', found '" + std::string{ token.m_szText } + "'" );
			SkipToDirective();
			continue;
		}
		const std::string szDirective = Lower( token.m_szText );
		if ( const auto it = CLASS_KINDS.find( szDirective ); it != CLASS_KINDS.end() )
		{
			ParseClass( it->second, token );
		}
		else if ( szDirective == "include" )
		{
			if ( m_Lexer.Peek().m_nKind != FgdTokenKind_t::FGD_TOKEN_KIND_STRING )
			{
				Error( m_Lexer.Peek(), "@include without a file name" );
				continue;
			}
			const std::string szName{ m_Lexer.Take().m_szText };
			std::string szPath;
			std::optional<std::string> text = m_pfnInclude ? m_pfnInclude( szName, m_szFile, szPath ) : std::nullopt;
			if ( !text )
			{
				Error( token, "can't include \"" + szName + "\"" );
				continue;
			}
			if ( m_Fgd.m_nIncludeDepth >= MAX_INCLUDE_DEPTH )
			{
				Error( token, "includes nested too deeply (or including themselves)" );
				continue;
			}
			m_Fgd.m_nIncludeDepth++;
			m_Fgd.AddText( *text, szPath, m_pfnInclude );
			m_Fgd.m_nIncludeDepth--;
		}
		else if ( szDirective == "mapsize" )
		{
			if ( m_Lexer.Peek().Is( '(' ) )
			{
				const std::vector<std::string> arguments = ParseArguments();
				const auto flMin = arguments.size() == 2 ? ParseNumber<double>( arguments[ 0 ] ) : std::nullopt;
				const auto flMax = arguments.size() == 2 ? ParseNumber<double>( arguments[ 1 ] ) : std::nullopt;
				if ( flMin && flMax && *flMin < *flMax )
				{
					m_Fgd.m_MapSize = std::pair{ *flMin, *flMax };
				}
				else
				{
					Error( token, "@mapsize needs a smaller and a bigger number" );
				}
			}
		}
		else if ( szDirective == "materialexclusion" )
		{
			if ( !Expect( '[', "after @MaterialExclusion" ) )
			{
				SkipToDirective();
				continue;
			}
			while ( m_Lexer.Peek().IsValue() )
			{
				m_Fgd.m_MaterialExclusions.push_back( ReadString() );
			}
			Expect( ']', "to close @MaterialExclusion" );
		}
		else if ( szDirective == "autovisgroup" )
		{
			if ( !Expect( '=', "after @AutoVisGroup" ) || !m_Lexer.Peek().IsValue() )
			{
				SkipToDirective();
				continue;
			}
			const std::string szParent = ReadString();
			if ( !Expect( '[', "to open @AutoVisGroup" ) )
			{
				SkipToDirective();
				continue;
			}
			while ( m_Lexer.Peek().IsValue() )
			{
				FgdAutoVisGroup_t group;
				group.m_szParent = szParent;
				group.m_szName = ReadString();
				if ( !Expect( '[', "to open an auto visgroup's classes" ) )
				{
					break;
				}
				while ( m_Lexer.Peek().IsValue() )
				{
					group.m_Classes.push_back( ReadString() );
				}
				Expect( ']', "to close an auto visgroup's classes" );
				m_Fgd.m_AutoVisGroups.push_back( std::move( group ) );
			}
			if ( !Expect( ']', "to close @AutoVisGroup" ) )
			{
				SkipToDirective();
			}
		}
		else
		{
			// @version, @gridnav and the old @Main: nothing a viewer needs.
			if ( szDirective != "version" && szDirective != "gridnav" && szDirective != "main" )
			{
				Error( token, "unknown @" + std::string{ token.m_szText }, false );
			}
			SkipToDirective();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const FgdHelper_t *FgdClass_t::FindHelper( std::string_view szName ) const
{
	const auto it = std::ranges::find_if( m_Helpers.rbegin(), m_Helpers.rend(), [ szName ]( const FgdHelper_t &helper ) { return helper.m_szName == szName; } );
	return it == m_Helpers.rend() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<const FgdHelper_t *> FgdClass_t::FindHelpers( std::string_view szName ) const
{
	std::vector<const FgdHelper_t *> found;
	for ( const FgdHelper_t &helper : m_Helpers )
	{
		if ( helper.m_szName == szName )
		{
			found.push_back( &helper );
		}
	}
	return found;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const FgdKey_t *FgdClass_t::FindKey( std::string_view szName ) const
{
	const std::string szLower = Lower( szName );
	const auto it = std::ranges::find_if( m_Keys, [ & ]( const FgdKey_t &key ) { return Lower( key.m_szName ) == szLower; } );
	return it == m_Keys.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose: Whether entities of the class stand at a point (rather than being brushes).
//-----------------------------------------------------------------------------
bool FgdClass_t::IsPointClass() const
{
	return m_nKind != FgdClassKind_t::FGD_CLASS_KIND_SOLID && m_nKind != FgdClassKind_t::FGD_CLASS_KIND_BASE;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CFgd::AddDiagnostic( std::string szFile, std::size_t nLine, bool bError, std::string szMessage )
{
	if ( m_Diagnostics.size() < 1000 )
	{
		m_Diagnostics.push_back( { std::move( szFile ), nLine, bError, std::move( szMessage ) } );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CFgd::AddText( std::string_view szText, const std::string &szFile, const Includer_t &pfnInclude )
{
	m_Files.push_back( szFile );
	CFgdParser{ *this, szText, szFile, pfnInclude }.Parse();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CFgd::AddFile( const std::filesystem::path &path )
{
	const std::optional<std::string> text = ReadText( path );
	if ( !text )
	{
		AddDiagnostic( path.string(), 0, true, "can't read the file" );
		return false;
	}
	const Includer_t include = []( std::string_view szName, const std::string &szFrom, std::string &szPath ) -> std::optional<std::string> {
		const std::filesystem::path found = vfs::FindCaseInsensitive( std::filesystem::path{ szFrom }.parent_path() / std::filesystem::path{ std::string{ szName } } );
		szPath = found.string();
		return ReadText( found );
	};
	AddText( *text, path.string(), include );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A class with its bases' keys, I/O and helpers folded in, its own coming last and
//          replacing theirs by name.
//-----------------------------------------------------------------------------
FgdClass_t CFgd::Resolve( const std::string &szKey, int nDepth ) const
{
	const auto it = m_Classes.find( szKey );
	if ( it == m_Classes.end() )
	{
		return {};
	}
	const FgdClass_t &own = it->second;
	if ( nDepth > MAX_BASE_DEPTH )
	{
		return own;
	}
	FgdClass_t resolved;
	auto mergeKeys = []( std::vector<FgdKey_t> &into, const std::vector<FgdKey_t> &from ) {
		for ( const FgdKey_t &key : from )
		{
			const std::string szName = Lower( key.m_szName );
			const auto existing = std::ranges::find_if( into, [ & ]( const FgdKey_t &k ) { return Lower( k.m_szName ) == szName; } );
			if ( existing != into.end() )
			{
				*existing = key;
			}
			else
			{
				into.push_back( key );
			}
		}
	};
	auto mergeIo = []( std::vector<FgdIo_t> &into, const std::vector<FgdIo_t> &from ) {
		for ( const FgdIo_t &io : from )
		{
			const auto existing = std::ranges::find_if( into, [ & ]( const FgdIo_t &other ) { return Lower( other.m_szName ) == Lower( io.m_szName ); } );
			if ( existing != into.end() )
			{
				*existing = io;
			}
			else
			{
				into.push_back( io );
			}
		}
	};
	for ( const std::string &szBase : own.m_Bases )
	{
		const std::string szBaseKey = Lower( szBase );
		if ( szBaseKey == szKey || !m_Classes.contains( szBaseKey ) )
		{
			continue;
		}
		const FgdClass_t base = Resolve( szBaseKey, nDepth + 1 );
		mergeKeys( resolved.m_Keys, base.m_Keys );
		mergeIo( resolved.m_Inputs, base.m_Inputs );
		mergeIo( resolved.m_Outputs, base.m_Outputs );
		resolved.m_Helpers.insert( resolved.m_Helpers.end(), base.m_Helpers.begin(), base.m_Helpers.end() );
	}
	mergeKeys( resolved.m_Keys, own.m_Keys );
	mergeIo( resolved.m_Inputs, own.m_Inputs );
	mergeIo( resolved.m_Outputs, own.m_Outputs );
	resolved.m_Helpers.insert( resolved.m_Helpers.end(), own.m_Helpers.begin(), own.m_Helpers.end() );
	resolved.m_nKind = own.m_nKind;
	resolved.m_szName = own.m_szName;
	resolved.m_szDescription = own.m_szDescription;
	resolved.m_Bases = own.m_Bases;
	resolved.m_szFile = own.m_szFile;
	resolved.m_nLine = own.m_nLine;
	return resolved;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CFgd::Finish()
{
	// Hammer++ reads extensions last: their helpers and keys replace the class's own of the
	// same name, and a helper already there exactly as written isn't added twice.
	for ( const FgdClass_t &extension : m_Extensions )
	{
		const auto it = m_Classes.find( Lower( extension.m_szName ) );
		if ( it == m_Classes.end() )
		{
			continue;
		}
		FgdClass_t &target = it->second;
		std::set<std::string> names;
		for ( const FgdHelper_t &helper : extension.m_Helpers )
		{
			names.insert( helper.m_szName );
		}
		std::erase_if( target.m_Helpers, [ & ]( const FgdHelper_t &helper ) { return names.contains( helper.m_szName ); } );
		target.m_Helpers.insert( target.m_Helpers.end(), extension.m_Helpers.begin(), extension.m_Helpers.end() );
		for ( const FgdKey_t &key : extension.m_Keys )
		{
			const auto existing = std::ranges::find_if( target.m_Keys, [ & ]( const FgdKey_t &k ) { return Lower( k.m_szName ) == Lower( key.m_szName ); } );
			if ( existing != target.m_Keys.end() )
			{
				*existing = key;
			}
			else
			{
				target.m_Keys.push_back( key );
			}
		}
	}
	m_Extensions.clear();

	m_Resolved.clear();
	for ( const auto &[ szKey, fgdClass ] : m_Classes )
	{
		for ( const std::string &szBase : fgdClass.m_Bases )
		{
			if ( !m_Classes.contains( Lower( szBase ) ) )
			{
				AddDiagnostic( fgdClass.m_szFile, fgdClass.m_nLine, false, fgdClass.m_szName + "'s base " + szBase + " isn't defined" );
			}
		}
		m_Resolved.emplace( szKey, Resolve( szKey, 0 ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const FgdClass_t *CFgd::Find( std::string_view szClassname ) const
{
	const auto it = m_Resolved.find( Lower( szClassname ) );
	return it == m_Resolved.end() ? nullptr : &it->second;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CFgd::HasErrors() const
{
	return std::ranges::any_of( m_Diagnostics, &FgdDiagnostic_t::m_bError );
}

} // namespace xv::map
