// SPDX-License-Identifier: GPL-3.0-or-later
#include "Dmx.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstring>
#include <format>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace xv::dmx {

static_assert(std::endian::native == std::endian::little, "DMX binary I/O assumes a little-endian host");

namespace {

constexpr std::array<std::string_view, 29> TYPE_NAMES{
	"unknown",
	"element", "int", "float", "bool", "string", "binary", "elementid",
	"color", "vector2", "vector3", "vector4", "qangle", "quaternion", "matrix",
	"element_array", "int_array", "float_array", "bool_array", "string_array", "binary_array", "elementid_array",
	"color_array", "vector2_array", "vector3_array", "vector4_array", "qangle_array", "quaternion_array", "matrix_array",
};

template<typename T> struct IsVector : std::false_type {};
template<typename T> struct IsVector<std::vector<T>> : std::true_type {};

// Scalars as stored in a Value (Bytes is a vector, but a binary blob is a single value).
template<typename T>
constexpr bool IS_TEXT_SCALAR = !IsVector<T>::value || std::is_same_v<T, Bytes>;

AttrType baseType(AttrType type) {
	return isArray(type) ? static_cast<AttrType>(static_cast<std::uint8_t>(type) - ATTR_TYPE_ARRAY_OFFSET) : type;
}

// ---------------------------------------------------------------------------------------------
// Header: <!-- dmx encoding binary 2 format pcf 1 -->

struct Header {
	std::string encoding;
	int encodingVersion = 0;
	std::string format;
	int formatVersion = 0;
};

std::optional<int> parseInt(std::string_view text) {
	int value = 0;
	const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (ec != std::errc{} || end != text.data() + text.size()) {
		return std::nullopt;
	}
	return value;
}

std::optional<Header> parseHeader(std::string_view text) {
	std::vector<std::string_view> words;
	std::size_t pos = 0;
	while (pos < text.size()) {
		const auto start = text.find_first_not_of(" \t\r\n", pos);
		if (start == std::string_view::npos) {
			break;
		}
		const auto end = std::min(text.find_first_of(" \t\r\n", start), text.size());
		words.push_back(text.substr(start, end - start));
		pos = end;
	}
	if (words.size() != 9 || words[0] != "<!--" || words[1] != "dmx" || words[2] != "encoding" || words[5] != "format" || words[8] != "-->") {
		return std::nullopt;
	}
	const auto encodingVersion = parseInt(words[4]);
	const auto formatVersion = parseInt(words[7]);
	if (!encodingVersion || !formatVersion) {
		return std::nullopt;
	}
	return Header{std::string{words[3]}, *encodingVersion, std::string{words[6]}, *formatVersion};
}

// ---------------------------------------------------------------------------------------------
// Binary reading

class Reader {
public:
	explicit Reader(std::span<const std::byte> data) : data_(data) {}

	template<typename T>
	bool read(T& out) {
		static_assert(std::is_trivially_copyable_v<T>);
		if (remaining() < sizeof(T)) {
			return fail("unexpected end of file");
		}
		std::memcpy(&out, data_.data() + pos_, sizeof(T));
		pos_ += sizeof(T);
		return true;
	}

	bool readString(std::string& out) {
		const auto rest = data_.subspan(pos_);
		const auto nul = std::ranges::find(rest, std::byte{0});
		if (nul == rest.end()) {
			return fail("unterminated string");
		}
		const auto length = static_cast<std::size_t>(nul - rest.begin());
		out.assign(reinterpret_cast<const char*>(rest.data()), length);
		pos_ += length + 1;
		return true;
	}

	bool readBytes(std::size_t count, Bytes& out) {
		if (remaining() < count) {
			return fail("unexpected end of file");
		}
		out.assign(data_.begin() + static_cast<std::ptrdiff_t>(pos_), data_.begin() + static_cast<std::ptrdiff_t>(pos_ + count));
		pos_ += count;
		return true;
	}

