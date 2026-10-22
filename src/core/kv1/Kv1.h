// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Lossless KeyValues1 (the text format of soundscapes, VMTs, gameinfo.txt, manifests...).
//
// Every token keeps the exact bytes that preceded it (whitespace and // comments), so
// write(parse(x).document) == x for any input, including malformed files. Edits touch only
// the tokens they change.
namespace xv::kv1 {

enum class TokenKind {
	Quoted,
	Unquoted,
	OpenBrace,
	CloseBrace,
	Conditional, // [$WIN32], [!$X360], ...
};

struct Token {
	std::string leading;          // whitespace and comments before the token, verbatim
	std::string raw;              // the token's own bytes, quotes included
	TokenKind kind = TokenKind::Unquoted;
	std::size_t offset = 0;       // byte offset of raw in the parsed source
	bool unterminated = false;    // a quoted string that ran into the end of the file

	// The token without its surrounding quotes. Escape sequences are not decoded.
	[[nodiscard]] std::string_view text() const;
};

struct Diagnostic {
	enum class Severity { Warning, Error };

	Severity severity = Severity::Error;
	std::size_t line = 0;   // 1-based
	std::size_t column = 0; // 1-based, counted in bytes
	std::string message;
};

struct ParseOptions {
	// The engine only decodes \" \\ \n \t for files loaded with escape sequences enabled.
	// Soundscapes, VMTs and gameinfo.txt are not.
	bool escapeSequences = false;
};

// How an inserted node is formatted.
enum class Layout {
	Infer, // indent like its new siblings (for new nodes)
	Keep,  // leave its whitespace as is (for putting a removed node back, e.g. on undo)
};

class Node {
public:
	enum class Kind {
		Pair,  // key value [condition]
		Block, // key [condition] { children }
		Stray, // a token the grammar could not place; kept so the file still round-trips
	};

	static Node makePair(std::string_view key, std::string_view value, bool escapeSequences = false);
	static Node makeBlock(std::string_view key, bool escapeSequences = false);

	[[nodiscard]] Kind kind() const { return kind_; }
	[[nodiscard]] bool isPair() const { return kind_ == Kind::Pair; }
	[[nodiscard]] bool isBlock() const { return kind_ == Kind::Block; }

	[[nodiscard]] std::string key() const;
	[[nodiscard]] std::string value() const; // empty unless this is a Pair
	[[nodiscard]] std::optional<std::string_view> condition() const;

	// Keep the original quoting style where the new text allows it. Return false if the text
	// cannot be represented (a double quote while escape sequences are off).
	bool setKey(std::string_view key);
	bool setValue(std::string_view value);

	[[nodiscard]] const std::vector<Node>& children() const { return children_; }
	[[nodiscard]] const Node& child(std::size_t index) const { return children_.at(index); }
	[[nodiscard]] Node& child(std::size_t index) { return children_.at(index); }

	// Lookups are case-insensitive, like the engine's.
	[[nodiscard]] const Node* find(std::string_view key) const;
	[[nodiscard]] Node* find(std::string_view key);
	[[nodiscard]] std::vector<const Node*> findAll(std::string_view key) const;

	// Only valid on Blocks. With Layout::Infer the child is indented like its siblings, and a
	// pair's key-to-value spacing is copied from a neighbouring pair.
	Node& insertChild(std::size_t index, Node child, Layout layout = Layout::Infer);
	Node& appendChild(Node child, Layout layout = Layout::Infer);
	void removeChild(std::size_t index);
	Node takeChild(std::size_t index);

	[[nodiscard]] const Token& keyToken() const { return key_; }
	[[nodiscard]] const Token& valueToken() const { return value_; }

private:
	friend class Parser;
	friend struct Document;
	friend void writeNode(std::string& out, const Node& node);

	[[nodiscard]] std::string_view indentation() const;
	void layout(std::string_view indent);
	void setEscapeSequences(bool escapeSequences);

	Kind kind_ = Kind::Stray;
	Token key_;
	Token value_;                   // Pair
	std::optional<Token> condition_;
	bool conditionAfterKey_ = true; // Pair: `key [cond] value` rather than `key value [cond]`
	Token open_;                    // Block
	std::optional<Token> close_;    // Block; missing when the file ended inside it
	std::vector<Node> children_;
	bool escapeSequences_ = false;
};

struct Document {
	std::vector<Node> roots;
	std::string trailing; // whitespace and comments after the last token
	ParseOptions options;

	[[nodiscard]] const Node* find(std::string_view key) const;
	[[nodiscard]] Node* find(std::string_view key);
	[[nodiscard]] std::vector<const Node*> findAll(std::string_view key) const;

	Node& appendRoot(Node node);
	Node& insertRoot(std::size_t index, Node node, Layout layout = Layout::Infer);
	Node takeRoot(std::size_t index);
};

struct ParseResult {
	Document document;
	std::vector<Diagnostic> diagnostics;

	[[nodiscard]] bool hasErrors() const;
};

[[nodiscard]] ParseResult parse(std::string_view text, ParseOptions options = {});
[[nodiscard]] std::string write(const Document& document);

[[nodiscard]] bool equalsIgnoreCase(std::string_view a, std::string_view b);

} // namespace xv::kv1
