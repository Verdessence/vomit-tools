// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Datamodel exchange (DMX) documents: the container format of particle (.pcf) files.
//
// Binary encodings 1 and 2 (Source 2007/2013: CS:S, TF2, HL2, GMod...) are read and written.
// Later encodings (3-5, used from L4D onwards) are rejected until there are real files to test against.
namespace xv::dmx {

using Guid = std::array<std::uint8_t, 16>;
using Bytes = std::vector<std::byte>;

enum class AttrType : std::uint8_t {
	Unknown = 0,
	Element,
	Int,
	Float,
	Bool,
	String,
	Binary,
	ObjectId,
	Color,
	Vector2,
	Vector3,
	Vector4,
	QAngle,
	Quaternion,
	Matrix,
	ElementArray,
	IntArray,
	FloatArray,
	BoolArray,
	StringArray,
	BinaryArray,
	ObjectIdArray,
	ColorArray,
	Vector2Array,
	Vector3Array,
	Vector4Array,
	QAngleArray,
	QuaternionArray,
	MatrixArray,
};

constexpr std::uint8_t ATTR_TYPE_ARRAY_OFFSET = 14;

[[nodiscard]] constexpr bool isArray(AttrType type) {
	return type >= AttrType::ElementArray;
}

// keyvalues2 spelling, e.g. "vector3" or "element_array".
[[nodiscard]] std::string_view typeName(AttrType type);

struct ElementRef {
	static constexpr std::int32_t NONE = -1;
	static constexpr std::int32_t EXTERNAL = -2;

	std::int32_t index = NONE; // into Document::elements
	std::string externalId;    // set when index == EXTERNAL

	bool operator==(const ElementRef&) const = default;
};

struct Color {
	std::uint8_t r = 0, g = 0, b = 0, a = 0;

	bool operator==(const Color&) const = default;
};

using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;   // also QAngle
using Vec4 = std::array<float, 4>;   // also Quaternion
using Matrix = std::array<float, 16>;

// One alternative per storage shape; Attribute::type tells QAngle from Vector3 and so on.
using Value = std::variant<
		std::monostate,
		ElementRef, std::int32_t, float, bool, std::string, Bytes, Guid, Color, Vec2, Vec3, Vec4, Matrix,
		std::vector<ElementRef>, std::vector<std::int32_t>, std::vector<float>, std::vector<bool>,
		std::vector<std::string>, std::vector<Bytes>, std::vector<Guid>, std::vector<Color>,
		std::vector<Vec2>, std::vector<Vec3>, std::vector<Vec4>, std::vector<Matrix>>;

struct Attribute {
	std::string name;
	AttrType type = AttrType::Unknown;
	Value value;
};

struct Element {
	std::string type;
	std::string name;
	Guid id{};
	std::vector<Attribute> attributes;

	[[nodiscard]] const Attribute* find(std::string_view attributeName) const;
	[[nodiscard]] Attribute* find(std::string_view attributeName);
};

struct Document {
	std::string encoding = "binary";
	int encodingVersion = 2;
	std::string format = "dmx";
	int formatVersion = 1;

	std::vector<Element> elements; // elements[0] is the root

	// Dictionary order of the file this was read from. The writer reuses it, so an
	// unchanged document is written back byte for byte.
	std::vector<std::string> stringTable;
};

struct ReadResult {
	std::optional<Document> document;
	std::string error;
	std::size_t bytesRead = 0;
};

[[nodiscard]] bool isSupportedBinaryVersion(int version);

// Input is untrusted (files come from game servers); every count and index is bounds-checked.
[[nodiscard]] ReadResult read(std::span<const std::byte> data);

// Writes binary encoding document.encodingVersion.
[[nodiscard]] std::optional<Bytes> writeBinary(const Document& document, std::string* error = nullptr);

// Human-readable value: "1.5", "255 128 0 255", "#3" for element references; arrays joined with ", ".
[[nodiscard]] std::string valueText(const Value& value);

// keyvalues2 text, for viewing and diffing. Elements referenced once are written inline.
[[nodiscard]] std::string writeKeyValues2(const Document& document);

// Structural equality; floats are compared bit for bit.
[[nodiscard]] bool equivalent(const Document& a, const Document& b, std::string* difference = nullptr);

[[nodiscard]] std::string formatGuid(const Guid& guid);

} // namespace xv::dmx
