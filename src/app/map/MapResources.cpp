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
#include <utility>

#include "map/Instance.h"
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
std::shared_ptr<xv::map::RasterTexture_t> ReadMapTexture( std::span<const std::byte> vtf, int nMaxSize, bool bDecode )
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
	const xv::texture::VtfFormat_t nFormat = file->m_Info.m_nFormat;
	const bool bCompressed = nFormat == xv::texture::VtfFormat_t::DXT1 || nFormat == xv::texture::VtfFormat_t::DXT1_ONEBITALPHA || nFormat == xv::texture::VtfFormat_t::DXT3 || nFormat == xv::texture::VtfFormat_t::DXT5;
	for ( int nMip = nFirst; nMip < file->m_Info.m_nMipCount; nMip++ )
	{
		const int nWidth = std::max( 1, file->m_Info.m_nWidth >> nMip );
		const int nHeight = std::max( 1, file->m_Info.m_nHeight >> nMip );
		if ( bCompressed )
		{
			const std::span<const std::byte> data = file->Image( nMip );
			if ( data.size() < xv::texture::ImageSize( nFormat, nWidth, nHeight ) )
			{
				break;
			}
			pTexture->m_Compressed.push_back( { nWidth, nHeight, std::vector<std::byte>( data.begin(), data.end() ) } );
			pTexture->m_nCompressedFormat = nFormat;
		}
		if ( bDecode || !bCompressed )
		{
			std::optional<xv::texture::CImage> image = file->Decode( nMip );
			if ( !image || image->Empty() )
			{
				break;
			}
			pTexture->m_Mips.push_back( std::move( *image ) );
		}
	}
	// Both sets go down the same mips, or the card's would say one thing and software another.
	if ( !pTexture->m_Mips.empty() && !pTexture->m_Compressed.empty() && pTexture->m_Mips.size() != pTexture->m_Compressed.size() )
	{
		pTexture->m_Compressed.resize( std::min( pTexture->m_Mips.size(), pTexture->m_Compressed.size() ) );
		pTexture->m_Mips.resize( pTexture->m_Compressed.size() );
	}
	return pTexture->GetMipCount() == 0 ? nullptr : pTexture;
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
	for ( std::thread &extra : m_Extras )
	{
		extra.join();
	}
	m_Extras.clear();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapResources::Start( std::shared_ptr<const xv::map::CScene> pScene, std::optional<xv::vfs::CGameInfo> game, int nMaxSize, bool bDecoded )
{
	Stop();
	m_bStop = false;
	m_bDecoded = bDecoded;
	const int nGeneration = ++m_nGeneration;
	const std::size_t nMaterials = pScene ? pScene->m_Materials.size() : 0;
	m_nEntityMaterial = static_cast<std::uint32_t>( nMaterials ) + MAP_MATERIAL_RESERVE;
	m_Current.assign( m_nEntityMaterial + 1, {} ); // the map's, the places kept for new ones, the entity boxes'
	m_Keep.clear();
	m_Placing = {};
	m_pRefreshNext.reset();
	m_bRefreshing = false;
	m_Game = game;
	m_nMaxSize = nMaxSize;
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
	m_Worker = std::thread( [ this, pScene = std::move( pScene ), game = std::move( *game ), nMaxSize, bDecoded, nGeneration ] { Run( pScene, game, nMaxSize, bDecoded, nGeneration ); } );
}

