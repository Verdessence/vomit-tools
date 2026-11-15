//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Network.h.
//
//===========================================================================//
#include "Network.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <utility>

#include "Bits.h"

namespace xv::demo
{

namespace
{

// A message's kind is this many bits.
constexpr int MESSAGE_KIND_BITS = 6;
constexpr int ENTITY_BITS = 11;
constexpr int SERIAL_BITS = 10;
// A string table entry can begin as one of the last this many did.
constexpr std::size_t STRING_HISTORY = 32;
constexpr int MAX_TABLE_DEPTH = 64;

// The property flags that say how a value is written.
constexpr std::uint32_t PROP_UNSIGNED = 1u << 0;
constexpr std::uint32_t PROP_COORD = 1u << 1;
constexpr std::uint32_t PROP_NO_SCALE = 1u << 2;
constexpr std::uint32_t PROP_NORMAL = 1u << 5; // on a whole number: a varint
constexpr std::uint32_t PROP_EXCLUDE = 1u << 6;
constexpr std::uint32_t PROP_INSIDE_ARRAY = 1u << 8;
constexpr std::uint32_t PROP_CHANGES_OFTEN = 1u << 10;
constexpr std::uint32_t PROP_COLLAPSIBLE = 1u << 12;
constexpr std::uint32_t PROP_COORD_MP = 1u << 13;
constexpr std::uint32_t PROP_COORD_MP_LOW_PRECISION = 1u << 14;
constexpr std::uint32_t PROP_COORD_MP_INTEGRAL = 1u << 15;

// The messages, by the numbers the protocol gives them.
enum MessageKind_t
{
	MESSAGE_KIND_NOP = 0,
	MESSAGE_KIND_DISCONNECT = 1,
	MESSAGE_KIND_FILE = 2,
	MESSAGE_KIND_TICK = 3,
	MESSAGE_KIND_STRING_COMMAND = 4,
	MESSAGE_KIND_SET_CONVARS = 5,
	MESSAGE_KIND_SIGNON_STATE = 6,
	MESSAGE_KIND_PRINT = 7,
	MESSAGE_KIND_SERVER_INFO = 8,
	MESSAGE_KIND_SEND_TABLE = 9,
	MESSAGE_KIND_CLASS_INFO = 10,
	MESSAGE_KIND_SET_PAUSE = 11,
	MESSAGE_KIND_CREATE_STRING_TABLE = 12,
	MESSAGE_KIND_UPDATE_STRING_TABLE = 13,
	MESSAGE_KIND_VOICE_INIT = 14,
	MESSAGE_KIND_VOICE_DATA = 15,
	MESSAGE_KIND_SOUNDS = 17,
	MESSAGE_KIND_SET_VIEW = 18,
	MESSAGE_KIND_FIX_ANGLE = 19,
	MESSAGE_KIND_CROSSHAIR_ANGLE = 20,
	MESSAGE_KIND_BSP_DECAL = 21,
	MESSAGE_KIND_USER_MESSAGE = 23,
	MESSAGE_KIND_ENTITY_MESSAGE = 24,
	MESSAGE_KIND_GAME_EVENT = 25,
	MESSAGE_KIND_PACKET_ENTITIES = 26,
	MESSAGE_KIND_TEMP_ENTITIES = 27,
	MESSAGE_KIND_PREFETCH = 28,
	MESSAGE_KIND_MENU = 29,
	MESSAGE_KIND_GAME_EVENT_LIST = 30,
	MESSAGE_KIND_GET_CVAR_VALUE = 31,
	MESSAGE_KIND_COMMAND_KEY_VALUES = 32,
	MESSAGE_KIND_SET_PAUSE_TIMED = 33,
};

//-----------------------------------------------------------------------------
// Purpose: How many bits a number below nValue needs, less one: the engine's log2.
//-----------------------------------------------------------------------------
int Log2( int nValue )
{
	int nBits = 0;
	while ( nValue > 1 )
	{
		nValue >>= 1;
		nBits++;
	}
	return nBits;
}

//-----------------------------------------------------------------------------
// Purpose: Valve's LZSS ("LZSS", the size, then commands: a bit for each of the next eight
//          items, set for a copy of 1 to 16 bytes from up to 4096 back, clear for a byte).
//-----------------------------------------------------------------------------
bool UnpackLzss( std::span<const std::byte> packed, std::vector<std::byte> &out )
{
	if ( packed.size() < 8 )
	{
		return false;
	}
	std::uint32_t nSize = 0;
	std::memcpy( &nSize, packed.data() + 4, 4 );
	out.clear();
	out.reserve( nSize );
	std::size_t nAt = 8;
	std::uint32_t nCommands = 0;
	int nLeft = 0;
	for ( ;; )
	{
		if ( nLeft == 0 )
		{
			if ( nAt >= packed.size() )
			{
				return false;
			}
			nCommands = static_cast<std::uint8_t>( packed[ nAt++ ] );
			nLeft = 8;
		}
		nLeft--;
		if ( nCommands & 1 )
		{
			if ( nAt + 2 > packed.size() )
			{
				return false;
			}
			const std::uint32_t nFirst = static_cast<std::uint8_t>( packed[ nAt ] );
			const std::uint32_t nSecond = static_cast<std::uint8_t>( packed[ nAt + 1 ] );
			nAt += 2;
			const std::size_t nBack = ( nFirst << 4 | nSecond >> 4 ) + 1;
			const std::size_t nCount = ( nSecond & 15 ) + 1;
			if ( nCount == 1 )
			{
				break;
			}
			if ( nBack > out.size() )
			{
				return false;
			}
			for ( std::size_t i = 0; i < nCount; i++ )
			{
				out.push_back( out[ out.size() - nBack ] );
			}
		}
		else
		{
			if ( nAt >= packed.size() )
			{
				return false;
			}
			out.push_back( packed[ nAt++ ] );
		}
		nCommands >>= 1;
	}
	return out.size() == nSize;
}

//-----------------------------------------------------------------------------
// Purpose: Snappy's raw format: the size as a varint, then literals and copies from earlier
//          in the output, told apart by each tag's low two bits.
//-----------------------------------------------------------------------------
bool UnpackSnappy( std::span<const std::byte> packed, std::vector<std::byte> &out )
{
	std::size_t nAt = 0;
	const auto byte = [ & ]( std::size_t n ) { return static_cast<std::size_t>( static_cast<std::uint8_t>( packed[ n ] ) ); };
	std::size_t nSize = 0;
	for ( int nShift = 0;; nShift += 7 )
	{
		if ( nAt >= packed.size() || nShift > 28 )
		{
			return false;
		}
		const std::size_t nByte = byte( nAt++ );
		nSize |= ( nByte & 0x7f ) << nShift;
		if ( ( nByte & 0x80 ) == 0 )
		{
			break;
		}
	}
	out.clear();
	out.reserve( nSize );
	while ( nAt < packed.size() )
	{
		const std::size_t nTag = byte( nAt++ );
		std::size_t nCount = 0;
		std::size_t nBack = 0;
		switch ( nTag & 3 )
		{
			case 0:
			{
				nCount = nTag >> 2;
				if ( nCount >= 60 )
				{
					const std::size_t nBytes = nCount - 59;
					if ( nAt + nBytes > packed.size() )
					{
						return false;
					}
					nCount = 0;
					for ( std::size_t i = 0; i < nBytes; i++ )
					{
						nCount |= byte( nAt + i ) << ( 8 * i );
					}
					nAt += nBytes;
				}
				nCount++;
				if ( nAt + nCount > packed.size() )
				{
					return false;
				}
				out.insert( out.end(), packed.begin() + static_cast<std::ptrdiff_t>( nAt ), packed.begin() + static_cast<std::ptrdiff_t>( nAt + nCount ) );
				nAt += nCount;
				continue;
			}
			case 1:
				if ( nAt + 1 > packed.size() )
				{
					return false;
				}
				nCount = ( ( nTag >> 2 ) & 7 ) + 4;
				nBack = ( nTag >> 5 ) << 8 | byte( nAt );
				nAt += 1;
				break;
			case 2:
				if ( nAt + 2 > packed.size() )
				{
					return false;
				}
				nCount = ( nTag >> 2 ) + 1;
				nBack = byte( nAt ) | byte( nAt + 1 ) << 8;
				nAt += 2;
				break;
			default:
				if ( nAt + 4 > packed.size() )
				{
					return false;
				}
				nCount = ( nTag >> 2 ) + 1;
				nBack = byte( nAt ) | byte( nAt + 1 ) << 8 | byte( nAt + 2 ) << 16 | byte( nAt + 3 ) << 24;
				nAt += 4;
				break;
		}
		if ( nBack == 0 || nBack > out.size() )
		{
			return false;
		}
		for ( std::size_t i = 0; i < nCount; i++ )
		{
			out.push_back( out[ out.size() - nBack ] );
		}
	}
	return out.size() == nSize;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CNetworkState::CNetworkState( Watch_t pfnWatch, int nSlots )
	: m_pfnWatch( std::move( pfnWatch ) ),
	  m_nSlots( std::max( nSlots, 0 ) ),
	  m_Entities( MAX_ENTITIES )
{
}

//-----------------------------------------------------------------------------
// Purpose: The first reason stands; later ones follow from it.
//-----------------------------------------------------------------------------
bool CNetworkState::Fail( std::string szWhy )
{
	if ( m_szError.empty() )
	{
		m_szError = std::move( szWhy );
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CNetworkState::IsWatched( int nClass ) const
{
	return nClass >= 0 && static_cast<std::size_t>( nClass ) < m_Flat.size() && !m_Flat[ static_cast<std::size_t>( nClass ) ].m_Watch.empty();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const StringTable_t *CNetworkState::FindTable( std::string_view szName ) const
{
	const auto found = std::ranges::find( m_StringTables, szName, &StringTable_t::m_szName );
	return found == m_StringTables.end() ? nullptr : &*found;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
StringTable_t *CNetworkState::FindTableToChange( std::string_view szName )
{
	const auto found = std::ranges::find( m_StringTables, szName, &StringTable_t::m_szName );
	return found == m_StringTables.end() ? nullptr : &*found;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const std::vector<FlatProp_t> *CNetworkState::GetProps( int nClass )
{
	const Flat_t *pFlat = Flatten( nClass );
	return pFlat ? &pFlat->m_Props : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The network tables frame: every table (a bit before each says another follows),
//          then the classes with the table each sends.
//-----------------------------------------------------------------------------
bool CNetworkState::ReadDataTables( std::span<const std::byte> block )
{
	CBitReader reader( block );
	while ( reader.ReadBit() )
	{
		if ( !ReadSendTable( reader ) )
		{
			return false;
		}
	}
	const int nClasses = static_cast<int>( reader.ReadBits( 16 ) );
	for ( int i = 0; i < nClasses && !reader.IsOverflowed(); i++ )
	{
		const std::size_t nId = reader.ReadBits( 16 );
		ServerClass_t serverClass;
		serverClass.m_szName = reader.ReadString();
		serverClass.m_szTable = reader.ReadString();
		if ( m_Classes.size() <= nId )
		{
			m_Classes.resize( nId + 1 );
		}
		m_Classes[ nId ] = std::move( serverClass );
	}
	if ( reader.IsOverflowed() )
	{
		return Fail( "the network tables are cut short" );
	}
	m_Flat.assign( m_Classes.size(), {} );
	m_nClassBits = Log2( static_cast<int>( m_Classes.size() ) ) + 1;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: One table: its properties as declared. A property that brings in another table, or
//          excludes one of another's, names it; an array says how many elements at most (the
//          property before it describes one); any other gives its range and bits.
//-----------------------------------------------------------------------------
bool CNetworkState::ReadSendTable( CBitReader &reader )
{
	SendTable_t table;
	(void)reader.ReadBit(); // whether the client must decode it: every table is described here
	table.m_szName = reader.ReadString();
	const int nProps = static_cast<int>( reader.ReadBits( 10 ) );
	table.m_Props.reserve( static_cast<std::size_t>( nProps ) );
	for ( int i = 0; i < nProps && !reader.IsOverflowed(); i++ )
	{
		SendProp_t prop;
		const std::uint32_t nKind = reader.ReadBits( 5 );
		if ( nKind > static_cast<std::uint32_t>( PropKind_t::PROP_KIND_TABLE ) )
		{
			return Fail( "a network table has a property of a kind this doesn't read (" + std::to_string( nKind ) + ")" );
		}
		prop.m_nKind = static_cast<PropKind_t>( nKind );
		prop.m_szName = reader.ReadString();
		prop.m_nFlags = reader.ReadBits( 16 );
		if ( prop.m_nKind == PropKind_t::PROP_KIND_TABLE || ( prop.m_nFlags & PROP_EXCLUDE ) )
		{
			prop.m_szTable = reader.ReadString();
		}
		else if ( prop.m_nKind == PropKind_t::PROP_KIND_ARRAY )
		{
			prop.m_nElements = static_cast<int>( reader.ReadBits( 10 ) );
		}
		else
		{
			prop.m_flLow = reader.ReadFloat();
			prop.m_flHigh = reader.ReadFloat();
			prop.m_nBits = static_cast<int>( reader.ReadBits( 7 ) );
		}
		table.m_Props.push_back( std::move( prop ) );
	}
	if ( reader.IsOverflowed() )
	{
		return Fail( "a network table is cut short" );
	}
	m_TableByName[ table.m_szName ] = m_Tables.size();
	m_Tables.push_back( std::move( table ) );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A class's properties in the order entity messages number them. Excludes are
//          gathered from every table the class reaches; then each table gives its own
//          properties after those of the tables it brings in, a "collapsible" table's counting
//          as the bringing table's own; last, the ones flagged as changing often move to the
//          front, each swapped with whatever stood there.
//-----------------------------------------------------------------------------
CNetworkState::Flat_t *CNetworkState::Flatten( int nClass )
{
	if ( nClass < 0 || static_cast<std::size_t>( nClass ) >= m_Flat.size() )
	{
		return nullptr;
	}
	Flat_t &flat = m_Flat[ static_cast<std::size_t>( nClass ) ];
	if ( flat.m_bMade )
	{
		return &flat;
	}
	const ServerClass_t &serverClass = m_Classes[ static_cast<std::size_t>( nClass ) ];
	const auto find = [ this ]( const std::string &szName ) -> const SendTable_t * {
		const auto found = m_TableByName.find( szName );
		return found == m_TableByName.end() ? nullptr : &m_Tables[ found->second ];
	};
	const SendTable_t *pRoot = find( serverClass.m_szTable );
	if ( !pRoot )
	{
		Fail( "the class " + serverClass.m_szName + " sends a table the demo doesn't describe (" + serverClass.m_szTable + ")" );
		return nullptr;
	}

	std::set<std::pair<std::string, std::string>> excludes;
	bool bWhole = true;
	const auto gatherExcludes = [ & ]( auto &&self, const SendTable_t &table, int nDepth ) -> void {
		for ( const SendProp_t &prop : table.m_Props )
		{
			if ( prop.m_nFlags & PROP_EXCLUDE )
			{
				excludes.emplace( prop.m_szTable, prop.m_szName );
			}
			else if ( prop.m_nKind == PropKind_t::PROP_KIND_TABLE )
			{
				const SendTable_t *pInner = find( prop.m_szTable );
				if ( !pInner || nDepth >= MAX_TABLE_DEPTH )
				{
					bWhole = false;
					return;
				}
				self( self, *pInner, nDepth + 1 );
			}
		}
	};
	gatherExcludes( gatherExcludes, *pRoot, 0 );

	std::vector<FlatProp_t> props;
	const auto gather = [ & ]( auto &&self, const SendTable_t &table, std::vector<FlatProp_t> &own, int nDepth ) -> void {
		for ( std::size_t i = 0; i < table.m_Props.size(); i++ )
		{
			const SendProp_t &prop = table.m_Props[ i ];
			if ( ( prop.m_nFlags & ( PROP_EXCLUDE | PROP_INSIDE_ARRAY ) ) || excludes.contains( { table.m_szName, prop.m_szName } ) )
			{
				continue;
			}
			if ( prop.m_nKind == PropKind_t::PROP_KIND_TABLE )
			{
				const SendTable_t *pInner = find( prop.m_szTable );
				if ( !pInner || nDepth >= MAX_TABLE_DEPTH )
				{
					bWhole = false;
					return;
				}
				if ( prop.m_nFlags & PROP_COLLAPSIBLE )
				{
					self( self, *pInner, own, nDepth + 1 );
				}
				else
				{
					std::vector<FlatProp_t> inner;
					self( self, *pInner, inner, nDepth + 1 );
					props.insert( props.end(), std::make_move_iterator( inner.begin() ), std::make_move_iterator( inner.end() ) );
				}
				continue;
			}
			const auto describe = [ &table ]( const SendProp_t &from ) {
				FlatProp_t to;
				to.m_szTable = table.m_szName;
				to.m_szName = from.m_szName;
				to.m_nKind = from.m_nKind;
				to.m_nFlags = from.m_nFlags;
				to.m_nBits = from.m_nBits;
				to.m_flLow = from.m_flLow;
				to.m_flHigh = from.m_flHigh;
				to.m_nElements = from.m_nElements;
				return to;
			};
			FlatProp_t to = describe( prop );
			if ( prop.m_nKind == PropKind_t::PROP_KIND_ARRAY )
			{
				// An array of arrays or of tables isn't something the engine sends.
				if ( i == 0 || table.m_Props[ i - 1 ].m_nKind >= PropKind_t::PROP_KIND_ARRAY )
				{
					bWhole = false;
					return;
				}
				to.m_nElement = static_cast<int>( flat.m_Elements.size() );
				flat.m_Elements.push_back( describe( table.m_Props[ i - 1 ] ) );
			}
			own.push_back( std::move( to ) );
		}
	};
	std::vector<FlatProp_t> own;
	gather( gather, *pRoot, own, 0 );
	if ( !bWhole )
	{
		Fail( "the network table " + serverClass.m_szTable + " can't be put together" );
		return nullptr;
	}
	props.insert( props.end(), std::make_move_iterator( own.begin() ), std::make_move_iterator( own.end() ) );

	std::size_t nFront = 0;
	for ( std::size_t i = 0; i < props.size(); i++ )
	{
		if ( props[ i ].m_nFlags & PROP_CHANGES_OFTEN )
		{
			if ( i != nFront )
			{
				std::swap( props[ i ], props[ nFront ] );
			}
			nFront++;
		}
	}
	flat.m_Props = std::move( props );
	flat.m_bMade = true;

	if ( m_pfnWatch )
	{
		flat.m_Watch = m_pfnWatch( serverClass, flat.m_Props );
		if ( flat.m_Watch.size() != flat.m_Props.size() )
		{
			flat.m_Watch.clear();
		}
		for ( std::size_t i = 0; i < flat.m_Watch.size(); i++ )
		{
			const int nCode = flat.m_Watch[ i ];
			const PropKind_t nKind = flat.m_Props[ i ].m_nKind;
			if ( nCode >= 0 && ( nCode / 4 >= m_nSlots || nCode % 4 > 2 || nKind == PropKind_t::PROP_KIND_STRING || nKind == PropKind_t::PROP_KIND_ARRAY ) )
			{
				flat.m_Watch[ i ] = -1;
			}
		}
	}
	return &flat;
}

//-----------------------------------------------------------------------------
// Purpose: A float as its property's flags say it was written: a coordinate, a raw float, a
//          unit vector's part (a sign and 11 bits), or so many bits across the range.
//-----------------------------------------------------------------------------
float CNetworkState::ReadFloatProp( CBitReader &reader, const FlatProp_t &prop )
{
	const std::uint32_t nFlags = prop.m_nFlags;
	if ( nFlags & PROP_COORD )
	{
		return reader.ReadCoord();
	}
	if ( nFlags & PROP_COORD_MP )
	{
		return reader.ReadCoordMp( false, false );
	}
	if ( nFlags & PROP_COORD_MP_LOW_PRECISION )
	{
		return reader.ReadCoordMp( false, true );
	}
	if ( nFlags & PROP_COORD_MP_INTEGRAL )
	{
		return reader.ReadCoordMp( true, false );
	}
	if ( nFlags & PROP_NO_SCALE )
	{
		return reader.ReadFloat();
	}
	if ( nFlags & PROP_NORMAL )
	{
		const bool bNegative = reader.ReadBit();
		const float flValue = static_cast<float>( reader.ReadBits( 11 ) ) / 2047.0f;
		return bNegative ? -flValue : flValue;
	}
	const int nBits = std::clamp( prop.m_nBits, 0, 32 );
	if ( nBits == 0 )
	{
		return prop.m_flLow;
	}
	const double flMost = static_cast<double>( ( std::uint64_t{ 1 } << nBits ) - 1 );
	return static_cast<float>( prop.m_flLow + ( prop.m_flHigh - prop.m_flLow ) * ( reader.ReadBits( nBits ) / flMost ) );
}

//-----------------------------------------------------------------------------
// Purpose: One property's value, read past; its numbers go to pOut (as many as nRoom) when
//          it is kept.
//-----------------------------------------------------------------------------
void CNetworkState::ReadValue( CBitReader &reader, const FlatProp_t &prop, const Flat_t &flat, double *pOut, int nRoom )
{
	const auto put = [ & ]( int n, double flValue ) {
		if ( pOut && n < nRoom )
		{
			pOut[ n ] = flValue;
		}
	};
	switch ( prop.m_nKind )
	{
		case PropKind_t::PROP_KIND_INT:
			if ( prop.m_nFlags & PROP_NORMAL )
			{
				const std::uint32_t nValue = reader.ReadVarInt();
				put( 0, ( prop.m_nFlags & PROP_UNSIGNED ) ? static_cast<double>( nValue ) : static_cast<double>( static_cast<std::int32_t>( ( nValue >> 1 ) ^ ( ~( nValue & 1 ) + 1 ) ) ) );
			}
			else if ( prop.m_nFlags & PROP_UNSIGNED )
			{
				put( 0, static_cast<double>( reader.ReadBits( std::min( prop.m_nBits, 32 ) ) ) );
			}
			else
			{
				put( 0, static_cast<double>( reader.ReadSignedBits( std::min( prop.m_nBits, 32 ) ) ) );
			}
			break;
		case PropKind_t::PROP_KIND_FLOAT:
			put( 0, ReadFloatProp( reader, prop ) );
			break;
		case PropKind_t::PROP_KIND_VECTOR:
		{
			const float flX = ReadFloatProp( reader, prop );
			const float flY = ReadFloatProp( reader, prop );
			float flZ = 0.0f;
			if ( prop.m_nFlags & PROP_NORMAL )
			{
				// A unit vector sends two parts and the third's sign.
				const bool bNegative = reader.ReadBit();
				const float flSquares = flX * flX + flY * flY;
				flZ = flSquares < 1.0f ? std::sqrt( 1.0f - flSquares ) : 0.0f;
				flZ = bNegative ? -flZ : flZ;
			}
			else
			{
				flZ = ReadFloatProp( reader, prop );
			}
			put( 0, flX );
			put( 1, flY );
			put( 2, flZ );
			break;
		}
		case PropKind_t::PROP_KIND_VECTOR_XY:
		{
			const float flX = ReadFloatProp( reader, prop );
			put( 0, flX );
			put( 1, ReadFloatProp( reader, prop ) );
			break;
		}
		case PropKind_t::PROP_KIND_STRING:
			reader.Skip( static_cast<std::size_t>( reader.ReadBits( 9 ) ) * 8 );
			break;
		case PropKind_t::PROP_KIND_ARRAY:
		{
			const int nCount = static_cast<int>( reader.ReadBits( Log2( prop.m_nElements ) + 1 ) );
			if ( prop.m_nElement < 0 || static_cast<std::size_t>( prop.m_nElement ) >= flat.m_Elements.size() )
			{
				break;
			}
			const FlatProp_t &element = flat.m_Elements[ static_cast<std::size_t>( prop.m_nElement ) ];
			for ( int i = 0; i < nCount && !reader.IsOverflowed(); i++ )
			{
				ReadValue( reader, element, flat, nullptr, 0 );
			}
			break;
		}
		case PropKind_t::PROP_KIND_TABLE:
			break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: An entity's changed properties: a bit says another follows, then how far past the
//          last one it is, then its value.
//-----------------------------------------------------------------------------
bool CNetworkState::ReadProps( CBitReader &reader, const Flat_t &flat, std::vector<PropValue_t> *pValues )
{
	int nProp = -1;
	const bool bKeep = pValues && !flat.m_Watch.empty();
	while ( reader.ReadBit() )
	{
		nProp += static_cast<int>( reader.ReadVarBits() ) + 1;
		if ( nProp < 0 || static_cast<std::size_t>( nProp ) >= flat.m_Props.size() )
		{
			return Fail( "an entity changes a property its class doesn't have" );
		}
		const int nCode = bKeep ? flat.m_Watch[ static_cast<std::size_t>( nProp ) ] : -1;
		if ( nCode >= 0 )
		{
			ReadValue( reader, flat.m_Props[ static_cast<std::size_t>( nProp ) ], flat, ( *pValues )[ static_cast<std::size_t>( nCode / 4 ) ].data() + nCode % 4, 3 - nCode % 4 );
		}
		else
		{
			ReadValue( reader, flat.m_Props[ static_cast<std::size_t>( nProp ) ], flat, nullptr, 0 );
		}
		if ( reader.IsOverflowed() )
		{
			return Fail( "an entity's properties run past their message" );
		}
	}
	return !reader.IsOverflowed() || Fail( "an entity's properties run past their message" );
}

//-----------------------------------------------------------------------------
// Purpose: The values every entity of a class starts from: the class's entry in the
//          instancebaseline table (named by the class's number), written as an entity's
//          properties are.
//-----------------------------------------------------------------------------
const std::vector<PropValue_t> *CNetworkState::InstanceBaseline( int nClass, Flat_t &flat )
{
	const StringTable_t *pTable = FindTable( "instancebaseline" );
	const std::uint64_t nRevision = pTable ? pTable->m_nRevision : 0;
	if ( flat.m_nBaselineRevision == nRevision && !flat.m_Baseline.empty() )
	{
		return &flat.m_Baseline;
	}
	flat.m_nBaselineRevision = nRevision;
	flat.m_Baseline.assign( static_cast<std::size_t>( m_nSlots ), PropValue_t{} );
	if ( pTable )
	{
		const std::string szClass = std::to_string( nClass );
		const auto found = std::ranges::find( pTable->m_Entries, szClass, &StringEntry_t::m_szText );
		if ( found != pTable->m_Entries.end() && !found->m_Data.empty() )
		{
			CBitReader reader( found->m_Data );
			if ( !ReadProps( reader, flat, &flat.m_Baseline ) )
			{
				return nullptr;
			}
		}
	}
	return &flat.m_Baseline;
}

//-----------------------------------------------------------------------------
// Purpose: A string table's changed entries. Each says whether it is the one after the last
//          (else its number), then its text if that changed (which may begin as one of the
//          last 32 read did, so many letters of it), then its data if that did.
//-----------------------------------------------------------------------------
bool CNetworkState::ReadTableEntries( StringTable_t &table, CBitReader &reader, int nEntries )
{
	const int nIndexBits = Log2( table.m_nMaxEntries );
	std::vector<std::string> history;
	int nLast = -1;
	for ( int i = 0; i < nEntries; i++ )
	{
		int nIndex = nLast + 1;
		if ( !reader.ReadBit() )
		{
			nIndex = static_cast<int>( reader.ReadBits( nIndexBits ) );
		}
		nLast = nIndex;
		if ( nIndex < 0 || nIndex >= std::max( table.m_nMaxEntries, 1 ) )
		{
			return Fail( "the string table " + table.m_szName + " has an entry past its end" );
		}
		if ( table.m_Entries.size() <= static_cast<std::size_t>( nIndex ) )
		{
			table.m_Entries.resize( static_cast<std::size_t>( nIndex ) + 1 );
		}
		StringEntry_t &entry = table.m_Entries[ static_cast<std::size_t>( nIndex ) ];
		if ( reader.ReadBit() )
		{
			if ( reader.ReadBit() )
			{
				const std::size_t nFrom = reader.ReadBits( 5 );
				const std::size_t nCopy = reader.ReadBits( 5 );
				if ( nFrom >= history.size() )
				{
					return Fail( "the string table " + table.m_szName + " can't be read" );
				}
				entry.m_szText = history[ nFrom ].substr( 0, nCopy ) + reader.ReadString();
			}
			else
			{
				entry.m_szText = reader.ReadString();
			}
		}
		if ( reader.ReadBit() )
		{
			if ( table.m_bFixedData )
			{
				entry.m_Data.assign( static_cast<std::size_t>( table.m_nFixedBits + 7 ) / 8, std::byte{ 0 } );
				for ( int nBit = 0; nBit < table.m_nFixedBits; nBit += 8 )
				{
					entry.m_Data[ static_cast<std::size_t>( nBit ) / 8 ] = static_cast<std::byte>( reader.ReadBits( std::min( 8, table.m_nFixedBits - nBit ) ) );
				}
			}
			else
			{
				entry.m_Data.resize( reader.ReadBits( 14 ) );
				if ( reader.GetLeft() < entry.m_Data.size() * 8 )
				{
					entry.m_Data.clear();
					return Fail( "the string table " + table.m_szName + " is cut short" );
				}
				reader.ReadBytes( entry.m_Data.data(), entry.m_Data.size() );
			}
		}
		if ( reader.IsOverflowed() )
		{
			return Fail( "the string table " + table.m_szName + " is cut short" );
		}
		if ( history.size() >= STRING_HISTORY )
		{
			history.erase( history.begin() );
		}
		history.push_back( entry.m_szText );
	}
	table.m_nRevision++;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A new string table and its first entries, which may be packed (Snappy or LZSS,
//          the sizes before them).
//-----------------------------------------------------------------------------
bool CNetworkState::ReadCreateStringTable( CBitReader &reader )
{
	StringTable_t table;
	table.m_szName = reader.ReadString();
	table.m_nMaxEntries = static_cast<int>( reader.ReadBits( 16 ) );
	const int nEntries = static_cast<int>( reader.ReadBits( Log2( table.m_nMaxEntries ) + 1 ) );
	const std::size_t nBits = reader.ReadVarInt();
	table.m_bFixedData = reader.ReadBit();
	if ( table.m_bFixedData )
	{
		(void)reader.ReadBits( 12 ); // the size in bytes
		table.m_nFixedBits = static_cast<int>( reader.ReadBits( 4 ) );
	}
	const bool bPacked = reader.ReadBit();
	CBitReader entries = reader.ReadBlock( nBits );
	if ( reader.IsOverflowed() )
	{
		return Fail( "the string table " + table.m_szName + " is cut short" );
	}
	// Tables are numbered in the order they are made, which updates name them by.
	m_StringTables.push_back( std::move( table ) );
	StringTable_t &made = m_StringTables.back();
	if ( !bPacked )
	{
		return ReadTableEntries( made, entries, nEntries );
	}
	const std::size_t nSize = entries.ReadBits( 32 );
	std::vector<std::byte> packed( entries.ReadBits( 32 ) );
	if ( entries.GetLeft() < packed.size() * 8 || packed.size() < 4 )
	{
		return Fail( "the string table " + made.m_szName + " is cut short" );
	}
	entries.ReadBytes( packed.data(), packed.size() );
	std::vector<std::byte> unpacked;
	bool bUnpacked = false;
	if ( std::memcmp( packed.data(), "SNAP", 4 ) == 0 )
	{
		bUnpacked = UnpackSnappy( std::span( packed ).subspan( 4 ), unpacked );
	}
	else if ( std::memcmp( packed.data(), "LZSS", 4 ) == 0 )
	{
		bUnpacked = UnpackLzss( packed, unpacked );
	}
	if ( !bUnpacked || unpacked.size() != nSize )
	{
		return Fail( "the string table " + made.m_szName + " couldn't be unpacked" );
	}
	CBitReader plain( unpacked );
	return ReadTableEntries( made, plain, nEntries );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CNetworkState::ReadUpdateStringTable( CBitReader &reader )
{
	const std::size_t nTable = reader.ReadBits( 5 );
	const int nEntries = reader.ReadBit() ? static_cast<int>( reader.ReadBits( 16 ) ) : 1;
	CBitReader entries = reader.ReadBlock( reader.ReadBits( 20 ) );
	if ( reader.IsOverflowed() )
	{
		return Fail( "a string table's update is cut short" );
	}
	if ( nTable >= m_StringTables.size() )
	{
		return Fail( "an update for a string table that was never made" );
	}
	return ReadTableEntries( m_StringTables[ nTable ], entries, nEntries );
}

//-----------------------------------------------------------------------------
// Purpose: The string tables frame: every table whole, as it stood when recording began.
//-----------------------------------------------------------------------------
bool CNetworkState::ReadStringTables( std::span<const std::byte> block )
{
	CBitReader reader( block );
	const int nTables = static_cast<int>( reader.ReadBits( 8 ) );
	for ( int i = 0; i < nTables && !reader.IsOverflowed(); i++ )
	{
		const std::string szName = reader.ReadString();
		StringTable_t *pTable = FindTableToChange( szName );
		if ( !pTable )
		{
			m_StringTables.emplace_back();
			pTable = &m_StringTables.back();
			pTable->m_szName = szName;
		}
		const std::size_t nEntries = reader.ReadBits( 16 );
		if ( pTable->m_Entries.size() < nEntries )
		{
			pTable->m_Entries.resize( nEntries );
		}
		for ( std::size_t n = 0; n < nEntries && !reader.IsOverflowed(); n++ )
		{
			StringEntry_t &entry = pTable->m_Entries[ n ];
			entry.m_szText = reader.ReadString();
			entry.m_Data.clear();
			if ( reader.ReadBit() )
			{
				entry.m_Data.resize( reader.ReadBits( 16 ) );
				if ( reader.GetLeft() < entry.m_Data.size() * 8 )
				{
					entry.m_Data.clear();
					return Fail( "the string tables are cut short" );
				}
				reader.ReadBytes( entry.m_Data.data(), entry.m_Data.size() );
			}
		}
		// The client's own entries, which nothing here uses.
		if ( reader.ReadBit() )
		{
			const std::size_t nOwn = reader.ReadBits( 16 );
			for ( std::size_t n = 0; n < nOwn && !reader.IsOverflowed(); n++ )
			{
				(void)reader.ReadString();
				if ( reader.ReadBit() )
				{
					reader.Skip( static_cast<std::size_t>( reader.ReadBits( 16 ) ) * 8 );
				}
			}
		}
		pTable->m_nRevision++;
	}
	return !reader.IsOverflowed() || Fail( "the string tables are cut short" );
}

//-----------------------------------------------------------------------------
// Purpose: The entities that changed since an earlier tick (or all of them, when the message
//          isn't a delta). Each is so many past the last; two bits then say whether it comes
//          into sight (its class and serial follow, and it starts from its baseline), stays
//          (changed properties only), goes out of sight, or is deleted.
//-----------------------------------------------------------------------------
bool CNetworkState::ReadPacketEntities( CBitReader &reader )
{
	(void)reader.ReadBits( ENTITY_BITS ); // the most entities there can be
	const bool bDelta = reader.ReadBit();
	if ( bDelta )
	{
		(void)reader.ReadBits( 32 ); // the tick it changes
	}
	const std::size_t nBaseline = reader.ReadBits( 1 );
	const int nUpdated = static_cast<int>( reader.ReadBits( ENTITY_BITS ) );
	const std::size_t nBits = reader.ReadBits( 20 );
	const bool bUpdateBaseline = reader.ReadBit();
	CBitReader data = reader.ReadBlock( nBits );
	if ( reader.IsOverflowed() )
	{
		return Fail( "an entity message is cut short" );
	}
	if ( !bDelta )
	{
		for ( Entity_t &entity : m_Entities )
		{
			entity.m_nClass = -1;
			entity.m_Values.clear();
		}
	}
	int nEntity = -1;
	for ( int i = 0; i < nUpdated; i++ )
	{
		nEntity += 1 + static_cast<int>( data.ReadVarBits() );
		if ( data.IsOverflowed() || nEntity < 0 || nEntity >= MAX_ENTITIES )
		{
			return Fail( "an entity message names an entity past the last" );
		}
		Entity_t &entity = m_Entities[ static_cast<std::size_t>( nEntity ) ];
		if ( data.ReadBit() )
		{
			// Out of sight; a second bit says it is gone for good. Either way it is described
			// again from its class when it comes back.
			(void)data.ReadBit();
			entity.m_nClass = -1;
			entity.m_Values.clear();
			continue;
		}
		if ( data.ReadBit() )
		{
			const int nClass = static_cast<int>( data.ReadBits( m_nClassBits ) );
			entity.m_nSerial = static_cast<int>( data.ReadBits( SERIAL_BITS ) );
			Flat_t *pFlat = Flatten( nClass );
			if ( !pFlat )
			{
				return Fail( "an entity of a class the demo doesn't describe (" + std::to_string( nClass ) + ")" );
			}
			entity.m_nClass = nClass;
			entity.m_Values.clear();
			if ( !pFlat->m_Watch.empty() )
			{
				const auto own = m_Baselines[ nBaseline ].find( nEntity );
				if ( own != m_Baselines[ nBaseline ].end() && own->second.m_nClass == nClass )
				{
					entity.m_Values = own->second.m_Values;
				}
				else if ( const std::vector<PropValue_t> *pStart = InstanceBaseline( nClass, *pFlat ) )
				{
					entity.m_Values = *pStart;
				}
				else
				{
					return false;
				}
			}
			if ( !ReadProps( data, *pFlat, &entity.m_Values ) )
			{
				return false;
			}
			if ( bUpdateBaseline && !pFlat->m_Watch.empty() )
			{
				m_Baselines[ 1 - nBaseline ][ nEntity ] = { nClass, entity.m_Values };
			}
			continue;
		}
		const Flat_t *pFlat = Flatten( entity.m_nClass );
		if ( !pFlat )
		{
			return Fail( "an entity message changes an entity that isn't there (" + std::to_string( nEntity ) + ")" );
		}
		if ( !ReadProps( data, *pFlat, &entity.m_Values ) )
		{
			return false;
		}
	}
	if ( bDelta )
	{
		// Then the entities deleted without a word above.
		while ( data.ReadBit() )
		{
			const std::size_t nGone = data.ReadBits( ENTITY_BITS );
			m_Entities[ nGone ].m_nClass = -1;
			m_Entities[ nGone ].m_Values.clear();
		}
	}
	if ( data.IsOverflowed() )
	{
		return Fail( "an entity message is cut short" );
	}
	m_nSnapshots++;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: One message, read or stepped over; each kind has a layout of its own, and one
//          misread puts every later message out of step.
//-----------------------------------------------------------------------------
bool CNetworkState::ReadMessage( int nType, CBitReader &reader )
{
	switch ( nType )
	{
		case MESSAGE_KIND_NOP:
			return true;
		case MESSAGE_KIND_DISCONNECT:
		case MESSAGE_KIND_STRING_COMMAND:
		case MESSAGE_KIND_PRINT:
			(void)reader.ReadString();
			return true;
		case MESSAGE_KIND_FILE:
			reader.Skip( 32 );
			(void)reader.ReadString();
			reader.Skip( 1 );
			return true;
		case MESSAGE_KIND_TICK:
			// The tick, then how long the server's frames take and how much that varies.
			reader.Skip( 32 + 16 + 16 );
			return true;
		case MESSAGE_KIND_SET_CONVARS:
		{
			const int nCount = static_cast<int>( reader.ReadBits( 8 ) );
			for ( int i = 0; i < nCount && !reader.IsOverflowed(); i++ )
			{
				(void)reader.ReadString();
				(void)reader.ReadString();
			}
			return true;
		}
		case MESSAGE_KIND_SIGNON_STATE:
			reader.Skip( 8 + 32 );
			return true;
		case MESSAGE_KIND_SERVER_INFO:
		{
			ServerInfo_t info;
			info.m_nProtocol = static_cast<int>( reader.ReadBits( 16 ) );
			if ( info.m_nProtocol != NETWORK_PROTOCOL )
			{
				return Fail( "network protocol " + std::to_string( info.m_nProtocol ) + "'s messages aren't read (only protocol " + std::to_string( NETWORK_PROTOCOL ) + "'s)" );
			}
			reader.Skip( 32 ); // how many maps the server has run
			info.m_bSourceTv = reader.ReadBit();
			reader.Skip( 1 + 32 + 16 + 128 ); // dedicated, the client's checksum, the class count, the map's hash
			info.m_nPlayerSlot = static_cast<int>( reader.ReadBits( 8 ) );
			info.m_nMaxClients = static_cast<int>( reader.ReadBits( 8 ) );
			info.m_flTickInterval = reader.ReadFloat();
			reader.Skip( 8 ); // the server's system
			info.m_szGameDir = reader.ReadString();
			info.m_szMap = reader.ReadString();
			info.m_szSky = reader.ReadString();
			info.m_szHost = reader.ReadString();
			reader.Skip( 1 ); // a replay
			m_ServerInfo = std::move( info );
			return true;
		}
		case MESSAGE_KIND_SEND_TABLE:
			reader.Skip( 1 );
			reader.Skip( reader.ReadBits( 16 ) );
			return true;
		case MESSAGE_KIND_CLASS_INFO:
		{
			const int nClasses = static_cast<int>( reader.ReadBits( 16 ) );
			// Else the client makes them from its own tables, as the demo's tables frame does.
			if ( !reader.ReadBit() )
			{
				for ( int i = 0; i < nClasses && !reader.IsOverflowed(); i++ )
				{
					reader.Skip( static_cast<std::size_t>( Log2( nClasses ) ) + 1 );
					(void)reader.ReadString();
					(void)reader.ReadString();
				}
			}
			return true;
		}
		case MESSAGE_KIND_SET_PAUSE:
			reader.Skip( 1 );
			return true;
		case MESSAGE_KIND_CREATE_STRING_TABLE:
			return ReadCreateStringTable( reader );
		case MESSAGE_KIND_UPDATE_STRING_TABLE:
			return ReadUpdateStringTable( reader );
		case MESSAGE_KIND_VOICE_INIT:
			(void)reader.ReadString();
			if ( reader.ReadBits( 8 ) == 255 )
			{
				reader.Skip( 16 ); // the sample rate
			}
			return true;
		case MESSAGE_KIND_VOICE_DATA:
			reader.Skip( 8 + 8 );
			reader.Skip( reader.ReadBits( 16 ) );
			return true;
		case MESSAGE_KIND_SOUNDS:
			if ( reader.ReadBit() )
			{
				reader.Skip( reader.ReadBits( 8 ) );
			}
			else
			{
				reader.Skip( 8 );
				reader.Skip( reader.ReadBits( 16 ) );
			}
			return true;
		case MESSAGE_KIND_SET_VIEW:
			reader.Skip( ENTITY_BITS );
			return true;
		case MESSAGE_KIND_FIX_ANGLE:
			reader.Skip( 1 + 3 * 16 );
			return true;
		case MESSAGE_KIND_CROSSHAIR_ANGLE:
			reader.Skip( 3 * 16 );
			return true;
		case MESSAGE_KIND_BSP_DECAL:
		{
			const bool bX = reader.ReadBit();
			const bool bY = reader.ReadBit();
			const bool bZ = reader.ReadBit();
			for ( const bool bThere : { bX, bY, bZ } )
			{
				if ( bThere )
				{
					(void)reader.ReadCoord();
				}
			}
			reader.Skip( 9 ); // the decal's texture
			if ( reader.ReadBit() )
			{
				reader.Skip( ENTITY_BITS + 12 ); // the entity it is on, and that one's model
			}
			reader.Skip( 1 );
			return true;
		}
		case MESSAGE_KIND_USER_MESSAGE:
			reader.Skip( 8 );
			reader.Skip( reader.ReadBits( 11 ) );
			return true;
		case MESSAGE_KIND_ENTITY_MESSAGE:
			reader.Skip( ENTITY_BITS + 9 );
			reader.Skip( reader.ReadBits( 11 ) );
			return true;
		case MESSAGE_KIND_GAME_EVENT:
			reader.Skip( reader.ReadBits( 11 ) );
			return true;
		case MESSAGE_KIND_PACKET_ENTITIES:
			return ReadPacketEntities( reader );
		case MESSAGE_KIND_TEMP_ENTITIES:
			reader.Skip( 8 );
			reader.Skip( reader.ReadVarInt() );
			return true;
		case MESSAGE_KIND_PREFETCH:
			reader.Skip( 14 );
			return true;
		case MESSAGE_KIND_MENU:
			reader.Skip( 16 );
			reader.Skip( static_cast<std::size_t>( reader.ReadBits( 16 ) ) * 8 );
			return true;
		case MESSAGE_KIND_GAME_EVENT_LIST:
			reader.Skip( 9 );
			reader.Skip( reader.ReadBits( 20 ) );
			return true;
		case MESSAGE_KIND_GET_CVAR_VALUE:
			reader.Skip( 32 );
			(void)reader.ReadString();
			return true;
		case MESSAGE_KIND_COMMAND_KEY_VALUES:
			reader.Skip( static_cast<std::size_t>( reader.ReadBits( 32 ) ) * 8 );
			return true;
		case MESSAGE_KIND_SET_PAUSE_TIMED:
			reader.Skip( 1 + 32 );
			return true;
		default:
			return Fail( "a message of an unknown kind (" + std::to_string( nType ) + ")" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: A packet: messages one after another to its end (the last byte's spare bits are
//          fewer than a message's kind takes).
//-----------------------------------------------------------------------------
bool CNetworkState::ReadPacket( std::span<const std::byte> block )
{
	CBitReader reader( block );
	while ( reader.GetLeft() >= MESSAGE_KIND_BITS )
	{
		const int nType = static_cast<int>( reader.ReadBits( MESSAGE_KIND_BITS ) );
		if ( !ReadMessage( nType, reader ) )
		{
			return false;
		}
		if ( reader.IsOverflowed() )
		{
			return Fail( "a message (kind " + std::to_string( nType ) + ") runs past its packet" );
		}
	}
	return true;
}

} // namespace xv::demo