	// A count is rejected if that many items of at least minItemSize bytes cannot fit in the rest of the file.
	bool readCount(std::int32_t& count, std::size_t minItemSize) {
		if (!read(count)) {
			return false;
		}
		if (count < 0 || static_cast<std::size_t>(count) > remaining() / minItemSize) {
			return fail(std::format("impossible count {}", count));
		}
		return true;
	}

	bool fail(std::string_view message) {
		if (error_.empty()) {
			error_ = std::format("{} at offset {}", message, pos_);
		}
		return false;
	}

	[[nodiscard]] std::size_t remaining() const { return data_.size() - pos_; }
	[[nodiscard]] std::size_t position() const { return pos_; }
	[[nodiscard]] const std::string& error() const { return error_; }

private:
	std::span<const std::byte> data_;
	std::size_t pos_ = 0;
	std::string error_;
};

struct ReadContext {
	Reader& reader;
	std::int32_t elementCount;
};

template<typename T> requires std::is_trivially_copyable_v<T>
bool readOne(ReadContext& c, T& out) {
	return c.reader.read(out);
}

bool readOne(ReadContext& c, bool& out) {
	std::uint8_t byte = 0;
	if (!c.reader.read(byte)) {
		return false;
	}
	out = byte != 0;
	return true;
}

bool readOne(ReadContext& c, std::string& out) {
	return c.reader.readString(out);
}

bool readOne(ReadContext& c, Bytes& out) {
	std::int32_t size = 0;
	return c.reader.readCount(size, 1) && c.reader.readBytes(static_cast<std::size_t>(size), out);
}

bool readOne(ReadContext& c, ElementRef& out) {
	if (!c.reader.read(out.index)) {
		return false;
	}
	if (out.index == ElementRef::EXTERNAL) {
		return c.reader.readString(out.externalId);
	}
	if (out.index < ElementRef::NONE || out.index >= c.elementCount) {
		return c.reader.fail(std::format("element reference {} out of range", out.index));
	}
	return true;
}

template<typename T>
bool readAs(ReadContext& c, Value& out, bool array, std::size_t minItemSize) {
	if (!array) {
		T value{};
		if (!readOne(c, value)) {
			return false;
		}
		out = std::move(value);
		return true;
	}
	std::int32_t count = 0;
	if (!c.reader.readCount(count, minItemSize)) {
		return false;
	}
	std::vector<T> items;
	items.reserve(static_cast<std::size_t>(count));
	for (std::int32_t i = 0; i < count; i++) {
		T value{};
		if (!readOne(c, value)) {
			return false;
		}
		items.push_back(std::move(value));
	}
	out = std::move(items);
	return true;
}

bool readValue(ReadContext& c, AttrType type, Value& out) {
	const bool array = isArray(type);
	switch (baseType(type)) {
		case AttrType::Element:    return readAs<ElementRef>(c, out, array, 4);
		case AttrType::Int:        return readAs<std::int32_t>(c, out, array, 4);
		case AttrType::Float:      return readAs<float>(c, out, array, 4);
		case AttrType::Bool:       return readAs<bool>(c, out, array, 1);
		case AttrType::String:     return readAs<std::string>(c, out, array, 1);
		case AttrType::Binary:     return readAs<Bytes>(c, out, array, 4);
		case AttrType::ObjectId:   return readAs<Guid>(c, out, array, sizeof(Guid));
		case AttrType::Color:      return readAs<Color>(c, out, array, sizeof(Color));
		case AttrType::Vector2:    return readAs<Vec2>(c, out, array, sizeof(Vec2));
		case AttrType::Vector3:
		case AttrType::QAngle:     return readAs<Vec3>(c, out, array, sizeof(Vec3));
		case AttrType::Vector4:
		case AttrType::Quaternion: return readAs<Vec4>(c, out, array, sizeof(Vec4));
		case AttrType::Matrix:     return readAs<Matrix>(c, out, array, sizeof(Matrix));
		default:                   return c.reader.fail("unknown attribute type");
	}
}

// ---------------------------------------------------------------------------------------------
// Binary writing

class Writer {
public:
	template<typename T>
	void put(const T& value) {
		static_assert(std::is_trivially_copyable_v<T>);
		const auto* bytes = reinterpret_cast<const std::byte*>(&value);
		out_.insert(out_.end(), bytes, bytes + sizeof(T));
	}