//-----------------------------------------------------------------------------
// Purpose: The worker: mounts the game for itself, then reads materials a batch at a time,
//          reading files one by one and decoding the batch's textures on every core.
//-----------------------------------------------------------------------------
void CMapResources::Run( std::shared_ptr<const xv::map::CScene> pScene, xv::vfs::CGameInfo game, int nMaxSize, bool bDecoded, int nGeneration )
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
	const std::size_t nMaterials = pScene->m_Materials.size();
	const std::size_t nTable = static_cast<std::size_t>( nMaterials ) + MAP_MATERIAL_RESERVE + 1;
	std::vector<std::array<int, 2>> sizes( nTable, { 128, 128 } );
	std::vector<bool> drawable( nTable, false );
	std::vector<bool> additive( nTable, false ); // sprites' materials that add
	std::vector<double> decalScales( nTable, 1.0 );
	auto load = [ & ]( const std::vector<std::pair<std::uint32_t, std::string>> &todo, MaterialUse_t nUse ) {
		for ( std::size_t nStart = 0; nStart < todo.size() && !m_bStop; nStart += BATCH )
		{
			const std::vector<std::pair<std::uint32_t, std::string>> part( todo.begin() + static_cast<std::ptrdiff_t>( nStart ), todo.begin() + static_cast<std::ptrdiff_t>( std::min( todo.size(), nStart + BATCH ) ) );
			std::vector<Loaded_t> batch = ReadMaterials( part, nUse, read, nMaxSize, bDecoded, additive );
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
	for ( const xv::map::CMapMesh *pMesh : { &pScene->GetMesh(), &pScene->GetInstanceMesh() } )
	{
		for ( const xv::map::MeshBatch_t &batch : pMesh->m_Batches )
		{
			if ( batch.m_nMaterial < nMaterials )
			{
				triangles[ batch.m_nMaterial ] += batch.m_nIndexCount / 3;
			}
		}
	}
	std::vector<std::uint32_t> order( nMaterials );
	std::iota( order.begin(), order.end(), 0u );
	std::ranges::stable_sort( order, std::greater<>{}, [ & ]( std::uint32_t n ) { return triangles[ n ]; } );
	std::vector<std::pair<std::uint32_t, std::string>> todo;
	for ( const std::uint32_t n : order )
	{
		todo.emplace_back( n, pScene->m_Materials[ n ] );
	}
	load( todo, MaterialUse_t::MATERIAL_USE_FACES );

	// The table keeps places after the map's own materials for those edits bring in, then one
	// for the entity boxes'; the sprites' and the props' come after it.
	auto nNext = static_cast<std::uint32_t>( nTable );

	// Sprites and props next (see PlaceEntities), then what was read for them is kept for edits.
	Placing_t placing;
	placing.m_nNext = nNext;
	placing.m_Sizes = std::move( sizes );
	placing.m_Drawable = std::move( drawable );
	placing.m_Additive = std::move( additive );
	const xv::model::Reader_t readModel = [ &fileSystem ]( std::string_view szPath ) { return fileSystem.Read( szPath ); };
	auto readMaterials = [ & ]( const std::vector<std::pair<std::uint32_t, std::string>> &todo, MaterialUse_t nUse ) {
		post( [ this, nMore = static_cast<int>( todo.size() ) ] { m_nTotal += nMore; } );
		decalScales.resize( std::max<std::size_t>( decalScales.size(), placing.m_nNext ), 1.0 );
		sizes = placing.m_Sizes;
		drawable = placing.m_Drawable;
		additive = placing.m_Additive;
		load( todo, nUse );
		placing.m_Sizes = sizes;
		placing.m_Drawable = drawable;
	};
	PlaceEntities( *pScene, placing, readMaterials, readModel, [ & ]( auto pfnCall ) { post( std::move( pfnCall ) ); } );
	nNext = placing.m_nNext;
	sizes = placing.m_Sizes;
	drawable = placing.m_Drawable;
	decalScales.resize( std::max<std::size_t>( decalScales.size(), nNext ), 1.0 );
	post( [ this, placing ]() mutable { m_Placing = std::move( placing ); } );

	// Last the overlays and decals, which need their materials' sizes to be laid on the faces.
	std::vector<std::string> overlayNames;
	xv::map::ForEachInstance( *pScene, [ & ]( const xv::map::CScene &inner, const xv::map::Transform_t &, std::optional<std::uint32_t> ) {
		for ( std::string &szName : xv::map::OverlayMaterialNames( inner ) )
		{
			if ( std::ranges::find( overlayNames, szName ) == overlayNames.end() )
			{
				overlayNames.push_back( std::move( szName ) );
			}
		}
	} );
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
		auto pOverlays = std::make_shared<xv::map::CMapMesh>();
		xv::map::ForEachInstance( *pScene, [ & ]( const xv::map::CScene &inner, const xv::map::Transform_t &toScene, std::optional<std::uint32_t> nObject ) {
			std::vector<xv::map::OverlayMaterial_t> innerMaterials;
			for ( const std::string &szName : xv::map::OverlayMaterialNames( inner ) )
			{
				innerMaterials.push_back( overlayMaterials[ static_cast<std::size_t>( std::ranges::find( overlayNames, szName ) - overlayNames.begin() ) ] );
			}
			xv::map::AppendPlaced( *pOverlays, xv::map::BuildOverlayMesh( inner, innerMaterials ), toScene, nObject );
		} );
		post( [ this, pOverlays = std::shared_ptr<const xv::map::CMapMesh>( std::move( pOverlays ) ) ] {
			m_pOverlays = pOverlays;
			emit Arrived();
		} );
	}
	post( [ this ] {
		m_bFinished = true;
		emit Finished();
		if ( m_pRefreshNext )
		{
			Refresh( std::exchange( m_pRefreshNext, nullptr ) );
		}
	} );
}

