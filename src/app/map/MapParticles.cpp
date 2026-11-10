//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See MapParticles.h.
//
//===========================================================================//
#include "MapParticles.h"

#include <algorithm>
#include <format>
#include <set>

#include <QMetaObject>

#include "dmx/Dmx.h"
#include "kv1/Kv1.h"
#include "map/Sprites.h"
#include "particles/Particles.h"

namespace
{

// The most systems played at once: a preview, not the game.
constexpr std::size_t MAX_PLAYING = 256;
// The seconds a system is given to start with, so it shows as it looks once going.
constexpr float WARM_UP = 1.0f;

//-----------------------------------------------------------------------------
// Purpose: The control points an info_particle_system gives: its origin, then the origins of
//          the entities its cpoint1... keys name (the first of each name), as far as the
//          simulation has points.
//-----------------------------------------------------------------------------
std::vector<std::optional<xv::CVec3>> ControlPointsOf( const xv::map::CScene &scene, std::size_t nEntity )
{
	const auto &entities = scene.m_Map.m_Entities;
	const xv::map::Entity_t &entity = entities[ nEntity ];
	std::vector<std::optional<xv::CVec3>> points( xv::particles::CSimulation::CONTROL_POINTS );
	points[ 0 ] = xv::map::ToVec3( scene.m_Entities[ nEntity ].m_Origin );
	for ( int n = 1; n < xv::particles::CSimulation::CONTROL_POINTS; n++ )
	{
		const std::string_view szName = entity.GetValue( std::format( "cpoint{}", n ) );
		if ( szName.empty() )
			continue;
		for ( std::size_t e = 0; e < entities.size(); e++ )
		{
			if ( !entities[ e ].m_bDeleted && xv::kv1::EqualsIgnoreCase( entities[ e ].GetValue( "targetname" ), szName ) )
			{
				points[ static_cast<std::size_t>( n ) ] = xv::map::ToVec3( scene.m_Entities[ e ].m_Origin );
				break;
			}
		}
	}
	return points;
}

//-----------------------------------------------------------------------------
// Purpose: What starts dust over when it changes: its keys and the bounds of its brushes.
//-----------------------------------------------------------------------------
std::string DustKey( const xv::map::Entity_t &entity, const xv::map::DustEmitter_t &emitter )
{
	std::string szKey;
	for ( const xv::map::EntityKey_t &key : entity.m_Keys )
	{
		if ( !key.m_bDeleted )
			szKey += std::format( "{}={}|", key.m_szKey, key.m_szValue );
	}
	szKey += std::format( "{} {} {} {} {} {} {}", emitter.m_Bounds.m_Mins[ 0 ], emitter.m_Bounds.m_Mins[ 1 ], emitter.m_Bounds.m_Mins[ 2 ], emitter.m_Bounds.m_Maxs[ 0 ], emitter.m_Bounds.m_Maxs[ 1 ], emitter.m_Bounds.m_Maxs[ 2 ], emitter.m_Brushes.size() );
	return szKey;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapParticles::CMapParticles( QObject *pParent )
	: QObject( pParent )
{
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CMapParticles::~CMapParticles()
{
	Stop();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapParticles::Stop()
{
	m_bStop = true;
	if ( m_Worker.joinable() )
	{
		m_Worker.join();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Every file is read and compiled on the thread; the names and the mount come back to
//          the window's thread, which then plays what the scene asks for.
//-----------------------------------------------------------------------------
void CMapParticles::Start( std::optional<xv::vfs::CGameInfo> game, std::string szMapName )
{
	Stop();
	m_bStop = false;
	const int nGeneration = ++m_nGeneration;
	m_bReady = false;
	m_Systems.clear();
	m_Playing.clear();
	m_Sprites.clear();
	m_Dust.clear();
	m_DustSprite.reset();
	m_pDetailTypes.reset();
	m_szDetailFile.clear();
	m_szDetailMaterial.clear();
	m_pDetailDefinitions.reset();
	m_pDetailProps.reset();
	m_DetailSprite.reset();
	m_Missing.clear();
	m_pFileSystem.reset();
	m_pMaterials = std::make_shared<const std::vector<xv::map::RasterMaterial_t>>();
	if ( !game )
	{
		m_bReady = true;
		emit Ready();
		return;
	}
	m_Worker = std::thread( [ this, game = std::move( *game ), szMapName = std::move( szMapName ), nGeneration ] {
		auto pFileSystem = std::make_shared<xv::vfs::CFileSystem>();
		pFileSystem->Mount( game );
		std::map<std::string, Found_t, xv::particles::CLessNoCase> systems;
		std::set<std::string> read;
		auto readFile = [ & ]( const std::string &szFile ) -> std::shared_ptr<const xv::particles::CLibrary> {
			read.insert( szFile );
			const auto bytes = pFileSystem->Read( szFile );
			if ( !bytes )
				return nullptr;
			auto document = xv::dmx::Read( *bytes );
			if ( !document.m_Document || !xv::particles::IsParticleFile( *document.m_Document ) )
				return nullptr;
			return std::make_shared<const xv::particles::CLibrary>( xv::particles::Compile( *document.m_Document ) );
		};
		// The map's own manifest comes last: its systems stand in for the game's of the same name.
		for ( const std::string &szManifest : { std::string{ xv::particles::MANIFEST_PATH }, xv::particles::MapManifestPath( szMapName ) } )
		{
			const std::optional<std::string> text = pFileSystem->ReadText( szManifest );
			if ( !text )
				continue;
			for ( const std::string &szFile : xv::particles::ManifestFiles( xv::kv1::Parse( *text ).m_Document ) )
			{
				if ( m_bStop )
					return;
				if ( const auto pLibrary = readFile( szFile ) )
				{
					for ( std::size_t i = 0; i < pLibrary->m_Definitions.size(); i++ )
						systems.insert_or_assign( pLibrary->m_Definitions[ i ].m_szName, Found_t{ pLibrary, i, {} } );
				}
			}
		}
		// The game loads nothing else, but a map being made often plays files its manifest
		// doesn't list yet: they play too, and Check for Problems says what the game will miss.
		for ( const std::string &szFile : pFileSystem->Find( "particles", ".pcf", true ) )
		{
			if ( m_bStop )
				return;
			if ( read.contains( szFile ) )
				continue;
			if ( const auto pLibrary = readFile( szFile ) )
			{
				for ( std::size_t i = 0; i < pLibrary->m_Definitions.size(); i++ )
					systems.try_emplace( pLibrary->m_Definitions[ i ].m_szName, Found_t{ pLibrary, i, szFile } );
			}
		}
		QMetaObject::invokeMethod(
			this, [ this, nGeneration, systems = std::move( systems ), pFileSystem = std::move( pFileSystem ) ]() mutable {
				if ( nGeneration != m_nGeneration )
					return;
				m_Systems = std::move( systems );
				m_pFileSystem = std::move( pFileSystem );
				m_bReady = true;
				if ( m_pScene )
					SetScene( m_pScene );
				emit Ready();
			},
			Qt::QueuedConnection );
	} );
}

//-----------------------------------------------------------------------------
// Purpose: A system keeps playing while its entity names the same effect at the same control
//          points; anything else about it changing starts it over.
//-----------------------------------------------------------------------------
void CMapParticles::SetScene( std::shared_ptr<const xv::map::CScene> pScene )
{
	m_pScene = std::move( pScene );
	if ( !m_bReady || !m_pScene )
	{
		return;
	}
	std::vector<Playing_t> playing;
	std::vector<Dust_t> dust;
	m_Missing.clear();
	const auto &entities = m_pScene->m_Map.m_Entities;
	for ( std::size_t e = 0; e < entities.size() && playing.size() < MAX_PLAYING; e++ )
	{
		const xv::map::Entity_t &entity = entities[ e ];
		if ( dust.size() < MAX_PLAYING )
		{
			if ( std::optional<xv::map::DustEmitter_t> emitter = xv::map::ReadDustEmitter( *m_pScene, e ) )
			{
				const std::uint32_t nObject = m_pScene->EntityObject( e );
				std::string szKey = DustKey( entity, *emitter );
				const auto old = std::ranges::find_if( m_Dust, [ & ]( const Dust_t &d ) { return d.m_nObject == nObject && d.m_szKey == szKey; } );
				if ( old != m_Dust.end() )
				{
					dust.push_back( std::move( *old ) );
					continue;
				}
				const float flWarm = std::min( emitter->m_flLifetimeMax, 10.0f );
				auto pDust = std::make_unique<xv::map::CDust>( std::move( *emitter ), static_cast<std::uint32_t>( entity.m_nId ) );
				pDust->Advance( flWarm ); // filled, as it looks once going
				dust.push_back( { nObject, std::move( szKey ), std::move( pDust ) } );
				continue;
			}
		}
		if ( entity.m_bDeleted || !xv::kv1::EqualsIgnoreCase( entity.GetClassname(), "info_particle_system" ) )
			continue;
		const std::string szEffect{ entity.GetValue( "effect_name" ) };
		if ( szEffect.empty() )
			continue;
		const auto found = m_Systems.find( szEffect );
		if ( found == m_Systems.end() )
		{
			if ( std::ranges::find( m_Missing, szEffect ) == m_Missing.end() )
				m_Missing.push_back( szEffect );
			continue;
		}
		const std::vector<std::optional<xv::CVec3>> points = ControlPointsOf( *m_pScene, e );
		std::string szKey = szEffect;
		for ( const auto &point : points )
			szKey += point ? std::format( "|{} {} {}", point->m_flX, point->m_flY, point->m_flZ ) : std::string{ "|" };
		const std::uint32_t nObject = m_pScene->EntityObject( e );
		const auto old = std::ranges::find_if( m_Playing, [ & ]( const Playing_t &p ) { return p.m_nObject == nObject && p.m_szKey == szKey; } );
		if ( old != m_Playing.end() && old->m_pSimulation )
		{
			playing.push_back( std::move( *old ) );
			continue;
		}
		Playing_t system;
		system.m_nObject = nObject;
		system.m_szKey = std::move( szKey );
		system.m_pLibrary = found->second.m_pLibrary;
		system.m_pSimulation = std::make_unique<xv::particles::CSimulation>( found->second.m_pLibrary, found->second.m_nDefinition, static_cast<std::uint32_t>( entity.m_nId ) );
		for ( std::size_t n = 0; n < points.size(); n++ )
		{
			if ( points[ n ] )
				system.m_pSimulation->SetControlPoint( static_cast<int>( n ), *points[ n ] );
		}
		const xv::particles::CLibrary *pLibrary = found->second.m_pLibrary.get();
		system.m_pSimulation->SetSequenceFrames( [ this, pLibrary ]( std::size_t nDefinition, int nSequence ) {
			const xv::particles::Sprite_t *pSprite = SpriteFor( *pLibrary, nDefinition );
			const auto *pSequence = pSprite ? pSprite->m_Sheet.Sequence( nSequence ) : nullptr;
			return pSequence ? static_cast<int>( pSequence->m_Frames.size() ) : 0;
		} );
		system.m_pSimulation->SetViewer( *points[ 0 ] + xv::CVec3{ -200, 0, 60 } );
		system.m_pSimulation->Advance( WARM_UP );
		playing.push_back( std::move( system ) );
	}
	m_Playing = std::move( playing );
	m_Dust = std::move( dust );
	UpdateDetail();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::string> CMapParticles::GetSystemNames() const
{
	std::vector<std::string> names;
	names.reserve( m_Systems.size() );
	for ( const auto &[ szName, found ] : m_Systems )
		names.push_back( szName );
	return names;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CMapParticles::SetDetailTypes( std::shared_ptr<const std::vector<std::string>> pTypes )
{
	if ( pTypes == m_pDetailTypes )
		return;
	m_pDetailTypes = std::move( pTypes );
	UpdateDetail();
}

//-----------------------------------------------------------------------------
// Purpose: The worldspawn's detailvbsp and detailmaterial (vbsp's defaults without them) say
//          which file and sheet; the props are placed again from the scene as it is now.
//-----------------------------------------------------------------------------
void CMapParticles::UpdateDetail()
{
	if ( !m_bReady || !m_pScene )
		return;
	const xv::map::Entity_t &world = m_pScene->m_Map.m_World;
	std::string szFile{ world.GetValue( "detailvbsp" ) };
	if ( szFile.empty() )
		szFile = "detail.vbsp";
	std::string szMaterial{ world.GetValue( "detailmaterial" ) };
	if ( szMaterial.empty() )
		szMaterial = "detail/detailsprites";
	if ( !m_pDetailDefinitions || szFile != m_szDetailFile )
	{
		m_szDetailFile = szFile;
		const std::optional<std::string> text = m_pFileSystem ? m_pFileSystem->ReadText( szFile ) : std::nullopt;
		m_pDetailDefinitions = std::make_shared<const xv::map::DetailTypes_t>( text ? xv::map::ReadDetailTypes( xv::kv1::Parse( *text ).m_Document ) : xv::map::DetailTypes_t{} );
	}
	if ( szMaterial != m_szDetailMaterial )
	{
		m_szDetailMaterial = szMaterial;
		m_DetailSprite.reset();
	}
	const std::shared_ptr<const std::vector<std::string>> pTypes = m_pDetailTypes;
	m_pDetailProps = std::make_unique<xv::map::CDetailProps>( m_pDetailDefinitions, *m_pScene, [ &pTypes ]( std::uint32_t nMaterial ) {
		return pTypes && nMaterial < pTypes->size() ? ( *pTypes )[ nMaterial ] : std::string{};
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CMapParticles::Advance( float flSeconds )
{
	for ( Playing_t &system : m_Playing )
	{
		system.m_pSimulation->Advance( flSeconds );
		// Effects that end (a burst) go round again, as they would placed over and over.
		if ( system.m_pSimulation->GetFinished() )
			system.m_pSimulation->Restart();
	}
	for ( Dust_t &dust : m_Dust )
		dust.m_pDust->Advance( flSeconds );
	return IsPlaying();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::size_t CMapParticles::GetParticleCount() const
{
	std::size_t nCount = 0;
	for ( const Playing_t &system : m_Playing )
		nCount += system.m_pSimulation->GetParticleCount();
	for ( const Dust_t &dust : m_Dust )
		nCount += dust.m_pDust->GetCount();
	return nCount;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const xv::particles::CSimulation *CMapParticles::GetSimulation( std::uint32_t nObject ) const
{
	const auto found = std::ranges::find( m_Playing, nObject, &Playing_t::m_nObject );
	return found != m_Playing.end() ? found->m_pSimulation.get() : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<xv::map::ParticleSource_t> CMapParticles::FindSource( std::string_view szEffect ) const
{
	if ( !m_bReady || !m_pFileSystem )
		return std::nullopt;
	const auto found = m_Systems.find( std::string{ szEffect } );
	if ( found == m_Systems.end() )
		return xv::map::ParticleSource_t{};
	return xv::map::ParticleSource_t{ true, found->second.m_szUnlistedFile };
}

//-----------------------------------------------------------------------------
// Purpose: The sprite as the particle editor reads it (a soft dot when its material can't be).
//-----------------------------------------------------------------------------
const xv::particles::Sprite_t *CMapParticles::SpriteFor( const xv::particles::CLibrary &library, std::size_t nDefinition )
{
	if ( nDefinition >= library.m_Definitions.size() )
		return nullptr;
	const auto key = std::pair{ &library, nDefinition };
	if ( const auto found = m_Sprites.find( key ); found != m_Sprites.end() )
		return found->second.m_pSprite.get();
	const xv::vfs::CFileSystem *pFileSystem = m_pFileSystem.get();
	const xv::particles::FileReader_t read = [ pFileSystem ]( std::string_view szPath ) { return pFileSystem ? pFileSystem->Read( szPath ) : std::nullopt; };
	Sprite_t &sprite = m_Sprites[ key ];
	sprite.m_pSprite = std::make_unique<xv::particles::Sprite_t>( xv::particles::LoadSprite( library.m_Definitions[ nDefinition ].m_szMaterial, read ) );
	MakeMaterial( sprite );
	return sprite.m_pSprite.get();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const CMapParticles::Sprite_t &CMapParticles::DustSprite()
{
	if ( !m_DustSprite )
	{
		const xv::vfs::CFileSystem *pFileSystem = m_pFileSystem.get();
		const xv::particles::FileReader_t read = [ pFileSystem ]( std::string_view szPath ) { return pFileSystem ? pFileSystem->Read( szPath ) : std::nullopt; };
		m_DustSprite.emplace();
		m_DustSprite->m_pSprite = std::make_unique<xv::particles::Sprite_t>( xv::particles::LoadSprite( xv::map::DUST_MATERIAL, read ) );
		MakeMaterial( *m_DustSprite );
	}
	return *m_DustSprite;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const CMapParticles::Sprite_t &CMapParticles::DetailSprite()
{
	if ( !m_DetailSprite )
	{
		const xv::vfs::CFileSystem *pFileSystem = m_pFileSystem.get();
		const xv::particles::FileReader_t read = [ pFileSystem ]( std::string_view szPath ) { return pFileSystem ? pFileSystem->Read( szPath ) : std::nullopt; };
		m_DetailSprite.emplace();
		m_DetailSprite->m_pSprite = std::make_unique<xv::particles::Sprite_t>( xv::particles::LoadSprite( m_szDetailMaterial, read ) );
		MakeMaterial( *m_DetailSprite );
	}
	return *m_DetailSprite;
}

//-----------------------------------------------------------------------------
// Purpose: The sprite's texture, and a material of the map's kind for it: unlit, blended or
//          added, coloured by each particle.
//-----------------------------------------------------------------------------
void CMapParticles::MakeMaterial( Sprite_t &sprite )
{
	sprite.m_pTexture = std::make_shared<xv::map::RasterTexture_t>();
	sprite.m_pTexture->m_Mips = sprite.m_pSprite->m_Mips;
	if ( !sprite.m_pTexture->m_Mips.empty() )
	{
		sprite.m_pTexture->m_nWidth = sprite.m_pTexture->m_Mips.front().m_nWidth;
		sprite.m_pTexture->m_nHeight = sprite.m_pTexture->m_Mips.front().m_nHeight;
	}
	// Sheets' frames sit side by side: sampling past a frame's edge would show its neighbour.
	sprite.m_pTexture->m_bClampS = sprite.m_pTexture->m_bClampT = true;
	xv::map::RasterMaterial_t material;
	xv::map::ShadeMaterial_t &shade = material.m_Shade;
	shade.m_pBase = sprite.m_pTexture.get();
	shade.m_nWidth = sprite.m_pTexture->m_nWidth;
	shade.m_nHeight = sprite.m_pTexture->m_nHeight;
	shade.m_bUnlit = true;
	shade.m_bVertexColor = true;
	shade.m_bAdditive = sprite.m_pSprite->m_nBlend == xv::particles::Sprite_t::Blend_t::BLEND_ADDITIVE;
	shade.m_bTranslucent = !shade.m_bAdditive;
	// Blended as the particle editor blends: overbright past 1 and $addself still brighten.
	shade.m_bPremultiplied = true;
	shade.m_bAddOver = sprite.m_pSprite->m_nBlend == xv::particles::Sprite_t::Blend_t::BLEND_ADD_OVER;
	shade.m_flAddSelf = sprite.m_pSprite->m_flAddSelf;
	material.m_bNoCull = true;
	auto pMaterials = std::make_shared<std::vector<xv::map::RasterMaterial_t>>( *m_pMaterials );
	sprite.m_nMaterial = static_cast<std::uint32_t>( pMaterials->size() );
	pMaterials->push_back( material );
	m_pMaterials = std::move( pMaterials );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::shared_ptr<const xv::map::CMapMesh> CMapParticles::BuildMesh( xv::map::Point_t eye, xv::map::Point_t right, xv::map::Point_t up, std::uint32_t nFirstMaterial, const std::vector<std::uint32_t> *pOnly, bool bParticles, bool bDetail ) const
{
	const bool bDetailProps = bDetail && GetDetailPropCount() > 0;
	if ( !( bParticles && IsPlaying() ) && !bDetailProps )
	{
		return nullptr;
	}
	auto *pSelf = const_cast<CMapParticles *>( this ); // sprites are read when first drawn
	std::vector<xv::map::ParticleSprite_t> sprites;
	if ( bDetailProps )
	{
		const Sprite_t &sheet = pSelf->DetailSprite();
		const std::array<int, 2> size{ std::max( 1, sheet.m_pTexture->m_nWidth ), std::max( 1, sheet.m_pTexture->m_nHeight ) };
		m_pDetailProps->Layout( eye, right, up, xv::map::DetailView_t{}, nFirstMaterial + sheet.m_nMaterial, size, sprites );
	}
	if ( !bParticles )
	{
		return sprites.empty() ? nullptr : std::make_shared<const xv::map::CMapMesh>( xv::map::BuildParticleMesh( sprites ) );
	}
	if ( !m_Dust.empty() )
	{
		const Sprite_t &dust = pSelf->DustSprite();
		const std::array<int, 2> size{ std::max( 1, dust.m_pTexture->m_nWidth ), std::max( 1, dust.m_pTexture->m_nHeight ) };
		for ( const Dust_t &emitter : m_Dust )
		{
			if ( pOnly && !std::ranges::binary_search( *pOnly, emitter.m_nObject ) )
				continue;
			emitter.m_pDust->Layout( eye, right, up, nFirstMaterial + dust.m_nMaterial, size, emitter.m_nObject, sprites );
		}
	}
	for ( const Playing_t &system : m_Playing )
	{
		// The others play on unseen, so they are warm when shown again.
		if ( pOnly && !std::ranges::binary_search( *pOnly, system.m_nObject ) )
			continue;
		const xv::particles::CLibrary *pLibrary = system.m_pLibrary.get();
		const auto quads = xv::particles::WorldLayout( *system.m_pSimulation, xv::map::ToVec3( eye ), xv::map::ToVec3( right ), xv::map::ToVec3( up ), [ pSelf, pLibrary ]( std::size_t nDefinition ) { return pSelf->SpriteFor( *pLibrary, nDefinition ); } );
		for ( const xv::particles::WorldQuad_t &quad : quads )
		{
			const auto found = m_Sprites.find( { pLibrary, quad.m_nDefinition } );
			if ( found == m_Sprites.end() )
				continue;
			xv::map::ParticleSprite_t sprite;
			sprite.m_Corners = quad.m_Corners;
			sprite.m_Rect = quad.m_Rect;
			sprite.m_Color = { quad.m_vecColor[ 0 ], quad.m_vecColor[ 1 ], quad.m_vecColor[ 2 ], quad.m_flAlpha };
			sprite.m_nMaterial = nFirstMaterial + found->second.m_nMaterial;
			sprite.m_TextureSize = { std::max( 1, found->second.m_pTexture->m_nWidth ), std::max( 1, found->second.m_pTexture->m_nHeight ) };
			sprite.m_nObject = system.m_nObject;
			sprites.push_back( sprite );
		}
	}
	return sprites.empty() ? nullptr : std::make_shared<const xv::map::CMapMesh>( xv::map::BuildParticleMesh( sprites ) );
}
