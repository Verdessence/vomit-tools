//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Dmx.h.
//
//===========================================================================//
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

namespace xv::dmx
{

static_assert( std::endian::native == std::endian::little, "DMX binary I/O assumes a little-endian host" );

namespace
{

constexpr std::array<std::string_view, 29> TYPE_NAMES{
	"unknown",
	"element",
	"int",
	"float",
	"bool",
	"string",
	"binary",
	"elementid",
	"color",
	"vector2",
	"vector3",
	"vector4",
	"qangle",
	"quaternion",
	"matrix",
	"element_array",
	"int_array",
	"float_array",
	"bool_array",
	"string_array",
	"binary_array",
	"elementid_array",
	"color_array",
	"vector2_array",
	"vector3_array",
	"vector4_array",
	"qangle_array",
	"quaternion_array",
	"matrix_array",
};

template <typename T>
struct IsVector : std::false_type
{};
template <typename T>
struct IsVector<std::vector<T>> : std::true_type
{};

// Scalars as stored in a Value (Bytes is a vector, but a binary blob is a single value).
template <typename T>
constexpr bool IS_TEXT_SCALAR = !IsVector<T>::value || std::is_same_v<T, Bytes_t>;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
AttrType_t BaseType( AttrType_t nType )
{
	return IsArray( nType ) ? static_cast<AttrType_t>( static_cast<std::uint8_t>( nType ) - ATTR_TYPE_ARRAY_OFFSET ) : nType;
}

// ---------------------------------------------------------------------------------------------
// Header: <!-- dmx encoding binary 2 format pcf 1 -->

struct Header_t
{
	std::string m_szEncoding;
	int m_nEncodingVersion = 0;
	std::string m_szFormat;
	int m_nFormatVersion = 0;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<int> ParseInt( std::string_view szText )
{
	int nValue = 0;
	const auto [ end, ec ] = std::from_chars( szText.data(), szText.data() + szText.size(), nValue );
	if ( ec != std::errc{} || end != szText.data() + szText.size() )
	{
		return std::nullopt;
	}
	return nValue;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<Header_t> ParseHeader( std::string_view szText )
{
	std::vector<std::string_view> words;
	std::size_t nPos = 0;
	while ( nPos < szText.size() )
	{
		const auto nStart = szText.find_first_not_of( " \t\r\n", nPos );
		if ( nStart == std::string_view::npos )
		{
			break;
		}
		const auto nEnd = std::min( szText.find_first_of( " \t\r\n", nStart ), szText.size() );
		words.push_back( szText.substr( nStart, nEnd - nStart ) );
		nPos = nEnd;
	}
	if ( words.size() != 9 || words[ 0 ] != "<!--" || words[ 1 ] != "dmx" || words[ 2 ] != "encoding" || words[ 5 ] != "format" || words[ 8 ] != "-->" )
	{
		return std::nullopt;
	}
	const auto encodingVersion = ParseInt( words[ 4 ] );
	const auto formatVersion = ParseInt( words[ 7 ] );
	if ( !encodingVersion || !formatVersion )
	{
		return std::nullopt;
	}
	return Header_t{ std::string{ words[ 3 ] }, *encodingVersion, std::string{ words[ 6 ] }, *formatVersion };
}

// ---------------------------------------------------------------------------------------------
// Binary reading

class CReader
{
public:
	explicit CReader( std::span<const std::byte> data )
		: m_Data( data )
	{}

	template <typename T>
	bool Read( T &out )
	{
		static_assert( std::is_trivially_copyable_v<T> );
		if ( GetRemaining() < sizeof( T ) )
		{
			return Fail( "unexpected end of file" );
		}
		std::memcpy( &out, m_Data.data() + m_nPos, sizeof( T ) );
		m_nPos += sizeof( T );
		return true;
	}

	bool ReadString( std::string &szOut )
	{
		const auto rest = m_Data.subspan( m_nPos );
		const auto nul = std::ranges::find( rest, std::byte{ 0 } );
		if ( nul == rest.end() )
		{
			return Fail( "unterminated string" );
		}
		const auto nLength = static_cast<std::size_t>( nul - rest.begin() );
		szOut.assign( reinterpret_cast<const char *>( rest.data() ), nLength );
		m_nPos += nLength + 1;
		return true;
	}

	bool ReadBytes( std::size_t nCount, Bytes_t &out )
	{
		if ( GetRemaining() < nCount )
		{
			return Fail( "unexpected end of file" );
		}
		out.assign( m_Data.begin() + static_cast<std::ptrdiff_t>( m_nPos ), m_Data.begin() + static_cast<std::ptrdiff_t>( m_nPos + nCount ) );
		m_nPos += nCount;
		return true;
	}

	// A count is rejected if that many items of at least minItemSize bytes cannot fit in the rest of the file.
	bool ReadCount( std::int32_t &nCount, std::size_t nMinItemSize )
	{
		if ( !Read( nCount ) )
		{
			return false;
		}
		if ( nCount < 0 || static_cast<std::size_t>( nCount ) > GetRemaining() / nMinItemSize )
		{
			return Fail( std::format( "impossible count {}", nCount ) );
		}
		return true;
	}

	bool Fail( std::string_view szMessage )
	{
		if ( m_szError.empty() )
		{
			m_szError = std::format( "{} at offset {}", szMessage, m_nPos );
		}
		return false;
	}

	[[nodiscard]] std::size_t GetRemaining() const { return m_Data.size() - m_nPos; }
	[[nodiscard]] std::size_t GetPosition() const { return m_nPos; }
	[[nodiscard]] const std::string &GetError() const { return m_szError; }

private:
	std::span<const std::byte> m_Data;
	std::size_t m_nPos = 0;
	std::string m_szError;
};

struct ReadContext_t
{
	CReader &m_Reader;
	std::int32_t m_nElementCount;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <typename T>
	requires std::is_trivially_copyable_v<T>
bool ReadOne( ReadContext_t &c, T &out )
{
	return c.m_Reader.Read( out );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool ReadOne( ReadContext_t &c, bool &bOut )
{
	std::uint8_t nByte = 0;
	if ( !c.m_Reader.Read( nByte ) )
	{
		return false;
	}
	bOut = nByte != 0;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool ReadOne( ReadContext_t &c, std::string &szOut )
{
	return c.m_Reader.ReadString( szOut );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool ReadOne( ReadContext_t &c, Bytes_t &out )
{
	std::int32_t nSize = 0;
	return c.m_Reader.ReadCount( nSize, 1 ) && c.m_Reader.ReadBytes( static_cast<std::size_t>( nSize ), out );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool ReadOne( ReadContext_t &c, CElementRef &out )
{
	if ( !c.m_Reader.Read( out.m_nIndex ) )
	{
		return false;
	}
	if ( out.m_nIndex == CElementRef::EXTERNAL )
	{
		return c.m_Reader.ReadString( out.m_szExternalId );
	}
	if ( out.m_nIndex < CElementRef::NONE || out.m_nIndex >= c.m_nElementCount )
	{
		return c.m_Reader.Fail( std::format( "element reference {} out of range", out.m_nIndex ) );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <typename T>
bool ReadAs( ReadContext_t &c, Value_t &out, bool bArray, std::size_t nMinItemSize )
{
	if ( !bArray )
	{
		T value{};
		if ( !ReadOne( c, value ) )
		{
			return false;
		}
		out = std::move( value );
		return true;
	}
	std::int32_t nCount = 0;
	if ( !c.m_Reader.ReadCount( nCount, nMinItemSize ) )
	{
		return false;
	}
	std::vector<T> items;
	items.reserve( static_cast<std::size_t>( nCount ) );
	for ( std::int32_t i = 0; i < nCount; i++ )
	{
		T value{};
		if ( !ReadOne( c, value ) )
		{
			return false;
		}
		items.push_back( std::move( value ) );
	}
	out = std::move( items );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool ReadValue( ReadContext_t &c, AttrType_t nType, Value_t &out )
{
	const bool bArray = IsArray( nType );
	switch ( BaseType( nType ) )
	{
		case AttrType_t::ATTR_TYPE_ELEMENT: return ReadAs<CElementRef>( c, out, bArray, 4 );
		case AttrType_t::ATTR_TYPE_INT: return ReadAs<std::int32_t>( c, out, bArray, 4 );
		case AttrType_t::ATTR_TYPE_FLOAT: return ReadAs<float>( c, out, bArray, 4 );
		case AttrType_t::ATTR_TYPE_BOOL: return ReadAs<bool>( c, out, bArray, 1 );
		case AttrType_t::ATTR_TYPE_STRING: return ReadAs<std::string>( c, out, bArray, 1 );
		case AttrType_t::ATTR_TYPE_BINARY: return ReadAs<Bytes_t>( c, out, bArray, 4 );
		case AttrType_t::ATTR_TYPE_OBJECT_ID: return ReadAs<Guid_t>( c, out, bArray, sizeof( Guid_t ) );
		case AttrType_t::ATTR_TYPE_COLOR: return ReadAs<CColor>( c, out, bArray, sizeof( CColor ) );
		case AttrType_t::ATTR_TYPE_VECTOR2: return ReadAs<Vec2_t>( c, out, bArray, sizeof( Vec2_t ) );
		case AttrType_t::ATTR_TYPE_VECTOR3:
		case AttrType_t::ATTR_TYPE_QANGLE: return ReadAs<Vec3_t>( c, out, bArray, sizeof( Vec3_t ) );
		case AttrType_t::ATTR_TYPE_VECTOR4:
		case AttrType_t::ATTR_TYPE_QUATERNION: return ReadAs<Vec4_t>( c, out, bArray, sizeof( Vec4_t ) );
		case AttrType_t::ATTR_TYPE_MATRIX: return ReadAs<Matrix_t>( c, out, bArray, sizeof( Matrix_t ) );
		default: return c.m_Reader.Fail( "unknown attribute type" );
	}
}

// ---------------------------------------------------------------------------------------------
// Binary writing

class CWriter
{
public:
	template <typename T>
	void Put( const T &value )
	{
		static_assert( std::is_trivially_copyable_v<T> );
		const auto *pBytes = reinterpret_cast<const std::byte *>( &value );
		m_Out.insert( m_Out.end(), pBytes, pBytes + sizeof( T ) );
	}

	void PutString( std::string_view szText )
	{
		const auto *pBytes = reinterpret_cast<const std::byte *>( szText.data() );
		m_Out.insert( m_Out.end(), pBytes, pBytes + szText.size() );
		m_Out.push_back( std::byte{ 0 } );
	}

	void PutBytes( const Bytes_t &bytes )
	{
		m_Out.insert( m_Out.end(), bytes.begin(), bytes.end() );
	}

	Bytes_t Take() { return std::move( m_Out ); }

private:
	Bytes_t m_Out;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <typename T>
	requires std::is_trivially_copyable_v<T>
void WriteOne( CWriter &w, const T &value )
{
	w.Put( value );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void WriteOne( CWriter &w, bool bValue )
{
	w.Put( static_cast<std::uint8_t>( bValue ? 1 : 0 ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void WriteOne( CWriter &w, const std::string &szValue )
{
	w.PutString( szValue );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void WriteOne( CWriter &w, const Bytes_t &value )
{
	w.Put( static_cast<std::int32_t>( value.size() ) );
	w.PutBytes( value );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void WriteOne( CWriter &w, const CElementRef &value )
{
	w.Put( value.m_nIndex );
	if ( value.m_nIndex == CElementRef::EXTERNAL )
	{
		w.PutString( value.m_szExternalId );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <typename T>
bool WriteAs( CWriter &w, const Value_t &value, bool bArray )
{
	if ( !bArray )
	{
		const T *pScalar = std::get_if<T>( &value );
		if ( pScalar )
		{
			WriteOne( w, *pScalar );
		}
		return pScalar != nullptr;
	}
	const auto *pItems = std::get_if<std::vector<T>>( &value );
	if ( !pItems )
	{
		return false;
	}
	w.Put( static_cast<std::int32_t>( pItems->size() ) );
	for ( const auto &item : *pItems )
	{
		if constexpr ( std::is_same_v<T, bool> )
		{
			WriteOne( w, static_cast<bool>( item ) );
		}
		else
		{
			WriteOne( w, item );
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool WriteValue( CWriter &w, AttrType_t nType, const Value_t &value )
{
	const bool bArray = IsArray( nType );
	switch ( BaseType( nType ) )
	{
		case AttrType_t::ATTR_TYPE_ELEMENT: return WriteAs<CElementRef>( w, value, bArray );
		case AttrType_t::ATTR_TYPE_INT: return WriteAs<std::int32_t>( w, value, bArray );
		case AttrType_t::ATTR_TYPE_FLOAT: return WriteAs<float>( w, value, bArray );
		case AttrType_t::ATTR_TYPE_BOOL: return WriteAs<bool>( w, value, bArray );
		case AttrType_t::ATTR_TYPE_STRING: return WriteAs<std::string>( w, value, bArray );
		case AttrType_t::ATTR_TYPE_BINARY: return WriteAs<Bytes_t>( w, value, bArray );
		case AttrType_t::ATTR_TYPE_OBJECT_ID: return WriteAs<Guid_t>( w, value, bArray );
		case AttrType_t::ATTR_TYPE_COLOR: return WriteAs<CColor>( w, value, bArray );
		case AttrType_t::ATTR_TYPE_VECTOR2: return WriteAs<Vec2_t>( w, value, bArray );
		case AttrType_t::ATTR_TYPE_VECTOR3:
		case AttrType_t::ATTR_TYPE_QANGLE: return WriteAs<Vec3_t>( w, value, bArray );
		case AttrType_t::ATTR_TYPE_VECTOR4:
		case AttrType_t::ATTR_TYPE_QUATERNION: return WriteAs<Vec4_t>( w, value, bArray );
		case AttrType_t::ATTR_TYPE_MATRIX: return WriteAs<Matrix_t>( w, value, bArray );
		default: return false;
	}
}

// ---------------------------------------------------------------------------------------------
// Equality

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool Same( float a, float b )
{
	return std::bit_cast<std::uint32_t>( a ) == std::bit_cast<std::uint32_t>( b );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <std::size_t N>
bool Same( const std::array<float, N> &a, const std::array<float, N> &b )
{
	return std::ranges::equal( a, b, []( float x, float y ) { return Same( x, y ); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <typename T>
bool Same( const T &a, const T &b )
{
	return a == b;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
template <typename T>
bool Same( const std::vector<T> &a, const std::vector<T> &b )
{
	if ( a.size() != b.size() )
	{
		return false;
	}
	for ( std::size_t i = 0; i < a.size(); i++ )
	{
		if ( !Same( static_cast<const T &>( a[ i ] ), static_cast<const T &>( b[ i ] ) ) )
		{
			return false;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool ValuesMatch( const Value_t &a, const Value_t &b )
{
	if ( a.index() != b.index() )
	{
		return false;
	}
	return std::visit( [ &b ]( const auto &x ) {
		using T_t = std::decay_t<decltype( x )>;
		return Same( x, std::get<T_t>( b ) );
	},
		a );
}

// ---------------------------------------------------------------------------------------------
// keyvalues2 text

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string QuoteKv2( std::string_view szText )
{
	std::string szOut = "\"";
	for ( const char c : szText )
	{
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
template <std::size_t N>
std::string JoinFloats( const std::array<float, N> &values )
{
	std::string szOut;
	for ( std::size_t i = 0; i < N; i++ )
	{
		szOut += std::format( "{}{}", i ? " " : "", values[ i ] );
	}
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string ScalarText( const auto &value )
{
	using T_t = std::decay_t<decltype( value )>;
	if constexpr ( std::is_same_v<T_t, bool> )
	{
		return value ? "1" : "0";
	}
	else if constexpr ( std::is_same_v<T_t, std::int32_t> || std::is_same_v<T_t, float> )
	{
		return std::format( "{}", value );
	}
	else if constexpr ( std::is_same_v<T_t, std::string> )
	{
		return value;
	}
	else if constexpr ( std::is_same_v<T_t, Bytes_t> )
	{
		std::string szOut;
		for ( const std::byte b : value )
		{
			szOut += std::format( "{:02X}", static_cast<unsigned>( b ) );
		}
		return szOut;
	}
	else if constexpr ( std::is_same_v<T_t, Guid_t> )
	{
		return FormatGuid( value );
	}
	else if constexpr ( std::is_same_v<T_t, CColor> )
	{
		return std::format( "{} {} {} {}", value.m_nR, value.m_nG, value.m_nB, value.m_nA );
	}
	else
	{
		return JoinFloats( value );
	}
}

class CKv2Writer
{
public:
	explicit CKv2Writer( const Document_t &document )
		: m_Doc( document ),
		  m_References( document.m_Elements.size(), 0 )
	{
		for ( const CElement &element : m_Doc.m_Elements )
		{
			for ( const Attribute_t &attribute : element.m_Attributes )
			{
				if ( const auto *pRef = std::get_if<CElementRef>( &attribute.m_Value ) )
				{
					CountReference( *pRef );
				}
				else if ( const auto *pRefs = std::get_if<std::vector<CElementRef>>( &attribute.m_Value ) )
				{
					std::ranges::for_each( *pRefs, [ this ]( const CElementRef &r ) { CountReference( r ); } );
				}
			}
		}
	}

	std::string Run()
	{
		m_szOut = std::format( "<!-- dmx encoding keyvalues2 1 format {} {} -->\n", m_Doc.m_szFormat, m_Doc.m_nFormatVersion );
		for ( std::size_t i = 0; i < m_Doc.m_Elements.size(); i++ )
		{
			if ( i == 0 || !IsInline( static_cast<std::int32_t>( i ) ) )
			{
				WriteElement( static_cast<std::int32_t>( i ), 0 );
				m_szOut += "\n\n";
			}
		}
		return std::move( m_szOut );
	}

private:
	void CountReference( const CElementRef &ref )
	{
		if ( ref.m_nIndex >= 0 )
		{
			m_References[ static_cast<std::size_t>( ref.m_nIndex ) ]++;
		}
	}

	bool IsInline( std::int32_t nIndex ) const
	{
		return nIndex > 0 && m_References[ static_cast<std::size_t>( nIndex ) ] == 1;
	}

	static std::string Indent( int nDepth )
	{
		return std::string( static_cast<std::size_t>( nDepth ), '\t' );
	}

	void WriteElement( std::int32_t nIndex, int nDepth )
	{
		const CElement &element = m_Doc.m_Elements[ static_cast<std::size_t>( nIndex ) ];
		m_szOut += QuoteKv2( element.m_szType ) + "\n" + Indent( nDepth ) + "{\n";
		m_szOut += Indent( nDepth + 1 ) + "\"id\" \"elementid\" \"" + FormatGuid( element.m_Id ) + "\"\n";
		m_szOut += Indent( nDepth + 1 ) + "\"name\" \"string\" " + QuoteKv2( element.m_szName ) + "\n";
		for ( const Attribute_t &attribute : element.m_Attributes )
		{
			WriteAttribute( attribute, nDepth + 1 );
		}
		m_szOut += Indent( nDepth ) + "}";
	}

	void WriteReference( const CElementRef &ref, int nDepth )
	{
		if ( ref.m_nIndex >= 0 && IsInline( ref.m_nIndex ) )
		{
			WriteElement( ref.m_nIndex, nDepth );
		}
		else if ( ref.m_nIndex >= 0 )
		{
			m_szOut += "\"element\" \"" + FormatGuid( m_Doc.m_Elements[ static_cast<std::size_t>( ref.m_nIndex ) ].m_Id ) + "\"";
		}
		else
		{
			m_szOut += "\"element\" " + QuoteKv2( ref.m_szExternalId );
		}
	}

	void WriteAttribute( const Attribute_t &attribute, int nDepth )
	{
		const std::string szPad = Indent( nDepth );
		m_szOut += szPad + QuoteKv2( attribute.m_szName ) + " ";
		if ( const auto *pRef = std::get_if<CElementRef>( &attribute.m_Value ) )
		{
			WriteReference( *pRef, nDepth );
			m_szOut += "\n";
			return;
		}
		m_szOut += QuoteKv2( TypeName( attribute.m_nType ) );
		if ( !IsArray( attribute.m_nType ) )
		{
			std::visit( [ this ]( const auto &value ) {
				using T_t = std::decay_t<decltype( value )>;
				if constexpr ( IS_TEXT_SCALAR<T_t> && !std::is_same_v<T_t, std::monostate> && !std::is_same_v<T_t, CElementRef> )
				{
					m_szOut += " " + QuoteKv2( ScalarText( value ) );
				}
			},
				attribute.m_Value );
			m_szOut += "\n";
			return;
		}
		m_szOut += "\n" + szPad + "[\n";
		bool bFirst = true;
		const auto separator = [ & ] {
			m_szOut += bFirst ? "" : ",\n";
			bFirst = false;
		};
		std::visit( [ & ]( const auto &value ) {
			using T_t = std::decay_t<decltype( value )>;
			if constexpr ( std::is_same_v<T_t, std::vector<CElementRef>> )
			{
				for ( const CElementRef &ref : value )
				{
					separator();
					m_szOut += szPad + "\t";
					WriteReference( ref, nDepth + 1 );
				}
			}
			else if constexpr ( std::is_same_v<T_t, std::vector<bool>> )
			{
				for ( const bool bItem : value )
				{
					separator();
					m_szOut += szPad + "\t" + QuoteKv2( ScalarText( bItem ) );
				}
			}
			else if constexpr ( !IS_TEXT_SCALAR<T_t> )
			{
				for ( const auto &item : value )
				{
					separator();
					m_szOut += szPad + "\t" + QuoteKv2( ScalarText( item ) );
				}
			}
		},
			attribute.m_Value );
		m_szOut += "\n" + szPad + "]\n";
	}

	const Document_t &m_Doc;
	std::vector<int> m_References;
	std::string m_szOut;
};

} // namespace

//-----------------------------------------------------------------------------
// Purpose: keyvalues2 spelling, e.g. "vector3" or "element_array".
//-----------------------------------------------------------------------------
std::string_view TypeName( AttrType_t nType )
{
	const auto nIndex = static_cast<std::size_t>( nType );
	return nIndex < TYPE_NAMES.size() ? TYPE_NAMES[ nIndex ] : TYPE_NAMES[ 0 ];
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const Attribute_t *CElement::Find( std::string_view szAttributeName ) const
{
	const auto it = std::ranges::find( m_Attributes, szAttributeName, &Attribute_t::m_szName );
	return it == m_Attributes.end() ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Attribute_t *CElement::Find( std::string_view szAttributeName )
{
	return const_cast<Attribute_t *>( std::as_const( *this ).Find( szAttributeName ) );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsSupportedBinaryVersion( int nVersion )
{
	return nVersion == 1 || nVersion == 2;
}

//-----------------------------------------------------------------------------
// Purpose: Input is untrusted (files come from game servers); every count and index is bounds-
//          checked.
//-----------------------------------------------------------------------------
ReadResult_t Read( std::span<const std::byte> data )
{
	ReadResult_t result;

	const std::string_view szHead{ reinterpret_cast<const char *>( data.data() ), std::min<std::size_t>( data.size(), 512 ) };
	const auto nClose = szHead.find( "-->" );
	if ( nClose == std::string_view::npos )
	{
		result.m_szError = "not a DMX file (no header)";
		return result;
	}
	const auto header = ParseHeader( szHead.substr( 0, nClose + 3 ) );
	if ( !header )
	{
		result.m_szError = "unrecognised DMX header";
		return result;
	}
	if ( header->m_szEncoding != "binary" )
	{
		result.m_szError = std::format( "{} encoding is not supported yet", header->m_szEncoding );
		return result;
	}
	if ( !IsSupportedBinaryVersion( header->m_nEncodingVersion ) )
	{
		result.m_szError = std::format( "binary encoding {} is not supported yet", header->m_nEncodingVersion );
		return result;
	}

	Document_t doc;
	doc.m_szEncoding = header->m_szEncoding;
	doc.m_nEncodingVersion = header->m_nEncodingVersion;
	doc.m_szFormat = header->m_szFormat;
	doc.m_nFormatVersion = header->m_nFormatVersion;
	const bool bDictionary = doc.m_nEncodingVersion >= 2;

	CReader reader{ data };
	const auto finish = [ & ]() -> ReadResult_t {
		result.m_nBytesRead = reader.GetPosition();
		if ( !reader.GetError().empty() )
		{
			result.m_szError = reader.GetError();
		}
		else
		{
			result.m_Document = std::move( doc );
		}
		return std::move( result );
	};

	std::string szHeaderText;
	if ( !reader.ReadString( szHeaderText ) )
	{
		return finish();
	}

	if ( bDictionary )
	{
		std::int16_t nCount = 0;
		if ( !reader.Read( nCount ) )
		{
			return finish();
		}
		if ( nCount < 0 )
		{
			reader.Fail( "negative string table size" );
			return finish();
		}
		doc.m_StringTable.resize( static_cast<std::size_t>( nCount ) );
		for ( std::string &szEntry : doc.m_StringTable )
		{
			if ( !reader.ReadString( szEntry ) )
			{
				return finish();
			}
		}
	}

	const auto readName = [ & ]( std::string &szOut ) {
		if ( !bDictionary )
		{
			return reader.ReadString( szOut );
		}
		std::int16_t nIndex = 0;
		if ( !reader.Read( nIndex ) )
		{
			return false;
		}
		if ( nIndex < 0 || static_cast<std::size_t>( nIndex ) >= doc.m_StringTable.size() )
		{
			return reader.Fail( std::format( "string index {} out of range", nIndex ) );
		}
		szOut = doc.m_StringTable[ static_cast<std::size_t>( nIndex ) ];
		return true;
	};

	std::int32_t nElementCount = 0;
	if ( !reader.ReadCount( nElementCount, ( bDictionary ? 2 : 1 ) + 1 + sizeof( Guid_t ) ) )
	{
		return finish();
	}
	doc.m_Elements.resize( static_cast<std::size_t>( nElementCount ) );
	for ( CElement &element : doc.m_Elements )
	{
		if ( !readName( element.m_szType ) || !reader.ReadString( element.m_szName ) || !reader.Read( element.m_Id ) )
		{
			return finish();
		}
	}

	ReadContext_t context{ reader, nElementCount };
	for ( CElement &element : doc.m_Elements )
	{
		std::int32_t nAttributeCount = 0;
		if ( !reader.ReadCount( nAttributeCount, ( bDictionary ? 2 : 1 ) + 2 ) )
		{
			return finish();
		}
		element.m_Attributes.resize( static_cast<std::size_t>( nAttributeCount ) );
		for ( Attribute_t &attribute : element.m_Attributes )
		{
			std::uint8_t nType = 0;
			if ( !readName( attribute.m_szName ) || !reader.Read( nType ) )
			{
				return finish();
			}
			if ( nType == 0 || nType >= TYPE_NAMES.size() )
			{
				reader.Fail( std::format( "unknown attribute type {}", nType ) );
				return finish();
			}
			attribute.m_nType = static_cast<AttrType_t>( nType );
			if ( !ReadValue( context, attribute.m_nType, attribute.m_Value ) )
			{
				return finish();
			}
		}
	}
	return finish();
}

//-----------------------------------------------------------------------------
// Purpose: Writes binary encoding document.encodingVersion.
//-----------------------------------------------------------------------------
std::optional<Bytes_t> WriteBinary( const Document_t &document, std::string *pError )
{
	const auto failed = [ pError ]( std::string szMessage ) -> std::optional<Bytes_t> {
		if ( pError )
		{
			*pError = std::move( szMessage );
		}
		return std::nullopt;
	};
	if ( document.m_szEncoding != "binary" || !IsSupportedBinaryVersion( document.m_nEncodingVersion ) )
	{
		return failed( std::format( "cannot write {} encoding {}", document.m_szEncoding, document.m_nEncodingVersion ) );
	}
	const bool bDictionary = document.m_nEncodingVersion >= 2;

	// Keep the dictionary order the document was read with; new strings go at the end.
	std::vector<std::string> table;
	std::unordered_map<std::string, std::int16_t> indices;
	if ( bDictionary )
	{
		std::unordered_set<std::string> used;
		std::vector<std::string> firstUse;
		const auto use = [ & ]( const std::string &s ) {
			if ( used.insert( s ).second )
			{
				firstUse.push_back( s );
			}
		};
		// Valve's writer also lists "name": the element name is an attribute, even though
		// encoding 2 stores it inline in the element header.
		for ( const CElement &element : document.m_Elements )
		{
			use( element.m_szType );
			for ( const Attribute_t &attribute : element.m_Attributes )
			{
				use( attribute.m_szName );
			}
			use( "name" );
		}
		for ( const std::string &s : document.m_StringTable )
		{
			if ( used.contains( s ) && !indices.contains( s ) )
			{
				indices.emplace( s, 0 );
				table.push_back( s );
			}
		}
		for ( const std::string &s : firstUse )
		{
			if ( !indices.contains( s ) )
			{
				indices.emplace( s, 0 );
				table.push_back( s );
			}
		}
		if ( table.size() > static_cast<std::size_t>( INT16_MAX ) )
		{
			return failed( "too many distinct names for binary encoding 2" );
		}
		for ( std::size_t i = 0; i < table.size(); i++ )
		{
			indices[ table[ i ] ] = static_cast<std::int16_t>( i );
		}
	}

	CWriter w;
	w.PutString( std::format( "<!-- dmx encoding binary {} format {} {} -->\n", document.m_nEncodingVersion, document.m_szFormat, document.m_nFormatVersion ) );
	const auto putName = [ & ]( const std::string &szName ) {
		if ( bDictionary )
		{
			w.Put( indices.at( szName ) );
		}
		else
		{
			w.PutString( szName );
		}
	};

	if ( bDictionary )
	{
		w.Put( static_cast<std::int16_t>( table.size() ) );
		for ( const std::string &s : table )
		{
			w.PutString( s );
		}
	}

	w.Put( static_cast<std::int32_t>( document.m_Elements.size() ) );
	for ( const CElement &element : document.m_Elements )
	{
		putName( element.m_szType );
		w.PutString( element.m_szName );
		w.Put( element.m_Id );
	}
	for ( std::size_t e = 0; e < document.m_Elements.size(); e++ )
	{
		const CElement &element = document.m_Elements[ e ];
		w.Put( static_cast<std::int32_t>( element.m_Attributes.size() ) );
		for ( const Attribute_t &attribute : element.m_Attributes )
		{
			putName( attribute.m_szName );
			w.Put( static_cast<std::uint8_t>( attribute.m_nType ) );
			if ( !WriteValue( w, attribute.m_nType, attribute.m_Value ) )
			{
				return failed( std::format( "element {} attribute \"{}\": value does not match type {}", e, attribute.m_szName, TypeName( attribute.m_nType ) ) );
			}
		}
	}
	return w.Take();
}

//-----------------------------------------------------------------------------
// Purpose: Human-readable value: "1.5", "255 128 0 255", "#3" for element references; arrays
//          joined with ", ".
//-----------------------------------------------------------------------------
std::string ValueText( const Value_t &value )
{
	const auto refText = []( const CElementRef &ref ) -> std::string {
		if ( ref.m_nIndex >= 0 )
		{
			return std::format( "#{}", ref.m_nIndex );
		}
		return ref.m_nIndex == CElementRef::EXTERNAL ? "external " + ref.m_szExternalId : "null";
	};
	return std::visit( [ &refText ]( const auto &v ) -> std::string {
		using T_t = std::decay_t<decltype( v )>;
		if constexpr ( std::is_same_v<T_t, std::monostate> )
		{
			return {};
		}
		else if constexpr ( std::is_same_v<T_t, CElementRef> )
		{
			return refText( v );
		}
		else if constexpr ( IS_TEXT_SCALAR<T_t> )
		{
			return ScalarText( v );
		}
		else
		{
			std::string szOut;
			for ( std::size_t i = 0; i < v.size(); i++ )
			{
				szOut += i ? ", " : "";
				if constexpr ( std::is_same_v<T_t, std::vector<CElementRef>> )
				{
					szOut += refText( v[ i ] );
				}
				else
				{
					szOut += ScalarText( static_cast<typename T_t::value_type>( v[ i ] ) );
				}
			}
			return szOut;
		}
	},
		value );
}

//-----------------------------------------------------------------------------
// Purpose: keyvalues2 text, for viewing and diffing. Elements referenced once are written inline.
//-----------------------------------------------------------------------------
std::string WriteKeyValues2( const Document_t &document )
{
	return CKv2Writer{ document }.Run();
}

//-----------------------------------------------------------------------------
// Purpose: Structural equality; floats are compared bit for bit.
//-----------------------------------------------------------------------------
bool Equivalent( const Document_t &a, const Document_t &b, std::string *pDifference )
{
	const auto differ = [ pDifference ]( std::string szMessage ) {
		if ( pDifference )
		{
			*pDifference = std::move( szMessage );
		}
		return false;
	};
	if ( a.m_szFormat != b.m_szFormat || a.m_nFormatVersion != b.m_nFormatVersion )
	{
		return differ( "format differs" );
	}
	if ( a.m_Elements.size() != b.m_Elements.size() )
	{
		return differ( std::format( "element count {} vs {}", a.m_Elements.size(), b.m_Elements.size() ) );
	}
	for ( std::size_t i = 0; i < a.m_Elements.size(); i++ )
	{
		const CElement &x = a.m_Elements[ i ];
		const CElement &y = b.m_Elements[ i ];
		if ( x.m_szType != y.m_szType || x.m_szName != y.m_szName || x.m_Id != y.m_Id )
		{
			return differ( std::format( "element {} header differs", i ) );
		}
		if ( x.m_Attributes.size() != y.m_Attributes.size() )
		{
			return differ( std::format( "element {} attribute count differs", i ) );
		}
		for ( std::size_t j = 0; j < x.m_Attributes.size(); j++ )
		{
			const Attribute_t &p = x.m_Attributes[ j ];
			const Attribute_t &q = y.m_Attributes[ j ];
			if ( p.m_szName != q.m_szName || p.m_nType != q.m_nType || !ValuesMatch( p.m_Value, q.m_Value ) )
			{
				return differ( std::format( "element {} attribute \"{}\" differs", i, p.m_szName ) );
			}
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool SameValue( const Value_t &a, const Value_t &b )
{
	return ValuesMatch( a, b );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool SameElement( const CElement &a, const CElement &b )
{
	if ( a.m_szType != b.m_szType || a.m_szName != b.m_szName || a.m_Id != b.m_Id || a.m_Attributes.size() != b.m_Attributes.size() )
	{
		return false;
	}
	for ( std::size_t i = 0; i < a.m_Attributes.size(); i++ )
	{
		const Attribute_t &p = a.m_Attributes[ i ];
		const Attribute_t &q = b.m_Attributes[ i ];
		if ( p.m_szName != q.m_szName || p.m_nType != q.m_nType || !ValuesMatch( p.m_Value, q.m_Value ) )
		{
			return false;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string FormatGuid( const Guid_t &g )
{
	// Laid out like a Windows GUID: three little-endian fields, then eight bytes as-is.
	return std::format( "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
		g[ 3 ], g[ 2 ], g[ 1 ], g[ 0 ], g[ 5 ], g[ 4 ], g[ 7 ], g[ 6 ],
		g[ 8 ], g[ 9 ], g[ 10 ], g[ 11 ], g[ 12 ], g[ 13 ], g[ 14 ], g[ 15 ] );
}

} // namespace xv::dmx
