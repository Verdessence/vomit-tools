//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Lossless KeyValues1 (the text format of soundscapes, VMTs, gameinfo.txt, manifests...).
//          Every token keeps the exact bytes that preceded it (whitespace and // comments), so
//          write(parse(x).document) == x for any input, including malformed files. Edits touch
//          only the tokens they change.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xv::kv1
{

enum class TokenKind_t
{
	TOKEN_KIND_QUOTED,
	TOKEN_KIND_UNQUOTED,
	TOKEN_KIND_OPEN_BRACE,
	TOKEN_KIND_CLOSE_BRACE,
	TOKEN_KIND_CONDITIONAL, // [$WIN32], [!$X360], ...
};

struct CToken
{
	std::string m_szLeading;          // whitespace and comments before the token, verbatim
	std::string m_szRaw;              // the token's own bytes, quotes included
	TokenKind_t m_nKind = TokenKind_t::TOKEN_KIND_UNQUOTED;
	std::size_t m_nOffset = 0;       // byte offset of raw in the parsed source
	bool m_bUnterminated = false;    // a quoted string that ran into the end of the file

	// The token without its surrounding quotes. Escape sequences are not decoded.
	[[nodiscard]] std::string_view GetText() const;
};

struct Diagnostic_t
{
	enum class Severity_t
	{
		SEVERITY_WARNING,
		SEVERITY_ERROR
	};

	Severity_t m_nSeverity = Severity_t::SEVERITY_ERROR;
	std::size_t m_nLine = 0;   // 1-based
	std::size_t m_nColumn = 0; // 1-based, counted in bytes
	std::string m_szMessage;
};

struct ParseOptions_t
{
	// The engine only decodes \" \\ \n \t for files loaded with escape sequences enabled.
	// Soundscapes, VMTs and gameinfo.txt are not.
	bool m_bEscapeSequences = false;
};

// How an inserted node is formatted.
enum class Layout_t
{
	LAYOUT_INFER, // indent like its new siblings (for new nodes)
	LAYOUT_KEEP,  // leave its whitespace as is (for putting a removed node back, e.g. on undo)
};

class CNode
{
public:
	enum class Kind_t
	{
		KIND_PAIR,  // key value [condition]
		KIND_BLOCK, // key [condition] { children }
		KIND_STRAY, // a token the grammar could not place; kept so the file still round-trips
	};

	static CNode MakePair( std::string_view szKey, std::string_view szValue, bool bEscapeSequences = false );
	static CNode MakeBlock( std::string_view szKey, bool bEscapeSequences = false );

	[[nodiscard]] Kind_t GetKind() const { return m_nKind; }
	[[nodiscard]] bool IsPair() const { return m_nKind == Kind_t::KIND_PAIR; }
	[[nodiscard]] bool IsBlock() const { return m_nKind == Kind_t::KIND_BLOCK; }

	[[nodiscard]] std::string GetKey() const;
	[[nodiscard]] std::string GetValue() const; // empty unless this is a Pair
	[[nodiscard]] std::optional<std::string_view> GetCondition() const;

	// Keep the original quoting style where the new text allows it. Return false if the text
	// cannot be represented (a double quote while escape sequences are off).
	bool SetKey( std::string_view szKey );
	bool SetValue( std::string_view szValue );

	[[nodiscard]] const std::vector<CNode> &GetChildren() const { return m_Children; }
	[[nodiscard]] const CNode &Child( std::size_t nIndex ) const { return m_Children.at( nIndex ); }
	[[nodiscard]] CNode &Child( std::size_t nIndex ) { return m_Children.at( nIndex ); }

	// Lookups are case-insensitive, like the engine's.
	[[nodiscard]] const CNode *Find( std::string_view szKey ) const;
	[[nodiscard]] CNode *Find( std::string_view szKey );
	[[nodiscard]] std::vector<const CNode *> FindAll( std::string_view szKey ) const;

	// Only valid on Blocks. With Layout::Infer the child is indented like its siblings, and a
	// pair's key-to-value spacing is copied from a neighbouring pair.
	CNode &InsertChild( std::size_t nIndex, CNode child, Layout_t nLayout = Layout_t::LAYOUT_INFER );
	CNode &AppendChild( CNode child, Layout_t nLayout = Layout_t::LAYOUT_INFER );
	void RemoveChild( std::size_t nIndex );
	CNode TakeChild( std::size_t nIndex );

	[[nodiscard]] const CToken &GetKeyToken() const { return m_Key; }
	[[nodiscard]] const CToken &GetValueToken() const { return m_Value; }

private:
	friend class CParser;
	friend struct CDocument;
	friend void WriteNode( std::string &szOut, const CNode &node );

	[[nodiscard]] std::string_view GetIndentation() const;
	void Layout( std::string_view szIndent );
	void SetEscapeSequences( bool bEscapeSequences );

	Kind_t m_nKind = Kind_t::KIND_STRAY;
	CToken m_Key;
	CToken m_Value;                   // Pair
	std::optional<CToken> m_Condition;
	bool m_bConditionAfterKey = true; // Pair: `key [cond] value` rather than `key value [cond]`
	CToken m_Open;                    // Block
	std::optional<CToken> m_Close;    // Block; missing when the file ended inside it
	std::vector<CNode> m_Children;
	bool m_bEscapeSequences = false;
};

struct CDocument
{
	std::vector<CNode> m_Roots;
	std::string m_szTrailing; // whitespace and comments after the last token
	ParseOptions_t m_Options;

	[[nodiscard]] const CNode *Find( std::string_view szKey ) const;
	[[nodiscard]] CNode *Find( std::string_view szKey );
	[[nodiscard]] std::vector<const CNode *> FindAll( std::string_view szKey ) const;

	CNode &AppendRoot( CNode node );
	CNode &InsertRoot( std::size_t nIndex, CNode node, Layout_t nLayout = Layout_t::LAYOUT_INFER );
	CNode TakeRoot( std::size_t nIndex );
};

struct CParseResult
{
	CDocument m_Document;
	std::vector<Diagnostic_t> m_Diagnostics;

	[[nodiscard]] bool HasErrors() const;
};

[[nodiscard]] CParseResult Parse( std::string_view szText, ParseOptions_t options = {} );
[[nodiscard]] std::string Write( const CDocument &document );

[[nodiscard]] bool EqualsIgnoreCase( std::string_view a, std::string_view b );

} // namespace xv::kv1
