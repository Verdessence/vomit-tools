// SPDX-License-Identifier: GPL-3.0-or-later
#include "Kv1.h"

#include <algorithm>
#include <utility>

namespace xv::kv1 {

namespace {

constexpr std::string_view UTF8_BOM = "\xEF\xBB\xBF";

bool isSpace(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

char lower(char c) {
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string decode(std::string_view text, bool escapeSequences) {
	if (!escapeSequences) {
		return std::string{text};
	}
	std::string out;
	out.reserve(text.size());
	for (std::size_t i = 0; i < text.size(); i++) {
		if (text[i] != '\\' || i + 1 == text.size()) {
			out += text[i];
			continue;
		}
		switch (const char next = text[++i]) {
			case 'n': out += '\n'; break;
			case 't': out += '\t'; break;
			default:  out += next; break;
		}
	}
	return out;
}

std::optional<std::string> quote(std::string_view text, bool escapeSequences) {
	std::string out = "\"";
	for (const char c : text) {
		if (!escapeSequences) {
			if (c == '"') {
				return std::nullopt;
			}
			out += c;
			continue;
		}
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\t': out += "\\t"; break;
			default:   out += c; break;
		}
	}
	out += '"';
	return out;
}

bool canBeUnquoted(std::string_view text) {
	if (text.empty() || text.front() == '[' || text.starts_with("//")) {
		return false;
	}
	return std::ranges::none_of(text, [](char c) {
		return isSpace(c) || c == '"' || c == '{' || c == '}';
	});
}

// Re-spell a token with new text, keeping it unquoted only if it was and still can be.
bool respell(Token& token, std::string_view text, bool escapeSequences) {
	if (token.kind == TokenKind::Unquoted && canBeUnquoted(text) && !(escapeSequences && text.find('\\') != std::string_view::npos)) {
		token.raw = text;
		return true;
	}
	auto quoted = quote(text, escapeSequences);
	if (!quoted) {
		return false;
	}
	token.raw = std::move(*quoted);
	token.kind = TokenKind::Quoted;
	token.unterminated = false;
	return true;
}

class LineIndex {
public:
	explicit LineIndex(std::string_view text) {
		starts_.push_back(0);
		for (std::size_t i = 0; i < text.size(); i++) {
			if (text[i] == '\n') {
				starts_.push_back(i + 1);
			}
		}
	}

	Diagnostic make(std::size_t offset, Diagnostic::Severity severity, std::string message) const {
		const auto it = std::ranges::upper_bound(starts_, offset);
		const auto line = static_cast<std::size_t>(it - starts_.begin());
		return {severity, line, offset - starts_[line - 1] + 1, std::move(message)};
	}

private:
	std::vector<std::size_t> starts_;
};

struct LexResult {
	std::vector<Token> tokens;
	std::string trailing;
};

LexResult lex(std::string_view src, bool escapeSequences, const LineIndex& lines, std::vector<Diagnostic>& diagnostics) {
	LexResult result;
	std::string trivia;
	std::size_t pos = 0;

	if (src.starts_with(UTF8_BOM)) {
		trivia += UTF8_BOM;
		pos = UTF8_BOM.size();
	}

	while (true) {
		while (pos < src.size()) {
			if (isSpace(src[pos])) {
				trivia += src[pos++];
			} else if (src.substr(pos).starts_with("//")) {
				const auto end = std::min(src.find('\n', pos), src.size());
				trivia += src.substr(pos, end - pos);
				pos = end;
			} else {
				break;
			}
		}
		if (pos >= src.size()) {
			result.trailing = std::move(trivia);
			return result;
		}

		Token token;
		token.leading = std::move(trivia);
		trivia.clear();
		token.offset = pos;
		const std::size_t start = pos;

		switch (src[pos]) {
			case '{':
				token.kind = TokenKind::OpenBrace;
				pos++;
				break;
			case '}':
				token.kind = TokenKind::CloseBrace;
				pos++;
				break;
			case '"': {
				token.kind = TokenKind::Quoted;
				token.unterminated = true;
				pos++;
				while (pos < src.size()) {
					if (escapeSequences && src[pos] == '\\' && pos + 1 < src.size()) {
						pos += 2;
						continue;
					}
					if (src[pos++] == '"') {
						token.unterminated = false;
						break;
					}
				}
				if (token.unterminated) {
					diagnostics.push_back(lines.make(start, Diagnostic::Severity::Error, "string is never closed"));
				} else if (src.substr(start, pos - start).find('\n') != std::string_view::npos) {
					diagnostics.push_back(lines.make(start, Diagnostic::Severity::Warning, "string spans several lines (missing closing quote?)"));
				}
				break;
			}
			case '[':
				token.kind = TokenKind::Conditional;
				while (pos < src.size() && src[pos] != '\n' && src[pos++] != ']') {}
				break;
			default:
				token.kind = TokenKind::Unquoted;
				while (pos < src.size() && !isSpace(src[pos]) && src[pos] != '"' && src[pos] != '{' && src[pos] != '}') {
					pos++;
				}
				break;
		}
		token.raw = src.substr(start, pos - start);
		result.tokens.push_back(std::move(token));
	}
}

} // namespace

std::string_view Token::text() const {
	std::string_view text = raw;
	if (kind == TokenKind::Quoted) {
		text.remove_prefix(1);
		if (!unterminated && !text.empty()) {
			text.remove_suffix(1);
		}
	}
	return text;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
	return std::ranges::equal(a, b, [](char x, char y) { return lower(x) == lower(y); });
}

class Parser {
public:
	Parser(std::vector<Token> tokens, bool escapeSequences, const LineIndex& lines, std::vector<Diagnostic>& diagnostics)
			: tokens_(std::move(tokens))
			, escapeSequences_(escapeSequences)
			, lines_(lines)
			, diagnostics_(diagnostics) {}

