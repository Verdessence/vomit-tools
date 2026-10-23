//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Kv1.h.
//
//===========================================================================//
#include "Kv1.h"

#include <algorithm>
#include <utility>

namespace xv::kv1
{

namespace
{

constexpr std::string_view UTF8_BOM = "\xEF\xBB\xBF";

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsSpace( char c )
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
char Lower( char c )
{
	return ( c >= 'A' && c <= 'Z' ) ? static_cast<char>( c - 'A' + 'a' ) : c;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Decode( std::string_view szText, bool bEscapeSequences )
{
	if ( !bEscapeSequences )
	{
		return std::string{ szText };
	}
	std::string szOut;
	szOut.reserve( szText.size() );
	for ( std::size_t i = 0; i < szText.size(); i++ )
	{
		if ( szText[ i ] != '\\' || i + 1 == szText.size() )
		{
			szOut += szText[ i ];
			continue;
		}
		switch ( const char chNext = szText[ ++i ] )
		{
			case 'n': szOut += '\n'; break;
			case 't': szOut += '\t'; break;
			default: szOut += chNext; break;
		}
	}
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::string> Quote( std::string_view szText, bool bEscapeSequences )
{
	std::string szOut = "\"";
	for ( const char c : szText )
	{
		if ( !bEscapeSequences )
		{
			if ( c == '"' )
			{
				return std::nullopt;
			}
			szOut += c;
			continue;
		}
		switch ( c )
		{
			case '"': szOut += "\\\""; break;
			case '\\': szOut += "\\\\"; break;
			case '\n': szOut += "\\n"; break;
			case '\t': szOut += "\\t"; break;
			default: szOut += c; break;
		}
	}
	szOut += '"';
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CanBeUnquoted( std::string_view szText )
{
	if ( szText.empty() || szText.front() == '[' || szText.starts_with( "//" ) )
	{
		return false;
	}
	return std::ranges::none_of( szText, []( char c ) {
		return IsSpace( c ) || c == '"' || c == '{' || c == '}';
	} );
}

//-----------------------------------------------------------------------------
// Purpose: Re-spell a token with new text, keeping it unquoted only if it was and still can be.
//-----------------------------------------------------------------------------
bool Respell( CToken &token, std::string_view szText, bool bEscapeSequences )
{
	if ( token.m_nKind == TokenKind_t::TOKEN_KIND_UNQUOTED && CanBeUnquoted( szText ) && !( bEscapeSequences && szText.find( '\\' ) != std::string_view::npos ) )
	{
		token.m_szRaw = szText;
		return true;
	}
	auto quoted = Quote( szText, bEscapeSequences );
	if ( !quoted )
	{
		return false;
	}
	token.m_szRaw = std::move( *quoted );
	token.m_nKind = TokenKind_t::TOKEN_KIND_QUOTED;
	token.m_bUnterminated = false;
	return true;
}

class CLineIndex
{
public:
	explicit CLineIndex( std::string_view szText )
	{
		m_Starts.push_back( 0 );
		for ( std::size_t i = 0; i < szText.size(); i++ )
		{
			if ( szText[ i ] == '\n' )
			{
				m_Starts.push_back( i + 1 );
			}
		}
	}

	Diagnostic_t Make( std::size_t nOffset, Diagnostic_t::Severity_t nSeverity, std::string szMessage ) const
	{
		const auto it = std::ranges::upper_bound( m_Starts, nOffset );
		const auto nLine = static_cast<std::size_t>( it - m_Starts.begin() );
		return { nSeverity, nLine, nOffset - m_Starts[ nLine - 1 ] + 1, std::move( szMessage ) };
	}

private:
	std::vector<std::size_t> m_Starts;
};

struct LexResult_t
{
	std::vector<CToken> m_Tokens;
	std::string m_szTrailing;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
LexResult_t Lex( std::string_view szSrc, bool bEscapeSequences, const CLineIndex &lines, std::vector<Diagnostic_t> &diagnostics )
{
	LexResult_t result;
	std::string szTrivia;
	std::size_t nPos = 0;

	if ( szSrc.starts_with( UTF8_BOM ) )
	{
		szTrivia += UTF8_BOM;
		nPos = UTF8_BOM.size();
	}

	while ( true )
	{
		while ( nPos < szSrc.size() )
		{
			if ( IsSpace( szSrc[ nPos ] ) )
			{
				szTrivia += szSrc[ nPos++ ];
			}
			else if ( szSrc.substr( nPos ).starts_with( "//" ) )
			{
				const auto nEnd = std::min( szSrc.find( '\n', nPos ), szSrc.size() );
				szTrivia += szSrc.substr( nPos, nEnd - nPos );
				nPos = nEnd;
			}
			else
			{
				break;
			}
		}
		if ( nPos >= szSrc.size() )
		{
			result.m_szTrailing = std::move( szTrivia );
			return result;
		}

		CToken token;
		token.m_szLeading = std::move( szTrivia );
		szTrivia.clear();
		token.m_nOffset = nPos;
		const std::size_t nStart = nPos;

		switch ( szSrc[ nPos ] )
		{
			case '{':
				token.m_nKind = TokenKind_t::TOKEN_KIND_OPEN_BRACE;
				nPos++;
				break;
			case '}':
				token.m_nKind = TokenKind_t::TOKEN_KIND_CLOSE_BRACE;
				nPos++;
				break;
			case '"':
			{
				token.m_nKind = TokenKind_t::TOKEN_KIND_QUOTED;
				token.m_bUnterminated = true;
				nPos++;
				while ( nPos < szSrc.size() )
				{
					if ( bEscapeSequences && szSrc[ nPos ] == '\\' && nPos + 1 < szSrc.size() )
					{
						nPos += 2;
						continue;
					}
					if ( szSrc[ nPos++ ] == '"' )
					{
						token.m_bUnterminated = false;
						break;
					}
				}
				if ( token.m_bUnterminated )
				{
					diagnostics.push_back( lines.Make( nStart, Diagnostic_t::Severity_t::SEVERITY_ERROR, "string is never closed" ) );
				}
				else if ( szSrc.substr( nStart, nPos - nStart ).find( '\n' ) != std::string_view::npos )
				{
					diagnostics.push_back( lines.Make( nStart, Diagnostic_t::Severity_t::SEVERITY_WARNING, "string spans several lines (missing closing quote?)" ) );
				}
				break;
			}
			case '[':
				token.m_nKind = TokenKind_t::TOKEN_KIND_CONDITIONAL;
				while ( nPos < szSrc.size() && szSrc[ nPos ] != '\n' && szSrc[ nPos++ ] != ']' )
				{
				}
				break;
			default:
				token.m_nKind = TokenKind_t::TOKEN_KIND_UNQUOTED;
				while ( nPos < szSrc.size() && !IsSpace( szSrc[ nPos ] ) && szSrc[ nPos ] != '"' && szSrc[ nPos ] != '{' && szSrc[ nPos ] != '}' )
				{
					nPos++;
				}
				break;
		}
		token.m_szRaw = szSrc.substr( nStart, nPos - nStart );
		result.m_Tokens.push_back( std::move( token ) );
	}
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The token without its surrounding quotes. Escape sequences are not decoded.
//-----------------------------------------------------------------------------
std::string_view CToken::GetText() const
{
	std::string_view szText = m_szRaw;
	if ( m_nKind == TokenKind_t::TOKEN_KIND_QUOTED )
	{
		szText.remove_prefix( 1 );
		if ( !m_bUnterminated && !szText.empty() )
		{
			szText.remove_suffix( 1 );
		}
	}
	return szText;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool EqualsIgnoreCase( std::string_view a, std::string_view b )
{
	return std::ranges::equal( a, b, []( char x, char y ) { return Lower( x ) == Lower( y ); } );
}

class CParser
{
public:
	CParser( std::vector<CToken> tokens, bool bEscapeSequences, const CLineIndex &lines, std::vector<Diagnostic_t> &diagnostics )
		: m_Tokens( std::move( tokens ) ),
		  m_bEscapeSequences( bEscapeSequences ),
		  m_Lines( lines ),
		  m_Diagnostics( diagnostics )
	{}

	std::vector<CNode> ParseList( int nDepth )
	{
		std::vector<CNode> out;
		while ( const CToken *pToken = GetPeek() )
		{
			if ( pToken->m_nKind == TokenKind_t::TOKEN_KIND_CLOSE_BRACE )
			{
				if ( nDepth > 0 )
				{
					return out;
				}
				Error( *pToken, "unexpected '}'" );
				out.push_back( Stray() );
				continue;
			}
			if ( pToken->m_nKind == TokenKind_t::TOKEN_KIND_CONDITIONAL )
			{
				Error( *pToken, "condition without a key" );
				out.push_back( Stray() );
				continue;
			}

			CNode node;
			node.m_bEscapeSequences = m_bEscapeSequences;
			if ( pToken->m_nKind == TokenKind_t::TOKEN_KIND_OPEN_BRACE )
			{
				Error( *pToken, "block without a key" );
			}
			else
			{
				node.m_Key = Take();
				if ( PeekIs( TokenKind_t::TOKEN_KIND_CONDITIONAL ) )
				{
					node.m_Condition = Take();
				}
				if ( PeekIs( TokenKind_t::TOKEN_KIND_QUOTED ) || PeekIs( TokenKind_t::TOKEN_KIND_UNQUOTED ) )
				{
					node.m_nKind = CNode::Kind_t::KIND_PAIR;
					node.m_Value = Take();
					if ( !node.m_Condition && PeekIs( TokenKind_t::TOKEN_KIND_CONDITIONAL ) )
					{
						node.m_Condition = Take();
						node.m_bConditionAfterKey = false;
					}
					out.push_back( std::move( node ) );
					continue;
				}
				if ( !PeekIs( TokenKind_t::TOKEN_KIND_OPEN_BRACE ) )
				{
					Error( node.m_Key, "key has no value" );
					node.m_nKind = CNode::Kind_t::KIND_STRAY;
					out.push_back( std::move( node ) );
					continue;
				}
			}

			node.m_nKind = CNode::Kind_t::KIND_BLOCK;
			node.m_Open = Take();
			node.m_Children = ParseList( nDepth + 1 );
			if ( PeekIs( TokenKind_t::TOKEN_KIND_CLOSE_BRACE ) )
			{
				node.m_Close = Take();
			}
			else
			{
				Error( node.m_Open, "block is never closed" );
			}
			out.push_back( std::move( node ) );
		}
		return out;
	}

private:
	const CToken *GetPeek() const
	{
		return m_nIndex < m_Tokens.size() ? &m_Tokens[ m_nIndex ] : nullptr;
	}

	bool PeekIs( TokenKind_t nKind ) const
	{
		const CToken *pToken = GetPeek();
		return pToken && pToken->m_nKind == nKind;
	}

	CToken Take()
	{
		return std::move( m_Tokens[ m_nIndex++ ] );
	}

	CNode Stray()
	{
		CNode node;
		node.m_nKind = CNode::Kind_t::KIND_STRAY;
		node.m_Key = Take();
		node.m_bEscapeSequences = m_bEscapeSequences;
		return node;
	}

	void Error( const CToken &token, std::string szMessage )
	{
		m_Diagnostics.push_back( m_Lines.Make( token.m_nOffset, Diagnostic_t::Severity_t::SEVERITY_ERROR, std::move( szMessage ) ) );
	}

	std::vector<CToken> m_Tokens;
	std::size_t m_nIndex = 0;
	bool m_bEscapeSequences;
	const CLineIndex &m_Lines;
	std::vector<Diagnostic_t> &m_Diagnostics;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CNode CNode::MakePair( std::string_view szKey, std::string_view szValue, bool bEscapeSequences )
{
	CNode node;
	node.m_nKind = Kind_t::KIND_PAIR;
	node.m_bEscapeSequences = bEscapeSequences;
	node.m_Key.m_nKind = TokenKind_t::TOKEN_KIND_QUOTED;
	node.m_Value.m_nKind = TokenKind_t::TOKEN_KIND_QUOTED;
	node.m_Value.m_szLeading = "\t";
	node.SetKey( szKey );
	node.SetValue( szValue );
	return node;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CNode CNode::MakeBlock( std::string_view szKey, bool bEscapeSequences )
{
	CNode node;
	node.m_nKind = Kind_t::KIND_BLOCK;
	node.m_bEscapeSequences = bEscapeSequences;
	node.m_Key.m_nKind = TokenKind_t::TOKEN_KIND_QUOTED;
	node.SetKey( szKey );
	node.m_Open.m_nKind = TokenKind_t::TOKEN_KIND_OPEN_BRACE;
	node.m_Open.m_szRaw = "{";
	node.m_Close.emplace();
	node.m_Close->m_szRaw = "}";
	node.m_Close->m_nKind = TokenKind_t::TOKEN_KIND_CLOSE_BRACE;
	return node;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CNode::GetKey() const
{
	return Decode( m_Key.GetText(), m_bEscapeSequences );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string CNode::GetValue() const
{
	return m_nKind == Kind_t::KIND_PAIR ? Decode( m_Value.GetText(), m_bEscapeSequences ) : std::string{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<std::string_view> CNode::GetCondition() const
{
	if ( !m_Condition )
	{
		return std::nullopt;
	}
	return m_Condition->m_szRaw;
}

//-----------------------------------------------------------------------------
// Purpose: Keep the original quoting style where the new text allows it. Return false if the text
//          cannot be represented (a double quote while escape sequences are off).
//-----------------------------------------------------------------------------
bool CNode::SetKey( std::string_view szKey )
{
	return Respell( m_Key, szKey, m_bEscapeSequences );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CNode::SetValue( std::string_view szValue )
{
	return m_nKind == Kind_t::KIND_PAIR && Respell( m_Value, szValue, m_bEscapeSequences );
}

//-----------------------------------------------------------------------------
// Purpose: Lookups are case-insensitive, like the engine's.
//-----------------------------------------------------------------------------
const CNode *CNode::Find( std::string_view szKey ) const
{
	const auto it = std::ranges::find_if( m_Children, [ szKey ]( const CNode &child ) {
		return child.m_nKind != Kind_t::KIND_STRAY && EqualsIgnoreCase( child.m_Key.GetText(), szKey );
	} );
	return it == m_Children.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CNode *CNode::Find( std::string_view szKey )
{
	return const_cast<CNode *>( std::as_const( *this ).Find( szKey ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<const CNode *> CNode::FindAll( std::string_view szKey ) const
{
	std::vector<const CNode *> out;
	for ( const CNode &child : m_Children )
	{
		if ( child.m_nKind != Kind_t::KIND_STRAY && EqualsIgnoreCase( child.m_Key.GetText(), szKey ) )
		{
			out.push_back( &child );
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view CNode::GetIndentation() const
{
	const std::string &szLeading = m_Key.m_szRaw.empty() ? m_Open.m_szLeading : m_Key.m_szLeading;
	const auto nNewline = szLeading.rfind( '\n' );
	const std::string_view szTail = nNewline == std::string::npos ? std::string_view{ szLeading } : std::string_view{ szLeading }.substr( nNewline + 1 );
	return std::ranges::all_of( szTail, IsSpace ) ? szTail : std::string_view{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CNode::Layout( std::string_view szIndent )
{
	const std::string szLineStart = "\n" + std::string{ szIndent };
	m_Key.m_szLeading = szLineStart;
	if ( m_nKind != Kind_t::KIND_BLOCK )
	{
		return;
	}
	m_Open.m_szLeading = szLineStart;
	if ( m_Close )
	{
		m_Close->m_szLeading = szLineStart;
	}
	const std::string szChildIndent = std::string{ szIndent } + "\t";
	for ( CNode &child : m_Children )
	{
		child.Layout( szChildIndent );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CNode::SetEscapeSequences( bool bEscapeSequences )
{
	m_bEscapeSequences = bEscapeSequences;
	for ( CNode &child : m_Children )
	{
		child.SetEscapeSequences( bEscapeSequences );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Only valid on Blocks. With Layout::Infer the child is indented like its siblings, and a
//          pair's key-to-value spacing is copied from a neighbouring pair.
//-----------------------------------------------------------------------------
CNode &CNode::InsertChild( std::size_t nIndex, CNode child, Layout_t nLayout )
{
	nIndex = std::min( nIndex, m_Children.size() );
	child.SetEscapeSequences( m_bEscapeSequences );
	if ( nLayout == Layout_t::LAYOUT_INFER )
	{
		const std::string szIndent = m_Children.empty()
			? std::string{ GetIndentation() } + "\t"
			: std::string{ m_Children[ nIndex > 0 ? nIndex - 1 : 0 ].GetIndentation() };
		child.Layout( szIndent );

		// Line values up the way the neighbouring pairs do ("key"\t\t"value").
		if ( child.m_nKind == Kind_t::KIND_PAIR )
		{
			const auto spacing = []( const CNode &n ) {
				return n.m_nKind == Kind_t::KIND_PAIR && !n.m_Value.m_szLeading.empty() && n.m_Value.m_szLeading.find( '\n' ) == std::string::npos;
			};
			for ( std::size_t nDistance = 1; nDistance <= m_Children.size(); nDistance++ )
			{
				const CNode *pBefore = nIndex >= nDistance ? &m_Children[ nIndex - nDistance ] : nullptr;
				const CNode *pAfter = nIndex + nDistance - 1 < m_Children.size() ? &m_Children[ nIndex + nDistance - 1 ] : nullptr;
				const CNode *pSibling = pBefore && spacing( *pBefore ) ? pBefore : pAfter && spacing( *pAfter ) ? pAfter
																												: nullptr;
				if ( pSibling )
				{
					child.m_Value.m_szLeading = pSibling->m_Value.m_szLeading;
					break;
				}
			}
		}

		if ( nIndex == m_Children.size() && m_Close )
		{
			std::string &szBeforeClose = m_Close->m_szLeading;
			// Comments just before the closing brace ("// List additional files here") stay above appended entries.
			if ( const auto nComment = szBeforeClose.rfind( "//" ); nComment != std::string::npos )
			{
				const auto nLineEnd = szBeforeClose.find( '\n', nComment );
				child.m_Key.m_szLeading.insert( 0, szBeforeClose.substr( 0, nLineEnd ) );
				szBeforeClose.erase( 0, nLineEnd );
			}
			// Keep the closing brace on its own line when appending to a block written as `{}`.
			if ( szBeforeClose.find( '\n' ) == std::string::npos )
			{
				szBeforeClose = "\n" + std::string{ GetIndentation() };
			}
		}
	}
	return *m_Children.insert( m_Children.begin() + static_cast<std::ptrdiff_t>( nIndex ), std::move( child ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CNode &CNode::AppendChild( CNode child, Layout_t nLayout )
{
	return InsertChild( m_Children.size(), std::move( child ), nLayout );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CNode::RemoveChild( std::size_t nIndex )
{
	if ( nIndex < m_Children.size() )
	{
		m_Children.erase( m_Children.begin() + static_cast<std::ptrdiff_t>( nIndex ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CNode CNode::TakeChild( std::size_t nIndex )
{
	CNode taken = std::move( m_Children.at( nIndex ) );
	m_Children.erase( m_Children.begin() + static_cast<std::ptrdiff_t>( nIndex ) );
	return taken;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const CNode *CDocument::Find( std::string_view szKey ) const
{
	const auto it = std::ranges::find_if( m_Roots, [ szKey ]( const CNode &root ) {
		return root.GetKind() != CNode::Kind_t::KIND_STRAY && EqualsIgnoreCase( root.GetKeyToken().GetText(), szKey );
	} );
	return it == m_Roots.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CNode *CDocument::Find( std::string_view szKey )
{
	return const_cast<CNode *>( std::as_const( *this ).Find( szKey ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<const CNode *> CDocument::FindAll( std::string_view szKey ) const
{
	std::vector<const CNode *> out;
	for ( const CNode &root : m_Roots )
	{
		if ( root.GetKind() != CNode::Kind_t::KIND_STRAY && EqualsIgnoreCase( root.GetKeyToken().GetText(), szKey ) )
		{
			out.push_back( &root );
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CNode &CDocument::AppendRoot( CNode node )
{
	return InsertRoot( m_Roots.size(), std::move( node ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CNode &CDocument::InsertRoot( std::size_t nIndex, CNode node, Layout_t nLayout )
{
	nIndex = std::min( nIndex, m_Roots.size() );
	node.SetEscapeSequences( m_Options.m_bEscapeSequences );
	if ( nLayout == Layout_t::LAYOUT_INFER )
	{
		node.Layout( "" );
		if ( nIndex == 0 )
		{
			node.m_Key.m_szLeading = m_Roots.empty() ? "" : "\n\n";
		}
		else if ( !m_Roots[ nIndex - 1 ].IsBlock() )
		{
			node.m_Key.m_szLeading = "\n";
		}
		else
		{
			node.m_Key.m_szLeading = "\n\n";
		}
	}
	return *m_Roots.insert( m_Roots.begin() + static_cast<std::ptrdiff_t>( nIndex ), std::move( node ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CNode CDocument::TakeRoot( std::size_t nIndex )
{
	CNode taken = std::move( m_Roots.at( nIndex ) );
	m_Roots.erase( m_Roots.begin() + static_cast<std::ptrdiff_t>( nIndex ) );
	return taken;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CParseResult::HasErrors() const
{
	return std::ranges::any_of( m_Diagnostics, []( const Diagnostic_t &d ) { return d.m_nSeverity == Diagnostic_t::Severity_t::SEVERITY_ERROR; } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CParseResult Parse( std::string_view szText, ParseOptions_t options )
{
	CParseResult result;
	result.m_Document.m_Options = options;
	const CLineIndex lines{ szText };
	auto [ tokens, trailing ] = Lex( szText, options.m_bEscapeSequences, lines, result.m_Diagnostics );
	CParser parser{ std::move( tokens ), options.m_bEscapeSequences, lines, result.m_Diagnostics };
	result.m_Document.m_Roots = parser.ParseList( 0 );
	result.m_Document.m_szTrailing = std::move( trailing );
	std::ranges::stable_sort( result.m_Diagnostics, {}, []( const Diagnostic_t &d ) { return std::pair{ d.m_nLine, d.m_nColumn }; } );
	return result;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void WriteNode( std::string &szOut, const CNode &node )
{
	const auto token = [ &szOut ]( const CToken &t ) {
		szOut += t.m_szLeading;
		szOut += t.m_szRaw;
	};

	token( node.m_Key );
	switch ( node.m_nKind )
	{
		case CNode::Kind_t::KIND_STRAY:
			if ( node.m_Condition )
			{
				token( *node.m_Condition );
			}
			break;
		case CNode::Kind_t::KIND_PAIR:
			if ( node.m_Condition && node.m_bConditionAfterKey )
			{
				token( *node.m_Condition );
			}
			token( node.m_Value );
			if ( node.m_Condition && !node.m_bConditionAfterKey )
			{
				token( *node.m_Condition );
			}
			break;
		case CNode::Kind_t::KIND_BLOCK:
			if ( node.m_Condition )
			{
				token( *node.m_Condition );
			}
			token( node.m_Open );
			for ( const CNode &child : node.m_Children )
			{
				WriteNode( szOut, child );
			}
			if ( node.m_Close )
			{
				token( *node.m_Close );
			}
			break;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Write( const CDocument &document )
{
	std::string szOut;
	for ( const CNode &root : document.m_Roots )
	{
		WriteNode( szOut, root );
	}
	szOut += document.m_szTrailing;
	return szOut;
}

} // namespace xv::kv1
