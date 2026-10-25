//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Checked little-endian field and relative-offset reads for studio files.
//===========================================================================//
#pragma once
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <cmath>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include "util/Vec3.h"

namespace xv::model
{
class CBinary
{
public:
	explicit CBinary( std::span<const std::byte> bytes )
		: m_Bytes( bytes )
	{}
	void Check( std::size_t nAt, std::size_t nSize ) const
	{
		if ( nAt > m_Bytes.size() || nSize > m_Bytes.size() - nAt )
			throw std::runtime_error( "A model section runs outside its file (offset " + std::to_string( nAt ) + ", size " + std::to_string( nSize ) + ", file " + std::to_string( m_Bytes.size() ) + ")" );
	}
	template <class T>
	T Read( std::size_t nAt ) const
	{
		Check( nAt, sizeof( T ) );
		T value;
		std::memcpy( &value, m_Bytes.data() + nAt, sizeof( T ) );
		if constexpr ( std::is_floating_point_v<T> )
			if ( !std::isfinite( value ) )
				throw std::runtime_error( "A model contains a non-finite number at " + std::to_string( nAt ) );
		return value;
	}
	int Int( std::size_t nAt ) const { return Read<std::int32_t>( nAt ); }
	std::size_t Relative( std::size_t nBase, std::size_t nField ) const
	{
		const auto nAt = static_cast<std::int64_t>( nBase ) + Int( nField );
		if ( nAt < 0 )
			throw std::runtime_error( "A model offset is negative" );
		if ( static_cast<std::size_t>( nAt ) > m_Bytes.size() )
			throw std::runtime_error( "Model relative field " + std::to_string( nField ) + " (base " + std::to_string( nBase ) + ") points outside the file" );
		return static_cast<std::size_t>( nAt );
	}
	int Array( std::size_t nAt, int nCount, std::size_t nStride ) const
	{
		if ( nCount < 0 || nCount > ( 1 << 20 ) )
			throw std::runtime_error( "A model count is out of range" );
		Check( nAt, static_cast<std::size_t>( nCount ) * nStride );
		return nCount;
	}
	std::string Text( std::size_t nAt, std::size_t nLimit = SIZE_MAX ) const
	{
		Check( nAt, 1 );
		nLimit = std::min( nLimit, m_Bytes.size() - nAt );
		const char *p = reinterpret_cast<const char *>( m_Bytes.data() + nAt );
		const auto *pEnd = static_cast<const char *>( std::memchr( p, 0, nLimit ) );
		if ( !pEnd )
			throw std::runtime_error( "A model string has no terminator" );
		return { p, pEnd };
	}
	std::string FixedText( std::size_t nAt, std::size_t nSize ) const
	{
		Check( nAt, nSize );
		const char *p = reinterpret_cast<const char *>( m_Bytes.data() + nAt );
		const auto *pEnd = static_cast<const char *>( std::memchr( p, 0, nSize ) );
		return std::string( p, pEnd ? pEnd : p + nSize );
	}
	std::string Name( std::size_t nBase, std::size_t nField ) const
	{
		return Int( nField ) == 0 ? std::string{} : Text( Relative( nBase, nField ) );
	}
	CVec3 Vector( std::size_t nAt ) const { return { Read<float>( nAt ), Read<float>( nAt + 4 ), Read<float>( nAt + 8 ) }; }
	// Whether a printable name of at most 255 characters starts there.
	bool IsName( std::int64_t nAt ) const
	{
		if ( nAt <= 0 || static_cast<std::size_t>( nAt ) >= m_Bytes.size() )
			return false;
		for ( std::size_t i = static_cast<std::size_t>( nAt ); i < m_Bytes.size() && i < static_cast<std::size_t>( nAt ) + 256; i++ )
		{
			const auto c = static_cast<unsigned char>( m_Bytes[ i ] );
			if ( c == 0 )
				return true;
			if ( c < 0x20 || c == 0x7f )
				return false;
		}
		return false;
	}
	// Where a hitbox's name counts from: the box, as the engine reads it; version 44 files were
	// often written counting from the file's start, which is taken when only it reads as a name.
	std::size_t HitboxNameBase( int nVersion, std::size_t nBox ) const
	{
		if ( nVersion != 44 )
			return nBox;
		const int nName = Int( nBox + 32 );
		return !IsName( static_cast<std::int64_t>( nBox ) + nName ) && IsName( nName ) ? 0 : nBox;
	}

private:
	std::span<const std::byte> m_Bytes;
};
} // namespace xv::model
