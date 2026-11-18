//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Select, animate and draw the weapon in the demo's first-person view.
//
//===========================================================================//
#include "DemoWorld.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <numbers>

#include "DemoViewer.h"
#include "ImageFiles.h"
#include "map/MapResources.h"
#include "map/Props.h"
#include "map/Raster.h"
#include "vfs/Vfs.h"

namespace
{
//-----------------------------------------------------------------------------
// Purpose: The CS:S first-person counterpart of a held weapon model.
//-----------------------------------------------------------------------------
std::string ViewModelName( std::string_view szModel )
{
	std::string szPath( szModel );
	std::ranges::transform( szPath, szPath.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	const auto nSlash = szPath.find_last_of( '/' );
	const auto nName = nSlash == std::string::npos ? 0 : nSlash + 1;
	if ( szPath.compare( nName, 2, "w_" ) == 0 )
		szPath[ nName ] = 'v';
	if ( !szPath.ends_with( ".mdl" ) || szPath.compare( nName, 2, "v_" ) != 0 )
		return {};
	if ( szPath.ends_with( "_dropped.mdl" ) )
		szPath.replace( szPath.size() - 12, 12, ".mdl" );
	return szPath;
}

//-----------------------------------------------------------------------------
// Purpose: The last key at the playhead, including the start of a constant animation value.
//-----------------------------------------------------------------------------
template <typename Keys_t>
int LastKeyTick( const Keys_t &keys, int nTick, int nDefault )
{
	const auto after = std::upper_bound( keys.begin(), keys.end(), nTick, []( int nAt, const auto &key ) { return nAt < key.m_nTick; } );
	return after == keys.begin() ? nDefault : ( after - 1 )->m_nTick;
}
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Load only the view models this demo can use and index recorded animation cues.
//-----------------------------------------------------------------------------
void CDemoWorld::IndexViewModels( std::set<std::pair<std::string, int>> &templates, std::set<std::string> &studios )
{
	m_ViewModels.clear();
	m_WeaponActions.clear();
	const auto &contents = *m_Viewer.GetContents();
	std::set<std::pair<std::string, int>> models;
	for ( const auto &[ szWorld, nSkin ] : templates )
	{
		if ( szWorld.starts_with( "models/weapons/w_" ) )
		{
			if ( const auto szView = ViewModelName( szWorld ); !szView.empty() )
				models.emplace( szView, 0 );
		}
	}
	for ( const auto &life : contents.GetLives() )
	{
		const auto szClass = contents.ClassName( life.m_nClass );
		if ( szClass != "CPredictedViewModel" && szClass != "CBaseViewModel" )
			continue;
		m_ViewModels.push_back( &life );
		std::set<int> skins{ 0 };
		for ( const auto &key : life.m_Fields[ xv::demo::ENTITY_FIELD_EXTRA_1 ] )
			skins.insert( static_cast<int>( key.m_flValue[ 0 ] ) );
		for ( const auto &key : life.m_Fields[ xv::demo::ENTITY_FIELD_MODEL ] )
		{
			if ( const auto szView = ViewModelName( contents.ModelName( static_cast<int>( key.m_flValue[ 0 ] ) ) ); !szView.empty() )
			{
				for ( const int nSkin : skins )
					models.emplace( szView, nSkin );
			}
		}
	}
	for ( const auto &[ szModel, nSkin ] : models )
		studios.insert( szModel );
	templates.merge( models );

	const int nFire = contents.FindEventKind( "weapon_fire" );
	const int nReload = contents.FindEventKind( "weapon_reload" );
	for ( const auto &event : contents.GetEvents() )
	{
		if ( event.m_nKind < 0 || ( event.m_nKind != nFire && event.m_nKind != nReload ) )
			continue;
		if ( const auto *pUser = contents.EventValue( event, "userid" ); pUser && pUser->m_flNumber > 0 )
		{
			m_WeaponActions[ static_cast<int>( pUser->m_flNumber ) ].emplace_back( event.m_nTick,
				event.m_nKind == nFire ? ViewModelAction_t::VIEW_MODEL_ACTION_FIRE : ViewModelAction_t::VIEW_MODEL_ACTION_RELOAD );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Read the recorder's pose when available. Reconstruct the other players' animation
//          from weapon ownership, recorded actions and clip changes at the playhead.
//-----------------------------------------------------------------------------
CDemoWorld::ViewModelPose_t CDemoWorld::ViewModelAt( int nPlayer, double flTick ) const
{
	ViewModelPose_t pose;
	const auto *pContents = m_Viewer.GetContents();
	if ( !pContents || nPlayer <= 0 )
		return pose;
	const int nTick = static_cast<int>( flTick );
	const auto player = m_Viewer.GetPlayers().At( nPlayer, flTick );
	const auto *pLife = pContents->LifeAt( nPlayer, nTick );
	if ( ( player && !player->m_bAlive ) || ( pLife && pContents->StatOf( *pLife, "m_bIsScoped", nTick ).value_or( 0 ) != 0 ) )
		return pose;
	const double flInterval = pContents->GetTickInterval() > 0.0 ? pContents->GetTickInterval() : 0.015;
	const auto cycle = [ flTick, flInterval ]( const CDemoFigures::Timing_t &timing, int nFrom, double flRate = 1.0 ) {
		const double flCycle = timing.m_flSeconds > 0.0 ? std::max( 0.0, ( flTick - nFrom ) * flInterval * flRate / timing.m_flSeconds ) : 0.0;
		return timing.m_bLoops ? std::fmod( flCycle, 1.0 ) : std::min( flCycle, 1.0 );
	};
	for ( const auto *pView : m_ViewModels )
	{
		if ( !pView->IsThere( nTick ) || pContents->StatOf( *pView, "m_nViewModelIndex", nTick ).value_or( 0 ) != 0 )
			continue;
		const auto owner = pContents->StatOf( *pView, "m_hOwner", nTick );
		if ( !owner || ( static_cast<std::uint32_t>( *owner ) & xv::demo::ENTITY_HANDLE_INDEX_MASK ) != static_cast<unsigned>( nPlayer ) )
			continue;
		const auto state = pContents->StateOf( *pView, flTick );
		if ( !state || !state->IsDrawn() )
			return {};
		pose.m_szModel = ViewModelName( pContents->ModelName( state->m_nModel ) );
		if ( pose.m_szModel.empty() )
			break;
		pose.m_nSkin = static_cast<int>( state->m_flExtra[ 1 ] );
		pose.m_nSequence = static_cast<int>( state->m_flExtra[ 0 ] );
		if ( const auto timing = m_Figures.SequenceTiming( pose.m_szModel, pose.m_nSequence ) )
		{
			int nFrom = LastKeyTick( pView->m_Fields[ xv::demo::ENTITY_FIELD_EXTRA_0 ], nTick, pView->m_nFrom );
			for ( const auto szParity : { "m_nAnimationParity", "m_nNewSequenceParity" } )
			{
				const int nParity = pContents->FindStat( pView->m_nClass, szParity );
				if ( nParity >= 0 && static_cast<std::size_t>( nParity ) < pView->m_Stats.size() )
					nFrom = std::max( nFrom, LastKeyTick( pView->m_Stats[ nParity ], nTick, nFrom ) );
			}
			const auto recorded = pContents->StatOf( *pView, "m_flCycle", nTick );
			pose.m_flCycle = recorded ? std::clamp<double>( *recorded, 0, 1 ) : cycle( *timing, nFrom, pContents->StatOf( *pView, "m_flPlaybackRate", nTick ).value_or( 1 ) );
		}
		return pose;
	}
	if ( !pLife || pContents->GetExtras( pLife->m_nClass )[ 0 ] != "m_hActiveWeapon" )
		return {};
	const auto nHandle = static_cast<std::uint32_t>( pLife->ValueAt( xv::demo::ENTITY_FIELD_EXTRA_0, nTick )[ 0 ] );
	const auto *pWeapon = HandleLife( nHandle, nTick );
	if ( !pWeapon )
		return {};
	pose.m_szModel = ViewModelName( pContents->ModelName( static_cast<int>( pWeapon->ValueAt( xv::demo::ENTITY_FIELD_MODEL, nTick )[ 0 ] ) ) );
	if ( pose.m_szModel.empty() && pContents->GetExtras( pWeapon->m_nClass )[ 0 ] == "m_iWorldModelIndex" )
		pose.m_szModel = ViewModelName( pContents->ModelName( static_cast<int>( pWeapon->ValueAt( xv::demo::ENTITY_FIELD_EXTRA_0, nTick )[ 0 ] ) ) );
	if ( pose.m_szModel.empty() )
		return {};

	const int nEquipped = LastKeyTick( pLife->m_Fields[ xv::demo::ENTITY_FIELD_EXTRA_0 ], nTick, pWeapon->m_nFrom );
	int nAction = nEquipped;
	auto action = nEquipped > 0 ? ViewModelAction_t::VIEW_MODEL_ACTION_DRAW : ViewModelAction_t::VIEW_MODEL_ACTION_IDLE;
	const auto *pIdentity = player ? player->m_pIdentity : nullptr;
	if ( pIdentity )
	{
		if ( const auto found = m_WeaponActions.find( pIdentity->m_nUserId ); found != m_WeaponActions.end() )
		{
			const auto &actions = found->second;
			const auto after = std::upper_bound( actions.begin(), actions.end(), nTick, []( int nAt, const auto &key ) { return nAt < key.first; } );
			if ( after != actions.begin() && ( after - 1 )->first >= nEquipped )
				std::tie( nAction, action ) = *( after - 1 );
		}
	}
	const int nClip = pContents->FindStat( pWeapon->m_nClass, "m_iClip1" );
	if ( nClip >= 0 && static_cast<std::size_t>( nClip ) < pWeapon->m_Stats.size() )
	{
		const auto &keys = pWeapon->m_Stats[ nClip ];
		const auto after = std::upper_bound( keys.begin(), keys.end(), nTick, []( int nAt, const auto &key ) { return nAt < key.m_nTick; } );
		if ( after - keys.begin() >= 2 && ( after - 1 )->m_nTick > nAction && ( after - 2 )->m_flValue >= 0 )
		{
			const bool bReload = ( after - 1 )->m_flValue > ( after - 2 )->m_flValue;
			if ( !bReload || action != ViewModelAction_t::VIEW_MODEL_ACTION_RELOAD )
			{
				nAction = ( after - 1 )->m_nTick;
				action = bReload ? ViewModelAction_t::VIEW_MODEL_ACTION_RELOAD : ViewModelAction_t::VIEW_MODEL_ACTION_FIRE;
			}
		}
	}
	constexpr std::array activities{ "ACT_VM_IDLE", "ACT_VM_DRAW", "ACT_VM_PRIMARYATTACK", "ACT_VM_RELOAD" };
	constexpr std::array names{ "idle", "draw", "shoot", "reload" };
	const auto nChoice = static_cast<std::size_t>( action );
	pose.m_nSequence = m_Figures.ViewModelSequence( pose.m_szModel, activities[ nChoice ], names[ nChoice ] );
	const auto timing = m_Figures.SequenceTiming( pose.m_szModel, pose.m_nSequence );
	if ( timing && ( action == ViewModelAction_t::VIEW_MODEL_ACTION_IDLE || ( flTick - nAction ) * flInterval < timing->m_flSeconds ) )
		pose.m_flCycle = cycle( *timing, nAction );
	else
	{
		pose.m_nSequence = m_Figures.ViewModelSequence( pose.m_szModel, activities[ 0 ], names[ 0 ] );
		if ( const auto idle = m_Figures.SequenceTiming( pose.m_szModel, pose.m_nSequence ) )
			pose.m_flCycle = cycle( *idle, nEquipped );
	}
	return pose;
}

//-----------------------------------------------------------------------------
// Purpose: Draw only the held model into a transparent pass with its own projection and depth.
//          World lighting is sampled in the player's cell, with normals turned to match aim.
//-----------------------------------------------------------------------------
QImage CDemoWorld::RenderViewModel( const xv::map::CViewCamera &worldCamera )
{
	if ( !m_bViewModel || !IsDrawingCompiled() || !m_pResources )
		return {};
	const int nPlayer = GetEarPlayer();
	const auto pose = ViewModelAt( nPlayer, m_Viewer.GetPlayTick() );
	if ( pose.m_szModel.empty() )
		return {};
	xv::map::PropKey_t key{ pose.m_szModel, pose.m_nSkin };
	const auto *pModel = m_pResources->FindPropModel( key );
	if ( !pModel && key.m_nSkin != 0 )
	{
		key.m_nSkin = 0;
		pModel = m_pResources->FindPropModel( key );
	}
	const auto materials = m_pResources->GetMaterials();
	if ( !pModel || !materials )
		return {};
	auto pMesh = m_Figures.PoseProp( { key.m_szModel, pModel, key.m_nSkin, pose.m_nSequence, pose.m_flCycle }, 0 );
	if ( !pMesh )
	{
		auto &pDefault = m_ViewModelDefaults[ key ];
		if ( !pDefault )
		{
			const xv::map::PropPlacement_t place{};
			pDefault = std::make_shared<const xv::map::CMapMesh>( xv::map::BuildPropMesh( { pModel, 1 }, { &place, 1 } ) );
		}
		pMesh = pDefault;
	}
	if ( pMesh->m_Indices.empty() )
		return {};
	xv::map::RasterScene_t scene;
	scene.m_Meshes = { pMesh.get() };
	scene.m_Materials = *materials;
	xv::map::RasterOptions_t options;
	options.m_bTransparent = true;
	options.m_bSelectionEdges = false;
	options.m_Background = { 0, 0, 0 };
	options.m_Shade.m_nMode = m_bFullbright ? xv::map::RasterMode_t::RASTER_MODE_TEXTURED : xv::map::RasterMode_t::RASTER_MODE_LIGHTING;

	std::shared_ptr<const std::vector<xv::map::LightVertex_t>> lights;
	if ( !m_bFullbright )
	{
		const auto player = m_Viewer.GetPlayers().At( nPlayer, m_Viewer.GetPlayTick() );
		const auto &light = FigureLightAt( nPlayer, player ? player->m_vecOrigin : xv::map::ToVec3( worldCamera.m_Eye ) );
		if ( light.m_Cube || light.m_vecBelow )
		{
			xv::map::CMapMesh normals;
			normals.m_Vertices = pMesh->m_Vertices;
			const auto turn = xv::map::Transform_t::FromAngles( {}, { worldCamera.m_flPitch, worldCamera.m_flYaw, 0 } );
			for ( auto &vertex : normals.m_Vertices )
				vertex.m_vecNormal = xv::map::ToVec3( turn.Rotate( xv::map::ToPoint( vertex.m_vecNormal ) ) );
			lights = CMapCompiled::LightModel( normals, light );
			scene.m_MeshLights = { *lights };
		}
		options.m_Shade.m_bLightHdr = m_pCompiled->GetMap().m_bHdr;
		options.m_Shade.m_flLightScale = options.m_Shade.m_bLightHdr ? m_flExposure : 1.0f;
	}
	xv::map::CViewCamera camera;
	camera.m_flYaw = 0;
	camera.m_flNear = 1;
	camera.m_flFar = 256;
	const double flScale = std::min( { 1.0, 640.0 / worldCamera.m_nWidth, 480.0 / worldCamera.m_nHeight } );
	camera.m_nWidth = std::max( 1, static_cast<int>( worldCamera.m_nWidth * flScale ) );
	camera.m_nHeight = std::max( 1, static_cast<int>( worldCamera.m_nHeight * flScale ) );
	const double flAspect = static_cast<double>( worldCamera.m_nWidth ) / worldCamera.m_nHeight;
	camera.m_flFov = 2.0 * std::atan( std::tan( 27.0 * std::numbers::pi / 180.0 ) * flAspect / ( 4.0 / 3.0 ) ) * 180.0 / std::numbers::pi;
	xv::texture::CImage image;
	xv::map::RenderView( scene, camera, options, image );
	return ToQImage( image );
}

//-----------------------------------------------------------------------------
// Purpose: Toggle the first-person pass in the World view.
//-----------------------------------------------------------------------------
void CDemoWorld::SetShowingViewModel( bool bShow )
{
	m_bViewModel = bShow;
	Redraw();
}