	std::vector<Node> parseList(int depth) {
		std::vector<Node> out;
		while (const Token* token = peek()) {
			if (token->kind == TokenKind::CloseBrace) {
				if (depth > 0) {
					return out;
				}
				error(*token, "unexpected '}'");
				out.push_back(stray());
				continue;
			}
			if (token->kind == TokenKind::Conditional) {
				error(*token, "condition without a key");
				out.push_back(stray());
				continue;
			}

			Node node;
			node.escapeSequences_ = escapeSequences_;
			if (token->kind == TokenKind::OpenBrace) {
				error(*token, "block without a key");
			} else {
				node.key_ = take();
				if (peekIs(TokenKind::Conditional)) {
					node.condition_ = take();
				}
				if (peekIs(TokenKind::Quoted) || peekIs(TokenKind::Unquoted)) {
					node.kind_ = Node::Kind::Pair;
					node.value_ = take();
					if (!node.condition_ && peekIs(TokenKind::Conditional)) {
						node.condition_ = take();
						node.conditionAfterKey_ = false;
					}
					out.push_back(std::move(node));
					continue;
				}
				if (!peekIs(TokenKind::OpenBrace)) {
					error(node.key_, "key has no value");
					node.kind_ = Node::Kind::Stray;
					out.push_back(std::move(node));
					continue;
				}
			}

			node.kind_ = Node::Kind::Block;
			node.open_ = take();
			node.children_ = parseList(depth + 1);
			if (peekIs(TokenKind::CloseBrace)) {
				node.close_ = take();
			} else {
				error(node.open_, "block is never closed");
			}
			out.push_back(std::move(node));
		}
		return out;
	}

private:
	const Token* peek() const {
		return index_ < tokens_.size() ? &tokens_[index_] : nullptr;
	}

	bool peekIs(TokenKind kind) const {
		const Token* token = peek();
		return token && token->kind == kind;
	}

	Token take() {
		return std::move(tokens_[index_++]);
	}

	Node stray() {
		Node node;
		node.kind_ = Node::Kind::Stray;
		node.key_ = take();
		node.escapeSequences_ = escapeSequences_;
		return node;
	}

	void error(const Token& token, std::string message) {
		diagnostics_.push_back(lines_.make(token.offset, Diagnostic::Severity::Error, std::move(message)));
	}