	void putString(std::string_view text) {
		const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
		out_.insert(out_.end(), bytes, bytes + text.size());
		out_.push_back(std::byte{0});
	}

	void putBytes(const Bytes& bytes) {
		out_.insert(out_.end(), bytes.begin(), bytes.end());
	}

	Bytes take() { return std::move(out_); }

private:
	Bytes out_;
};

template<typename T> requires std::is_trivially_copyable_v<T>
void writeOne(Writer& w, const T& value) {
	w.put(value);
}

void writeOne(Writer& w, bool value) {
	w.put(static_cast<std::uint8_t>(value ? 1 : 0));
}

void writeOne(Writer& w, const std::string& value) {
	w.putString(value);
}

void writeOne(Writer& w, const Bytes& value) {
	w.put(static_cast<std::int32_t>(value.size()));
	w.putBytes(value);
}

void writeOne(Writer& w, const ElementRef& value) {
	w.put(value.index);
	if (value.index == ElementRef::EXTERNAL) {
		w.putString(value.externalId);
	}
}

template<typename T>
bool writeAs(Writer& w, const Value& value, bool array) {
	if (!array) {
		const T* scalar = std::get_if<T>(&value);
		if (scalar) {
			writeOne(w, *scalar);
		}
		return scalar != nullptr;
	}
	const auto* items = std::get_if<std::vector<T>>(&value);
	if (!items) {
		return false;
	}
	w.put(static_cast<std::int32_t>(items->size()));
	for (const auto& item : *items) {
		if constexpr (std::is_same_v<T, bool>) {
			writeOne(w, static_cast<bool>(item));
		} else {
			writeOne(w, item);
		}
	}
	return true;
}

bool writeValue(Writer& w, AttrType type, const Value& value) {
	const bool array = isArray(type);
	switch (baseType(type)) {
		case AttrType::Element:    return writeAs<ElementRef>(w, value, array);
		case AttrType::Int:        return writeAs<std::int32_t>(w, value, array);
		case AttrType::Float:      return writeAs<float>(w, value, array);
		case AttrType::Bool:       return writeAs<bool>(w, value, array);
		case AttrType::String:     return writeAs<std::string>(w, value, array);
		case AttrType::Binary:     return writeAs<Bytes>(w, value, array);
		case AttrType::ObjectId:   return writeAs<Guid>(w, value, array);
		case AttrType::Color:      return writeAs<Color>(w, value, array);
		case AttrType::Vector2:    return writeAs<Vec2>(w, value, array);
		case AttrType::Vector3:
		case AttrType::QAngle:     return writeAs<Vec3>(w, value, array);
		case AttrType::Vector4:
		case AttrType::Quaternion: return writeAs<Vec4>(w, value, array);
		case AttrType::Matrix:     return writeAs<Matrix>(w, value, array);
		default:                   return false;
	}
}

// ---------------------------------------------------------------------------------------------
// Equality

bool same(float a, float b) {
	return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

template<std::size_t N>
bool same(const std::array<float, N>& a, const std::array<float, N>& b) {
	return std::ranges::equal(a, b, [](float x, float y) { return same(x, y); });
}

template<typename T>
bool same(const T& a, const T& b) {
	return a == b;
}

template<typename T>
bool same(const std::vector<T>& a, const std::vector<T>& b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.size(); i++) {
		if (!same(static_cast<const T&>(a[i]), static_cast<const T&>(b[i]))) {
			return false;
		}
	}
	return true;
}

bool sameValue(const Value& a, const Value& b) {
	if (a.index() != b.index()) {
		return false;
	}
	return std::visit([&b](const auto& x) {
		using T = std::decay_t<decltype(x)>;
		return same(x, std::get<T>(b));
	}, a);
}

// ---------------------------------------------------------------------------------------------
// keyvalues2 text

std::string quoteKv2(std::string_view text) {
	std::string out = "\"";
	for (const char c : text) {
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

template<std::size_t N>
std::string joinFloats(const std::array<float, N>& values) {
	std::string out;
	for (std::size_t i = 0; i < N; i++) {
		out += std::format("{}{}", i ? " " : "", values[i]);
	}
	return out;
}

std::string scalarText(const auto& value) {
	using T = std::decay_t<decltype(value)>;
	if constexpr (std::is_same_v<T, bool>) {
		return value ? "1" : "0";
	} else if constexpr (std::is_same_v<T, std::int32_t> || std::is_same_v<T, float>) {
		return std::format("{}", value);
	} else if constexpr (std::is_same_v<T, std::string>) {
		return value;
	} else if constexpr (std::is_same_v<T, Bytes>) {
		std::string out;
		for (const std::byte b : value) {
			out += std::format("{:02X}", static_cast<unsigned>(b));
		}
		return out;
	} else if constexpr (std::is_same_v<T, Guid>) {
		return formatGuid(value);
	} else if constexpr (std::is_same_v<T, Color>) {
		return std::format("{} {} {} {}", value.r, value.g, value.b, value.a);
	} else {
		return joinFloats(value);
	}
}

class Kv2Writer {
public:
	explicit Kv2Writer(const Document& document)
			: doc_(document)
			, references_(document.elements.size(), 0) {
		for (const Element& element : doc_.elements) {
			for (const Attribute& attribute : element.attributes) {
				if (const auto* ref = std::get_if<ElementRef>(&attribute.value)) {
					countReference(*ref);
				} else if (const auto* refs = std::get_if<std::vector<ElementRef>>(&attribute.value)) {
					std::ranges::for_each(*refs, [this](const ElementRef& r) { countReference(r); });
				}
			}
		}
	}

	std::string run() {
		out_ = std::format("<!-- dmx encoding keyvalues2 1 format {} {} -->\n", doc_.format, doc_.formatVersion);
		for (std::size_t i = 0; i < doc_.elements.size(); i++) {
			if (i == 0 || !isInline(static_cast<std::int32_t>(i))) {
				writeElement(static_cast<std::int32_t>(i), 0);
				out_ += "\n\n";
			}
		}
		return std::move(out_);
	}

private:
	void countReference(const ElementRef& ref) {
		if (ref.index >= 0) {
			references_[static_cast<std::size_t>(ref.index)]++;
		}
	}

	bool isInline(std::int32_t index) const {
		return index > 0 && references_[static_cast<std::size_t>(index)] == 1;
	}

	static std::string indent(int depth) {
		return std::string(static_cast<std::size_t>(depth), '\t');
	}

	void writeElement(std::int32_t index, int depth) {
		const Element& element = doc_.elements[static_cast<std::size_t>(index)];
		out_ += quoteKv2(element.type) + "\n" + indent(depth) + "{\n";
		out_ += indent(depth + 1) + "\"id\" \"elementid\" \"" + formatGuid(element.id) + "\"\n";
		out_ += indent(depth + 1) + "\"name\" \"string\" " + quoteKv2(element.name) + "\n";
		for (const Attribute& attribute : element.attributes) {
			writeAttribute(attribute, depth + 1);
		}
		out_ += indent(depth) + "}";
	}

	void writeReference(const ElementRef& ref, int depth) {
		if (ref.index >= 0 && isInline(ref.index)) {
			writeElement(ref.index, depth);
		} else if (ref.index >= 0) {
			out_ += "\"element\" \"" + formatGuid(doc_.elements[static_cast<std::size_t>(ref.index)].id) + "\"";
		} else {
			out_ += "\"element\" " + quoteKv2(ref.externalId);
		}
	}

	void writeAttribute(const Attribute& attribute, int depth) {
		const std::string pad = indent(depth);
		out_ += pad + quoteKv2(attribute.name) + " ";
		if (const auto* ref = std::get_if<ElementRef>(&attribute.value)) {
			writeReference(*ref, depth);
			out_ += "\n";
			return;
		}
		out_ += quoteKv2(typeName(attribute.type));
		if (!isArray(attribute.type)) {
			std::visit([this](const auto& value) {
				using T = std::decay_t<decltype(value)>;
				if constexpr (IS_TEXT_SCALAR<T> && !std::is_same_v<T, std::monostate> && !std::is_same_v<T, ElementRef>) {
					out_ += " " + quoteKv2(scalarText(value));
				}
			}, attribute.value);
			out_ += "\n";
			return;
		}
		out_ += "\n" + pad + "[\n";
		bool first = true;
		const auto separator = [&] {
			out_ += first ? "" : ",\n";
			first = false;
		};
		std::visit([&](const auto& value) {
			using T = std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<T, std::vector<ElementRef>>) {
				for (const ElementRef& ref : value) {
					separator();
					out_ += pad + "\t";
					writeReference(ref, depth + 1);
				}
			} else if constexpr (std::is_same_v<T, std::vector<bool>>) {
				for (const bool item : value) {
					separator();
					out_ += pad + "\t" + quoteKv2(scalarText(item));
				}
			} else if constexpr (!IS_TEXT_SCALAR<T>) {
				for (const auto& item : value) {
					separator();
					out_ += pad + "\t" + quoteKv2(scalarText(item));
				}
			}
		}, attribute.value);
		out_ += "\n" + pad + "]\n";
	}

	const Document& doc_;
	std::vector<int> references_;
	std::string out_;
};

} // namespace

std::string_view typeName(AttrType type) {
	const auto index = static_cast<std::size_t>(type);
	return index < TYPE_NAMES.size() ? TYPE_NAMES[index] : TYPE_NAMES[0];
}

const Attribute* Element::find(std::string_view attributeName) const {
	const auto it = std::ranges::find(attributes, attributeName, &Attribute::name);
	return it == attributes.end() ? nullptr : &*it;
}

Attribute* Element::find(std::string_view attributeName) {
	return const_cast<Attribute*>(std::as_const(*this).find(attributeName));
}

bool isSupportedBinaryVersion(int version) {
	return version == 1 || version == 2;
}

ReadResult read(std::span<const std::byte> data) {
	ReadResult result;

	const std::string_view head{reinterpret_cast<const char*>(data.data()), std::min<std::size_t>(data.size(), 512)};
	const auto close = head.find("-->");
	if (close == std::string_view::npos) {
		result.error = "not a DMX file (no header)";
		return result;
	}
	const auto header = parseHeader(head.substr(0, close + 3));
	if (!header) {
		result.error = "unrecognised DMX header";
		return result;
	}
	if (header->encoding != "binary") {
		result.error = std::format("{} encoding is not supported yet", header->encoding);
		return result;
	}
	if (!isSupportedBinaryVersion(header->encodingVersion)) {
		result.error = std::format("binary encoding {} is not supported yet", header->encodingVersion);
		return result;
	}

	Document doc;
	doc.encoding = header->encoding;
	doc.encodingVersion = header->encodingVersion;
	doc.format = header->format;
	doc.formatVersion = header->formatVersion;
	const bool dictionary = doc.encodingVersion >= 2;

	Reader reader{data};
	const auto finish = [&]() -> ReadResult {
		result.bytesRead = reader.position();
		if (!reader.error().empty()) {
			result.error = reader.error();
		} else {
			result.document = std::move(doc);
		}
		return std::move(result);
	};

	std::string headerText;
	if (!reader.readString(headerText)) {
		return finish();
	}

	if (dictionary) {
		std::int16_t count = 0;
		if (!reader.read(count)) {
			return finish();
		}
		if (count < 0) {
			reader.fail("negative string table size");
			return finish();
		}
		doc.stringTable.resize(static_cast<std::size_t>(count));
		for (std::string& entry : doc.stringTable) {
			if (!reader.readString(entry)) {
				return finish();
			}
		}
	}

	const auto readName = [&](std::string& out) {
		if (!dictionary) {
			return reader.readString(out);
		}
		std::int16_t index = 0;
		if (!reader.read(index)) {
			return false;
		}
		if (index < 0 || static_cast<std::size_t>(index) >= doc.stringTable.size()) {
			return reader.fail(std::format("string index {} out of range", index));
		}
		out = doc.stringTable[static_cast<std::size_t>(index)];
		return true;
	};

	std::int32_t elementCount = 0;
	if (!reader.readCount(elementCount, (dictionary ? 2 : 1) + 1 + sizeof(Guid))) {
		return finish();
	}
	doc.elements.resize(static_cast<std::size_t>(elementCount));
	for (Element& element : doc.elements) {
		if (!readName(element.type) || !reader.readString(element.name) || !reader.read(element.id)) {
			return finish();
		}
	}

	ReadContext context{reader, elementCount};
	for (Element& element : doc.elements) {
		std::int32_t attributeCount = 0;
		if (!reader.readCount(attributeCount, (dictionary ? 2 : 1) + 2)) {
			return finish();
		}
		element.attributes.resize(static_cast<std::size_t>(attributeCount));
		for (Attribute& attribute : element.attributes) {
			std::uint8_t type = 0;
			if (!readName(attribute.name) || !reader.read(type)) {
				return finish();
			}
			if (type == 0 || type >= TYPE_NAMES.size()) {
				reader.fail(std::format("unknown attribute type {}", type));
				return finish();
			}
			attribute.type = static_cast<AttrType>(type);
			if (!readValue(context, attribute.type, attribute.value)) {
				return finish();
			}
		}
	}
	return finish();
}

std::optional<Bytes> writeBinary(const Document& document, std::string* error) {
	const auto failed = [error](std::string message) -> std::optional<Bytes> {
		if (error) {
			*error = std::move(message);
		}
		return std::nullopt;
	};
	if (document.encoding != "binary" || !isSupportedBinaryVersion(document.encodingVersion)) {
		return failed(std::format("cannot write {} encoding {}", document.encoding, document.encodingVersion));
	}
	const bool dictionary = document.encodingVersion >= 2;

	// Keep the dictionary order the document was read with; new strings go at the end.
	std::vector<std::string> table;
	std::unordered_map<std::string, std::int16_t> indices;
	if (dictionary) {
		std::unordered_set<std::string> used;
		std::vector<std::string> firstUse;
		const auto use = [&](const std::string& s) {
			if (used.insert(s).second) {
				firstUse.push_back(s);
			}
		};
		// Valve's writer also lists "name": the element name is an attribute, even though
		// encoding 2 stores it inline in the element header.
		for (const Element& element : document.elements) {
			use(element.type);
			for (const Attribute& attribute : element.attributes) {
				use(attribute.name);
			}
			use("name");
		}
		for (const std::string& s : document.stringTable) {
			if (used.contains(s) && !indices.contains(s)) {
				indices.emplace(s, 0);
				table.push_back(s);
			}
		}
		for (const std::string& s : firstUse) {
			if (!indices.contains(s)) {
				indices.emplace(s, 0);
				table.push_back(s);
			}
		}
		if (table.size() > static_cast<std::size_t>(INT16_MAX)) {
			return failed("too many distinct names for binary encoding 2");
		}
		for (std::size_t i = 0; i < table.size(); i++) {
			indices[table[i]] = static_cast<std::int16_t>(i);
		}
	}

	Writer w;
	w.putString(std::format("<!-- dmx encoding binary {} format {} {} -->\n", document.encodingVersion, document.format, document.formatVersion));
	const auto putName = [&](const std::string& name) {
		if (dictionary) {
			w.put(indices.at(name));
		} else {
			w.putString(name);
		}
	};

	if (dictionary) {
		w.put(static_cast<std::int16_t>(table.size()));
		for (const std::string& s : table) {
			w.putString(s);
		}
	}

	w.put(static_cast<std::int32_t>(document.elements.size()));
	for (const Element& element : document.elements) {
		putName(element.type);
		w.putString(element.name);
		w.put(element.id);
	}
	for (std::size_t e = 0; e < document.elements.size(); e++) {
		const Element& element = document.elements[e];
		w.put(static_cast<std::int32_t>(element.attributes.size()));
		for (const Attribute& attribute : element.attributes) {
			putName(attribute.name);
			w.put(static_cast<std::uint8_t>(attribute.type));
			if (!writeValue(w, attribute.type, attribute.value)) {
				return failed(std::format("element {} attribute \"{}\": value does not match type {}", e, attribute.name, typeName(attribute.type)));
			}
		}
	}
	return w.take();
}

std::string valueText(const Value& value) {
	const auto refText = [](const ElementRef& ref) -> std::string {
		if (ref.index >= 0) {
			return std::format("#{}", ref.index);
		}
		return ref.index == ElementRef::EXTERNAL ? "external " + ref.externalId : "null";
	};
	return std::visit([&refText](const auto& v) -> std::string {
		using T = std::decay_t<decltype(v)>;
		if constexpr (std::is_same_v<T, std::monostate>) {
			return {};
		} else if constexpr (std::is_same_v<T, ElementRef>) {
			return refText(v);
		} else if constexpr (IS_TEXT_SCALAR<T>) {
			return scalarText(v);
		} else {
			std::string out;
			for (std::size_t i = 0; i < v.size(); i++) {
				out += i ? ", " : "";
				if constexpr (std::is_same_v<T, std::vector<ElementRef>>) {
					out += refText(v[i]);
				} else {
					out += scalarText(static_cast<typename T::value_type>(v[i]));
				}
			}
			return out;
		}
	}, value);
}

std::string writeKeyValues2(const Document& document) {
	return Kv2Writer{document}.run();
}

bool equivalent(const Document& a, const Document& b, std::string* difference) {
	const auto differ = [difference](std::string message) {
		if (difference) {
			*difference = std::move(message);
		}
		return false;
	};
	if (a.format != b.format || a.formatVersion != b.formatVersion) {
		return differ("format differs");
	}
	if (a.elements.size() != b.elements.size()) {
		return differ(std::format("element count {} vs {}", a.elements.size(), b.elements.size()));
	}
	for (std::size_t i = 0; i < a.elements.size(); i++) {
		const Element& x = a.elements[i];
		const Element& y = b.elements[i];
		if (x.type != y.type || x.name != y.name || x.id != y.id) {
			return differ(std::format("element {} header differs", i));
		}
		if (x.attributes.size() != y.attributes.size()) {
			return differ(std::format("element {} attribute count differs", i));
		}
		for (std::size_t j = 0; j < x.attributes.size(); j++) {
			const Attribute& p = x.attributes[j];
			const Attribute& q = y.attributes[j];
			if (p.name != q.name || p.type != q.type || !sameValue(p.value, q.value)) {
				return differ(std::format("element {} attribute \"{}\" differs", i, p.name));
			}
		}
	}
	return true;
}

std::string formatGuid(const Guid& g) {
	// Laid out like a Windows GUID: three little-endian fields, then eight bytes as-is.
	return std::format("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
	                   g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6],
	                   g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}

} // namespace xv::dmx