//-----------------------------------------------------------------------------
// Purpose: The entities' sprites (few and small, and they show where the lights are) and props.
//          Sprites' materials get places of their own, as they draw unlit and tinted; a model's
//          materials take the map's places where the map uses them too. Only what placing hasn't
//          read yet is read: models through readModel, materials through readMaterials (which
//          updates placing's sizes). Instances' sprites and props are drawn as their
//          func_instance. The results go to the window through post.
//-----------------------------------------------------------------------------
void CMapResources::PlaceEntities( const xv::map::CScene &scene, Placing_t &placing, const ReadMaterials_t &readMaterials, const xv::model::Reader_t &readModel, const Post_t &post )
{
	auto grow = [ &placing ]( std::uint32_t nPlace ) {
		placing.m_Sizes.resize( std::max<std::size_t>( placing.m_Sizes.size(), nPlace + 1 ), { 128, 128 } );
		placing.m_Drawable.resize( std::max<std::size_t>( placing.m_Drawable.size(), nPlace + 1 ), false );
		placing.m_Additive.resize( std::max<std::size_t>( placing.m_Additive.size(), nPlace + 1 ), false );
	};
	std::vector<xv::map::SpritePlacement_t> sprites;
	std::vector<double> scales;
	std::vector<std::pair<std::uint32_t, std::string>> todo;
	xv::map::ForEachInstance( scene, [ & ]( const xv::map::CScene &inner, const xv::map::Transform_t &toScene, std::optional<std::uint32_t> nObject ) {
		for ( std::size_t e = 0; e < inner.m_Entities.size(); e++ )
		{
			const xv::map::EntityVisual_t &visual = inner.m_Entities[ e ];
			if ( !visual.m_bPoint || visual.m_szSprite.empty() || inner.m_Map.m_Entities[ e ].m_bDeleted )
			{
				continue;
			}
			// A sprite that adds and one that blends need materials of their own.
			const std::string szMaterial = xv::map::NormalizeMaterial( visual.m_szSprite );
			const auto [ it, bAdded ] = placing.m_Places.try_emplace( szMaterial + ( visual.m_bSpriteAdditive ? "+" : "" ), placing.m_nNext );
			if ( bAdded )
			{
				grow( placing.m_nNext );
				placing.m_Additive[ placing.m_nNext ] = visual.m_bSpriteAdditive;
				todo.emplace_back( placing.m_nNext++, szMaterial );
			}
			xv::map::SpritePlacement_t sprite;
			sprite.m_nObject = nObject.value_or( inner.EntityObject( e ) );
			sprite.m_nMaterial = it->second;
			sprite.m_Origin = toScene.Apply( visual.m_Origin );
			sprite.m_Tint = { visual.m_SpriteTint.m_nR, visual.m_SpriteTint.m_nG, visual.m_SpriteTint.m_nB };
			sprites.push_back( sprite );
			scales.push_back( visual.m_flSpriteScale );
		}
	} );
	if ( !todo.empty() )
	{
		readMaterials( todo, MaterialUse_t::MATERIAL_USE_SPRITES );
	}
	if ( m_bStop )
	{
		return;
	}
	for ( std::size_t i = 0; i < sprites.size(); i++ )
	{
		xv::map::SpritePlacement_t &sprite = sprites[ i ];
		sprite.m_TextureSize = placing.m_Sizes[ sprite.m_nMaterial ];
		sprite.m_flHalfWidth = sprite.m_TextureSize[ 0 ] * scales[ i ] * 0.5;
		sprite.m_flHalfHeight = sprite.m_TextureSize[ 1 ] * scales[ i ] * 0.5;
	}
	// A sprite the game doesn't have leaves its entity's box.
	std::erase_if( sprites, [ & ]( const xv::map::SpritePlacement_t &sprite ) { return !placing.m_Drawable[ sprite.m_nMaterial ]; } );
	post( [ this, pSprites = std::make_shared<const std::vector<xv::map::SpritePlacement_t>>( std::move( sprites ) ) ] {
		m_pSprites = pSprites;
		emit Arrived();
	} );

	// The props: each model once, and all of them placed in one mesh.
	struct PropUse_t
	{
		std::uint32_t m_nObject = 0;
		xv::map::PropKey_t m_Key;
		xv::map::Transform_t m_Transform;
	};
	std::vector<PropUse_t> uses;
	xv::map::ForEachInstance( scene, [ & ]( const xv::map::CScene &inner, const xv::map::Transform_t &toScene, std::optional<std::uint32_t> nObject ) {
		const xv::map::PropList_t list = xv::map::ListProps( inner );
		for ( const auto &[ nInner, nKey ] : list.m_Entities )
		{
			const xv::map::EntityVisual_t &visual = inner.m_Entities[ inner.GetIndex( nInner ) ];
			std::array<double, 3> angles = visual.m_Angles;
			// lightprop draws a light's model aimed the way its pitch key says.
			angles[ 0 ] = visual.m_bInvertPitch ? -angles[ 0 ] : angles[ 0 ];
			uses.push_back( { nObject.value_or( nInner ), list.m_Keys[ nKey ], toScene * xv::map::Transform_t::FromAngles( visual.m_Origin, angles, visual.m_flModelScale ) } );
		}
	} );
	std::map<std::string, std::uint32_t, std::less<>> indexOf;
	for ( std::uint32_t n = 0; n < scene.m_Materials.size(); n++ )
	{
		indexOf.try_emplace( scene.m_Materials[ n ], n );
	}
	todo.clear();
	std::vector<xv::map::PropKey_t> fresh;
	for ( const PropUse_t &use : uses )
	{
		if ( m_bStop || placing.m_Models.contains( use.m_Key ) )
		{
			continue;
		}
		xv::map::PropModel_t &prop = placing.m_Models[ use.m_Key ];
		xv::model::Selection_t selection;
		selection.m_nSkin = use.m_Key.m_nSkin;
		std::optional<xv::model::CModel> model = xv::model::LoadModel( use.m_Key.m_szModel, readModel, nullptr, selection );
		if ( !model )
		{
			continue; // remembered as not there
		}
		for ( std::string szMaterial : model->m_Materials )
		{
			if ( szMaterial.size() > 4 && szMaterial.ends_with( ".vmt" ) )
			{
				szMaterial.resize( szMaterial.size() - 4 );
			}
			szMaterial = xv::map::NormalizeMaterial( szMaterial );
			std::uint32_t nPlace = 0;
			if ( const auto it = indexOf.find( szMaterial ); it != indexOf.end() )
			{
				nPlace = it->second;
			}
			else
			{
				const auto [ placed, bAdded ] = placing.m_Places.try_emplace( szMaterial, placing.m_nNext );
				if ( bAdded )
				{
					grow( placing.m_nNext );
					todo.emplace_back( placing.m_nNext++, szMaterial );
				}
				nPlace = placed->second;
			}
			prop.m_Materials.push_back( nPlace );
		}
		prop.m_pModel = std::make_shared<const xv::model::CModel>( std::move( *model ) );
		fresh.push_back( use.m_Key );
	}
	if ( !todo.empty() )
	{
		readMaterials( todo, MaterialUse_t::MATERIAL_USE_FACES );
	}
	if ( m_bStop )
	{
		return;
	}
	for ( const xv::map::PropKey_t &key : fresh )
	{
		xv::map::PropModel_t &prop = placing.m_Models[ key ];
		for ( const std::uint32_t nMaterial : prop.m_Materials )
		{
			prop.m_TextureSizes.push_back( nMaterial < placing.m_Sizes.size() ? placing.m_Sizes[ nMaterial ] : std::array<int, 2>{ 128, 128 } );
		}
	}
	std::vector<xv::map::PropModel_t> models;
	std::map<xv::map::PropKey_t, std::uint32_t> modelIndex;
	std::vector<xv::map::PropPlacement_t> placements;
	std::vector<std::uint32_t> drawn;
	int nModels = 0;
	for ( const auto &[ key, prop ] : placing.m_Models )
	{
		nModels += prop.m_pModel ? 1 : 0;
	}
	for ( const PropUse_t &use : uses )
	{
		const xv::map::PropModel_t &prop = placing.m_Models[ use.m_Key ];
		if ( !prop.m_pModel )
		{
			continue;
		}
		const auto [ it, bAdded ] = modelIndex.try_emplace( use.m_Key, static_cast<std::uint32_t>( models.size() ) );
		if ( bAdded )
		{
			models.push_back( prop );
		}
		placements.push_back( { use.m_nObject, it->second, use.m_Transform } );
		drawn.push_back( use.m_nObject );
	}
	auto pProps = std::make_shared<const xv::map::CMapMesh>( xv::map::BuildPropMesh( models, placements ) );
	post( [ this, pProps, drawn = std::move( drawn ), nModels, nKeys = static_cast<int>( placing.m_Models.size() ) ]() mutable {
		m_pProps = pProps;
		m_PropObjects = std::move( drawn );
		m_nModels = nModels;
		m_nModelsTotal = nKeys;
		emit Arrived();
	} );
}

