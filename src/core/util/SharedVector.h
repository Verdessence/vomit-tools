//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A vector whose copies share their elements until one of them changes one: changing
//          an element another copy still holds copies that element first. A map's editing makes
//          a new snapshot of the scene for every edit (threads drawing or loading go on reading
//          theirs); copying the snapshot costs a pointer an element, and an edit pays only for
//          the objects it touches.
//
//          Only the thread that owns a copy may change it. Reading through a const copy never
//          copies.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace xv
{

template <typename T>
class CSharedVector
{
public:
	template <bool bConst>
	class CIterator
	{
	public:
		using iterator_category = std::random_access_iterator_tag;
		using iterator_concept = std::random_access_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = std::conditional_t<bConst, const T *, T *>;
		using reference = std::conditional_t<bConst, const T &, T &>;
		using Owner_t = std::conditional_t<bConst, const CSharedVector, CSharedVector>;

		CIterator() = default;
		CIterator( Owner_t *pOwner, std::size_t nIndex )
			: m_pOwner( pOwner ),
			  m_nIndex( nIndex )
		{
		}
		// A changing iterator reads as a const one.
		operator CIterator<true>() const
			requires( !bConst )
		{
			return { m_pOwner, m_nIndex };
		}

		reference operator*() const { return ( *m_pOwner )[ m_nIndex ]; }
		pointer operator->() const { return &( *m_pOwner )[ m_nIndex ]; }
		reference operator[]( difference_type n ) const { return ( *m_pOwner )[ m_nIndex + static_cast<std::size_t>( n ) ]; }
		CIterator &operator++()
		{
			m_nIndex++;
			return *this;
		}
		CIterator operator++( int )
		{
			CIterator before = *this;
			m_nIndex++;
			return before;
		}
		CIterator &operator--()
		{
			m_nIndex--;
			return *this;
		}
		CIterator operator--( int )
		{
			CIterator before = *this;
			m_nIndex--;
			return before;
		}
		CIterator &operator+=( difference_type n )
		{
			m_nIndex = static_cast<std::size_t>( static_cast<difference_type>( m_nIndex ) + n );
			return *this;
		}
		CIterator &operator-=( difference_type n ) { return *this += -n; }
		friend CIterator operator+( CIterator it, difference_type n ) { return it += n; }
		friend CIterator operator+( difference_type n, CIterator it ) { return it += n; }
		friend CIterator operator-( CIterator it, difference_type n ) { return it -= n; }
		friend difference_type operator-( const CIterator &a, const CIterator &b ) { return static_cast<difference_type>( a.m_nIndex ) - static_cast<difference_type>( b.m_nIndex ); }
		friend bool operator==( const CIterator &a, const CIterator &b ) { return a.m_nIndex == b.m_nIndex; }
		friend auto operator<=>( const CIterator &a, const CIterator &b ) { return a.m_nIndex <=> b.m_nIndex; }

	private:
		Owner_t *m_pOwner = nullptr;
		std::size_t m_nIndex = 0;
	};
	using iterator = CIterator<false>;
	using const_iterator = CIterator<true>;
	using value_type = T;
	using size_type = std::size_t;

	CSharedVector() = default;
	explicit CSharedVector( std::vector<T> values )
	{
		m_Items.reserve( values.size() );
		for ( T &value : values )
		{
			m_Items.push_back( std::make_shared<T>( std::move( value ) ) );
		}
	}

	[[nodiscard]] std::size_t size() const { return m_Items.size(); }
	[[nodiscard]] bool empty() const { return m_Items.empty(); }
	void reserve( std::size_t nCount ) { m_Items.reserve( nCount ); }
	void clear() { m_Items.clear(); }
	void resize( std::size_t nCount )
	{
		while ( m_Items.size() > nCount )
		{
			m_Items.pop_back();
		}
		while ( m_Items.size() < nCount )
		{
			m_Items.push_back( std::make_shared<T>() );
		}
	}

	[[nodiscard]] const T &operator[]( std::size_t i ) const { return *m_Items[ i ]; }
	// Copies the element first when another copy of the vector still holds it.
	[[nodiscard]] T &operator[]( std::size_t i )
	{
		std::shared_ptr<T> &pItem = m_Items[ i ];
		if ( pItem.use_count() > 1 )
		{
			pItem = std::make_shared<T>( *pItem );
		}
		return *pItem;
	}
	[[nodiscard]] const T &front() const { return ( *this )[ 0 ]; }
	[[nodiscard]] const T &back() const { return ( *this )[ size() - 1 ]; }
	[[nodiscard]] T &back() { return ( *this )[ size() - 1 ]; }

	void push_back( T value ) { m_Items.push_back( std::make_shared<T>( std::move( value ) ) ); }
	template <typename... Args>
	T &emplace_back( Args &&...args )
	{
		return *m_Items.emplace_back( std::make_shared<T>( std::forward<Args>( args )... ) );
	}
	void pop_back() { m_Items.pop_back(); }

	[[nodiscard]] const_iterator begin() const { return { this, 0 }; }
	[[nodiscard]] const_iterator end() const { return { this, size() }; }
	[[nodiscard]] iterator begin() { return { this, 0 }; }
	[[nodiscard]] iterator end() { return { this, size() }; }
	[[nodiscard]] const_iterator cbegin() const { return begin(); }
	[[nodiscard]] const_iterator cend() const { return end(); }

private:
	std::vector<std::shared_ptr<T>> m_Items;
};

} // namespace xv
