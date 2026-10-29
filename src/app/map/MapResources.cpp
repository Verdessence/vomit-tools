//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapResources.h.
//
//===========================================================================//
#include "MapResources.h"
#include "Preferences.h"

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

//-----------------------------------------------------------------------------
// Purpose: Isolate one instance's brush geometry while retaining the other instances' buffers.
//-----------------------------------------------------------------------------
xv::map::CMapMesh ObjectMesh( const xv::map::CMapMesh &source, std::uint32_t nObject )
{
	xv::map::CMapMesh mesh;
	std::map<std::uint32_t, std::uint32_t> vertices;
	auto vertex = [ & ]( std::uint32_t n ) {
		const auto [ it, bAdded ] = vertices.try_emplace( n, static_cast<std::uint32_t>( mesh.m_Vertices.size() ) );
		if ( bAdded )
			mesh.m_Vertices.push_back( source.m_Vertices[ n ] );
		return it->second;
	};
	for ( const auto &batch : source.m_Batches )
	{
		const auto nFirst = static_cast<std::uint32_t>( mesh.m_Indices.size() );
		for ( std::uint32_t i = batch.m_nFirstIndex; i + 2 < batch.m_nFirstIndex + batch.m_nIndexCount; i += 3 )
			if ( source.m_Vertices[ source.m_Indices[ i ] ].m_nObject == nObject )
				for ( std::uint32_t k = 0; k < 3; ++k )
					mesh.m_Indices.push_back( vertex( source.m_Indices[ i + k ] ) );
		if ( mesh.m_Indices.size() > nFirst )
			mesh.m_Batches.push_back( { batch.m_nMaterial, ~0u, nFirst, static_cast<std::uint32_t>( mesh.m_Indices.size() ) - nFirst } );
	}
	for ( std::size_t i = 0; i + 1 < source.m_Lines.size(); i += 2 )
		if ( source.m_Vertices[ source.m_Lines[ i ] ].m_nObject == nObject )
		{
			mesh.m_Lines.push_back( vertex( source.m_Lines[ i ] ) );
			mesh.m_Lines.push_back( vertex( source.m_Lines[ i + 1 ] ) );
		}
	return mesh;
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
	m_RefreshChanged.clear();
	m_bRefreshing = false;
	m_Game = game;
	m_nMaxSize = nMaxSize;
	m_pSky.reset();
	m_pProps.reset();
	m_PropObjects.clear();
	m_pSprites.reset();
	m_pOverlays.reset();
	m_pPropParts.reset();
	m_pOverlayParts.reset();
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
	const auto nNext = static_cast<std::uint32_t>( nTable );

	// Sprites and props next (see PlaceEntities), then what was read for them is kept for edits.
	Placing_t placing;
	placing.m_nNext = nNext;
	placing.m_Sizes = std::move( sizes );
	placing.m_Drawable = std::move( drawable );
	placing.m_Additive = std::move( additive );
	placing.m_DecalScales = std::move( decalScales );
	const xv::model::Reader_t readModel = [ &fileSystem ]( std::string_view szPath ) { return fileSystem.Read( szPath ); };
	auto readMaterials = [ & ]( const std::vector<std::pair<std::uint32_t, std::string>> &todo, MaterialUse_t nUse ) {
		post( [ this, nMore = static_cast<int>( todo.size() ) ] { m_nTotal += nMore; } );
		decalScales = placing.m_DecalScales;
		decalScales.resize( std::max<std::size_t>( decalScales.size(), placing.m_nNext ), 1.0 );
		sizes = placing.m_Sizes;
		drawable = placing.m_Drawable;
		additive = placing.m_Additive;
		load( todo, nUse );
		placing.m_Sizes = sizes;
		placing.m_Drawable = drawable;
		placing.m_DecalScales = decalScales;
	};
	PlaceEntities( *pScene, placing, readMaterials, readModel, [ & ]( auto pfnCall ) { post( std::move( pfnCall ) ); } );
	PlaceOverlays( *pScene, placing, readMaterials, post );
	post( [ this, placing = std::move( placing ) ]() mutable { m_Placing = std::move( placing ); } );
	post( [ this ] {
		m_bFinished = true;
		emit Finished();
		if ( m_pRefreshNext )
		{
			Refresh( std::exchange( m_pRefreshNext, nullptr ), std::exchange( m_RefreshChanged, {} ) );
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
void CMapResources::PlaceEntities( const xv::map::CScene &scene, Placing_t &placing, const ReadMaterials_t &readMaterials, const xv::model::Reader_t &readModel, const Post_t &post, const std::vector<std::uint32_t> &changed )
{
	const auto includes = [ & ]( std::uint32_t nObject ) { return changed.empty() || std::ranges::find( changed, nObject ) != changed.end(); };
	bool bSpritesChanged = changed.empty(), bPropsChanged = changed.empty();
	if ( changed.empty() )
	{
		placing.m_SpritePlacements.clear();
		placing.m_PropParts.clear();
	}
	else
		for ( const auto nObject : changed )
		{
			bSpritesChanged |= placing.m_SpritePlacements.erase( nObject ) != 0;
			bPropsChanged |= placing.m_PropParts.erase( nObject ) != 0;
		}
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
			if ( !includes( nObject.value_or( inner.EntityObject( e ) ) ) || !visual.m_bPoint || visual.m_szSprite.empty() || inner.m_Map.m_Entities[ e ].m_bDeleted )
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
	for ( const auto &sprite : sprites )
	{
		placing.m_SpritePlacements[ sprite.m_nObject ].push_back( sprite );
		bSpritesChanged = true;
	}
	if ( bSpritesChanged )
	{
		sprites.clear();
		for ( const auto &[ nObject, placed ] : placing.m_SpritePlacements )
			sprites.insert( sprites.end(), placed.begin(), placed.end() );
		post( [ this, pSprites = std::make_shared<const std::vector<xv::map::SpritePlacement_t>>( std::move( sprites ) ) ] {
			m_pSprites = pSprites;
			emit Arrived();
		} );
	}

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
			if ( !includes( nObject.value_or( nInner ) ) )
				continue;
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
	std::map<std::uint32_t, xv::map::CMapMesh> rebuilt;
	for ( const auto &placement : placements )
		xv::map::AppendPlaced( rebuilt[ placement.m_nObject ], xv::map::BuildPropMesh( models, std::span{ &placement, 1 } ), xv::map::Transform_t{}, std::nullopt );
	for ( auto &[ nObject, mesh ] : rebuilt )
	{
		placing.m_PropParts[ nObject ] = std::make_shared<const xv::map::CMapMesh>( std::move( mesh ) );
		bPropsChanged = true;
	}
	if ( bPropsChanged )
	{
		auto pProps = std::make_shared<xv::map::CMapMesh>();
		drawn.clear();
		for ( const auto &[ nObject, pPart ] : placing.m_PropParts )
		{
			xv::map::AppendPlaced( *pProps, *pPart, xv::map::Transform_t{}, std::nullopt );
			drawn.push_back( nObject );
		}
		post( [ this, pProps, pParts = std::make_shared<const xv::map::MeshParts_t>( placing.m_PropParts ), drawn = std::move( drawn ), nModels, nKeys = static_cast<int>( placing.m_Models.size() ) ]() mutable {
			m_pProps = pProps;
			m_pPropParts = pParts;
			m_PropObjects = std::move( drawn );
			m_nModels = nModels;
			m_nModelsTotal = nKeys;
			emit Arrived();
		} );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Keep each projected entity's mesh independently so editing one does not reproject
//          the others. Overlay materials have their own table places because they blend.
//-----------------------------------------------------------------------------
void CMapResources::PlaceOverlays( const xv::map::CScene &scene, Placing_t &placing, const ReadMaterials_t &readMaterials, const Post_t &post, const std::vector<std::uint32_t> &changed )
{
	std::vector<std::pair<std::uint32_t, std::string>> todo;
	xv::map::ForEachInstance( scene, [ & ]( const xv::map::CScene &inner, const xv::map::Transform_t &, std::optional<std::uint32_t> ) {
		for ( const std::string &szName : xv::map::OverlayMaterialNames( inner ) )
		{
			const auto [ it, bAdded ] = placing.m_OverlayPlaces.try_emplace( szName, placing.m_nNext );
			if ( bAdded )
				todo.emplace_back( placing.m_nNext++, szName );
		}
	} );
	placing.m_Sizes.resize( placing.m_nNext, { 128, 128 } );
	placing.m_Drawable.resize( placing.m_nNext, false );
	placing.m_Additive.resize( placing.m_nNext, false );
	placing.m_DecalScales.resize( placing.m_nNext, 1.0 );
	if ( !todo.empty() )
		readMaterials( todo, MaterialUse_t::MATERIAL_USE_OVERLAYS );
	if ( m_bStop )
		return;

	const bool bAll = changed.empty() || std::ranges::any_of( changed, [ & ]( std::uint32_t nObject ) { return nObject < scene.GetObjectCount() && scene.IsSolidObject( nObject ); } );
	bool bChanged = bAll;
	if ( bAll )
		placing.m_OverlayParts.clear();
	else
		for ( const auto nObject : changed )
			bChanged |= placing.m_OverlayParts.erase( nObject ) != 0;
	std::map<std::uint32_t, xv::map::CMapMesh> rebuilt;
	xv::map::ForEachInstance( scene, [ & ]( const xv::map::CScene &inner, const xv::map::Transform_t &toScene, std::optional<std::uint32_t> nInstance ) {
		std::vector<xv::map::OverlayMaterial_t> materials;
		for ( const std::string &szName : xv::map::OverlayMaterialNames( inner ) )
		{
			const auto nPlace = placing.m_OverlayPlaces.at( szName );
			materials.push_back( { nPlace, placing.m_Sizes[ nPlace ], placing.m_DecalScales[ nPlace ] } );
		}
		std::vector<std::uint32_t> objects;
		for ( std::size_t e = 0; e < inner.m_Entities.size() && !m_bStop; e++ )
		{
			const auto &visual = inner.m_Entities[ e ];
			const auto nObject = nInstance.value_or( inner.EntityObject( e ) );
			if ( ( !visual.m_bOverlay && !visual.m_bDecal ) || inner.m_Map.m_Entities[ e ].m_bDeleted ||
				( !bAll && std::ranges::find( changed, nObject ) == changed.end() ) )
				continue;
			objects.push_back( inner.EntityObject( e ) );
		}
		for ( const auto &[ nInner, mesh ] : xv::map::BuildOverlayMeshes( inner, materials, objects ) )
			xv::map::AppendPlaced( rebuilt[ nInstance.value_or( nInner ) ], mesh, toScene, nInstance );
	} );
	if ( !bChanged && rebuilt.empty() )
		return;
	for ( auto &[ nObject, mesh ] : rebuilt )
		placing.m_OverlayParts[ nObject ] = std::make_shared<const xv::map::CMapMesh>( std::move( mesh ) );
	auto pOverlays = std::make_shared<xv::map::CMapMesh>();
	for ( const auto &[ nObject, pPart ] : placing.m_OverlayParts )
		xv::map::AppendPlaced( *pOverlays, *pPart, xv::map::Transform_t{}, std::nullopt );
	post( [ this, pOverlays, pParts = std::make_shared<const xv::map::MeshParts_t>( placing.m_OverlayParts ) ] {
		m_pOverlays = pOverlays;
		m_pOverlayParts = pParts;
		emit Arrived();
	} );
}

//-----------------------------------------------------------------------------
// Purpose: Project and place only the entities under the mouse, using assets already loaded.
//          Their material table places are stable for the lifetime of the map.
//-----------------------------------------------------------------------------
xv::map::CMapMesh CMapResources::BuildEntityPreview( const xv::map::CScene &scene, const std::vector<std::uint32_t> &objects, xv::map::Point_t right, xv::map::Point_t up, const xv::map::CScene *pBase ) const
{
	xv::map::CMapMesh mesh;
	std::vector<std::uint32_t> projected;
	for ( const auto nObject : objects )
	{
		if ( nObject >= scene.GetObjectCount() || scene.IsSolidObject( nObject ) || scene.IsDeleted( nObject ) )
			continue;
		const auto &visual = scene.m_Entities[ scene.GetIndex( nObject ) ];
		if ( pBase && ( visual.m_bInstance || std::ranges::any_of( pBase->m_Instances, [ & ]( const auto &placed ) { return placed.m_nObject == nObject; } ) ) )
		{
			const auto &before = pBase->m_Entities[ pBase->GetIndex( nObject ) ];
			const auto old = xv::map::Transform_t::FromAngles( before.m_Origin, before.m_Angles );
			xv::map::Transform_t inverse;
			for ( int r = 0; r < 3; ++r )
				for ( int c = 0; c < 3; ++c )
					inverse.m_Rotation[ r * 3 + c ] = old.m_Rotation[ c * 3 + r ];
			inverse.m_Offset = inverse.Rotate( xv::map::Scale( before.m_Origin, -1 ) );
			const auto transform = xv::map::Transform_t::FromAngles( visual.m_Origin, visual.m_Angles ) * inverse;
			xv::map::AppendPlaced( mesh, ObjectMesh( pBase->GetInstanceMesh(), nObject ), transform, nObject );
			for ( const auto *pParts : { &m_Placing.m_PropParts, &m_Placing.m_OverlayParts } )
				if ( const auto found = pParts->find( nObject ); found != pParts->end() )
					xv::map::AppendPlaced( mesh, *found->second, transform, nObject );
			if ( const auto found = m_Placing.m_SpritePlacements.find( nObject ); found != m_Placing.m_SpritePlacements.end() )
			{
				auto sprites = found->second;
				for ( auto &sprite : sprites )
					sprite.m_Origin = transform.Apply( sprite.m_Origin );
				xv::map::AppendPlaced( mesh, xv::map::BuildSpriteMesh( sprites, right, up ), xv::map::Transform_t{}, std::nullopt );
			}
			continue;
		}

		if ( visual.m_bOverlay || visual.m_bDecal )
			projected.push_back( nObject );
		if ( !visual.m_szModel.empty() )
		{
			xv::map::PropKey_t key{ visual.m_szModel, visual.m_nSkin };
			std::ranges::transform( key.m_szModel, key.m_szModel.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
			if ( const auto found = m_Placing.m_Models.find( key ); found != m_Placing.m_Models.end() && found->second.m_pModel )
			{
				auto angles = visual.m_Angles;
				angles[ 0 ] = visual.m_bInvertPitch ? -angles[ 0 ] : angles[ 0 ];
				const xv::map::PropPlacement_t placement{ nObject, 0, xv::map::Transform_t::FromAngles( visual.m_Origin, angles, visual.m_flModelScale ) };
				xv::map::AppendPlaced( mesh, xv::map::BuildPropMesh( std::span{ &found->second, 1 }, std::span{ &placement, 1 } ), xv::map::Transform_t{}, std::nullopt );
			}
		}
		if ( CPreferences::Instance().GetMapPreferences().m_bSprites )
			if ( const auto found = m_Placing.m_SpritePlacements.find( nObject ); found != m_Placing.m_SpritePlacements.end() )
			{
				auto sprites = found->second;
				for ( auto &sprite : sprites )
					sprite.m_Origin = visual.m_Origin;
				xv::map::AppendPlaced( mesh, xv::map::BuildSpriteMesh( sprites, right, up ), xv::map::Transform_t{}, std::nullopt );
			}
	}
	if ( !projected.empty() )
	{
		std::vector<xv::map::OverlayMaterial_t> materials;
		for ( const auto &szName : xv::map::OverlayMaterialNames( scene ) )
		{
			const auto found = m_Placing.m_OverlayPlaces.find( szName );
			const auto nPlace = found == m_Placing.m_OverlayPlaces.end() ? m_nEntityMaterial : found->second;
			materials.push_back( { nPlace, nPlace < m_Placing.m_Sizes.size() ? m_Placing.m_Sizes[ nPlace ] : std::array<int, 2>{ 128, 128 }, nPlace < m_Placing.m_DecalScales.size() ? m_Placing.m_DecalScales[ nPlace ] : 1.0 } );
		}
		for ( const auto &[ nObject, part ] : xv::map::BuildOverlayMeshes( scene, materials, projected ) )
			xv::map::AppendPlaced( mesh, part, xv::map::Transform_t{}, std::nullopt );
	}
	return mesh;
}

//-----------------------------------------------------------------------------
// Purpose: After edits: the sprites and props placed again on a thread of their own, reading only
//          models and materials not read before. One at a time; the newest scene asked for goes
//          next.
//-----------------------------------------------------------------------------
void CMapResources::Refresh( std::shared_ptr<const xv::map::CScene> pScene, std::vector<std::uint32_t> changed )
{
	if ( !pScene || !m_Game )
	{
		return;
	}
	if ( !m_bFinished || m_bRefreshing )
	{
		if ( !m_pRefreshNext )
			m_RefreshChanged = std::move( changed );
		else if ( changed.empty() || m_RefreshChanged.empty() )
			m_RefreshChanged.clear();
		else
		{
			m_RefreshChanged.insert( m_RefreshChanged.end(), changed.begin(), changed.end() );
			std::ranges::sort( m_RefreshChanged );
			m_RefreshChanged.erase( std::unique( m_RefreshChanged.begin(), m_RefreshChanged.end() ), m_RefreshChanged.end() );
		}
		m_pRefreshNext = std::move( pScene );
		return;
	}
	// A class change can replace a prop or sprite with an overlay, so old placements count too.
	const bool bPlaceEntities = changed.empty() || std::ranges::any_of( changed, [ & ]( std::uint32_t nObject ) {
		if ( std::ranges::find( m_PropObjects, nObject ) != m_PropObjects.end() ||
			( m_pSprites && std::ranges::any_of( *m_pSprites, [ & ]( const auto &sprite ) { return sprite.m_nObject == nObject; } ) ) )
			return true;
		if ( nObject >= pScene->GetObjectCount() || pScene->IsSolidObject( nObject ) )
			return false;
		const auto &visual = pScene->m_Entities[ pScene->GetIndex( nObject ) ];
		return !visual.m_bOverlay && !visual.m_bDecal;
	} );
	m_bRefreshing = true;
	const int nGeneration = m_nGeneration;
	m_Extras.emplace_back( [ this, pScene = std::move( pScene ), changed = std::move( changed ), bPlaceEntities, placing = m_Placing, game = *m_Game, nMaxSize = m_nMaxSize, bDecoded = m_bDecoded, nGeneration ]() mutable {
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
			placing.m_DecalScales.resize( placing.m_nNext, 1.0 );
			for ( const Loaded_t &loaded : batch )
			{
				placing.m_DecalScales[ loaded.m_nMaterial ] = loaded.m_Info.m_flDecalScale;
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
		if ( bPlaceEntities )
			PlaceEntities( *pScene, placing, readMaterials, read, post, changed );
		PlaceOverlays( *pScene, placing, readMaterials, post, changed );
		post( [ this, placing = std::move( placing ) ]() mutable {
			m_Placing = std::move( placing );
			m_bRefreshing = false;
			if ( m_pRefreshNext )
			{
				Refresh( std::exchange( m_pRefreshNext, nullptr ), std::exchange( m_RefreshChanged, {} ) );
			}
			else
				emit Arrived();
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
