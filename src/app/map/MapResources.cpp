//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapResources.h.
//
//===========================================================================//
#include "MapResources.h"

#include <algorithm>
#include <map>
#include <numeric>

#include "map/Materials.h"
#include "map/Overlay.h"
#include "map/Props.h"
#include "map/Sprites.h"
#include "map/Vmf.h"
#include "model/Model.h"
#include "texture/Vtf.h"
#include "util/Parallel.h"

namespace
{

constexpr std::size_t BATCH = 16;

// What a batch of materials draws: faces, sprites and overlays each blend their own way.
enum class MaterialUse_t
{
	MATERIAL_USE_FACES,
	MATERIAL_USE_SPRITES,
	MATERIAL_USE_OVERLAYS,
};

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
	m_Current.assign( nMaterials + 1, {} ); // and the place kept for the entity boxes'
	m_Keep.clear();
	m_pSky.reset();
	m_pProps.reset();
	m_PropObjects.clear();
	m_pSprites.reset();
	m_pOverlays.reset();
	m_nModels = 0;
	m_nModelsTotal = 0;
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

	// Each material's base texture size, for placing models' texture coordinates.
	const std::size_t nMaterials = pScene->m_Map.m_Materials.size();
	std::vector<std::array<int, 2>> sizes( nMaterials + 1, { 128, 128 } );
	std::vector<bool> drawable( nMaterials + 1, false );
	std::vector<bool> additive( nMaterials + 1, false ); // sprites' materials that add
	std::vector<double> decalScales( nMaterials + 1, 1.0 );
	auto load = [ & ]( const std::vector<std::pair<std::uint32_t, std::string>> &todo, MaterialUse_t nUse ) {
		const bool bSprites = nUse == MaterialUse_t::MATERIAL_USE_SPRITES;
		for ( std::size_t nStart = 0; nStart < todo.size() && !m_bStop; nStart += BATCH )
		{
			const std::size_t nEnd = std::min( todo.size(), nStart + BATCH );
			std::vector<Loaded_t> batch( nEnd - nStart );
			std::vector<std::optional<std::vector<std::byte>>> base( batch.size() );
			std::vector<std::optional<std::vector<std::byte>>> base2( batch.size() );
			for ( std::size_t i = 0; i < batch.size(); i++ )
			{
				Loaded_t &loaded = batch[ i ];
				const auto &[ nMaterial, szName ] = todo[ nStart + i ];
				loaded.m_nMaterial = nMaterial;
				loaded.m_bSprite = bSprites;
				loaded.m_bOverlay = nUse == MaterialUse_t::MATERIAL_USE_OVERLAYS;
				loaded.m_bAdditive = bSprites && nMaterial < additive.size() && additive[ nMaterial ];
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
			for ( const Loaded_t &loaded : batch )
			{
				if ( loaded.m_pBase && loaded.m_nMaterial < sizes.size() )
				{
					sizes[ loaded.m_nMaterial ] = { loaded.m_pBase->m_nWidth, loaded.m_pBase->m_nHeight };
					drawable[ loaded.m_nMaterial ] = !loaded.m_bMissing;
				}
				if ( loaded.m_nMaterial < decalScales.size() )
				{
					decalScales[ loaded.m_nMaterial ] = loaded.m_Info.m_flDecalScale;
				}
			}
			post( [ this, batch = std::move( batch ) ]() mutable { Take( std::move( batch ) ); } );
		}
	};

	// Most triangles first: what the views show most of arrives first.
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
	std::vector<std::pair<std::uint32_t, std::string>> todo;
	for ( const std::uint32_t n : order )
	{
		todo.emplace_back( n, pScene->m_Map.m_Materials[ n ] );
	}
	load( todo, MaterialUse_t::MATERIAL_USE_FACES );

	// The table keeps one place after the map's own materials for the entity boxes'; the
	// sprites' and the props' come after it.
	auto nNext = static_cast<std::uint32_t>( nMaterials + 1 );

	// Sprites next: few and small, and they show where the lights are. Their materials get
	// places of their own, as they draw unlit and tinted.
	std::vector<xv::map::SpritePlacement_t> sprites;
	std::vector<double> scales;
	std::map<std::string, std::uint32_t, std::less<>> spriteIndex;
	todo.clear();
	for ( std::size_t e = 0; e < pScene->m_Entities.size(); e++ )
	{
		const xv::map::EntityVisual_t &visual = pScene->m_Entities[ e ];
		if ( !visual.m_bPoint || visual.m_szSprite.empty() )
		{
			continue;
		}
		// A sprite that adds and one that blends need materials of their own.
		const std::string szMaterial = xv::map::NormalizeMaterial( visual.m_szSprite );
		const auto [ it, bAdded ] = spriteIndex.try_emplace( szMaterial + ( visual.m_bSpriteAdditive ? "+" : "" ), nNext );
		if ( bAdded )
		{
			additive.resize( nNext + 1, false );
			additive[ nNext ] = visual.m_bSpriteAdditive;
			todo.emplace_back( nNext++, szMaterial );
		}
		xv::map::SpritePlacement_t sprite;
		sprite.m_nObject = pScene->EntityObject( e );
		sprite.m_nMaterial = it->second;
		sprite.m_Origin = visual.m_Origin;
		sprite.m_Tint = { visual.m_SpriteTint.m_nR, visual.m_SpriteTint.m_nG, visual.m_SpriteTint.m_nB };
		sprites.push_back( sprite );
		scales.push_back( visual.m_flSpriteScale );
	}
	post( [ this, nMore = static_cast<int>( todo.size() ) ] { m_nTotal += nMore; } );
	sizes.resize( nNext, { 128, 128 } );
	drawable.resize( nNext, false );
	decalScales.resize( nNext, 1.0 );
	load( todo, MaterialUse_t::MATERIAL_USE_SPRITES );
	if ( !m_bStop && !sprites.empty() )
	{
		for ( std::size_t i = 0; i < sprites.size(); i++ )
		{
			xv::map::SpritePlacement_t &sprite = sprites[ i ];
			sprite.m_TextureSize = sizes[ sprite.m_nMaterial ];
			sprite.m_flHalfWidth = sprite.m_TextureSize[ 0 ] * scales[ i ] * 0.5;
			sprite.m_flHalfHeight = sprite.m_TextureSize[ 1 ] * scales[ i ] * 0.5;
		}
		// A sprite the game doesn't have leaves its entity's box.
		std::erase_if( sprites, [ & ]( const xv::map::SpritePlacement_t &sprite ) { return !drawable[ sprite.m_nMaterial ]; } );
		auto pSprites = std::make_shared<const std::vector<xv::map::SpritePlacement_t>>( std::move( sprites ) );
		post( [ this, pSprites ] {
			m_pSprites = pSprites;
			emit Arrived();
		} );
	}

	// Then the props: each model once, the materials of theirs the map doesn't use yet, and
	// all of them placed in one mesh.
	const xv::map::PropList_t list = xv::map::ListProps( *pScene );
	std::vector<xv::map::PropModel_t> models( list.m_Keys.size() );
	std::map<std::string, std::uint32_t, std::less<>> indexOf;
	for ( std::uint32_t n = 0; n < nMaterials; n++ )
	{
		indexOf.try_emplace( pScene->m_Map.m_Materials[ n ], n );
	}
	todo.clear();
	const xv::model::Reader_t readModel = [ &fileSystem ]( std::string_view szPath ) { return fileSystem.Read( szPath ); };
	int nModels = 0;
	for ( std::size_t k = 0; k < list.m_Keys.size() && !m_bStop; k++ )
	{
		xv::model::Selection_t selection;
		selection.m_nSkin = list.m_Keys[ k ].m_nSkin;
		std::optional<xv::model::CModel> model = xv::model::LoadModel( list.m_Keys[ k ].m_szModel, readModel, nullptr, selection );
		if ( !model )
		{
			continue;
		}
		nModels++;
		for ( std::string szMaterial : model->m_Materials )
		{
			if ( szMaterial.size() > 4 && szMaterial.ends_with( ".vmt" ) )
			{
				szMaterial.resize( szMaterial.size() - 4 );
			}
			szMaterial = xv::map::NormalizeMaterial( szMaterial );
			const auto [ it, bAdded ] = indexOf.try_emplace( szMaterial, nNext );
			if ( bAdded )
			{
				todo.emplace_back( nNext++, szMaterial );
			}
			models[ k ].m_Materials.push_back( it->second );
		}
		models[ k ].m_pModel = std::make_shared<const xv::model::CModel>( std::move( *model ) );
	}
	post( [ this, nMore = static_cast<int>( todo.size() ), nModels, nKeys = static_cast<int>( list.m_Keys.size() ) ] {
		m_nTotal += nMore;
		m_nModels = nModels;
		m_nModelsTotal = nKeys;
	} );
	sizes.resize( nNext, { 128, 128 } );
	drawable.resize( nNext, false );
	decalScales.resize( nNext, 1.0 );
	load( todo, MaterialUse_t::MATERIAL_USE_FACES );
	if ( !m_bStop && !list.m_Entities.empty() )
	{
		for ( xv::map::PropModel_t &prop : models )
		{
			for ( const std::uint32_t nMaterial : prop.m_Materials )
			{
				prop.m_TextureSizes.push_back( sizes[ nMaterial ] );
			}
		}
		std::vector<xv::map::PropPlacement_t> placements;
		std::vector<std::uint32_t> drawn;
		for ( const auto &[ nObject, nModel ] : list.m_Entities )
		{
			if ( !models[ nModel ].m_pModel )
			{
				continue;
			}
			const xv::map::EntityVisual_t &visual = pScene->m_Entities[ nObject - pScene->m_Solids.size() ];
			xv::map::PropPlacement_t placement;
			placement.m_nObject = nObject;
			placement.m_nModel = nModel;
			placement.m_Origin = visual.m_Origin;
			placement.m_Angles = visual.m_Angles;
			// lightprop draws a light's model aimed the way its pitch key says.
			placement.m_Angles[ 0 ] = visual.m_bInvertPitch ? -visual.m_Angles[ 0 ] : visual.m_Angles[ 0 ];
			placement.m_flScale = visual.m_flModelScale;
			placements.push_back( placement );
			drawn.push_back( nObject );
		}
		auto pProps = std::make_shared<const xv::map::CMapMesh>( xv::map::BuildPropMesh( models, placements ) );
		post( [ this, pProps, drawn = std::move( drawn ) ]() mutable {
			m_pProps = pProps;
			m_PropObjects = std::move( drawn );
			emit Arrived();
		} );
	}

	// Last the overlays and decals, which need their materials' sizes to be laid on the faces.
	const std::vector<std::string> overlayNames = xv::map::OverlayMaterialNames( *pScene );
	if ( !m_bStop && !overlayNames.empty() )
	{
		todo.clear();
		std::vector<xv::map::OverlayMaterial_t> overlayMaterials;
		for ( const std::string &szName : overlayNames )
		{
			overlayMaterials.push_back( { nNext, { 128, 128 }, 1.0 } );
			todo.emplace_back( nNext++, szName );
		}
		post( [ this, nMore = static_cast<int>( todo.size() ) ] { m_nTotal += nMore; } );
		sizes.resize( nNext, { 128, 128 } );
		drawable.resize( nNext, false );
		decalScales.resize( nNext, 1.0 );
		load( todo, MaterialUse_t::MATERIAL_USE_OVERLAYS );
		for ( xv::map::OverlayMaterial_t &material : overlayMaterials )
		{
			material.m_TextureSize = sizes[ material.m_nMaterial ];
			material.m_flDecalScale = decalScales[ material.m_nMaterial ];
		}
		auto pOverlays = std::make_shared<const xv::map::CMapMesh>( xv::map::BuildOverlayMesh( *pScene, overlayMaterials ) );
		post( [ this, pOverlays ] {
			m_pOverlays = pOverlays;
			emit Arrived();
		} );
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
			m_Current.resize( loaded.m_nMaterial + 1 );
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
		shade.m_bTinted = loaded.m_bSprite;
		shade.m_bUnlit = loaded.m_bSprite;
		if ( loaded.m_bOverlay && !shade.m_bAlphaTest && !shade.m_bAdditive )
		{
			// Overlays and decals blend by their alpha, as the game draws decals.
			shade.m_bTranslucent = true;
		}
		if ( loaded.m_bSprite )
		{
			// Hammer blends sprites whatever their material says.
			shade.m_bAdditive = loaded.m_bAdditive || info.m_bAdditive;
			shade.m_bTranslucent = !shade.m_bAdditive;
			shade.m_bAlphaTest = false;
		}
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
