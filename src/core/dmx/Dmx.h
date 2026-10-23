//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Datamodel exchange (DMX) documents: the container format of particle (.pcf) files.
//          Binary encodings 1 and 2 (Source 2007/2013: CS:S, TF2, HL2, GMod...) are read and
//          written. Later encodings (3-5, used from L4D onwards) are rejected until there are real
//          files to test against.
//
//===========================================================================//
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

namespace xv::dmx
{

using Guid_t = std::array<std::uint8_t, 16>;
using Bytes_t = std::vector<std::byte>;

enum class AttrType_t : std::uint8_t
{
	ATTR_TYPE_UNKNOWN = 0,
	ATTR_TYPE_ELEMENT,
	ATTR_TYPE_INT,
	ATTR_TYPE_FLOAT,
	ATTR_TYPE_BOOL,
	ATTR_TYPE_STRING,
	ATTR_TYPE_BINARY,
	ATTR_TYPE_OBJECT_ID,
	ATTR_TYPE_COLOR,
	ATTR_TYPE_VECTOR2,
	ATTR_TYPE_VECTOR3,
	ATTR_TYPE_VECTOR4,
	ATTR_TYPE_QANGLE,
	ATTR_TYPE_QUATERNION,
	ATTR_TYPE_MATRIX,
	ATTR_TYPE_ELEMENT_ARRAY,
	ATTR_TYPE_INT_ARRAY,
	ATTR_TYPE_FLOAT_ARRAY,
	ATTR_TYPE_BOOL_ARRAY,
	ATTR_TYPE_STRING_ARRAY,
	ATTR_TYPE_BINARY_ARRAY,
	ATTR_TYPE_OBJECT_ID_ARRAY,
	ATTR_TYPE_COLOR_ARRAY,
	ATTR_TYPE_VECTOR2_ARRAY,
	ATTR_TYPE_VECTOR3_ARRAY,
	ATTR_TYPE_VECTOR4_ARRAY,
	ATTR_TYPE_QANGLE_ARRAY,
	ATTR_TYPE_QUATERNION_ARRAY,
	ATTR_TYPE_MATRIX_ARRAY,
};

constexpr std::uint8_t ATTR_TYPE_ARRAY_OFFSET = 14;

[[nodiscard]] constexpr bool IsArray( AttrType_t nType )
{
	return nType >= AttrType_t::ATTR_TYPE_ELEMENT_ARRAY;
}

// keyvalues2 spelling, e.g. "vector3" or "element_array".
[[nodiscard]] std::string_view TypeName( AttrType_t nType );

struct CElementRef
{
	static constexpr std::int32_t NONE = -1;
	static constexpr std::int32_t EXTERNAL = -2;

	std::int32_t m_nIndex = NONE; // into Document::elements
	std::string m_szExternalId;    // set when index == EXTERNAL

	bool operator==( const CElementRef & ) const = default;
};

struct CColor
{
	std::uint8_t m_nR = 0, m_nG = 0, m_nB = 0, m_nA = 0;

	bool operator==( const CColor & ) const = default;
};

using Vec2_t = std::array<float, 2>;
using Vec3_t = std::array<float, 3>;   // also QAngle
using Vec4_t = std::array<float, 4>;   // also Quaternion
using Matrix_t = std::array<float, 16>;

// One alternative per storage shape; Attribute::type tells QAngle from Vector3 and so on.
using Value_t = std::variant<
	std::monostate,
	CElementRef, std::int32_t, float, bool, std::string, Bytes_t, Guid_t, CColor, Vec2_t, Vec3_t, Vec4_t, Matrix_t,
	std::vector<CElementRef>, std::vector<std::int32_t>, std::vector<float>, std::vector<bool>,
	std::vector<std::string>, std::vector<Bytes_t>, std::vector<Guid_t>, std::vector<CColor>,
	std::vector<Vec2_t>, std::vector<Vec3_t>, std::vector<Vec4_t>, std::vector<Matrix_t>>;

struct Attribute_t
{
	std::string m_szName;
	AttrType_t m_nType = AttrType_t::ATTR_TYPE_UNKNOWN;
	Value_t m_Value;
};

struct CElement
{
	std::string m_szType;
	std::string m_szName;
	Guid_t m_Id{};
	std::vector<Attribute_t> m_Attributes;

	[[nodiscard]] const Attribute_t *Find( std::string_view szAttributeName ) const;
	[[nodiscard]] Attribute_t *Find( std::string_view szAttributeName );
};

struct Document_t
{
	std::string m_szEncoding = "binary";
	int m_nEncodingVersion = 2;
	std::string m_szFormat = "dmx";
	int m_nFormatVersion = 1;

	std::vector<CElement> m_Elements; // elements[0] is the root

	// Dictionary order of the file this was read from. The writer reuses it, so an
	// unchanged document is written back byte for byte.
	std::vector<std::string> m_StringTable;
};

struct ReadResult_t
{
	std::optional<Document_t> m_Document;
	std::string m_szError;
	std::size_t m_nBytesRead = 0;
};

[[nodiscard]] bool IsSupportedBinaryVersion( int nVersion );

// Input is untrusted (files come from game servers); every count and index is bounds-checked.
[[nodiscard]] ReadResult_t Read( std::span<const std::byte> data );

// Writes binary encoding document.encodingVersion.
[[nodiscard]] std::optional<Bytes_t> WriteBinary( const Document_t &document, std::string *pError = nullptr );

// Human-readable value: "1.5", "255 128 0 255", "#3" for element references; arrays joined with ", ".
[[nodiscard]] std::string ValueText( const Value_t &value );

// keyvalues2 text, for viewing and diffing. Elements referenced once are written inline.
[[nodiscard]] std::string WriteKeyValues2( const Document_t &document );

// Structural equality; floats are compared bit for bit.
[[nodiscard]] bool Equivalent( const Document_t &a, const Document_t &b, std::string *pDifference = nullptr );
[[nodiscard]] bool SameValue( const Value_t &a, const Value_t &b );
[[nodiscard]] bool SameElement( const CElement &a, const CElement &b );

[[nodiscard]] std::string FormatGuid( const Guid_t &guid );

} // namespace xv::dmx
