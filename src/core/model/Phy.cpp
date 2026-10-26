//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
// Purpose: Bounds-checked collision hull reading using the local VDC PHY format facts.
//===========================================================================//
#include "Phy.h"
#include "Binary.h"
#include <set>
namespace xv::model
{
//-----------------------------------------------------------------------------
// Purpose: Traverses each compact surface's ledge tree to find the convex pieces it describes.
//-----------------------------------------------------------------------------
std::optional<Physics_t> ReadPhysics( std::span<const std::byte> bytes, std::string *pError )
{
	try
	{
		CBinary r( bytes );
		r.Check( 0, 16 );
		Physics_t out;
		out.m_nChecksum = r.Int( 12 );
		out.m_nSolids = r.Int( 8 );
		if ( out.m_nSolids < 0 || out.m_nSolids > 4096 || r.Int( 0 ) < 16 )
			throw std::runtime_error( "Invalid PHY header" );
		std::size_t nAt = r.Int( 0 );
		std::size_t nTotalVertices = 0, nTotalIndices = 0;
		for ( int i = 0; i < out.m_nSolids; i++ )
		{
			const int nLength = r.Int( nAt );
			if ( nLength < 76 )
				throw std::runtime_error( "Invalid PHY solid length" );
			r.Check( nAt + 4, nLength );
			const bool bCompact = r.Int( nAt + 4 ) == 0x59485056;
			if ( bCompact && r.Read<std::uint16_t>( nAt + 10 ) != 0 )
				throw std::runtime_error( "Unsupported PHY surface type" );
			const std::size_t nPrefix = bCompact ? 32 : 4;
			const auto data = bytes.subspan( nAt + nPrefix, static_cast<std::size_t>( nLength ) + 4 - nPrefix );
			CBinary solid( data );
			solid.Check( 0, 48 );
			const auto nRoot = solid.Relative( 0, 32 );
			std::vector<std::size_t> pending{ nRoot };
			std::set<std::size_t> visited, ledges;
			for ( std::size_t j = 0; j < pending.size(); j++ )
			{
				const auto nNode = pending[ j ];
				solid.Check( nNode, 28 );
				if ( !visited.insert( nNode ).second || visited.size() > 65536 )
					throw std::runtime_error( "Cyclic or oversized PHY ledge tree" );
				if ( solid.Int( nNode ) )
				{
					pending.push_back( nNode + 28 );
					pending.push_back( solid.Relative( nNode, nNode ) );
					continue;
				}
				const auto nLedge = solid.Relative( nNode, nNode + 4 );
				solid.Check( nLedge, 16 );
				if ( !ledges.insert( nLedge ).second )
					continue;
				const int nTriangles = solid.Read<std::uint16_t>( nLedge + 12 );
				solid.Array( nLedge + 16, nTriangles, 16 );
				Convex_t convex;
				convex.m_nSolid = i;
				const int nBone = solid.Int( nLedge + 4 );
				if ( nBone < 0 || nBone > 1024 )
					throw std::runtime_error( "Invalid PHY bone index" );
				convex.m_nBone = nBone - 1;
				std::uint32_t nVertices = 0;
				for ( int t = 0; t < nTriangles; t++ )
					for ( int e = 0; e < 3; e++ )
					{
						const auto nIndex = solid.Read<std::uint16_t>( nLedge + 16 + t * 16 + 4 + e * 4 );
						convex.m_Indices.push_back( nIndex );
						nVertices = std::max( nVertices, static_cast<std::uint32_t>( nIndex ) + 1 );
					}
				nTotalVertices += nVertices;
				nTotalIndices += convex.m_Indices.size();
				if ( nTotalVertices > 2000000 || nTotalIndices > 6000000 )
					throw std::runtime_error( "PHY geometry exceeds the viewer limit" );
				const auto nVertexAt = solid.Relative( nLedge, nLedge );
				solid.Array( nVertexAt, nVertices, 16 );
				for ( std::uint32_t v = 0; v < nVertices; v++ )
				{
					const auto p = solid.Vector( nVertexAt + v * 16 );
					convex.m_Vertices.push_back( CVec3{ p.m_flX, p.m_flZ, -p.m_flY } * ( 1.0f / 0.0254f ) );
				}
				out.m_Convexes.push_back( std::move( convex ) );
			}
			nAt += static_cast<std::size_t>( nLength ) + 4;
		}
		if ( nAt < bytes.size() )
			out.m_szText = r.Text( nAt );
		return out;
	}
	catch ( const std::exception &e )
	{
		if ( pError )
			*pError = e.what();
		return std::nullopt;
	}
}
} // namespace xv::model