	std::vector<Token> tokens_;
	std::size_t index_ = 0;
	bool escapeSequences_;
	const LineIndex& lines_;
	std::vector<Diagnostic>& diagnostics_;
};

Node Node::makePair(std::string_view key, std::string_view value, bool escapeSequences) {
	Node node;
	node.kind_ = Kind::Pair;
	node.escapeSequences_ = escapeSequences;
	node.key_.kind = TokenKind::Quoted;
	node.value_.kind = TokenKind::Quoted;
	node.value_.leading = "\t";
	node.setKey(key);
	node.setValue(value);
	return node;
}

Node Node::makeBlock(std::string_view key, bool escapeSequences) {
	Node node;
	node.kind_ = Kind::Block;
	node.escapeSequences_ = escapeSequences;
	node.key_.kind = TokenKind::Quoted;
	node.setKey(key);
	node.open_.kind = TokenKind::OpenBrace;
	node.open_.raw = "{";
	node.close_.emplace();
	node.close_->raw = "}";
	node.close_->kind = TokenKind::CloseBrace;
	return node;
}

std::string Node::key() const {
	return decode(key_.text(), escapeSequences_);
}

std::string Node::value() const {
	return kind_ == Kind::Pair ? decode(value_.text(), escapeSequences_) : std::string{};
}

std::optional<std::string_view> Node::condition() const {
	if (!condition_) {
		return std::nullopt;
	}
	return condition_->raw;
}

bool Node::setKey(std::string_view key) {
	return respell(key_, key, escapeSequences_);
}

bool Node::setValue(std::string_view value) {
	return kind_ == Kind::Pair && respell(value_, value, escapeSequences_);
}

const Node* Node::find(std::string_view key) const {
	const auto it = std::ranges::find_if(children_, [key](const Node& child) {
		return child.kind_ != Kind::Stray && equalsIgnoreCase(child.key_.text(), key);
	});
	return it == children_.end() ? nullptr : &*it;
}

Node* Node::find(std::string_view key) {
	return const_cast<Node*>(std::as_const(*this).find(key));
}

std::vector<const Node*> Node::findAll(std::string_view key) const {
	std::vector<const Node*> out;
	for (const Node& child : children_) {
		if (child.kind_ != Kind::Stray && equalsIgnoreCase(child.key_.text(), key)) {
			out.push_back(&child);
		}
	}
	return out;
}

std::string_view Node::indentation() const {
	const std::string& leading = key_.raw.empty() ? open_.leading : key_.leading;
	const auto newline = leading.rfind('\n');
	const std::string_view tail = newline == std::string::npos ? std::string_view{leading} : std::string_view{leading}.substr(newline + 1);
	return std::ranges::all_of(tail, isSpace) ? tail : std::string_view{};
}

void Node::layout(std::string_view indent) {
	const std::string lineStart = "\n" + std::string{indent};
	key_.leading = lineStart;
	if (kind_ != Kind::Block) {
		return;
	}
	open_.leading = lineStart;
	if (close_) {
		close_->leading = lineStart;
	}
	const std::string childIndent = std::string{indent} + "\t";
	for (Node& child : children_) {
		child.layout(childIndent);
	}
}

void Node::setEscapeSequences(bool escapeSequences) {
	escapeSequences_ = escapeSequences;
	for (Node& child : children_) {
		child.setEscapeSequences(escapeSequences);
	}
}

Node& Node::insertChild(std::size_t index, Node child, Layout layout) {
	index = std::min(index, children_.size());
	child.setEscapeSequences(escapeSequences_);
	if (layout == Layout::Infer) {
		const std::string indent = children_.empty()
			? std::string{indentation()} + "\t"
			: std::string{children_[index > 0 ? index - 1 : 0].indentation()};
		child.layout(indent);

		// Line values up the way the neighbouring pairs do ("key"\t\t"value").
		if (child.kind_ == Kind::Pair) {
			const auto spacing = [](const Node& n) {
				return n.kind_ == Kind::Pair && !n.value_.leading.empty() && n.value_.leading.find('\n') == std::string::npos;
			};
			for (std::size_t distance = 1; distance <= children_.size(); distance++) {
				const Node* before = index >= distance ? &children_[index - distance] : nullptr;
				const Node* after = index + distance - 1 < children_.size() ? &children_[index + distance - 1] : nullptr;
				const Node* sibling = before && spacing(*before) ? before : after && spacing(*after) ? after : nullptr;
				if (sibling) {
					child.value_.leading = sibling->value_.leading;
					break;
				}
			}
		}

		if (index == children_.size() && close_) {
			std::string& beforeClose = close_->leading;
			// Comments just before the closing brace ("// List additional files here") stay above appended entries.
			if (const auto comment = beforeClose.rfind("//"); comment != std::string::npos) {
				const auto lineEnd = beforeClose.find('\n', comment);
				child.key_.leading.insert(0, beforeClose.substr(0, lineEnd));
				beforeClose.erase(0, lineEnd);
			}
			// Keep the closing brace on its own line when appending to a block written as `{}`.
			if (beforeClose.find('\n') == std::string::npos) {
				beforeClose = "\n" + std::string{indentation()};
			}
		}
	}
	return *children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(index), std::move(child));
}

Node& Node::appendChild(Node child, Layout layout) {
	return insertChild(children_.size(), std::move(child), layout);
}

void Node::removeChild(std::size_t index) {
	if (index < children_.size()) {
		children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(index));
	}
}

