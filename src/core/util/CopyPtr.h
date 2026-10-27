//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: An owning pointer that copies what it points to when it is copied: for parts most
//          objects don't have (a side's displacement), so the objects stay small and can still
//          be copied whole (undo keeps copies, duplicating makes them).
//
//===========================================================================//
#pragma once

#include <memory>

namespace xv
{

template <typename T>
class CCopyPtr
{
public:
	CCopyPtr() = default;
	CCopyPtr( std::unique_ptr<T> pValue )
		: m_pValue( std::move( pValue ) )
	{
	}
	CCopyPtr( const CCopyPtr &other )
		: m_pValue( other.m_pValue ? std::make_unique<T>( *other.m_pValue ) : nullptr )
	{
	}
	CCopyPtr( CCopyPtr && ) noexcept = default;
	CCopyPtr &operator=( const CCopyPtr &other )
	{
		if ( this != &other )
		{
			m_pValue = other.m_pValue ? std::make_unique<T>( *other.m_pValue ) : nullptr;
		}
		return *this;
	}
	CCopyPtr &operator=( CCopyPtr && ) noexcept = default;
	CCopyPtr &operator=( std::unique_ptr<T> pValue )
	{
		m_pValue = std::move( pValue );
		return *this;
	}

	[[nodiscard]] explicit operator bool() const { return m_pValue != nullptr; }
	[[nodiscard]] T &operator*() const { return *m_pValue; }
	[[nodiscard]] T *operator->() const { return m_pValue.get(); }
	[[nodiscard]] T *get() const { return m_pValue.get(); }
	void reset() { m_pValue.reset(); }

private:
	std::unique_ptr<T> m_pValue;
};

} // namespace xv
