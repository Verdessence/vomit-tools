//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Conf.h.
//
//===========================================================================//
#include "Conf.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

namespace xv::conf
{

namespace
{

// How deep IncludeScript may nest before it is taken for a loop the paths didn't show.
constexpr int MAX_INCLUDE_DEPTH = 16;
// The lists whose items all count, and the keyword each stands for.
constexpr std::pair<std::string_view, std::string_view> LISTS[] = { { "binds", "bind" }, { "unbinds", "unbind" }, { "rules", "rule" } };

//-----------------------------------------------------------------------------
// Purpose: The keyword a list's name stands for ("binds" is bind), or empty.
//-----------------------------------------------------------------------------
std::string_view KeywordOfList( std::string_view szName )
{
	for ( const auto &[ szList, szKeyword ] : LISTS )
	{
		if ( szList == szName )
		{
			return szKeyword;
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: The list a keyword is written as.
//-----------------------------------------------------------------------------
std::string_view ListOfKeyword( std::string_view szKeyword )
{
	for ( const auto &[ szList, szName ] : LISTS )
	{
		if ( szName == szKeyword )
		{
			return szList;
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsSpace( char c )
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Trimmed( std::string_view szText )
{
	std::size_t nBegin = 0;
	std::size_t nEnd = szText.size();
	while ( nBegin < nEnd && ( IsSpace( szText[ nBegin ] ) || szText[ nBegin ] == '\n' ) )
	{
		nBegin++;
	}
	while ( nEnd > nBegin && ( IsSpace( szText[ nEnd - 1 ] ) || szText[ nEnd - 1 ] == '\n' ) )
	{
		nEnd--;
	}
	return std::string{ szText.substr( nBegin, nEnd - nBegin ) };
}

struct Token_t
{
	enum class Kind_t
	{
		KIND_IDENTIFIER,
		KIND_NUMBER,
		KIND_STRING,
		KIND_PUNCT, // { } [ ] ( ) , ; = . : - and <-
		KIND_END,
	};
	Kind_t m_nKind = Kind_t::KIND_END;
	std::size_t m_nBegin = 0;
	std::size_t m_nEnd = 0;
	std::string m_szText;       // a string's characters; the token as written otherwise
	bool m_bLineBefore = false; // a line break between it and the token before
};

struct ParseError_t
{
	std::size_t m_nOffset;
	std::string m_szMessage;
};

//-----------------------------------------------------------------------------
// Purpose: A string token's characters from just past its opening quote; false when it never
//          closes. Verbatim strings (@"...") double a quote to hold one and take \ as it is.
//-----------------------------------------------------------------------------
bool ReadString( std::string_view s, std::size_t &i, bool bVerbatim, std::string &szOut )
{
	while ( i < s.size() )
	{
		const char ch = s[ i ];
		if ( ch == '"' )
		{
			if ( bVerbatim && i + 1 < s.size() && s[ i + 1 ] == '"' )
			{
				szOut += '"';
				i += 2;
				continue;
			}
			i++;
			return true;
		}
		if ( ch == '\n' && !bVerbatim )
		{
			return false;
		}
		if ( ch == '\\' && !bVerbatim && i + 1 < s.size() )
		{
			const char chEscape = s[ i + 1 ];
			i += 2;
			switch ( chEscape )
			{
				case 'n': szOut += '\n'; break;
				case 't': szOut += '\t'; break;
				case 'r': szOut += '\r'; break;
				case '0': szOut += '\0'; break;
				case 'x':
				{
					int nValue = 0;
					for ( int nDigits = 0; nDigits < 2 && i < s.size() && std::isxdigit( static_cast<unsigned char>( s[ i ] ) ); nDigits++, i++ )
					{
						const int c = std::tolower( static_cast<unsigned char>( s[ i ] ) );
						nValue = nValue * 16 + ( std::isdigit( c ) ? c - '0' : c - 'a' + 10 );
					}
					szOut += static_cast<char>( nValue );
					break;
				}
				default: szOut += chEscape; break; // \" \\ \'
			}
			continue;
		}
		szOut += ch;
		i++;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Where a number that starts at i ends: decimal (with a fraction and exponent) or hex.
//-----------------------------------------------------------------------------
std::size_t NumberEnd( std::string_view s, std::size_t i )
{
	const auto digits = [ & ]( auto pfnIs ) {
		while ( i < s.size() && pfnIs( static_cast<unsigned char>( s[ i ] ) ) )
		{
			i++;
		}
	};
	if ( s[ i ] == '0' && i + 1 < s.size() && ( s[ i + 1 ] == 'x' || s[ i + 1 ] == 'X' ) )
	{
		i += 2;
		digits( []( unsigned char c ) { return std::isxdigit( c ) != 0; } );
		return i;
	}
	const auto isDigit = []( unsigned char c ) { return std::isdigit( c ) != 0; };
	digits( isDigit );
	if ( i + 1 < s.size() && s[ i ] == '.' && std::isdigit( static_cast<unsigned char>( s[ i + 1 ] ) ) )
	{
		i++;
		digits( isDigit );
	}
	if ( i < s.size() && ( s[ i ] == 'e' || s[ i ] == 'E' ) )
	{
		std::size_t j = i + 1;
		if ( j < s.size() && ( s[ j ] == '+' || s[ j ] == '-' ) )
		{
			j++;
		}
		if ( j < s.size() && std::isdigit( static_cast<unsigned char>( s[ j ] ) ) )
		{
			i = j;
			digits( isDigit );
		}
	}
	return i;
}

//-----------------------------------------------------------------------------
// Purpose: The file as tokens, comments and spaces left out. A broken token (an unclosed string
//          or comment, a stray character) is a problem, and is skipped.
//-----------------------------------------------------------------------------
std::vector<Token_t> Tokenize( std::string_view s, std::vector<std::pair<std::size_t, std::string>> &problems )
{
	std::vector<Token_t> tokens;
	bool bLine = true;
	std::size_t i = 0;
	while ( i < s.size() )
	{
		const char c = s[ i ];
		if ( c == '\n' )
		{
			bLine = true;
			i++;
			continue;
		}
		if ( IsSpace( c ) )
		{
			i++;
			continue;
		}
		if ( c == '#' || ( c == '/' && i + 1 < s.size() && s[ i + 1 ] == '/' ) )
		{
			while ( i < s.size() && s[ i ] != '\n' )
			{
				i++;
			}
			continue;
		}
		if ( c == '/' && i + 1 < s.size() && s[ i + 1 ] == '*' )
		{
			const std::size_t nClose = s.find( "*/", i + 2 );
			if ( nClose == std::string_view::npos )
			{
				problems.emplace_back( i, "a /* comment is never closed with */" );
				break;
			}
			bLine = bLine || s.substr( i, nClose - i ).find( '\n' ) != std::string_view::npos;
			i = nClose + 2;
			continue;
		}
		Token_t token;
		token.m_nBegin = i;
		token.m_bLineBefore = bLine;
		bLine = false;
		if ( std::isalpha( static_cast<unsigned char>( c ) ) || c == '_' )
		{
			while ( i < s.size() && ( std::isalnum( static_cast<unsigned char>( s[ i ] ) ) || s[ i ] == '_' ) )
			{
				i++;
			}
			token.m_nKind = Token_t::Kind_t::KIND_IDENTIFIER;
			token.m_szText = std::string{ s.substr( token.m_nBegin, i - token.m_nBegin ) };
		}
		else if ( std::isdigit( static_cast<unsigned char>( c ) ) )
		{
			i = NumberEnd( s, i );
			token.m_nKind = Token_t::Kind_t::KIND_NUMBER;
			token.m_szText = std::string{ s.substr( token.m_nBegin, i - token.m_nBegin ) };
		}
		else if ( c == '"' || ( c == '@' && i + 1 < s.size() && s[ i + 1 ] == '"' ) )
		{
			const bool bVerbatim = c == '@';
			i += bVerbatim ? 2 : 1;
			if ( !ReadString( s, i, bVerbatim, token.m_szText ) )
			{
				// Taken to the end of its line, so only this line is lost to it.
				problems.emplace_back( token.m_nBegin, "a string is never closed with '\"'" );
			}
			token.m_nKind = Token_t::Kind_t::KIND_STRING;
		}
		else if ( c == '<' && i + 1 < s.size() && s[ i + 1 ] == '-' )
		{
			i += 2;
			token.m_nKind = Token_t::Kind_t::KIND_PUNCT;
			token.m_szText = "<-";
		}
		else if ( std::string_view{ "{}[](),;=.:-" }.find( c ) != std::string_view::npos )
		{
			i++;
			token.m_nKind = Token_t::Kind_t::KIND_PUNCT;
			token.m_szText = std::string( 1, c );
		}
		else
		{
			problems.emplace_back( i, std::string{ "'" } + c + "' doesn't belong here" );
			i++;
			continue;
		}
		token.m_nEnd = i;
		tokens.push_back( std::move( token ) );
	}
	Token_t end;
	end.m_nBegin = end.m_nEnd = s.size();
	end.m_bLineBefore = true;
	tokens.push_back( end );
	return tokens;
}

// Reads statements and values from tokens.
class CParser
{
public:
	explicit CParser( const std::vector<Token_t> &tokens )
		: m_Tokens( tokens )
	{
	}

	//-----------------------------------------------------------------------------
	// Purpose: Every statement; one that can't be read is a problem, and reading carries on at
	//          the next line that starts outside its brackets.
	//-----------------------------------------------------------------------------
	std::vector<Statement_t> Statements( std::vector<std::pair<std::size_t, std::string>> &problems )
	{
		std::vector<Statement_t> statements;
		while ( Peek().m_nKind != Token_t::Kind_t::KIND_END )
		{
			const std::size_t nStart = m_nAt;
			try
			{
				if ( IsPunct( ";" ) )
				{
					m_nAt++;
					continue;
				}
				statements.push_back( Statement() );
			}
			catch ( const ParseError_t &error )
			{
				problems.emplace_back( error.m_nOffset, error.m_szMessage );
				Recover( nStart );
			}
		}
		return statements;
	}

private:
	const Token_t &Peek( std::size_t nAhead = 0 ) const { return m_Tokens[ std::min( m_nAt + nAhead, m_Tokens.size() - 1 ) ]; }
	bool IsPunct( std::string_view szText, std::size_t nAhead = 0 ) const { return Peek( nAhead ).m_nKind == Token_t::Kind_t::KIND_PUNCT && Peek( nAhead ).m_szText == szText; }

	//-----------------------------------------------------------------------------
	// Purpose:
	//-----------------------------------------------------------------------------
	[[noreturn]] void Fail( const std::string &szMessage ) const
	{
		const Token_t &token = Peek();
		throw ParseError_t{ token.m_nBegin, szMessage + ( token.m_nKind == Token_t::Kind_t::KIND_END ? std::string{ ", not the end of the file" } : ", not \"" + token.m_szText + "\"" ) };
	}

	//-----------------------------------------------------------------------------
	// Purpose:
	//-----------------------------------------------------------------------------
	void Expect( std::string_view szPunct, const char *pszWhat )
	{
		if ( !IsPunct( szPunct ) )
		{
			Fail( std::string{ "expected " } + pszWhat );
		}
		m_nAt++;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Skips to the next token that starts a line (or follows a ';') outside the
	//          brackets the failed statement opened.
	//-----------------------------------------------------------------------------
	void Recover( std::size_t nStart )
	{
		const std::size_t nFailed = std::max( m_nAt, nStart + 1 );
		int nDepth = 0;
		for ( std::size_t i = nStart; i < m_Tokens.size(); i++ )
		{
			const Token_t &token = m_Tokens[ i ];
			const bool bAfterSemicolon = i > 0 && m_Tokens[ i - 1 ].m_nKind == Token_t::Kind_t::KIND_PUNCT && m_Tokens[ i - 1 ].m_szText == ";";
			if ( token.m_nKind == Token_t::Kind_t::KIND_END || ( i >= nFailed && nDepth <= 0 && ( token.m_bLineBefore || bAfterSemicolon ) ) )
			{
				m_nAt = i;
				return;
			}
			if ( token.m_nKind == Token_t::Kind_t::KIND_PUNCT )
			{
				if ( token.m_szText == "{" || token.m_szText == "[" || token.m_szText == "(" )
				{
					nDepth++;
				}
				else if ( token.m_szText == "}" || token.m_szText == "]" || token.m_szText == ")" )
				{
					nDepth--;
				}
			}
		}
		m_nAt = m_Tokens.size() - 1;
	}

	//-----------------------------------------------------------------------------
	// Purpose: A ';' ends a statement if it's there, else a new line must; the statement takes
	//          the ';' in.
	//-----------------------------------------------------------------------------
	void EndStatement( Statement_t &statement )
	{
		statement.m_nEnd = m_Tokens[ m_nAt - 1 ].m_nEnd;
		if ( IsPunct( ";" ) )
		{
			statement.m_nEnd = Peek().m_nEnd;
			m_nAt++;
		}
		else if ( !Peek().m_bLineBefore )
		{
			Fail( "expected a ';' or a new line" );
		}
	}

	//-----------------------------------------------------------------------------
	// Purpose: A name, and names after '.'s.
	//-----------------------------------------------------------------------------
	std::string Name( const char *pszAfter )
	{
		if ( Peek().m_nKind != Token_t::Kind_t::KIND_IDENTIFIER )
		{
			Fail( std::string{ "expected a name " } + pszAfter );
		}
		return m_Tokens[ m_nAt++ ].m_szText;
	}

	//-----------------------------------------------------------------------------
	// Purpose: local / const, IncludeScript( ... ), name.append( ... ), or a.b <- value.
	//-----------------------------------------------------------------------------
	Statement_t Statement()
	{
		Statement_t statement;
		statement.m_nBegin = Peek().m_nBegin;
		const std::string szFirst = Name( "for a setting" );
		if ( szFirst == "local" || szFirst == "const" )
		{
			statement.m_nKind = Statement_t::Kind_t::KIND_LOCAL;
			statement.m_Path = { Name( ( "after " + szFirst ).c_str() ) };
			Expect( "=", "'='" );
			statement.m_Value = Value();
			EndStatement( statement );
			return statement;
		}
		if ( szFirst == "IncludeScript" && IsPunct( "(" ) )
		{
			m_nAt++;
			if ( Peek().m_nKind != Token_t::Kind_t::KIND_STRING )
			{
				Fail( "IncludeScript takes a file name in quotes" );
			}
			statement.m_nKind = Statement_t::Kind_t::KIND_INCLUDE;
			statement.m_Value = Value();
			Expect( ")", "')'" );
			EndStatement( statement );
			return statement;
		}
		statement.m_Path.push_back( szFirst );
		while ( IsPunct( "." ) )
		{
			m_nAt++;
			statement.m_Path.push_back( Name( "after '.'" ) );
		}
		if ( IsPunct( "(" ) && statement.m_Path.size() == 2 && ( statement.m_Path[ 1 ] == "append" || statement.m_Path[ 1 ] == "push" ) )
		{
			m_nAt++;
			statement.m_nKind = Statement_t::Kind_t::KIND_APPEND;
			statement.m_Path.pop_back();
			statement.m_Value = Value();
			Expect( ")", "')'" );
			EndStatement( statement );
			return statement;
		}
		if ( !IsPunct( "<-" ) && !IsPunct( "=" ) )
		{
			Fail( "expected '<-' or '='" );
		}
		m_nAt++;
		statement.m_Value = Value();
		EndStatement( statement );
		return statement;
	}

	//-----------------------------------------------------------------------------
	// Purpose: A table's slots, up to its '}'.
	//-----------------------------------------------------------------------------
	void TableSlots( Node_t &node )
	{
		while ( !IsPunct( "}" ) )
		{
			Node_t::Slot_t slot;
			slot.m_nBegin = Peek().m_nBegin;
			if ( Peek().m_nKind == Token_t::Kind_t::KIND_IDENTIFIER || Peek().m_nKind == Token_t::Kind_t::KIND_STRING )
			{
				slot.m_szName = m_Tokens[ m_nAt++ ].m_szText;
			}
			else if ( IsPunct( "[" ) && Peek( 1 ).m_nKind == Token_t::Kind_t::KIND_STRING && IsPunct( "]", 2 ) )
			{
				slot.m_szName = Peek( 1 ).m_szText;
				m_nAt += 3;
			}
			else
			{
				Fail( "expected a key or '}'" );
			}
			if ( !IsPunct( "=" ) && !IsPunct( ":" ) )
			{
				Fail( "expected '=' after \"" + slot.m_szName + "\"" );
			}
			m_nAt++;
			slot.m_Value = Value();
			slot.m_nEnd = slot.m_Value.m_nEnd;
			if ( IsPunct( "," ) )
			{
				slot.m_nEnd = Peek().m_nEnd;
				m_nAt++;
			}
			node.m_Slots.push_back( std::move( slot ) );
		}
	}

	//-----------------------------------------------------------------------------
	// Purpose: An array's items, up to its ']'.
	//-----------------------------------------------------------------------------
	void ArrayItems( Node_t &node )
	{
		while ( !IsPunct( "]" ) )
		{
			Node_t item = Value();
			std::size_t nEnd = item.m_nEnd;
			if ( IsPunct( "," ) )
			{
				nEnd = Peek().m_nEnd;
				m_nAt++;
			}
			node.m_Items.push_back( std::move( item ) );
			node.m_ItemEnds.push_back( nEnd );
		}
	}

	//-----------------------------------------------------------------------------
	// Purpose: A table, array, string, number, true / false / null, or a local's name.
	//-----------------------------------------------------------------------------
	Node_t Value()
	{
		Node_t node;
		node.m_nBegin = Peek().m_nBegin;
		if ( IsPunct( "{" ) || IsPunct( "[" ) )
		{
			const bool bTable = IsPunct( "{" );
			m_nAt++;
			node.m_nKind = bTable ? Node_t::Kind_t::KIND_TABLE : Node_t::Kind_t::KIND_ARRAY;
			bTable ? TableSlots( node ) : ArrayItems( node );
			node.m_nEnd = Peek().m_nEnd;
			m_nAt++;
			return node;
		}
		if ( IsPunct( "-" ) && Peek( 1 ).m_nKind == Token_t::Kind_t::KIND_NUMBER )
		{
			node.m_nKind = Node_t::Kind_t::KIND_NUMBER;
			node.m_szText = "-" + Peek( 1 ).m_szText;
			node.m_nEnd = Peek( 1 ).m_nEnd;
			m_nAt += 2;
			return node;
		}
		const Token_t &token = Peek();
		switch ( token.m_nKind )
		{
			case Token_t::Kind_t::KIND_STRING: node.m_nKind = Node_t::Kind_t::KIND_STRING; break;
			case Token_t::Kind_t::KIND_NUMBER: node.m_nKind = Node_t::Kind_t::KIND_NUMBER; break;
			case Token_t::Kind_t::KIND_IDENTIFIER:
				node.m_nKind = token.m_szText == "true" || token.m_szText == "false" ? Node_t::Kind_t::KIND_BOOL
					: token.m_szText == "null"                                       ? Node_t::Kind_t::KIND_NULL
																					 : Node_t::Kind_t::KIND_IDENTIFIER;
				break;
			default: Fail( "expected a value" );
		}
		node.m_szText = token.m_szText;
		node.m_nEnd = token.m_nEnd;
		m_nAt++;
		return node;
	}

	const std::vector<Token_t> &m_Tokens;
	std::size_t m_nAt = 0;
};

//-----------------------------------------------------------------------------
// Purpose: Whether a file name matches a pattern with * and ?.
//-----------------------------------------------------------------------------
bool GlobMatch( std::string_view szPattern, std::string_view szName )
{
	if ( szPattern.empty() )
	{
		return szName.empty();
	}
	if ( szPattern[ 0 ] == '*' )
	{
		for ( std::size_t i = 0; i <= szName.size(); i++ )
		{
			if ( GlobMatch( szPattern.substr( 1 ), szName.substr( i ) ) )
			{
				return true;
			}
		}
		return false;
	}
	return !szName.empty() && ( szPattern[ 0 ] == '?' || szPattern[ 0 ] == szName[ 0 ] ) && GlobMatch( szPattern.substr( 1 ), szName.substr( 1 ) );
}

//-----------------------------------------------------------------------------
// Purpose: A whole file's text, or nullopt.
//-----------------------------------------------------------------------------
std::optional<std::string> ReadText( const std::filesystem::path &path )
{
	std::ifstream in{ path, std::ios::binary };
	if ( !in )
	{
		return std::nullopt;
	}
	return std::string{ std::istreambuf_iterator<char>{ in }, std::istreambuf_iterator<char>{} };
}

//-----------------------------------------------------------------------------
// Purpose: The home folder, for "~/".
//-----------------------------------------------------------------------------
std::filesystem::path Home()
{
	for ( const char *pszName : { "HOME", "USERPROFILE" } )
	{
		if ( const char *pszValue = std::getenv( pszName ); pszValue && *pszValue )
		{
			return pszValue;
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string PathText( const std::filesystem::path &path )
{
	const auto u8 = path.u8string();
	return { reinterpret_cast<const char *>( u8.data() ), u8.size() };
}

//-----------------------------------------------------------------------------
// Purpose: A list's items as Squirrel: [ "a", "b" ].
//-----------------------------------------------------------------------------
std::string ListLiteral( const std::vector<std::string> &items )
{
	std::string szText = "[";
	for ( std::size_t i = 0; i < items.size(); i++ )
	{
		szText += ( i == 0 ? " " : ", " ) + Literal( items[ i ] );
	}
	return szText + ( items.empty() ? "]" : " ]" );
}

//-----------------------------------------------------------------------------
// Purpose: A keyword's item as Squirrel: its fields as a list, or one field on its own.
//-----------------------------------------------------------------------------
std::string ItemLiteral( const std::vector<std::string> &fields )
{
	return fields.size() == 1 ? Literal( fields.front() ) : ListLiteral( fields );
}

//-----------------------------------------------------------------------------
// Purpose: Where to put a new line inside a table or array, and its text: on a line of its own
//          before the closing bracket, a tab deeper, when the bracket starts its line; else just
//          inside the opening bracket.
//-----------------------------------------------------------------------------
std::pair<std::size_t, std::string> InsertionBefore( const std::string &szText, const Node_t &container, const std::string &szLine )
{
	const std::size_t nClose = container.m_nEnd - 1;
	std::size_t nStart = nClose;
	while ( nStart > 0 && IsSpace( szText[ nStart - 1 ] ) )
	{
		nStart--;
	}
	if ( nStart == 0 || szText[ nStart - 1 ] == '\n' )
	{
		return { nStart, szText.substr( nStart, nClose - nStart ) + "\t" + szLine + ",\n" };
	}
	return { container.m_nBegin + 1, " " + szLine + "," };
}

//-----------------------------------------------------------------------------
// Purpose: The text a new statement at the end of a file needs first: a line break to end the
//          last line, and a blank line before it.
//-----------------------------------------------------------------------------
std::string GapAtEnd( const std::string &szText )
{
	return szText.empty() || szText.ends_with( "\n\n" ) ? "" : szText.ends_with( '\n' ) ? "\n"
																						: "\n\n";
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The keywords whose items all count.
//-----------------------------------------------------------------------------
bool IsKeyword( std::string_view szKey )
{
	return szKey == "source" || !ListOfKeyword( szKey ).empty();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CConfFile::CConfFile( std::string szText )
	: m_szText( std::move( szText ) )
{
	Parse();
}

//-----------------------------------------------------------------------------
// Purpose: Statements, problems and where each line starts.
//-----------------------------------------------------------------------------
void CConfFile::Parse()
{
	m_Problems.clear();
	m_LineStarts = { 0 };
	for ( std::size_t i = 0; i < m_szText.size(); i++ )
	{
		if ( m_szText[ i ] == '\n' )
		{
			m_LineStarts.push_back( i + 1 );
		}
	}
	const std::vector<Token_t> tokens = Tokenize( m_szText, m_Problems );
	m_Statements = CParser{ tokens }.Statements( m_Problems );
	std::ranges::sort( m_Problems );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CConfFile::LineOf( std::size_t nOffset ) const
{
	return static_cast<int>( std::ranges::upper_bound( m_LineStarts, nOffset ) - m_LineStarts.begin() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CConfFile::Replace( std::size_t nBegin, std::size_t nEnd, std::string_view szText )
{
	nBegin = std::min( nBegin, m_szText.size() );
	nEnd = std::clamp( nEnd, nBegin, m_szText.size() );
	m_szText.replace( nBegin, nEnd - nBegin, szText );
	Parse();
}

//-----------------------------------------------------------------------------
// Purpose: Reads the core file from disk; a file that isn't there is an empty config.
//-----------------------------------------------------------------------------
void CConfig::Load( const std::filesystem::path &core )
{
	LoadText( core, ReadText( core ).value_or( std::string{} ) );
}

//-----------------------------------------------------------------------------
// Purpose: Starts again from the core file's text.
//-----------------------------------------------------------------------------
void CConfig::LoadText( const std::filesystem::path &core, std::string_view szText )
{
	m_CorePath = core;
	m_Files.clear();
	AddFile( core, std::string{ szText } );
	Resolve();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CConfig::AddFile( const std::filesystem::path &path, std::string szText )
{
	m_Files.push_back( { path, CConfFile{ std::move( szText ) }, false } );
	return m_Files.size() - 1;
}

//-----------------------------------------------------------------------------
// Purpose: Every file the last read reached, the core first.
//-----------------------------------------------------------------------------
std::vector<std::filesystem::path> CConfig::Files() const
{
	std::vector<std::filesystem::path> files;
	for ( const std::size_t nFile : m_Used )
	{
		files.push_back( m_Files[ nFile ].m_Path );
	}
	return files;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Location_t CConfig::Where( std::size_t nFile, std::size_t nOffset ) const
{
	return { m_Files[ nFile ].m_Path, m_Files[ nFile ].m_File.LineOf( nOffset ) };
}

//-----------------------------------------------------------------------------
// Purpose: Works out the entries and locals from the files as they now are.
//-----------------------------------------------------------------------------
void CConfig::Resolve()
{
	m_Entries.clear();
	m_Variables.clear();
	m_Errors.clear();
	m_Used.clear();
	if ( m_Files.empty() )
	{
		return;
	}
	std::vector<std::filesystem::path> stack;
	ResolveFile( 0, stack, 0 );
}

//-----------------------------------------------------------------------------
// Purpose: A value as an entry: a scalar's text, a local's value, or a list's items. Tables are
//          Flatten's.
//-----------------------------------------------------------------------------
std::optional<Entry_t> CConfig::MakeEntry( std::size_t nFile, const Node_t &node, std::string szKey )
{
	Entry_t entry;
	entry.m_szKey = std::move( szKey );
	entry.m_nFile = nFile;
	entry.m_nBegin = node.m_nBegin;
	entry.m_nEnd = node.m_nEnd;
	entry.m_Where = Where( nFile, node.m_nBegin );
	const auto scalar = [ this, nFile ]( const Node_t &value ) -> std::optional<std::string> {
		switch ( value.m_nKind )
		{
			case Node_t::Kind_t::KIND_NULL: return std::string{};
			case Node_t::Kind_t::KIND_BOOL:
			case Node_t::Kind_t::KIND_NUMBER:
			case Node_t::Kind_t::KIND_STRING: return value.m_szText;
			case Node_t::Kind_t::KIND_IDENTIFIER:
				if ( const auto found = m_Variables.find( value.m_szText ); found != m_Variables.end() )
				{
					return found->second;
				}
				m_Errors.push_back( { Where( nFile, value.m_nBegin ), "nothing called \"" + value.m_szText + "\" is defined above (a local is \"local " + value.m_szText + " = ...\"; text goes in quotes)" } );
				return value.m_szText;
			default: m_Errors.push_back( { Where( nFile, value.m_nBegin ), "a table or list can't go here" } ); return std::nullopt;
		}
	};
	if ( node.m_nKind == Node_t::Kind_t::KIND_ARRAY )
	{
		entry.m_bList = true;
		for ( const Node_t &item : node.m_Items )
		{
			const auto szItem = scalar( item );
			if ( !szItem )
			{
				return std::nullopt;
			}
			entry.m_szValue += ( entry.m_Fields.empty() ? "" : ", " ) + *szItem;
			entry.m_Fields.push_back( *szItem );
		}
		return entry;
	}
	const auto szValue = scalar( node );
	if ( !szValue )
	{
		return std::nullopt;
	}
	entry.m_szValue = *szValue;
	entry.m_Fields = { *szValue };
	return entry;
}

//-----------------------------------------------------------------------------
// Purpose: A table's slots as keys under its own ("map" and { fov = 90 } give map:fov); other
//          values as one entry.
//-----------------------------------------------------------------------------
void CConfig::Flatten( std::size_t nFile, const Node_t &node, const std::string &szKey )
{
	if ( node.m_nKind == Node_t::Kind_t::KIND_TABLE )
	{
		for ( const Node_t::Slot_t &slot : node.m_Slots )
		{
			Flatten( nFile, slot.m_Value, szKey.empty() ? slot.m_szName : szKey + ":" + slot.m_szName );
		}
		return;
	}
	if ( auto entry = MakeEntry( nFile, node, szKey ) )
	{
		m_Entries.push_back( std::move( *entry ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: One file's statements in order; IncludeScript reads its file there and then, so what
//          it sets comes between the statements around it.
//-----------------------------------------------------------------------------
void CConfig::ResolveFile( std::size_t nFile, std::vector<std::filesystem::path> &stack, int nDepth )
{
	m_Used.insert( nFile );
	const std::filesystem::path path = m_Files[ nFile ].m_Path;
	stack.push_back( path );
	for ( const auto &[ nOffset, szProblem ] : m_Files[ nFile ].m_File.Problems() )
	{
		m_Errors.push_back( { Where( nFile, nOffset ), szProblem } );
	}
	// Copied: including files can add to m_Files, which moves them.
	const std::vector<Statement_t> statements = m_Files[ nFile ].m_File.Statements();
	for ( const Statement_t &statement : statements )
	{
		const Location_t where = Where( nFile, statement.m_nBegin );
		const std::string_view szKeyword = statement.m_Path.empty() ? std::string_view{} : KeywordOfList( statement.m_Path.front() );
		switch ( statement.m_nKind )
		{
			case Statement_t::Kind_t::KIND_LOCAL:
				if ( auto entry = MakeEntry( nFile, statement.m_Value, {} ); entry && !entry->m_bList )
				{
					m_Variables[ statement.m_Path.front() ] = entry->m_szValue;
				}
				else if ( entry )
				{
					m_Errors.push_back( { where, "a local holds one value, not a list" } );
				}
				break;
			case Statement_t::Kind_t::KIND_APPEND:
				if ( szKeyword.empty() )
				{
					m_Errors.push_back( { where, "only binds, unbinds and rules can be appended to" } );
				}
				else if ( auto entry = MakeEntry( nFile, statement.m_Value, std::string{ szKeyword } ) )
				{
					m_Entries.push_back( std::move( *entry ) );
				}
				break;
			case Statement_t::Kind_t::KIND_ASSIGN:
			{
				if ( szKeyword.empty() )
				{
					std::string szKey;
					for ( const std::string &szPart : statement.m_Path )
					{
						szKey += ( szKey.empty() ? "" : ":" ) + szPart;
					}
					Flatten( nFile, statement.m_Value, szKey );
					break;
				}
				if ( statement.m_Path.size() != 1 || statement.m_Value.m_nKind != Node_t::Kind_t::KIND_ARRAY )
				{
					m_Errors.push_back( { where, statement.m_Path.front() + " is a list: " + statement.m_Path.front() + " <- [ ... ]" } );
					break;
				}
				for ( const Node_t &item : statement.m_Value.m_Items )
				{
					if ( auto entry = MakeEntry( nFile, item, std::string{ szKeyword } ) )
					{
						m_Entries.push_back( std::move( *entry ) );
					}
				}
				break;
			}
			case Statement_t::Kind_t::KIND_INCLUDE:
			{
				auto entry = MakeEntry( nFile, statement.m_Value, "source" );
				if ( !entry )
				{
					break;
				}
				m_Entries.push_back( *entry );
				for ( const std::filesystem::path &include : IncludePaths( entry->m_szValue, path, where ) )
				{
					std::error_code ec;
					const std::filesystem::path canonical = std::filesystem::weakly_canonical( include, ec );
					if ( std::ranges::any_of( stack, [ & ]( const std::filesystem::path &open ) { return std::filesystem::weakly_canonical( open, ec ) == canonical; } ) )
					{
						m_Errors.push_back( { where, "\"" + PathText( include ) + "\" is already being read: including it again would loop" } );
						continue;
					}
					if ( nDepth + 1 > MAX_INCLUDE_DEPTH )
					{
						m_Errors.push_back( { where, "includes nest more than " + std::to_string( MAX_INCLUDE_DEPTH ) + " deep" } );
						continue;
					}
					std::size_t nChild = m_Files.size();
					for ( std::size_t f = 0; f < m_Files.size(); f++ )
					{
						if ( std::filesystem::weakly_canonical( m_Files[ f ].m_Path, ec ) == canonical )
						{
							nChild = f;
							break;
						}
					}
					if ( nChild == m_Files.size() )
					{
						auto text = ReadText( include );
						if ( !text )
						{
							m_Errors.push_back( { where, "can't read \"" + PathText( include ) + "\"" } );
							continue;
						}
						nChild = AddFile( include, std::move( *text ) );
					}
					ResolveFile( nChild, stack, nDepth + 1 );
				}
				break;
			}
		}
	}
	stack.pop_back();
}

//-----------------------------------------------------------------------------
// Purpose: The files IncludeScript names: "~/" is the home folder, a relative path is beside
//          the file it's in, and * and ? in the file name match every such file (sorted).
//-----------------------------------------------------------------------------
std::vector<std::filesystem::path> CConfig::IncludePaths( std::string_view szPattern, const std::filesystem::path &from, const Location_t &where )
{
	const std::string szPath{ szPattern };
	if ( szPath.empty() )
	{
		m_Errors.push_back( { where, "IncludeScript needs a file" } );
		return {};
	}
	std::filesystem::path path;
	if ( szPath == "~" || szPath.starts_with( "~/" ) )
	{
		path = Home() / std::filesystem::path{ szPath.size() > 2 ? szPath.substr( 2 ) : std::string{} };
	}
	else
	{
		path = std::filesystem::path{ szPath };
		if ( path.is_relative() )
		{
			path = from.parent_path() / path;
		}
	}
	const std::string szName = PathText( path.filename() );
	if ( szName.find_first_of( "*?" ) == std::string::npos )
	{
		return { path };
	}
	std::vector<std::filesystem::path> matches;
	std::error_code ec;
	for ( const auto &item : std::filesystem::directory_iterator{ path.parent_path(), ec } )
	{
		if ( item.is_regular_file( ec ) && GlobMatch( szName, PathText( item.path().filename() ) ) )
		{
			matches.push_back( item.path() );
		}
	}
	std::ranges::sort( matches );
	return matches;
}

//-----------------------------------------------------------------------------
// Purpose: The last value of a key that isn't a keyword.
//-----------------------------------------------------------------------------
const Entry_t *CConfig::Find( std::string_view szKey ) const
{
	if ( IsKeyword( szKey ) )
	{
		return nullptr;
	}
	for ( auto it = m_Entries.rbegin(); it != m_Entries.rend(); ++it )
	{
		if ( it->m_szKey == szKey )
		{
			return &*it;
		}
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::string> CConfig::Get( std::string_view szKey ) const
{
	const Entry_t *pEntry = Find( szKey );
	return pEntry ? std::optional<std::string>{ pEntry->m_szValue } : std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: Every item of a keyword, in order.
//-----------------------------------------------------------------------------
std::vector<const Entry_t *> CConfig::Keyword( std::string_view szKeyword ) const
{
	std::vector<const Entry_t *> entries;
	for ( const Entry_t &entry : m_Entries )
	{
		if ( entry.m_szKey == szKeyword )
		{
			entries.push_back( &entry );
		}
	}
	return entries;
}

//-----------------------------------------------------------------------------
// Purpose: Sets a key where it was last set, else adds it to the core file: in the deepest
//          table the core file already has for it (nesting what's missing), else in a new
//          table at the end.
//-----------------------------------------------------------------------------
bool CConfig::Set( std::string_view szKey, std::string_view szValue )
{
	if ( m_Files.empty() )
	{
		AddFile( m_CorePath, {} );
	}
	if ( const Entry_t *pEntry = Find( szKey ) )
	{
		const std::vector<std::string> items = SplitList( szValue );
		if ( pEntry->m_bList ? pEntry->m_Fields == items : pEntry->m_szValue == szValue )
		{
			return false;
		}
		File_t &file = m_Files[ pEntry->m_nFile ];
		file.m_File.Replace( pEntry->m_nBegin, pEntry->m_nEnd, pEntry->m_bList ? ListLiteral( items ) : Literal( szValue ) );
		file.m_bModified = true;
		Resolve();
		return true;
	}

	std::vector<std::string> parts;
	for ( std::size_t nAt = 0; nAt <= szKey.size(); )
	{
		std::size_t nColon = szKey.find( ':', nAt );
		if ( nColon == std::string_view::npos )
		{
			nColon = szKey.size();
		}
		parts.emplace_back( szKey.substr( nAt, nColon - nAt ) );
		nAt = nColon + 1;
	}
	// The core file's last table statement whose path starts the key the furthest, then down
	// through its tables.
	CConfFile &core = m_Files[ 0 ].m_File;
	const Node_t *pTable = nullptr;
	std::size_t nUsed = 0;
	for ( const Statement_t &statement : core.Statements() )
	{
		if ( statement.m_nKind == Statement_t::Kind_t::KIND_ASSIGN && statement.m_Value.m_nKind == Node_t::Kind_t::KIND_TABLE && statement.m_Path.size() < parts.size() &&
			std::equal( statement.m_Path.begin(), statement.m_Path.end(), parts.begin() ) && statement.m_Path.size() >= nUsed )
		{
			pTable = &statement.m_Value;
			nUsed = statement.m_Path.size();
		}
	}
	while ( pTable && nUsed + 1 < parts.size() )
	{
		const Node_t *pInner = nullptr;
		for ( const Node_t::Slot_t &slot : pTable->m_Slots )
		{
			if ( slot.m_szName == parts[ nUsed ] && slot.m_Value.m_nKind == Node_t::Kind_t::KIND_TABLE )
			{
				pInner = &slot.m_Value;
			}
		}
		if ( !pInner )
		{
			break;
		}
		pTable = pInner;
		nUsed++;
	}
	// What's left of the key, as tables nested round the value.
	const std::size_t nFrom = pTable ? nUsed : 1;
	std::string szSlot = parts.back() + " = " + Literal( szValue );
	for ( std::size_t i = parts.size() - 1; i-- > nFrom; )
	{
		szSlot = parts[ i ] + " = { " + szSlot + " }";
	}
	if ( pTable )
	{
		const auto [ nAt, szText ] = InsertionBefore( core.Text(), *pTable, szSlot );
		core.Replace( nAt, nAt, szText );
	}
	else
	{
		const std::string szAdd = GapAtEnd( core.Text() ) + ( parts.size() == 1 ? parts.front() + " <- " + Literal( szValue ) + "\n" : parts.front() + " <- {\n\t" + szSlot + ",\n}\n" );
		core.Replace( core.Text().size(), core.Text().size(), szAdd );
	}
	m_Files[ 0 ].m_bModified = true;
	Resolve();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Swaps the core file's items of a keyword for new ones, in its first list for that
//          keyword. An item that leaves its line empty takes the line with it.
//-----------------------------------------------------------------------------
bool CConfig::ReplaceKeyword( std::string_view szKeyword, const std::function<bool( const std::vector<std::string> &fields )> &pfnMatches, const std::vector<std::vector<std::string>> &items )
{
	if ( m_Files.empty() )
	{
		AddFile( m_CorePath, {} );
	}
	const std::string_view szList = ListOfKeyword( szKeyword );
	if ( szList.empty() )
	{
		return false;
	}
	CConfFile &core = m_Files[ 0 ].m_File;
	const std::string szBefore = core.Text();
	const auto fieldsOf = [ this ]( const Node_t &node ) {
		const auto entry = MakeEntry( 0, node, {} );
		return entry ? entry->m_Fields : std::vector<std::string>{};
	};
	std::vector<std::pair<std::size_t, std::size_t>> remove;
	for ( const Statement_t &statement : core.Statements() )
	{
		if ( statement.m_Path.size() != 1 || statement.m_Path.front() != szList )
		{
			continue;
		}
		if ( statement.m_nKind == Statement_t::Kind_t::KIND_APPEND && pfnMatches( fieldsOf( statement.m_Value ) ) )
		{
			remove.emplace_back( statement.m_nBegin, statement.m_nEnd );
		}
		else if ( statement.m_nKind == Statement_t::Kind_t::KIND_ASSIGN && statement.m_Value.m_nKind == Node_t::Kind_t::KIND_ARRAY )
		{
			for ( std::size_t i = 0; i < statement.m_Value.m_Items.size(); i++ )
			{
				if ( pfnMatches( fieldsOf( statement.m_Value.m_Items[ i ] ) ) )
				{
					remove.emplace_back( statement.m_Value.m_Items[ i ].m_nBegin, statement.m_Value.m_ItemEnds[ i ] );
				}
			}
		}
	}
	// From the end back, so the offsets still to come stay where they were.
	std::ranges::sort( remove, std::greater{} );
	for ( auto [ nBegin, nEnd ] : remove )
	{
		const std::string &szText = core.Text();
		const std::size_t nNewline = nBegin == 0 ? std::string::npos : szText.rfind( '\n', nBegin - 1 );
		const std::size_t nLineStart = nNewline == std::string::npos ? 0 : nNewline + 1;
		std::size_t nLineEnd = szText.find( '\n', nEnd );
		nLineEnd = nLineEnd == std::string::npos ? szText.size() : nLineEnd;
		const auto blank = [ &szText ]( std::size_t a, std::size_t b ) { return std::all_of( szText.begin() + static_cast<std::ptrdiff_t>( a ), szText.begin() + static_cast<std::ptrdiff_t>( b ), IsSpace ); };
		if ( blank( nLineStart, nBegin ) && blank( nEnd, nLineEnd ) )
		{
			core.Replace( nLineStart, std::min( nLineEnd + 1, szText.size() ), {} );
			continue;
		}
		while ( nEnd < nLineEnd && IsSpace( szText[ nEnd ] ) )
		{
			nEnd++;
		}
		core.Replace( nBegin, nEnd, {} );
	}
	if ( !items.empty() )
	{
		const Node_t *pList = nullptr;
		for ( const Statement_t &statement : core.Statements() )
		{
			if ( !pList && statement.m_nKind == Statement_t::Kind_t::KIND_ASSIGN && statement.m_Path.size() == 1 && statement.m_Path.front() == szList && statement.m_Value.m_nKind == Node_t::Kind_t::KIND_ARRAY )
			{
				pList = &statement.m_Value;
			}
		}
		if ( pList )
		{
			std::string szInsert;
			std::size_t nAt = 0;
			for ( const auto &fields : items )
			{
				const auto [ nWhere, szText ] = InsertionBefore( core.Text(), *pList, ItemLiteral( fields ) );
				nAt = nWhere;
				szInsert += szText;
			}
			core.Replace( nAt, nAt, szInsert );
		}
		else
		{
			std::string szAdd = GapAtEnd( core.Text() ) + std::string{ szList } + " <- [\n";
			for ( const auto &fields : items )
			{
				szAdd += "\t" + ItemLiteral( fields ) + ",\n";
			}
			szAdd += "]\n";
			core.Replace( core.Text().size(), core.Text().size(), szAdd );
		}
	}
	const bool bChanged = core.Text() != szBefore;
	m_Files[ 0 ].m_bModified |= bChanged;
	Resolve(); // also clears what fieldsOf said about the items it read
	return bChanged;
}

//-----------------------------------------------------------------------------
// Purpose: Writes each changed file through a temporary one, so a crash midway leaves the old.
//-----------------------------------------------------------------------------
bool CConfig::Save( std::string *pError )
{
	for ( File_t &file : m_Files )
	{
		if ( !file.m_bModified )
		{
			continue;
		}
		std::error_code ec;
		if ( file.m_Path.has_parent_path() )
		{
			std::filesystem::create_directories( file.m_Path.parent_path(), ec );
		}
		std::filesystem::path temporary = file.m_Path;
		temporary += ".new";
		{
			std::ofstream out{ temporary, std::ios::binary | std::ios::trunc };
			const std::string &szText = file.m_File.Text();
			out.write( szText.data(), static_cast<std::streamsize>( szText.size() ) );
			if ( !out )
			{
				if ( pError )
				{
					*pError = "can't write \"" + PathText( temporary ) + "\"";
				}
				return false;
			}
		}
		std::filesystem::rename( temporary, file.m_Path, ec );
		if ( ec )
		{
			if ( pError )
			{
				*pError = "can't replace \"" + PathText( file.m_Path ) + "\": " + ec.message();
			}
			return false;
		}
		file.m_bModified = false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CConfig::IsModified() const
{
	return std::ranges::any_of( m_Files, &File_t::m_bModified );
}

//-----------------------------------------------------------------------------
// Purpose: A value as Squirrel. Numbers and true/false stay bare; text with backslashes and no
//          quotes or line breaks is a verbatim string, so a Windows path reads as written.
//-----------------------------------------------------------------------------
std::string Literal( std::string_view szValue )
{
	if ( szValue == "true" || szValue == "false" )
	{
		return std::string{ szValue };
	}
	// A number exactly as Squirrel reads one (no leading zeros, which would be another number).
	const std::size_t nSign = szValue.starts_with( '-' ) ? 1 : 0;
	if ( szValue.size() > nSign && std::isdigit( static_cast<unsigned char>( szValue[ nSign ] ) ) )
	{
		const std::size_t nEnd = NumberEnd( szValue, nSign );
		const bool bLeadingZero = szValue[ nSign ] == '0' && nEnd > nSign + 1 && szValue[ nSign + 1 ] != '.';
		if ( nEnd == szValue.size() && !bLeadingZero )
		{
			return std::string{ szValue };
		}
	}
	if ( szValue.find( '\\' ) != std::string_view::npos && szValue.find_first_of( "\"\n" ) == std::string_view::npos )
	{
		return "@\"" + std::string{ szValue } + "\"";
	}
	std::string szText = "\"";
	for ( const char c : szValue )
	{
		switch ( c )
		{
			case '"': szText += "\\\""; break;
			case '\\': szText += "\\\\"; break;
			case '\n': szText += "\\n"; break;
			case '\t': szText += "\\t"; break;
			case '\r': szText += "\\r"; break;
			default: szText += c; break;
		}
	}
	return szText + "\"";
}

//-----------------------------------------------------------------------------
// Purpose: true, yes, on, 1 / false, no, off, 0, in any case.
//-----------------------------------------------------------------------------
std::optional<bool> ParseBool( std::string_view szValue )
{
	std::string szLower;
	for ( const char c : szValue )
	{
		szLower += static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
	}
	if ( szLower == "true" || szLower == "yes" || szLower == "on" || szLower == "1" )
	{
		return true;
	}
	if ( szLower == "false" || szLower == "no" || szLower == "off" || szLower == "0" )
	{
		return false;
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: The parts between commas, trimmed, empty ones dropped.
//-----------------------------------------------------------------------------
std::vector<std::string> SplitList( std::string_view szValue )
{
	std::vector<std::string> parts;
	std::size_t nAt = 0;
	while ( nAt <= szValue.size() )
	{
		std::size_t nEnd = szValue.find( ',', nAt );
		if ( nEnd == std::string_view::npos )
		{
			nEnd = szValue.size();
		}
		std::string szPart = Trimmed( szValue.substr( nAt, nEnd - nAt ) );
		if ( !szPart.empty() )
		{
			parts.push_back( std::move( szPart ) );
		}
		nAt = nEnd + 1;
	}
	return parts;
}

} // namespace xv::conf
