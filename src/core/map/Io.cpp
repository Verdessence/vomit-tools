//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Io.h.
//
//===========================================================================//
#include "Io.h"

#include <vector>

#include "util/Numbers.h"

namespace xv::map
{

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Connection_t ParseConnection( std::string_view szOutput, std::string_view szValue )
{
	Connection_t connection;
	connection.m_szOutput = szOutput;
	connection.m_chSeparator = szValue.find( '\x1b' ) != std::string_view::npos ? '\x1b' : ',';

	// Where each field starts and ends.
	std::vector<std::pair<std::size_t, std::size_t>> fields;
	std::size_t nStart = 0;
	for ( std::size_t i = 0; i <= szValue.size(); i++ )
	{
		if ( i == szValue.size() || szValue[ i ] == connection.m_chSeparator )
		{
			fields.emplace_back( nStart, i );
			nStart = i + 1;
		}
	}
	auto field = [ & ]( std::size_t nIndex ) {
		return szValue.substr( fields[ nIndex ].first, fields[ nIndex ].second - fields[ nIndex ].first );
	};
	if ( fields.size() < 4 )
	{
		connection.m_bMalformed = true;
		connection.m_szTarget = fields.size() > 0 ? field( 0 ) : std::string_view{};
		connection.m_szInput = fields.size() > 1 ? field( 1 ) : std::string_view{};
		connection.m_szParameter = fields.size() > 2 ? field( 2 ) : std::string_view{};
		return connection;
	}
	connection.m_szTarget = field( 0 );
	connection.m_szInput = field( 1 );
	const std::size_t nDelay = fields.size() == 4 ? 3 : fields.size() - 2;
	connection.m_szParameter = szValue.substr( fields[ 2 ].first, fields[ nDelay - 1 ].second - fields[ 2 ].first );
	if ( const auto flDelay = ParseNumber<float>( field( nDelay ) ) )
	{
		connection.m_flDelay = *flDelay;
	}
	else
	{
		connection.m_bMalformed = true;
	}
	if ( fields.size() == 4 )
	{
		connection.m_nTimes = 1;
	}
	else if ( const auto nTimes = ParseNumber<int>( field( fields.size() - 1 ) ) )
	{
		connection.m_nTimes = *nTimes;
	}
	else
	{
		connection.m_bMalformed = true;
	}
	return connection;
}

} // namespace xv::map