//-----------------------------------------------------------------------------
// Purpose: After edits: the sprites and props placed again on a thread of their own, reading only
//          models and materials not read before. One at a time; the newest scene asked for goes
//          next.
//-----------------------------------------------------------------------------
void CMapResources::Refresh( std::shared_ptr<const xv::map::CScene> pScene )
{
	if ( !pScene || !m_Game )
	{
		return;
	}
	if ( !m_bFinished || m_bRefreshing )
	{
		m_pRefreshNext = std::move( pScene );
		return;
	}
	m_bRefreshing = true;
	const int nGeneration = m_nGeneration;
	m_Extras.emplace_back( [ this, pScene = std::move( pScene ), placing = m_Placing, game = *m_Game, nMaxSize = m_nMaxSize, bDecoded = m_bDecoded, nGeneration ]() mutable {
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
		// The game is mounted only when something has to be read.
		std::unique_ptr<xv::vfs::CFileSystem> pFileSystem;
		auto fileSystem = [ & ]() -> xv::vfs::CFileSystem & {
			if ( !pFileSystem )
			{
				pFileSystem = std::make_unique<xv::vfs::CFileSystem>();
				pFileSystem->Mount( game );
			}
			return *pFileSystem;
		};
		const xv::material::Reader_t read = [ & ]( std::string_view szPath ) { return fileSystem().Read( szPath ); };
		auto readMaterials = [ & ]( const std::vector<std::pair<std::uint32_t, std::string>> &todo, MaterialUse_t nUse ) {
			std::vector<Loaded_t> batch = ReadMaterials( todo, nUse, read, nMaxSize, bDecoded, placing.m_Additive );
			for ( const Loaded_t &loaded : batch )
			{
				if ( loaded.m_pBase && loaded.m_nMaterial < placing.m_Sizes.size() )
				{
					placing.m_Sizes[ loaded.m_nMaterial ] = { loaded.m_pBase->m_nWidth, loaded.m_pBase->m_nHeight };
					placing.m_Drawable[ loaded.m_nMaterial ] = !loaded.m_bMissing;
				}
			}
			post( [ this, nMore = static_cast<int>( todo.size() ), batch = std::move( batch ) ]() mutable {
				m_nTotal += nMore;
				Take( std::move( batch ) );
			} );
		};
		PlaceEntities( *pScene, placing, readMaterials, read, post );
		post( [ this, placing = std::move( placing ) ]() mutable {
			m_Placing = std::move( placing );
			m_bRefreshing = false;
			if ( m_pRefreshNext )
			{
				Refresh( std::exchange( m_pRefreshNext, nullptr ) );
			}
		} );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: Each material's VMT and textures, the files read one by one, the textures decoded on
//          every core.
//-----------------------------------------------------------------------------
std::vector<CMapResources::Loaded_t> CMapResources::ReadMaterials( const std::vector<std::pair<std::uint32_t, std::string>> &todo, MaterialUse_t nUse, const xv::material::Reader_t &read, int nMaxSize, bool bDecoded, const std::vector<bool> &additive )
{
	const bool bSprites = nUse == MaterialUse_t::MATERIAL_USE_SPRITES;
	std::vector<Loaded_t> batch( todo.size() );
	std::vector<std::optional<std::vector<std::byte>>> base( batch.size() );
	std::vector<std::optional<std::vector<std::byte>>> base2( batch.size() );
	for ( std::size_t i = 0; i < batch.size(); i++ )
	{
		Loaded_t &loaded = batch[ i ];
		const auto &[ nMaterial, szName ] = todo[ i ];
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
				batch[ i ].m_pBase = ReadMapTexture( *base[ i ], nMaxSize, bDecoded );
			}
			if ( base2[ i ] )
			{
				batch[ i ].m_pBase2 = ReadMapTexture( *base2[ i ], nMaxSize, bDecoded );
			}
			// Source draws its checker for a material, or a texture, it can't find.
			batch[ i ].m_bMissing = !batch[ i ].m_Info.m_bFound || ( !batch[ i ].m_Info.m_szTexture.empty() && !batch[ i ].m_pBase );
		}
	} );
	return batch;
}