Node Node::takeChild(std::size_t index) {
	Node taken = std::move(children_.at(index));
	children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(index));
	return taken;
}

const Node* Document::find(std::string_view key) const {
	const auto it = std::ranges::find_if(roots, [key](const Node& root) {
		return root.kind() != Node::Kind::Stray && equalsIgnoreCase(root.keyToken().text(), key);
	});
	return it == roots.end() ? nullptr : &*it;
}

Node* Document::find(std::string_view key) {
	return const_cast<Node*>(std::as_const(*this).find(key));
}

std::vector<const Node*> Document::findAll(std::string_view key) const {
	std::vector<const Node*> out;
	for (const Node& root : roots) {
		if (root.kind() != Node::Kind::Stray && equalsIgnoreCase(root.keyToken().text(), key)) {
			out.push_back(&root);
		}
	}
	return out;
}

Node& Document::appendRoot(Node node) {
	return insertRoot(roots.size(), std::move(node));
}

Node& Document::insertRoot(std::size_t index, Node node, Layout layout) {
	index = std::min(index, roots.size());
	node.setEscapeSequences(options.escapeSequences);
	if (layout == Layout::Infer) {
		node.layout("");
		if (index == 0) {
			node.key_.leading = roots.empty() ? "" : "\n\n";
		} else if (!roots[index - 1].isBlock()) {
			node.key_.leading = "\n";
		} else {
			node.key_.leading = "\n\n";
		}
	}
	return *roots.insert(roots.begin() + static_cast<std::ptrdiff_t>(index), std::move(node));
}

Node Document::takeRoot(std::size_t index) {
	Node taken = std::move(roots.at(index));
	roots.erase(roots.begin() + static_cast<std::ptrdiff_t>(index));
	return taken;
}

bool ParseResult::hasErrors() const {
	return std::ranges::any_of(diagnostics, [](const Diagnostic& d) { return d.severity == Diagnostic::Severity::Error; });
}

ParseResult parse(std::string_view text, ParseOptions options) {
	ParseResult result;
	result.document.options = options;
	const LineIndex lines{text};
	auto [tokens, trailing] = lex(text, options.escapeSequences, lines, result.diagnostics);
	Parser parser{std::move(tokens), options.escapeSequences, lines, result.diagnostics};
	result.document.roots = parser.parseList(0);
	result.document.trailing = std::move(trailing);
	std::ranges::stable_sort(result.diagnostics, {}, [](const Diagnostic& d) { return std::pair{d.line, d.column}; });
	return result;
}

void writeNode(std::string& out, const Node& node) {
	const auto token = [&out](const Token& t) {
		out += t.leading;
		out += t.raw;
	};

	token(node.key_);
	switch (node.kind_) {
		case Node::Kind::Stray:
			if (node.condition_) {
				token(*node.condition_);
			}
			break;
		case Node::Kind::Pair:
			if (node.condition_ && node.conditionAfterKey_) {
				token(*node.condition_);
			}
			token(node.value_);
			if (node.condition_ && !node.conditionAfterKey_) {
				token(*node.condition_);
			}
			break;
		case Node::Kind::Block:
			if (node.condition_) {
				token(*node.condition_);
			}
			token(node.open_);
			for (const Node& child : node.children_) {
				writeNode(out, child);
			}
			if (node.close_) {
				token(*node.close_);
			}
			break;
	}
}

std::string write(const Document& document) {
	std::string out;
	for (const Node& root : document.roots) {
		writeNode(out, root);
	}
	out += document.trailing;
	return out;
}

} // namespace xv::kv1
