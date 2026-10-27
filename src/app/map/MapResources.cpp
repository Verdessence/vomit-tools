//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapResources.h.
//
//===========================================================================//
#include "MapResources.h"

#include <algorithm>
#include <numeric>

#include "map/Materials.h"
#include "texture/Vtf.h"
#include "util/Parallel.h"

namespace
{

constexpr std::size_t BATCH = 16;

//-----------------------------------------------------------------------------
// Purpose: A tool material: what Hammer++'s tool texture toggle hides (the sky stays).
//-----------------------------------------------------------------------------
bool IsToolMaterial( const xv::map::MaterialInfo_t &info, std::string_view szName )
{
	return info.IsTool() || ( szName.starts_with( "tools/" ) && !info.IsSky() );
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::shared_ptr<xv::map::RasterTexture_t> ReadMapTexture( std::span<const std::byte> vtf, int nMaxSize )
{
	const auto file = xv::texture::ReadVtf( vtf );
	if ( !file || file->m_Info.m_nWidth <= 0 || file->m_Info.m_nHeight <= 0 )
	{
		return nullptr;
	}
	auto pTexture = std::make_shared<xv::map::RasterTexture_t>();
	pTexture->m_nWidth = file->m_Info.m_nWidth;
	pTexture->m_nHeight = file->m_Info.m_nHeight;
	pTexture->m_bClampS = ( file->m_Info.m_nFlags & xv::texture::vtf_flag::CLAMPS ) != 0;
	pTexture->m_bClampT = ( file->m_Info.m_nFlags & xv::texture::vtf_flag::CLAMPT ) != 0;
	// The first mip that fits, then every smaller one the file has.
	int nFirst = 0;
	while ( nFirst + 1 < file->m_Info.m_nMipCount && std::max( file->m_Info.m_nWidth >> nFirst, file->m_Info.m_nHeight >> nFirst ) > nMaxSize )
	{
		nFirst++;
	}
	for ( int nMip = nFirst; nMip < file->m_Info.m_nMipCount; nMip++ )
	{
		std::optional<xv::texture::CImage> image = file->Decode( nMip );
		if ( !image || image->Empty() )
		{
			break;
		}
		pTexture->m_Mips.push_back( std::move( *image ) );
	}
	return pTexture->m_Mips.empty() ? nullptr : pTexture;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapResources::CMapResources( QObject *pParent )
	: QObject( pParent )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapResources::~CMapResources()
{
	Stop();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapResources::Stop()
{
	m_bStop = true;
	if ( m_Worker.joinable() )
	{
		m_Worker.join();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapResources::Start( std::shared_ptr<const xv::map::CScene> pScene, std::optional<xv::vfs::CGameInfo> game, int nMaxSize )
{
	Stop();
	m_bStop = false;
	const int nGeneration = ++m_nGeneration;
	const std::size_t nMaterials = pScene ? pScene->m_Map.m_Materials.size() : 0;
	m_Current.assign( nMaterials, {} );
	m_Keep.clear();
	m_pSky.reset();
	m_nLoaded = 0;
	m_nTotal = game ? static_cast<int>( nMaterials ) : 0;
	m_bFinished = !game || !pScene;
	m_pMaterials = std::make_shared<const std::vector<xv::map::RasterMaterial_t>>( m_Current );
	m_pTextures = std::make_shared<const std::vector<std::shared_ptr<xv::map::RasterTexture_t>>>( m_Keep );
	emit Arrived();
	if ( m_bFinished )
	{
		emit Finished();
		return;
	}
	m_Worker = std::thread( [ this, pScene = std::move( pScene ), game = std::move( *game ), nMaxSize, nGeneration ] { Run( pScene, game, nMaxSize, nGeneration ); } );
}

//-----------------------------------------------------------------------------
// Purpose: The worker: mounts the game for itself, then reads materials a batch at a time,
//          reading files one by one and decoding the batch's textures on every core.
//-----------------------------------------------------------------------------
void CMapResources::Run( std::shared_ptr<const xv::map::CScene> pScene, xv::vfs::CGameInfo game, int nMaxSize, int nGeneration )
{
	const auto post = [ this, nGeneration ]( auto pfnCall ) {
		QMetaObject::invokeMethod(
			this, [ this, nGeneration, pfnCall = std::move( pfnCall ) ]() mutable {
				if ( nGeneration == m_nGeneration )
				{
					pfnCall();
				}
			},
			Qt::QueuedConnection );
	};
	xv::vfs::CFileSystem fileSystem;
	fileSystem.Mount( game );
	const xv::material::Reader_t read = [ &fileSystem ]( std::string_view szPath ) { return fileSystem.Read( szPath ); };

	// The sky first: it fills the most of the screen.
	if ( const std::string_view szSky = pScene->m_Map.m_World.GetValue( "skyname" ); !szSky.empty() )
	{
		xv::sky::LoadedSky_t sky = xv::sky::LoadSky( fileSystem, szSky );
		auto pCube = std::make_shared<const xv::sky::CCube>( std::move( sky.m_Cube ) );
		post( [ this, pCube ] {
			m_pSky = pCube;
			emit Arrived();
		} );
	}

	// Most triangles first: what the views show most of arrives first.
	const std::size_t nMaterials = pScene->m_Map.m_Materials.size();
	std::vector<std::size_t> triangles( nMaterials, 0 );
	for ( const xv::map::MeshBatch_t &batch : pScene->m_Mesh.m_Batches )
	{
		if ( batch.m_nMaterial < nMaterials )
		{
			triangles[ batch.m_nMaterial ] += batch.m_nIndexCount / 3;
		}
	}
	std::vector<std::uint32_t> order( nMaterials );
	std::iota( order.begin(), order.end(), 0u );
	std::ranges::stable_sort( order, std::greater<>{}, [ & ]( std::uint32_t n ) { return triangles[ n ]; } );

	for ( std::size_t nStart = 0; nStart < order.size() && !m_bStop; nStart += BATCH )
	{
		const std::size_t nEnd = std::min( order.size(), nStart + BATCH );
		std::vector<Loaded_t> batch( nEnd - nStart );
		std::vector<std::optional<std::vector<std::byte>>> base( batch.size() );
		std::vector<std::optional<std::vector<std::byte>>> base2( batch.size() );
		for ( std::size_t i = 0; i < batch.size(); i++ )
		{
			Loaded_t &loaded = batch[ i ];
			loaded.m_nMaterial = order[ nStart + i ];
			const std::string &szName = pScene->m_Map.m_Materials[ loaded.m_nMaterial ];
			loaded.m_Info = xv::map::ReadMaterialInfo( szName, read );
			loaded.m_bTool = IsToolMaterial( loaded.m_Info, szName );
			if ( !loaded.m_Info.m_szTexture.empty() )
			{
				base[ i ] = read( xv::material::TexturePath( loaded.m_Info.m_szTexture ) );
			}
			if ( loaded.m_Info.m_bBlend && !loaded.m_Info.m_szTexture2.empty() )
			{
				base2[ i ] = read( xv::material::TexturePath( loaded.m_Info.m_szTexture2 ) );
			}
		}
		xv::ParallelFor( batch.size(), 1, [ & ]( std::size_t nBegin, std::size_t nFinish ) {
			for ( std::size_t i = nBegin; i < nFinish; i++ )
			{
				if ( base[ i ] )
				{
					batch[ i ].m_pBase = ReadMapTexture( *base[ i ], nMaxSize );
				}
				if ( base2[ i ] )
				{
					batch[ i ].m_pBase2 = ReadMapTexture( *base2[ i ], nMaxSize );
				}
				// Source draws its checker for a material, or a texture, it can't find.
				batch[ i ].m_bMissing = !batch[ i ].m_Info.m_bFound || ( !batch[ i ].m_Info.m_szTexture.empty() && !batch[ i ].m_pBase );
			}
		} );
		post( [ this, batch = std::move( batch ) ]() mutable { Take( std::move( batch ) ); } );
	}
	post( [ this ] {
		m_bFinished = true;
		emit Finished();
	} );
}

//-----------------------------------------------------------------------------
// Purpose: A batch arrived: the materials are drawn with it from the next frame.
//-----------------------------------------------------------------------------
void CMapResources::Take( std::vector<Loaded_t> batch )
{
	for ( Loaded_t &loaded : batch )
	{
		if ( loaded.m_nMaterial >= m_Current.size() )
		{
			continue;
		}
		const xv::map::MaterialInfo_t &info = loaded.m_Info;
		xv::map::RasterMaterial_t material;
		xv::map::ShadeMaterial_t &shade = material.m_Shade;
		shade.m_pBase = loaded.m_pBase.get();
		shade.m_pBase2 = loaded.m_pBase2.get();
		shade.m_bMissing = loaded.m_bMissing;
		shade.m_bBlend = info.m_bBlend && loaded.m_pBase2;
		shade.m_bAlphaTest = info.m_bAlphaTest;
		shade.m_bTranslucent = info.m_bTranslucent;
		shade.m_bAdditive = info.m_bAdditive;
		shade.m_flAlphaReference = info.m_flAlphaReference;
		shade.m_flAlpha = info.m_flAlpha;
		shade.m_vecColor = info.m_vecColor;
		material.m_bNoCull = info.m_bNoCull;
		shade.m_bSky = info.IsSky();
		material.m_bTool = loaded.m_bTool;
		m_Current[ loaded.m_nMaterial ] = material;
		if ( loaded.m_pBase )
		{
			m_Keep.push_back( loaded.m_pBase );
		}
		if ( loaded.m_pBase2 )
		{
			m_Keep.push_back( loaded.m_pBase2 );
		}
		m_nLoaded++;
	}
	m_pMaterials = std::make_shared<const std::vector<xv::map::RasterMaterial_t>>( m_Current );
	m_pTextures = std::make_shared<const std::vector<std::shared_ptr<xv::map::RasterTexture_t>>>( m_Keep );
	emit Arrived();
}