//-----------------------------------------------------------------------------
// Purpose: On a thread of its own, with its own mount of the game: a few materials, put in their
//          places as the main reading does.
//-----------------------------------------------------------------------------
bool CMapResources::LoadMaterials( std::shared_ptr<const xv::map::CScene> pScene, const std::vector<std::uint32_t> &materials )
{
	if ( !pScene || !m_Game )
	{
		return true;
	}
	std::vector<std::pair<std::uint32_t, std::string>> todo;
	for ( const std::uint32_t n : materials )
	{
		if ( n >= m_nEntityMaterial || n >= pScene->m_Materials.size() )
		{
			return false;
		}
		todo.emplace_back( n, pScene->m_Materials[ n ] );
	}
	if ( todo.empty() )
	{
		return true;
	}
	m_nTotal += static_cast<int>( todo.size() );
	const int nGeneration = m_nGeneration;
	m_Extras.emplace_back( [ this, todo = std::move( todo ), game = *m_Game, nMaxSize = m_nMaxSize, bDecoded = m_bDecoded, nGeneration ] {
		xv::vfs::CFileSystem fileSystem;
		fileSystem.Mount( game );
		const xv::material::Reader_t read = [ &fileSystem ]( std::string_view szPath ) { return fileSystem.Read( szPath ); };
		std::vector<Loaded_t> batch = ReadMaterials( todo, MaterialUse_t::MATERIAL_USE_FACES, read, nMaxSize, bDecoded, {} );
		QMetaObject::invokeMethod(
			this, [ this, nGeneration, batch = std::move( batch ) ]() mutable {
				if ( nGeneration == m_nGeneration )
				{
					Take( std::move( batch ) );
				}
			},
			Qt::QueuedConnection );
	} );
	return true;
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
