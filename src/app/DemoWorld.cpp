//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DemoWorld.h.
//
//===========================================================================//
#include "DemoWorld.h"

#include "LoadingView.h"

#include <QFile>
#include <QFileInfo>

#include "demo/Particles.h"
#include "particles/Render.h"
#include "texture/Vtf.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <numbers>
#include <set>

#include <QCursor>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QToolTip>
#include <QTemporaryDir>
#include <QDir>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QWheelEvent>

#include "DemoViewer.h"
#include "GpuView.h"
#include "Graphics.h"
#include "ImageFiles.h"
#include "Preferences.h"
#include "bsp/Decompile.h"
#include "map/MapCompiled.h"
#include "map/MapDocument.h"
#include "map/MapRenderer.h"
#include "map/Instance.h"
#include "map/MapResources.h"
#include "map/Raster.h"

namespace
{

using xv::demo::PlayerState_t;
using xv::map::Point_t;

// A player's figure: Source's hull is 32 wide and 72 tall (54 ducked); the figure is narrower
// front to back, so which way it faces shows, with a band at its eyes that sticks out ahead.
constexpr double FIGURE_HALF_WIDTH = 14.0;
constexpr double FIGURE_HALF_DEPTH = 9.0;
constexpr double FIGURE_HEIGHT = 72.0;
constexpr double FIGURE_DUCKED_HEIGHT = 54.0;
constexpr double VISOR_HALF_HEIGHT = 4.0;
constexpr double VISOR_REACH = 5.0;
constexpr double FLY_SPEED = 600.0;  // units a second
constexpr double FLY_FAST = 4.0;     // with Shift
constexpr double LOOK_SPEED = 0.25;  // degrees a pixel
constexpr double WHEEL_STEP = 96.0;  // units a notch
constexpr double CHASE_NEAREST = 48.0;
constexpr double CHASE_FURTHEST = 1024.0;
constexpr double CHASE_RISE = 24.0;  // above the eyes
constexpr double CLICK_SLACK = 4.0; // pixels a press may move and still be a click
// The packets' view within this many units of the recorder's feet is its feet (between
// snapshots the two are interpolated apart a little).
constexpr float RECORDER_FEET_SLACK = 12.0f;
// A spray: its size on the wall, how far from its point the surface is looked for, and how
// far off it it is drawn.
constexpr double SPRAY_SIZE = 64.0;
constexpr double SPRAY_REACH = 8.0;
constexpr double SPRAY_LIFT = 0.5;
// Names aren't written for players further than this: a crowd across the map is a smear.
constexpr double NAME_DISTANCE = 4096.0;
constexpr double NAME_RISE = 10.0;   // over the figure's top
// The first look at the players: from this far back and up, per unit of their spread.
constexpr double START_BACK = 1.2;
constexpr double START_NEAREST = 400.0;
constexpr int FLY_INTERVAL = 16;
// A playhead that moves further than this (seconds of the demo) between frames jumped: the
// particle systems start again where they'd be.
constexpr double PARTICLE_STEP_MOST = 0.5;
// Dispatched particle effects are looked for this far back (seconds): longer than one lasts.
constexpr double PARTICLE_DISPATCH_SECONDS = 5.0;
// The entity put at the origin for each model the demo's props use (its mesh is what they are
// placed copies of).
constexpr std::string_view PROP_TEMPLATE_NAME = "vomit_demo_template";
constexpr int PROP_TEMPLATE_FIRST_ID = 900000000;
// A shot's tracer shows this long, and reaches this far at most; a death's mark stays longer.
constexpr double TRACER_SECONDS = 0.08;
constexpr double TRACER_REACH = 8192.0;
constexpr double DEATH_SECONDS = 4.0;
constexpr std::array<std::uint8_t, 3> TRACER_COLOR{ 255, 225, 140 };
constexpr std::array<std::uint8_t, 3> DEATH_COLOR{ 230, 40, 40 };
// A figure's light is looked up again when it moves into another cell this many units wide.
constexpr float FIGURE_LIGHT_CELL = 16.0f;
// A handle's serial number is above its entity number's bits.
constexpr int HANDLE_SERIAL_SHIFT = 11;
// Parents followed this far up, at most.
constexpr int PARENT_DEPTH = 8;
// Source's render modes: none draws nothing; any but normal fades with the colour's alpha.
constexpr int RENDER_MODE_NORMAL = 0;
constexpr int RENDER_MODE_NONE = 10;
// kRenderGlow, kRenderTransAdd, kRenderWorldGlow: drawn added to what is behind.
constexpr int RENDER_MODE_GLOW = 3;
constexpr int RENDER_MODE_ADDITIVE = 5;
constexpr int RENDER_MODE_WORLD_GLOW = 9;

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string Lower( std::string_view szText )
{
	std::string szOut( szText );
	std::ranges::transform( szOut, szOut.begin(), []( unsigned char ch ) { return static_cast<char>( std::tolower( ch ) ); } );
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: Two meshes as one (the second's vertices, indices, batches and chunks after the
//          first's); either may be null.
//-----------------------------------------------------------------------------
std::shared_ptr<const xv::map::CMapMesh> MergeMeshes( std::shared_ptr<const xv::map::CMapMesh> pFirst, std::shared_ptr<const xv::map::CMapMesh> pSecond )
{
	if ( !pFirst || !pSecond )
		return pFirst ? pFirst : pSecond;
	auto pMesh = std::make_shared<xv::map::CMapMesh>( *pFirst );
	const auto nVertices = static_cast<std::uint32_t>( pMesh->m_Vertices.size() );
	const auto nIndices = static_cast<std::uint32_t>( pMesh->m_Indices.size() );
	const auto nChunks = static_cast<std::uint32_t>( pMesh->m_Chunks.size() );
	pMesh->m_Vertices.insert( pMesh->m_Vertices.end(), pSecond->m_Vertices.begin(), pSecond->m_Vertices.end() );
	for ( const std::uint32_t nIndex : pSecond->m_Indices )
		pMesh->m_Indices.push_back( nIndex + nVertices );
	pMesh->m_Chunks.insert( pMesh->m_Chunks.end(), pSecond->m_Chunks.begin(), pSecond->m_Chunks.end() );
	for ( xv::map::MeshBatch_t batch : pSecond->m_Batches )
	{
		batch.m_nFirstIndex += nIndices;
		if ( batch.m_nChunk != ~0u )
			batch.m_nChunk += nChunks;
		pMesh->m_Batches.push_back( batch );
	}
	return pMesh;
}

//-----------------------------------------------------------------------------
// Purpose: The world's skyname in a decompiled map (worldspawn comes first) set to szSky,
//          when that names another.
//-----------------------------------------------------------------------------
void UseSky( std::string &szText, const std::string &szSky )
{
	if ( szSky.empty() )
		return;
	const std::size_t nKey = szText.find( "\"skyname\" \"" );
	const std::size_t nEntity = szText.find( "\nentity" );
	if ( nKey == std::string::npos || ( nEntity != std::string::npos && nKey > nEntity ) )
		return;
	const std::size_t nFrom = nKey + 11;
	const std::size_t nTo = szText.find( '"', nFrom );
	if ( nTo != std::string::npos && szText.compare( nFrom, nTo - nFrom, szSky ) != 0 && szSky.find_first_of( "\"\n" ) == std::string::npos )
		szText.replace( nFrom, nTo - nFrom, szSky );
}

//-----------------------------------------------------------------------------
// Purpose: A template prop_dynamic at the origin for each (model, skin) the demo's props use,
//          put after the decompiled map's text.
//-----------------------------------------------------------------------------
std::string PropTemplates( const std::set<std::pair<std::string, int>> &templates )
{
	std::string szOut;
	int nId = PROP_TEMPLATE_FIRST_ID;
	for ( const auto &[ szModel, nSkin ] : templates )
	{
		szOut += "entity\n{\n\t\"id\" \"" + std::to_string( nId++ ) + "\"\n\t\"classname\" \"prop_dynamic\"\n\t\"targetname\" \"" + std::string( PROP_TEMPLATE_NAME ) + "\"\n\t\"model\" \"" + szModel + "\"\n\t\"skin\" \"" + std::to_string( nSkin ) + "\"\n\t\"origin\" \"0 0 0\"\n\t\"angles\" \"0 0 0\"\n}\n";
	}
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: Whether an entity class of the demo is drawn as its model where it stands: not the
//          players (C1's), their ragdolls or their view models.
//-----------------------------------------------------------------------------
bool IsPropClass( std::string_view szClass )
{
	return szClass != "CCSPlayer" && szClass != "CCSRagdoll" && szClass != "CPredictedViewModel" && szClass != "CBaseViewModel";
}

//-----------------------------------------------------------------------------
// Purpose: Whether the game draws an entity as the demo has it: not marked no-draw, and not
//          rendered as nothing, or faded out.
//-----------------------------------------------------------------------------
bool IsDrawnState( const xv::demo::EntityState_t &state )
{
	return state.IsDrawn() && state.m_nRenderMode != RENDER_MODE_NONE && ( state.m_nRenderMode == RENDER_MODE_NORMAL || state.GetAlpha() > 0 );
}

//-----------------------------------------------------------------------------
// Purpose: A brush model's number in a model name ("*12"); -1 for any other name.
//-----------------------------------------------------------------------------
int BrushModel( std::string_view szModel )
{
	int nModel = -1;
	if ( szModel.size() < 2 || szModel[ 0 ] != '*' || std::from_chars( szModel.data() + 1, szModel.data() + szModel.size(), nModel ).ec != std::errc{} )
	{
		return -1;
	}
	return nModel;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsStudioModel( std::string_view szModel )
{
	return szModel.size() > 4 && Lower( szModel.substr( szModel.size() - 4 ) ) == ".mdl";
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool SameTransform( const xv::map::Transform_t &a, const xv::map::Transform_t &b )
{
	return a.m_Rotation == b.m_Rotation && a.m_Offset == b.m_Offset;
}

//-----------------------------------------------------------------------------
// Purpose: A weapon's world model from its view model's name ("models/weapons/v_rif_ak47.mdl"
//          is held as "models/weapons/w_rif_ak47.mdl"), for a demo whose weapons don't send
//          their world model; lower case.
//-----------------------------------------------------------------------------
std::string WorldModelName( std::string_view szModel )
{
	std::string szName = Lower( szModel );
	const std::size_t nSlash = szName.find_last_of( '/' );
	const std::size_t nAt = nSlash == std::string::npos ? 0 : nSlash + 1;
	if ( szName.compare( nAt, 2, "v_" ) == 0 )
	{
		szName[ nAt ] = 'w';
	}
	return szName;
}

//-----------------------------------------------------------------------------
// Purpose: The model a weapon shows in its holder's hand: its world model, lower case.
//-----------------------------------------------------------------------------
std::string WorldModelOf( const xv::demo::CContents &contents, const xv::demo::EntityLife_t &life, const xv::demo::EntityState_t &state )
{
	if ( contents.GetExtras( life.m_nClass )[ 0 ] == "m_iWorldModelIndex" && state.m_flExtra[ 0 ] > 0.0f )
	{
		return Lower( contents.ModelName( static_cast<int>( state.m_flExtra[ 0 ] ) ) );
	}
	return WorldModelName( contents.ModelName( state.m_nModel ) );
}

//-----------------------------------------------------------------------------
// Purpose: Every model a player wore over the demo.
//-----------------------------------------------------------------------------
std::set<std::string> PlayerModels( const xv::demo::CContents &contents )
{
	std::set<std::string> models;
	for ( const xv::demo::EntityLife_t &life : contents.GetLives() )
	{
		if ( contents.GetExtras( life.m_nClass )[ 0 ] != "m_hActiveWeapon" )
		{
			continue;
		}
		for ( const xv::demo::EntityKey_t &key : life.m_Fields[ xv::demo::ENTITY_FIELD_MODEL ] )
		{
			if ( const std::string_view szModel = contents.ModelName( static_cast<int>( key.m_flValue[ 0 ] ) ); IsStudioModel( szModel ) )
			{
				models.insert( Lower( szModel ) );
			}
		}
	}
	return models;
}

//-----------------------------------------------------------------------------
// Purpose: The models (with the skins they wear) the demo's props are drawn as, for each a
//          template put in the map: every model and skin a prop entity had over the demo.
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Purpose: The models of props the demo has in a sequence other than their first, or changing
//          sequence: those that animate, whose studio files (with their animations) are read.
//-----------------------------------------------------------------------------
std::set<std::string> AnimatedModels( const xv::demo::CContents &contents )
{
	std::set<std::string> models;
	for ( const xv::demo::EntityLife_t &life : contents.GetLives() )
	{
		if ( contents.GetExtras( life.m_nClass )[ 0 ] != "m_nSequence" )
			continue;
		const auto &sequences = life.m_Fields[ xv::demo::ENTITY_FIELD_EXTRA_0 ];
		if ( !std::ranges::any_of( sequences, []( const xv::demo::EntityKey_t &key ) { return key.m_flValue[ 0 ] != 0.0f; } ) )
			continue;
		for ( const xv::demo::EntityKey_t &key : life.m_Fields[ xv::demo::ENTITY_FIELD_MODEL ] )
		{
			const std::string_view szModel = contents.ModelName( static_cast<int>( key.m_flValue[ 0 ] ) );
			if ( IsStudioModel( szModel ) )
				models.insert( Lower( szModel ) );
		}
	}
	return models;
}

//-----------------------------------------------------------------------------
// Purpose: When a run of keys holding the value the key at or before flAt holds began.
//-----------------------------------------------------------------------------
template <typename Key_t, typename Value_t>
std::optional<int> RunStart( const std::vector<Key_t> &keys, double flAt, Value_t pfnValue )
{
	auto at = std::upper_bound( keys.begin(), keys.end(), flAt, []( double flTick, const Key_t &key ) { return flTick < key.m_nTick; } );
	if ( at == keys.begin() )
		return std::nullopt;
	--at;
	const float flValue = pfnValue( *at );
	while ( at != keys.begin() && pfnValue( *std::prev( at ) ) == flValue )
		--at;
	return at->m_nTick;
}

std::set<std::pair<std::string, int>> PropModels( const xv::demo::CContents &contents )
{
	std::set<std::pair<std::string, int>> models;
	// The players' models (in each skin they wore), and every weapon one of them held.
	std::set<std::pair<int, int>> held; // entity, serial
	for ( const xv::demo::EntityLife_t &life : contents.GetLives() )
	{
		const auto &extras = contents.GetExtras( life.m_nClass );
		if ( extras[ 0 ] != "m_hActiveWeapon" )
		{
			continue;
		}
		std::set<int> skins{ 0 };
		for ( const xv::demo::EntityKey_t &key : life.m_Fields[ xv::demo::ENTITY_FIELD_EXTRA_1 ] )
		{
			skins.insert( static_cast<int>( key.m_flValue[ 0 ] ) );
		}
		for ( const xv::demo::EntityKey_t &key : life.m_Fields[ xv::demo::ENTITY_FIELD_MODEL ] )
		{
			const std::string_view szModel = contents.ModelName( static_cast<int>( key.m_flValue[ 0 ] ) );
			for ( const int nSkin : IsStudioModel( szModel ) ? skins : std::set<int>{} )
			{
				models.emplace( Lower( szModel ), nSkin );
			}
		}
		for ( const xv::demo::EntityKey_t &key : life.m_Fields[ xv::demo::ENTITY_FIELD_EXTRA_0 ] )
		{
			const auto nHandle = static_cast<std::uint32_t>( key.m_flValue[ 0 ] );
			if ( nHandle != xv::demo::ENTITY_NO_HANDLE )
			{
				held.emplace( static_cast<int>( nHandle & xv::demo::ENTITY_HANDLE_INDEX_MASK ), static_cast<int>( nHandle >> HANDLE_SERIAL_SHIFT ) );
			}
		}
	}
	for ( const xv::demo::EntityLife_t &life : contents.GetLives() )
	{
		if ( !held.contains( { life.m_nEntity, life.m_nSerial } ) )
		{
			continue;
		}
		const bool bWorldModel = contents.GetExtras( life.m_nClass )[ 0 ] == "m_iWorldModelIndex";
		for ( const xv::demo::EntityKey_t &key : life.m_Fields[ bWorldModel ? xv::demo::ENTITY_FIELD_EXTRA_0 : xv::demo::ENTITY_FIELD_MODEL ] )
		{
			if ( const std::string_view szModel = contents.ModelName( static_cast<int>( key.m_flValue[ 0 ] ) ); IsStudioModel( szModel ) )
			{
				models.emplace( bWorldModel ? Lower( szModel ) : WorldModelName( szModel ), 0 );
			}
		}
	}
	for ( const xv::demo::EntityLife_t &life : contents.GetLives() )
	{
		if ( !IsPropClass( contents.ClassName( life.m_nClass ) ) )
		{
			continue;
		}
		const bool bSkin = contents.GetExtras( life.m_nClass )[ 1 ] == "m_nSkin";
		// A weapon is drawn as its world model.
		if ( contents.GetExtras( life.m_nClass )[ 0 ] == "m_iWorldModelIndex" )
		{
			for ( const xv::demo::EntityKey_t &key : life.m_Fields[ xv::demo::ENTITY_FIELD_EXTRA_0 ] )
			{
				if ( const std::string_view szModel = contents.ModelName( static_cast<int>( key.m_flValue[ 0 ] ) ); IsStudioModel( szModel ) )
				{
					models.emplace( Lower( szModel ), 0 );
				}
			}
			for ( const xv::demo::EntityKey_t &key : life.m_Fields[ xv::demo::ENTITY_FIELD_MODEL ] )
			{
				if ( const std::string_view szModel = contents.ModelName( static_cast<int>( key.m_flValue[ 0 ] ) ); IsStudioModel( szModel ) )
				{
					models.emplace( WorldModelName( szModel ), 0 );
				}
			}
			continue;
		}
		std::set<int> skins{ 0 };
		if ( bSkin )
		{
			for ( const xv::demo::EntityKey_t &key : life.m_Fields[ xv::demo::ENTITY_FIELD_EXTRA_1 ] )
			{
				skins.insert( static_cast<int>( key.m_flValue[ 0 ] ) );
			}
		}
		for ( const xv::demo::EntityKey_t &key : life.m_Fields[ xv::demo::ENTITY_FIELD_MODEL ] )
		{
			const std::string_view szModel = contents.ModelName( static_cast<int>( key.m_flValue[ 0 ] ) );
			if ( IsStudioModel( szModel ) )
			{
				for ( const int nSkin : skins )
				{
					models.emplace( Lower( szModel ), nSkin );
				}
			}
		}
	}
	return models;
}

//-----------------------------------------------------------------------------
// Purpose: How tall a player's figure stands.
//-----------------------------------------------------------------------------
double FigureHeight( const PlayerState_t &player )
{
	return player.m_bDucked ? FIGURE_DUCKED_HEIGHT : FIGURE_HEIGHT;
}

//-----------------------------------------------------------------------------
// Purpose: Whether a player has a figure: alive, and on a team that plays.
//-----------------------------------------------------------------------------
bool HasFigure( const PlayerState_t &player )
{
	return player.m_bAlive && player.m_nTeam >= 2;
}

//-----------------------------------------------------------------------------
// Purpose: A box's eight corners, turned about the vertical by a yaw: forward along the yaw,
//          from flBack to flFront; flHalfWidth either side; flBottom to flTop above the feet.
//          Corner i has its high side along forward, right and up for bits 0, 1 and 2.
//-----------------------------------------------------------------------------
std::array<Point_t, 8> TurnedBox( const xv::CVec3 &vecFeet, double flYaw, double flBack, double flFront, double flHalfWidth, double flBottom, double flTop )
{
	const double flRadians = flYaw * std::numbers::pi / 180.0;
	const Point_t forward{ std::cos( flRadians ), std::sin( flRadians ), 0.0 };
	const Point_t right{ forward[ 1 ], -forward[ 0 ], 0.0 };
	std::array<Point_t, 8> corners{};
	for ( std::size_t i = 0; i < 8; i++ )
	{
		const double flAlong = ( i & 1 ) ? flFront : flBack;
		const double flAcross = ( i & 2 ) ? flHalfWidth : -flHalfWidth;
		const double flUp = ( i & 4 ) ? flTop : flBottom;
		corners[ i ] = { vecFeet.m_flX + forward[ 0 ] * flAlong + right[ 0 ] * flAcross, vecFeet.m_flY + forward[ 1 ] * flAlong + right[ 1 ] * flAcross, vecFeet.m_flZ + flUp };
	}
	return corners;
}

// A turned box's faces, as corners (TurnedBox's numbering), with how bright each is drawn: the
// top lightest, the bottom darkest, the front lighter than the back, so the shape reads without
// any lighting.
struct BoxFace_t
{
	std::array<int, 4> m_Corners;
	float m_flShade;
};
constexpr std::array<BoxFace_t, 6> BOX_FACES{ {
	{ { 4, 5, 7, 6 }, 1.0f },  // top
	{ { 0, 2, 3, 1 }, 0.45f }, // bottom
	{ { 1, 3, 7, 5 }, 0.9f },  // front
	{ { 0, 4, 6, 2 }, 0.6f },  // back
	{ { 2, 6, 7, 3 }, 0.75f }, // right
	{ { 0, 1, 5, 4 }, 0.7f },  // left
} };
constexpr std::array<std::array<int, 2>, 12> BOX_EDGES{ { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } } };

//-----------------------------------------------------------------------------
// Purpose: A figure's two boxes: the body, and the band at its eyes.
//-----------------------------------------------------------------------------
std::array<std::array<Point_t, 8>, 2> FigureBoxes( const PlayerState_t &player )
{
	const double flHeight = FigureHeight( player );
	const double flEyes = player.m_bDucked ? xv::demo::PLAYER_DUCKED_EYE_HEIGHT : xv::demo::PLAYER_EYE_HEIGHT;
	return { TurnedBox( player.m_vecOrigin, player.m_flYaw, -FIGURE_HALF_DEPTH, FIGURE_HALF_DEPTH, FIGURE_HALF_WIDTH, 0.0, flHeight ),
		TurnedBox( player.m_vecOrigin, player.m_flYaw, FIGURE_HALF_DEPTH, FIGURE_HALF_DEPTH + VISOR_REACH, FIGURE_HALF_WIDTH - 2.0, flEyes - VISOR_HALF_HEIGHT, flEyes + VISOR_HALF_HEIGHT ) };
}

//-----------------------------------------------------------------------------
// Purpose: The recorder's camera at a tick and fractions of one: the packet's at or before
//          it, moved towards the next (unless that one is somewhere else altogether).
//-----------------------------------------------------------------------------
std::optional<xv::demo::View_t> RecorderView( const xv::demo::CDemo &demo, double flTick )
{
	const std::vector<xv::demo::View_t> &views = demo.GetViews();
	const auto after = std::upper_bound( views.begin(), views.end(), flTick, []( double flAt, const xv::demo::View_t &view ) { return flAt < view.m_nTick; } );
	if ( after == views.begin() )
	{
		return views.empty() ? std::nullopt : std::optional{ views.front() };
	}
	xv::demo::View_t view = *( after - 1 );
	if ( after != views.end() && after->m_nTick > view.m_nTick && Length( after->m_vecOrigin - view.m_vecOrigin ) < 256.0f )
	{
		const float flPart = static_cast<float>( ( flTick - view.m_nTick ) / ( after->m_nTick - view.m_nTick ) );
		view.m_vecOrigin = view.m_vecOrigin + ( after->m_vecOrigin - view.m_vecOrigin ) * flPart;
		view.m_vecAngles.m_flX += std::remainder( after->m_vecAngles.m_flX - view.m_vecAngles.m_flX, 360.0f ) * flPart;
		view.m_vecAngles.m_flY += std::remainder( after->m_vecAngles.m_flY - view.m_vecAngles.m_flY, 360.0f ) * flPart;
	}
	return view;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Counter-Strike's two sides are everyone's picture of 2 and 3; any other team is
//          grey.
//-----------------------------------------------------------------------------
QColor DemoTeamColor( int nTeam )
{
	switch ( nTeam )
	{
		case 2:
			return QColor( 232, 104, 64 );
		case 3:
			return QColor( 88, 152, 240 );
		default:
			return QColor( 176, 176, 176 );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDemoWorldOverlay::CDemoWorldOverlay( CDemoWorld &world )
	: QWidget( &world ),
	  m_World( world )
{
	setAttribute( Qt::WA_TransparentForMouseEvents );
	setFocusPolicy( Qt::NoFocus );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorldOverlay::paintEvent( QPaintEvent * )
{
	QPainter painter( this );
	m_World.PaintLabels( painter );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDemoWorld::CDemoWorld( CDemoViewer &viewer, QWidget *pParent )
	: QWidget( pParent ),
	  m_Viewer( viewer )
{
	setObjectName( QStringLiteral( "demoWorld" ) );
	setFocusPolicy( Qt::ClickFocus );
	setMinimumSize( 320, 200 );
	m_pDocument = new CMapDocument( this );
	m_pRenderData = std::make_unique<CMapRenderData>( *m_pDocument );
	m_pResources = new CMapResources( this );
	// No 2D views here: the props are drawn from their parts alone.
	m_pResources->SetCombiningProps( false );
	m_pRenderData->SetResources( m_pResources );
	m_pParticles = new CMapParticles( this );
	connect( m_pParticles, &CMapParticles::Ready, this, &CDemoWorld::Redraw );
	connect( m_pDocument, &CMapDocument::Loaded, this, &CDemoWorld::MapLoaded );
	connect( m_pDocument, &CMapDocument::LoadFailed, this, [ this ]( const QString &szError ) {
		m_pLoading->End();
		SetStatus( tr( "The map couldn't be read: %1" ).arg( szError ) );
	} );
	connect( m_pDocument, &CMapDocument::LoadProgress, this, [ this ]( int nPercent, const QString &szStage ) {
		SetStatus( tr( "Reading the map: %1 (%2%)" ).arg( szStage ).arg( nPercent ) );
		m_pLoading->SetProgress( m_szStatus, nPercent );
	} );
	connect( m_pResources, &CMapResources::Arrived, this, &CDemoWorld::ResourcesArrived );
	connect( m_pResources, &CMapResources::Finished, this, [ this ] {
		m_pRenderData->CompactEntityBoxes();
		SetStatus( {} );
	} );
	m_pFly = new QTimer( this );
	m_pFly->setInterval( FLY_INTERVAL );
	connect( m_pFly, &QTimer::timeout, this, &CDemoWorld::Fly );
	m_Camera.m_flYaw = 0.0;
	m_nMode = m_Viewer.GetDemo().HasCamera() ? DemoCamera_t::DEMO_CAMERA_RECORDER : DemoCamera_t::DEMO_CAMERA_FREE;
	m_Worker = std::thread( [ this ] { WorkerLoop(); } );
	SetUseGpu( CGraphics::Instance().IsGpu() );
	m_pLoading = new CLoadingView( this, LoadingLayout_t::LOADING_LAYOUT_PREVIEW );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDemoWorld::~CDemoWorld()
{
	Stop();
	{
		const std::lock_guard lock( m_Mutex );
		m_bQuit = true;
	}
	m_Wake.notify_all();
	m_Worker.join();
}

//-----------------------------------------------------------------------------
// Purpose: The card draws in a widget of its own under the names' overlay; a card that fails
//          leaves the drawing to software from then on.
//-----------------------------------------------------------------------------
void CDemoWorld::SetUseGpu( bool bOn )
{
	delete m_pGpu;
	m_pGpu = nullptr;
	delete m_pOverlay;
	m_pOverlay = nullptr;
#ifdef VOMIT_TOOLS_GPU
	if ( bOn )
	{
		auto pRenderer = std::make_unique<CMapRenderer>( [ this ]( QSize size ) { return MakeFrame( size ); } );
		pRenderer->SetExposureSink( [ this ]( float flExposure ) { ExposureMeasured( flExposure ); },
			[ this ] { QTimer::singleShot( 0, this, [ this ] { Redraw(); } ); } );
		m_pGpu = new CGpuView( std::move( pRenderer ), this );
		m_pGpu->setAttribute( Qt::WA_TransparentForMouseEvents );
		m_pGpu->setFocusPolicy( Qt::NoFocus );
		m_pGpu->setGeometry( rect() );
		m_pGpu->show();
		connect( m_pGpu, &QRhiWidget::renderFailed, this, [ this ] { QTimer::singleShot( 0, this, [ this ] {
																		 SetUseGpu( false );
																		 // Software draws from decoded textures, which the card didn't need.
																		 if ( HasMap() && !m_pResources->IsDecoded() )
																			 MapLoaded();
																	 } ); } );
		m_pOverlay = new CDemoWorldOverlay( *this );
		m_pOverlay->setGeometry( rect() );
		m_pOverlay->show();
		m_pOverlay->raise();
		if ( m_pLoading && !m_pLoading->isHidden() )
			m_pLoading->raise();
		return;
	}
#else
	(void)bOn;
#endif
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetStatus( const QString &szStatus )
{
	if ( szStatus == m_szStatus )
	{
		return;
	}
	m_szStatus = szStatus;
	emit StatusChanged();
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CDemoWorld::HasMap() const
{
	return m_pDocument->IsLoaded();
}

//-----------------------------------------------------------------------------
// Purpose: Reading a big map back takes seconds: it is done on a thread, and the text then
//          loaded as any map's. The map's packed files are mounted first, as the game mounts
//          them.
//-----------------------------------------------------------------------------
void CDemoWorld::LoadCompiledMap( std::vector<std::byte> bsp, std::optional<xv::vfs::CGameInfo> game, const QString &szName, const std::filesystem::path &disk )
{
	Stop();
	if ( m_pMapFiles )
	{
		m_pMapFiles.reset();
		emit MapFilesMounted();
	}
	m_pMapSnapshot.reset();
	SetStatus( tr( "Reading %1…" ).arg( szName ) );
	m_pLoading->Begin( m_szStatus );
	if ( game && !disk.empty() )
	{
		game->m_SearchPaths.insert( game->m_SearchPaths.begin(), { { "game" }, disk.generic_string() } );
	}
	m_Game = game;
	m_pParticles->Start( game, QFileInfo( szName ).completeBaseName().toStdString() );
	m_flParticlesTick = -1.0;
	m_pCompiled.reset();
	m_PropTemplates.clear();
	m_HiddenObjects.clear();
	m_Placed.clear();
	m_pLoad = std::make_shared<int>( 0 );
	const std::weak_ptr<int> pLoad = m_pLoad;
	// The models the demo's props wear, each put in the map at the origin, so they are read
	// with the map's own.
	std::set<std::string> playerModels;
	std::set<std::pair<std::string, int>> templates;
	std::string szSky; // the 2D sky the server had (its sv_skyname), over the map's own
	if ( const xv::demo::CContents *pContents = m_Viewer.GetContents() )
	{
		templates = PropModels( *pContents );
		playerModels = PlayerModels( *pContents );
		// The animating props' models are read with the players'.
		playerModels.merge( AnimatedModels( *pContents ) );
		IndexViewModels( templates, playerModels );
		if ( pContents->GetServerInfo() )
			szSky = pContents->GetServerInfo()->m_szSky;
	}
	m_Figures.Clear();
	m_ViewModelDefaults.clear();
	m_FigureLight.clear();
	m_FigureLit.clear();
	m_FigureLitCell.clear();
	m_ShotEnds.clear();
	m_nShotClass = SHOT_CLASS_UNKNOWN;
	m_Decompiler = std::thread( [ this, pLoad, bsp = std::move( bsp ), game = std::move( game ), disk, szName, szSky = std::move( szSky ), templates = std::move( templates ), playerModels = std::move( playerModels ) ]() mutable {
		// The game's files with the map's packed ones first, for the sounds and the HUD too;
		// the studio files (with their animations) of the models that may be players'.
		std::shared_ptr<xv::vfs::CFileSystem> pFiles;
		std::shared_ptr<QTemporaryDir> pSnapshot;
		std::filesystem::path mount = disk;
		if ( mount.empty() )
		{
			// CPackage can read the pakfile on demand when the BSP has a filesystem path.
			// This copy is only needed for a BSP stored inside another archive.
			pSnapshot = std::make_shared<QTemporaryDir>( QDir::tempPath() + QStringLiteral( "/vomit-demo-XXXXXX" ) );
			if ( pSnapshot->isValid() )
			{
				QFile file( pSnapshot->filePath( QStringLiteral( "map.bsp" ) ) );
				const auto nSize = static_cast<qint64>( bsp.size() );
				if ( file.open( QIODevice::WriteOnly ) && file.write( reinterpret_cast<const char *>( bsp.data() ), nSize ) == nSize && file.flush() )
				{
					mount = std::filesystem::path( file.fileName().toStdU16String() );
					if ( game )
						game->m_SearchPaths.insert( game->m_SearchPaths.begin(), { { "game" }, mount.generic_string() } );
				}
			}
		}
		CDemoFigures::Studios_t studios;
		if ( game || !mount.empty() )
		{
			pFiles = std::make_shared<xv::vfs::CFileSystem>();
			if ( game )
				pFiles->Mount( *game );
			else
				pFiles->AddPackage( mount, { "game" } );
			if ( !playerModels.empty() )
				studios = CDemoFigures::ReadStudios( playerModels, [ &pFiles ]( std::string_view szPath ) { return pFiles->Read( szPath ); } );
		}
		std::string szError;
		auto text = xv::bsp::DecompileToVmf( bsp, nullptr, &szError );
		// The map as the game draws it: its own faces and light, the HDR compile's when it has one.
		std::string szCompiledError;
		auto compiled = xv::bsp::ReadCompiledMap( bsp, {}, &szCompiledError );
		auto logicMap = xv::demo::ReadLogicMap( bsp );
		bsp = {};
		if ( text )
		{
			UseSky( *text, szSky );
			*text += PropTemplates( templates );
			if ( compiled )
			{
				*text += CMapCompiled::MapText( *compiled );
			}
		}
		QMetaObject::invokeMethod(
			this, [ this, pLoad, text = std::move( text ), compiled = std::move( compiled ), logicMap = std::move( logicMap ), game = std::move( game ), szName, szError, szCompiledError, studios = std::move( studios ), pFiles = std::move( pFiles ), pSnapshot = std::move( pSnapshot ) ]() mutable {
				if ( pLoad.expired() )
				{
					return;
				}
				m_LogicMap = std::move( logicMap );
				emit LogicMapRead();
				if ( pFiles )
				{
					m_pMapSnapshot = std::move( pSnapshot );
					m_pMapFiles = std::move( pFiles );
					emit MapFilesMounted();
				}
				if ( !text )
				{
					m_pLoading->End();
					SetStatus( tr( "%1 couldn't be read: %2" ).arg( szName, QString::fromStdString( szError ) ) );
					return;
				}
				CMapDocument::Source_t source;
				source.m_szName = szName;
				source.m_Text.assign( text->begin(), text->end() );
				source.m_Game = std::move( game );
				if ( compiled )
				{
					m_pCompiled = std::make_unique<CMapCompiled>( std::make_shared<const xv::bsp::CompiledMap_t>( std::move( *compiled ) ) );
				}
				else
				{
					qDebug( "demo world: the map's own faces couldn't be read (%s); its brushes are drawn", szCompiledError.c_str() );
				}
				for ( const QString &szFgd : CPreferences::Instance().GetMapPreferences().m_Fgds )
				{
					source.m_Fgds.push_back( std::filesystem::path( szFgd.toStdU16String() ) );
				}
				m_Figures.SetStudios( std::move( studios ) );
				m_pDocument->Load( std::move( source ) );
			},
			Qt::QueuedConnection );
	} );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetNoMap( const QString &szWhy )
{
	Stop();
	SetStatus( szWhy );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::Stop()
{
	m_pLoading->End();
	m_pLoad.reset();
	if ( m_Decompiler.joinable() )
	{
		m_Decompiler.join();
	}
	m_pDocument->Stop();
	m_pResources->Stop();
	m_pParticles->Stop();
}

//-----------------------------------------------------------------------------
// Purpose: The map is in: its textures and models start arriving (decoded only when software
//          draws them; the card takes them as they are).
//-----------------------------------------------------------------------------
void CDemoWorld::MapLoaded()
{
	BuildCompiledMeshes();
	m_pLoading->End();
	SetStatus( tr( "Reading the map's textures…" ) );
	const bool bDecoded = !m_pGpu || qEnvironmentVariableIsSet( "VOMIT_TOOLS_DECODE_TEXTURES" );
	m_pResources->Start( m_pDocument->GetSharedScene(), m_pDocument->GetGame(), CPreferences::Instance().GetMapPreferences().m_nTextureSize, bDecoded );
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::ResourcesArrived()
{
	m_pRenderData->SetMaterials( m_pResources->GetMaterials(), m_pResources->GetTextures(), m_pResources->GetEntityMaterial() );
	m_pRenderData->SetSky( m_pResources->GetSky(), m_pResources->GetSkyTransforms() );
	m_pRenderData->SetEnvMap( m_pResources->GetEnvMap() );
	m_pRenderData->SetProps( m_pResources->GetProps(), m_pResources->GetPropObjects(), m_pResources->GetPropParts() );
	m_pRenderData->SetSprites( m_pResources->GetSprites() );
	m_pRenderData->SetOverlays( m_pResources->GetOverlays(), m_pResources->GetOverlayParts() );
	if ( !m_pResources->IsFinished() )
	{
		SetStatus( tr( "Reading the map's textures: %1 of %2" ).arg( m_pResources->GetLoaded() ).arg( m_pResources->GetTotal() ) );
	}
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetCameraMode( DemoCamera_t nMode )
{
	if ( nMode == DemoCamera_t::DEMO_CAMERA_RECORDER && !m_Viewer.GetDemo().HasCamera() )
	{
		nMode = DemoCamera_t::DEMO_CAMERA_FREE;
	}
	if ( nMode == m_nMode )
	{
		return;
	}
	// The free camera carries on from where the last one looked; third person starts behind.
	m_Camera = GetCamera();
	m_bPlaced = true;
	m_nMode = nMode;
	if ( nMode == DemoCamera_t::DEMO_CAMERA_CHASE )
		ResetOrbit();
	emit CameraModeChanged();
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetFollowed( int nEntity )
{
	if ( nEntity == m_nFollowed )
	{
		return;
	}
	m_nFollowed = nEntity;
	emit FollowedChanged( nEntity );
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetShowingNames( bool bNames )
{
	m_bNames = bNames;
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetSeeingThroughWalls( bool bThrough )
{
	m_bThroughWalls = bThrough;
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetShowingFog( bool bFog )
{
	m_bFog = bFog;
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetShowingSkybox3D( bool bSkybox )
{
	m_bSkybox3D = bSkybox;
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetFullbright( bool bFullbright )
{
	m_bFullbright = bFullbright;
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetShowingParticles( bool bParticles )
{
	m_bParticlesOn = bParticles;
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetCamera( const xv::map::CViewCamera &camera )
{
	m_Camera = camera;
	m_bPlaced = true;
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::LookAtPlayers()
{
	SetCameraMode( DemoCamera_t::DEMO_CAMERA_FREE );
	PlaceCamera();
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: The first look: behind and above the middle of the players there at the playhead
//          (or the first tick that has any), facing them.
//-----------------------------------------------------------------------------
void CDemoWorld::PlaceCamera()
{
	std::vector<PlayerState_t> players = GetPlayersShown();
	if ( players.empty() )
	{
		for ( const xv::demo::PlayerTrack_t &track : m_Viewer.GetPlayers().GetTracks() )
		{
			const auto first = std::ranges::find_if( track.m_Samples, []( const xv::demo::PlayerSample_t &sample ) { return sample.m_bThere && sample.m_bAlive && sample.m_nTeam >= 2; } );
			if ( first != track.m_Samples.end() )
			{
				PlayerState_t state;
				state.m_vecOrigin = first->m_vecOrigin;
				players.push_back( state );
			}
		}
	}
	if ( players.empty() )
	{
		// No players to look at: the middle of the map, once there is one.
		if ( const xv::map::CScene *pScene = m_pDocument->GetScene(); pScene && !pScene->m_Bounds.IsEmpty() )
		{
			const Point_t centre = pScene->m_Bounds.GetCenter();
			m_Camera.LookAt( { centre[ 0 ] - 512.0, centre[ 1 ] - 512.0, centre[ 2 ] + 384.0 }, centre );
			m_bPlaced = true;
		}
		return;
	}
	xv::map::Box_t box;
	for ( const PlayerState_t &player : players )
	{
		box.Add( xv::map::ToPoint( player.m_vecOrigin ) );
	}
	const Point_t centre = box.GetCenter();
	const Point_t size = box.GetSize();
	const double flBack = std::max( START_NEAREST, std::min( std::hypot( size[ 0 ], size[ 1 ] ), 2048.0 ) * START_BACK );
	m_Camera.LookAt( { centre[ 0 ] - flBack * 0.7, centre[ 1 ] - flBack * 0.7, centre[ 2 ] + flBack * 0.6 }, { centre[ 0 ], centre[ 1 ], centre[ 2 ] + 36.0 } );
	m_bPlaced = true;
}

//-----------------------------------------------------------------------------
// Purpose: Where this frame is seen from. A following camera whose player isn't there at the
//          playhead (dead, gone, out of the recorder's sight) stays where it last was.
//-----------------------------------------------------------------------------
xv::map::CViewCamera CDemoWorld::GetCamera()
{
	const double flTick = m_Viewer.GetPlayTick();
	switch ( m_nMode )
	{
		case DemoCamera_t::DEMO_CAMERA_RECORDER:
			if ( const auto view = RecorderView( m_Viewer.GetDemo(), flTick ) )
			{
				m_Camera.m_Eye = xv::map::ToPoint( view->m_vecOrigin );
				// The packets' view is where the recorder stands, not its eye (the game adds the
				// view offset when it plays the demo): lifted by it when it is at the recorder's
				// feet, else the camera sits in the floor and the map below shows through.
				if ( const auto player = m_Viewer.GetPlayers().At( m_Viewer.GetPlayers().GetRecorderEntity(), flTick ) )
				{
					const xv::CVec3 &vecFeet = player->m_vecOrigin;
					const xv::CVec3 &vecView = view->m_vecOrigin;
					if ( std::abs( vecFeet.m_flX - vecView.m_flX ) < RECORDER_FEET_SLACK && std::abs( vecFeet.m_flY - vecView.m_flY ) < RECORDER_FEET_SLACK && std::abs( vecFeet.m_flZ - vecView.m_flZ ) < RECORDER_FEET_SLACK )
						m_Camera.m_Eye[ 2 ] += player->GetEye().m_flZ - vecFeet.m_flZ;
				}
				m_Camera.m_flPitch = view->m_vecAngles.m_flX;
				m_Camera.m_flYaw = view->m_vecAngles.m_flY;
				m_bPlaced = true;
			}
			break;
		case DemoCamera_t::DEMO_CAMERA_FIRST_PERSON:
		case DemoCamera_t::DEMO_CAMERA_CHASE:
			if ( const auto player = m_Viewer.GetPlayers().At( m_nFollowed, flTick ) )
			{
				m_Camera.m_Eye = xv::map::ToPoint( player->GetEye() );
				m_Camera.m_flPitch = player->m_flPitch;
				m_Camera.m_flYaw = player->m_flYaw;
				if ( m_nMode == DemoCamera_t::DEMO_CAMERA_CHASE )
				{
					// Turned by hand around the player; where the player looks doesn't move it.
					m_Camera.m_flYaw = m_flOrbitYaw;
					m_Camera.m_flPitch = m_flOrbitPitch;
					const Point_t forward = m_Camera.GetForward();
					m_Camera.m_Eye = { m_Camera.m_Eye[ 0 ] - forward[ 0 ] * m_flChaseDistance, m_Camera.m_Eye[ 1 ] - forward[ 1 ] * m_flChaseDistance, m_Camera.m_Eye[ 2 ] - forward[ 2 ] * m_flChaseDistance + CHASE_RISE };
				}
				m_bPlaced = true;
			}
			break;
		case DemoCamera_t::DEMO_CAMERA_FREE:
			break;
	}
	if ( !m_bPlaced )
	{
		PlaceCamera();
	}
	return m_Camera;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
xv::map::CViewCamera CDemoWorld::SizedCamera()
{
	xv::map::CViewCamera camera = GetCamera();
	camera.m_nWidth = std::max( 1, width() );
	camera.m_nHeight = std::max( 1, height() );
	return camera;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<PlayerState_t> CDemoWorld::GetPlayersShown() const
{
	std::vector<PlayerState_t> players = m_Viewer.GetPlayers().At( m_Viewer.GetPlayTick() );
	std::erase_if( players, []( const PlayerState_t &player ) { return !HasFigure( player ); } );
	return players;
}

//-----------------------------------------------------------------------------
// Purpose: The figures as one mesh of the players' material, which takes its colour from
//          the vertices (carried where a normal would be, as particles carry theirs). The
//          player whose eyes the camera is in has none.
//-----------------------------------------------------------------------------
std::shared_ptr<const xv::map::CMapMesh> CDemoWorld::BuildPlayerMesh( const std::vector<PlayerState_t> &players, std::uint32_t nMaterial ) const
{
	auto pMesh = std::make_shared<xv::map::CMapMesh>();
	for ( const PlayerState_t &player : players )
	{
		if ( player.m_nEntity == GetEarPlayer() )
		{
			continue;
		}
		const QColor color = DemoTeamColor( player.m_nTeam );
		const auto boxes = FigureBoxes( player );
		for ( std::size_t nBox = 0; nBox < boxes.size(); nBox++ )
		{
			// The band at the eyes is near white, so the face shows from far off.
			const std::array<float, 3> base = nBox == 0 ? std::array<float, 3>{ static_cast<float>( color.redF() ), static_cast<float>( color.greenF() ), static_cast<float>( color.blueF() ) } : std::array<float, 3>{ 0.95f, 0.95f, 0.9f };
			for ( const BoxFace_t &face : BOX_FACES )
			{
				const auto nBase = static_cast<std::uint32_t>( pMesh->m_Vertices.size() );
				for ( const int nCorner : face.m_Corners )
				{
					xv::map::MeshVertex_t vertex;
					vertex.m_vecPosition = xv::map::ToVec3( boxes[ nBox ][ static_cast<std::size_t>( nCorner ) ] );
					vertex.m_vecNormal = { base[ 0 ] * face.m_flShade, base[ 1 ] * face.m_flShade, base[ 2 ] * face.m_flShade };
					vertex.m_flAlpha = 1.0f;
					// Over a compiled map the scene's own objects are hidden: its faces' is not.
					vertex.m_nObject = m_pCompiled && m_pCompiled->IsBuilt() ? m_pCompiled->GetObject() : 0;
					pMesh->m_Vertices.push_back( vertex );
				}
				pMesh->m_Indices.insert( pMesh->m_Indices.end(), { nBase, nBase + 1, nBase + 2, nBase, nBase + 2, nBase + 3 } );
			}
		}
	}
	if ( pMesh->m_Indices.empty() )
	{
		return nullptr;
	}
	pMesh->m_Batches.push_back( { nMaterial, ~0u, 0, static_cast<std::uint32_t>( pMesh->m_Indices.size() ) } );
	return pMesh;
}

//-----------------------------------------------------------------------------
// Purpose: The map as the game shows it rather than as Hammer does: no tool textures, no
//          boxes for point entities, the sky behind; then the players.
//-----------------------------------------------------------------------------
MapFrame_t CDemoWorld::MakeFrame( QSize size )
{
	xv::map::CViewCamera camera = GetCamera();
	camera.m_nWidth = std::max( 1, size.width() );
	camera.m_nHeight = std::max( 1, size.height() );
	xv::map::RasterOptions_t options;
	// Lit by the compile's light (CMapCompiled::Apply), unless fullbright.
	options.m_Shade.m_nMode = xv::map::RasterMode_t::RASTER_MODE_LIGHTING;
	options.m_bSelectionEdges = false;
	MapViewSettings_t settings;
	settings.m_bShowGrid = false;
	settings.m_bToolTextures = false;
	settings.m_bNodraw = false;
	settings.m_bHelpers = false;
	settings.m_bParticles = false;
	settings.m_bDetailProps = false;
	settings.m_bFog = m_bFog;
	settings.m_bSkybox3D = m_bSkybox3D;
	MapFrame_t frame = m_pRenderData->Snapshot( camera, options, settings );
	ApplyDemoSky( frame, camera );
	frame.m_pEntityBaseMesh = nullptr;
	frame.m_pEntityParts = nullptr;
	frame.m_pEntityPreviewMesh = nullptr;
	frame.m_pModelBoxMesh = nullptr;
	// The scene's sprites are the editor's (iconsprites; env_sprites as the map placed them):
	// the game shows none of those.
	frame.m_pSpriteMesh = nullptr;
	// The players need the map's material table to put theirs after; before the map is in,
	// the names alone show where they are.
	// The particles are stepped first: that can read new sprites, which adds materials.
	std::shared_ptr<const xv::map::CMapMesh> pParticles;
	std::shared_ptr<const xv::map::CMapMesh> pSprays;
	if ( frame.m_pScene && frame.m_pMaterials && m_bParticlesOn )
	{
		pParticles = StepParticles( camera, static_cast<std::uint32_t>( frame.m_pMaterials->size() ) );
	}
	if ( frame.m_pScene && frame.m_pMaterials )
	{
		const auto &pParticleMaterials = m_pParticles->GetMaterials();
		// The sprays' materials go after the particles': their images may load now.
		const std::uint32_t nSprayFirst = static_cast<std::uint32_t>( frame.m_pMaterials->size() + pParticleMaterials->size() );
		pSprays = MergeMeshes( m_bSprays ? BuildSprayMesh( nSprayFirst ) : nullptr, m_bEntitySprites ? BuildEntitySpriteMesh( camera, nSprayFirst ) : nullptr );
		if ( frame.m_pMaterials.get() != m_pMaterialsFor || pParticleMaterials.get() != m_pParticleMaterialsFor || m_pSprayMaterials.get() != m_pSprayMaterialsFor )
		{
			m_pMaterialsFor = frame.m_pMaterials.get();
			m_pParticleMaterialsFor = pParticleMaterials.get();
			m_pSprayMaterialsFor = m_pSprayMaterials.get();
			auto pMaterials = std::make_shared<std::vector<xv::map::RasterMaterial_t>>( *frame.m_pMaterials );
			pMaterials->insert( pMaterials->end(), pParticleMaterials->begin(), pParticleMaterials->end() );
			pMaterials->insert( pMaterials->end(), m_pSprayMaterials->begin(), m_pSprayMaterials->end() );
			xv::map::RasterMaterial_t material;
			material.m_Shade.m_bUnlit = true;
			material.m_Shade.m_bVertexColor = true;
			material.m_bNoCull = true;
			pMaterials->push_back( material );
			m_pMaterials = std::move( pMaterials );
		}
		frame.m_pMaterials = m_pMaterials;
	}
	// The players as their models where those are loaded (drawn with the map's own faces),
	// else as figures; seen through walls, the figures stay over the models.
	std::vector<CMapCompiled::Drawn_t> placed;
	std::vector<PlayerState_t> figures = GetPlayersShown();
	if ( IsDrawingCompiled() && frame.m_pScene )
	{
		std::set<int> modelled = PlaceFigures( placed );
		if ( !m_bThroughWalls )
		{
			std::erase_if( figures, [ & ]( const PlayerState_t &player ) { return modelled.contains( player.m_nEntity ); } );
		}
	}
	if ( frame.m_pScene && frame.m_pMaterials )
	{
		frame.m_pParticleMesh = MergeMeshes( MergeMeshes( BuildPlayerMesh( figures, static_cast<std::uint32_t>( m_pMaterials->size() - 1 ) ), pParticles ), pSprays );
	}
	if ( IsDrawingCompiled() && frame.m_pScene )
	{
		// The map's own faces in place of its brushes, lit by its own lightmaps, with the
		// brush models and props where the demo has them.
		PlaceEntities( placed );
		// Only what can be seen from where the camera is, as the game draws: the clusters of
		// the camera and of the 3D skybox's camera.
		std::vector<int> clusters;
		if ( const xv::bsp::CVisibility *pVisibility = m_pCompiled->GetMap().m_pVisibility.get(); pVisibility && m_bVisibility )
		{
			clusters.push_back( pVisibility->ClusterAt( camera.m_Eye ) );
			if ( frame.m_Options.m_SkyboxCamera )
				clusters.push_back( pVisibility->ClusterAt( frame.m_Options.m_SkyboxCamera->m_Eye ) );
		}
		m_pCompiled->SetClusters( std::move( clusters ) );
		m_pCompiled->Apply( frame, placed, m_HiddenObjects );
		AddShots( frame );
		if ( m_bFullbright )
		{
			frame.m_pLighting = nullptr;
			frame.m_pMeshLights = nullptr;
			frame.m_Options.m_Shade.m_pLighting = nullptr;
			frame.m_Options.m_Shade.m_nMode = xv::map::RasterMode_t::RASTER_MODE_TEXTURED;
		}
		if ( frame.m_pLighting && frame.m_Options.m_Shade.m_bLightHdr )
		{
			frame.m_flExposure = m_flExposure;
			frame.m_Options.m_Shade.m_flLightScale *= m_flExposure;
		}
	}
	return frame;
}

//-----------------------------------------------------------------------------
// Purpose: The fog and the 3D skybox's camera as the demo has them at the playhead, seen as
//          the HUD's player sees them, over the map's own: its fog controller turned on,
//          off or recoloured, and the sky_camera the player was given. A sky camera outside
//          the room the map's sky_camera stands in keeps the map's (that room's contents are
//          what the sky pass draws).
//-----------------------------------------------------------------------------
void CDemoWorld::ApplyDemoSky( MapFrame_t &frame, const xv::map::CViewCamera &camera )
{
	const xv::demo::CContents *pContents = m_Viewer.GetContents();
	if ( !pContents || !frame.m_pScene )
	{
		return;
	}
	const xv::demo::DemoSky_t sky = xv::demo::SkyAt( *pContents, static_cast<int>( m_Viewer.GetPlayTick() ), GetHudPlayer() );
	if ( m_bFog && sky.m_bFogKnown )
	{
		frame.m_pFog = sky.m_Fog ? std::make_shared<const xv::map::Fog_t>( *sky.m_Fog ) : nullptr;
		frame.m_Options.m_Shade.m_pFog = frame.m_pFog.get();
	}
	const auto &pSkybox = frame.m_pScene->m_Skybox3D;
	if ( sky.m_Camera && pSkybox && frame.m_Options.m_SkyboxCamera )
	{
		const xv::map::Point_t origin = xv::map::ToPoint( sky.m_Camera->m_vecOrigin );
		if ( pSkybox->m_Room.Contains( origin ) )
		{
			xv::map::Skybox3D_t given = *pSkybox;
			given.m_Camera = origin;
			given.m_flScale = sky.m_Camera->m_flScale;
			frame.m_Options.m_SkyboxCamera = xv::map::SkyboxCamera( camera, given );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: The systems the demo has on at the playhead: an info_particle_system where its
//          entity is (after its parents), a dispatched effect where it was sent (or on its
//          entity, when sent without a place), each as old as the demo says. A playhead that
//          went back or leapt starts them again where they'd be.
//-----------------------------------------------------------------------------
std::shared_ptr<const xv::map::CMapMesh> CDemoWorld::StepParticles( const xv::map::CViewCamera &camera, std::uint32_t nFirstMaterial )
{
	const xv::demo::CContents *pContents = m_Viewer.GetContents();
	if ( !pContents || !m_pParticles->IsReady() )
	{
		return nullptr;
	}
	const double flTick = m_Viewer.GetPlayTick();
	const double flInterval = pContents->GetTickInterval() > 0.0 ? pContents->GetTickInterval() : 0.015;
	const double flStep = m_flParticlesTick < 0.0 ? 0.0 : ( flTick - m_flParticlesTick ) * flInterval;
	const bool bJumped = m_flParticlesTick < 0.0 || flStep < 0.0 || flStep > PARTICLE_STEP_MOST;
	m_flParticlesTick = flTick;
	std::vector<CMapParticles::Wanted_t> wanted;
	const int nTick = static_cast<int>( std::floor( flTick ) );
	const int nDispatchTicks = static_cast<int>( std::lround( PARTICLE_DISPATCH_SECONDS / flInterval ) );
	const int nParticleClass = pContents->FindClass( "CParticleSystem" );
	const auto stats = pContents->GetStats( nParticleClass );
	constexpr std::string_view PREFIX = "m_hControlPointEnts.";
	std::vector<std::pair<int, std::size_t>> pointStats;
	for ( std::size_t i = 0; i < stats.size(); ++i )
	{
		const std::string &szStat = stats[ i ];
		if ( !szStat.starts_with( PREFIX ) )
			continue;
		int nIndex = 0;
		const char *pEnd = szStat.data() + szStat.size();
		const auto parsed = std::from_chars( szStat.data() + PREFIX.size(), pEnd, nIndex );
		// The array starts at CP1. CP0 belongs to the system itself.
		if ( parsed.ec == std::errc{} && parsed.ptr == pEnd && nIndex >= 0 && nIndex < xv::particles::CSimulation::CONTROL_POINTS - 1 )
			pointStats.emplace_back( static_cast<int>( i ), static_cast<std::size_t>( nIndex + 1 ) );
	}
	for ( const xv::demo::DemoParticles_t &system : xv::demo::ParticlesAt( *pContents, nTick, nDispatchTicks ) )
	{
		std::optional<xv::CVec3> at;
		if ( system.m_pLife )
		{
			if ( const auto transform = EntityTransform( *system.m_pLife, flTick ) )
				at = xv::map::ToVec3( transform->Apply( { 0, 0, 0 } ) );
		}
		else if ( system.m_vecOrigin != xv::CVec3{} || system.m_nEntity <= 0 )
		{
			at = system.m_vecOrigin;
		}
		else if ( const xv::demo::EntityLife_t *pLife = pContents->LifeAt( system.m_nEntity, nTick ) )
		{
			if ( const auto transform = EntityTransform( *pLife, flTick ) )
				at = xv::map::ToVec3( transform->Apply( { 0, 0, 0 } ) );
		}
		if ( !at )
			continue;
		CMapParticles::Wanted_t want;
		want.m_nKey = system.m_nKey;
		want.m_szEffect = std::string( system.m_szEffect );
		want.m_Points = { at };
		if ( system.m_pLife )
		{
			for ( const auto &[ nStat, nPoint ] : pointStats )
			{
				const auto handle = system.m_pLife->StatAt( nStat, nTick );
				if ( !handle || *handle == xv::demo::ENTITY_NO_HANDLE )
					continue;
				const auto nHandle = static_cast<std::uint32_t>( *handle );
				const auto *pPoint = pContents->LifeAt( static_cast<int>( nHandle & xv::demo::ENTITY_HANDLE_INDEX_MASK ), nTick );
				if ( !pPoint || pPoint->m_nSerial != static_cast<int>( nHandle / ( xv::demo::ENTITY_HANDLE_INDEX_MASK + 1 ) ) )
					continue;
				const auto transform = EntityTransform( *pPoint, flTick );
				if ( !transform )
					continue;
				if ( want.m_Points.size() <= nPoint )
					want.m_Points.resize( nPoint + 1 );
				want.m_Points[ nPoint ] = xv::map::ToVec3( transform->Apply( { 0, 0, 0 } ) );
			}
		}
		want.m_flAge = static_cast<float>( ( flTick - system.m_nStartTick ) * flInterval );
		wanted.push_back( std::move( want ) );
	}
	// Object zero can be a hidden editor brush. Use the compiled geometry's visible object.
	m_pParticles->PlayDemo( wanted, static_cast<float>( std::max( 0.0, flStep ) ), bJumped, xv::map::ToVec3( camera.m_Eye ),
		m_pCompiled && m_pCompiled->IsBuilt() ? m_pCompiled->GetObject() : 0 );
	return m_pParticles->BuildMesh( camera.m_Eye, camera.GetRight(), camera.GetUp(), nFirstMaterial, nullptr, true, false );
}

//-----------------------------------------------------------------------------
// Purpose: Each player shown whose model the map's resources have, posed as it moves (its
//          speed from where it is a little before and after), lit where it stands. The
//          player whose eyes the camera is in has none. Returns the players drawn.
//-----------------------------------------------------------------------------
std::set<int> CDemoWorld::PlaceFigures( std::vector<CMapCompiled::Drawn_t> &drawn )
{
	std::set<int> modelled;
	m_LastFigures.clear();
	const xv::demo::CContents *pContents = m_Viewer.GetContents();
	if ( !pContents || !m_pResources )
	{
		return modelled;
	}
	const double flTick = m_Viewer.GetPlayTick();
	const int nTick = static_cast<int>( std::floor( flTick ) );
	const double flInterval = m_Viewer.GetDemo().GetTickInterval() > 0.0 ? m_Viewer.GetDemo().GetTickInterval() : 1.0 / 66.0;
	const double flSpan = std::max( 1.0, 0.1 / flInterval ); // ticks either side
	const xv::demo::CPlayers &players = m_Viewer.GetPlayers();
	for ( const PlayerState_t &player : GetPlayersShown() )
	{
		if ( player.m_nEntity == GetEarPlayer() )
		{
			continue;
		}
		const xv::demo::EntityLife_t *pLife = pContents->LifeAt( player.m_nEntity, nTick );
		const auto state = pLife ? pContents->StateOf( *pLife, flTick ) : std::nullopt;
		if ( !state )
		{
			continue;
		}
		const std::array<std::string, xv::demo::ENTITY_EXTRA_COUNT> &extras = pContents->GetExtras( pLife->m_nClass );
		CDemoFigures::Figure_t figure;
		figure.m_nEntity = player.m_nEntity;
		figure.m_szModel = Lower( pContents->ModelName( state->m_nModel ) );
		figure.m_nSkin = extras[ 1 ] == "m_nSkin" ? static_cast<int>( state->m_flExtra[ 1 ] ) : 0;
		figure.m_pModel = m_pResources->FindPropModel( { figure.m_szModel, figure.m_nSkin } );
		if ( !figure.m_pModel )
		{
			continue;
		}
		if ( extras[ 0 ] == "m_hActiveWeapon" )
		{
			if ( const xv::demo::EntityLife_t *pWeapon = HandleLife( static_cast<std::uint32_t>( state->m_flExtra[ 0 ] ), nTick ) )
			{
				if ( const auto weapon = pContents->StateOf( *pWeapon, flTick ) )
				{
					figure.m_szWeapon = WorldModelOf( *pContents, *pWeapon, *weapon );
					figure.m_pWeapon = m_pResources->FindPropModel( { figure.m_szWeapon, 0 } );
				}
			}
		}
		figure.m_vecOrigin = player.m_vecOrigin;
		figure.m_flYaw = player.m_flYaw;
		figure.m_flPitch = player.m_flPitch;
		figure.m_bDucked = player.m_bDucked;
		figure.m_flSeconds = flTick * flInterval;
		const auto before = players.At( player.m_nEntity, flTick - flSpan );
		const auto after = players.At( player.m_nEntity, flTick + flSpan );
		if ( before && after )
		{
			figure.m_vecVelocity = ( after->m_vecOrigin - before->m_vecOrigin ) * static_cast<float>( 1.0 / ( 2.0 * flSpan * flInterval ) );
		}
		if ( auto pMesh = m_Figures.Build( figure, m_pCompiled->GetObject() ) )
		{
			const auto &light = FigureLightAt( player.m_nEntity, figure.m_vecOrigin );
			const auto &cell = m_FigureLight.at( player.m_nEntity ).first;
			// The same mesh in the same light as last frame keeps its light.
			auto &lit = m_FigureLit[ player.m_nEntity ];
			if ( lit.first != pMesh || cell != m_FigureLitCell[ player.m_nEntity ] )
			{
				lit = { pMesh, CMapCompiled::LightModel( *pMesh, light ) };
				m_FigureLitCell[ player.m_nEntity ] = cell;
			}
			auto pLights = lit.second;
			drawn.push_back( { std::move( pMesh ), std::move( pLights ) } );
			modelled.insert( player.m_nEntity );
			auto [ szLower, szUpper ] = m_Figures.SequencesFor( figure );
			m_LastFigures.push_back( { player.m_nEntity, figure.m_szModel, figure.m_szWeapon, std::move( szLower ), std::move( szUpper ) } );
		}
	}
	return modelled;
}

//-----------------------------------------------------------------------------
// Purpose: Reuse the ambient sample until the player has moved to another lighting cell.
//-----------------------------------------------------------------------------
const CMapCompiled::ModelLight_t &CDemoWorld::FigureLightAt( int nPlayer, xv::CVec3 vecOrigin )
{
	const std::array<int, 3> cell{ static_cast<int>( std::floor( vecOrigin.m_flX / FIGURE_LIGHT_CELL ) ),
		static_cast<int>( std::floor( vecOrigin.m_flY / FIGURE_LIGHT_CELL ) ), static_cast<int>( std::floor( vecOrigin.m_flZ / FIGURE_LIGHT_CELL ) ) };
	auto found = m_FigureLight.find( nPlayer );
	if ( found == m_FigureLight.end() || found->second.first != cell )
	{
		xv::map::Box_t bounds;
		bounds.Add( xv::map::ToPoint( vecOrigin + xv::CVec3{ -FIGURE_HALF_WIDTH, -FIGURE_HALF_WIDTH, 0.0f } ) );
		bounds.Add( xv::map::ToPoint( vecOrigin + xv::CVec3{ FIGURE_HALF_WIDTH, FIGURE_HALF_WIDTH, FIGURE_HEIGHT } ) );
		found = m_FigureLight.insert_or_assign( nPlayer, std::pair{ cell, m_pCompiled->ModelLightAt( bounds ) } ).first;
	}
	return found->second.second;
}

//-----------------------------------------------------------------------------
// Purpose: The shots of the last moment as tracers, from the shooter's eye along its aim to
//          where that meets the map (each worked out once), and a mark on the ground where a
//          player died in the last few seconds: a cross at its feet and a post up from it.
//-----------------------------------------------------------------------------
void CDemoWorld::AddShots( MapFrame_t &frame )
{
	const xv::demo::CContents *pContents = m_Viewer.GetContents();
	const xv::bsp::CSurfaceLight *pSurface = m_pCompiled ? m_pCompiled->GetSurfaceLight() : nullptr;
	if ( !pContents )
	{
		return;
	}
	const double flTick = m_Viewer.GetPlayTick();
	const int nTick = static_cast<int>( std::floor( flTick ) );
	const double flInterval = m_Viewer.GetDemo().GetTickInterval() > 0.0 ? m_Viewer.GetDemo().GetTickInterval() : 1.0 / 66.0;
	if ( m_nShotClass == SHOT_CLASS_UNKNOWN )
	{
		m_nShotClass = SHOT_CLASS_NONE;
		for ( const xv::demo::TempEntity_t &shot : pContents->GetTempEntities() )
		{
			if ( pContents->ClassName( shot.m_nClass ) == "CTEFireBullets" )
			{
				m_nShotClass = shot.m_nClass;
				break;
			}
		}
	}
	const std::vector<xv::demo::TempEntity_t> &temps = pContents->GetTempEntities();
	const int nShotTicks = std::max( 1, static_cast<int>( std::lround( TRACER_SECONDS / flInterval ) ) );
	auto first = std::ranges::lower_bound( temps, nTick - nShotTicks + 1, {}, &xv::demo::TempEntity_t::m_nTick );
	for ( auto it = first; m_nShotClass >= 0 && it != temps.end() && it->m_nTick <= nTick; ++it )
	{
		if ( it->m_nClass != m_nShotClass )
		{
			continue;
		}
		const auto nIndex = static_cast<std::size_t>( it - temps.begin() );
		const xv::demo::PropValue_t *pOrigin = pContents->TempValue( *it, "m_vecOrigin" );
		if ( !pOrigin )
		{
			continue;
		}
		const xv::map::Point_t from{ ( *pOrigin )[ 0 ], ( *pOrigin )[ 1 ], ( *pOrigin )[ 2 ] };
		// Kept once the map's faces can say where it ends.
		auto end = m_ShotEnds.find( nIndex );
		if ( end == m_ShotEnds.end() || !pSurface )
		{
			const xv::demo::PropValue_t *pPitch = pContents->TempValue( *it, "m_vecAngles[0]" );
			const xv::demo::PropValue_t *pYaw = pContents->TempValue( *it, "m_vecAngles[1]" );
			const double flPitch = ( pPitch ? ( *pPitch )[ 0 ] : 0.0 ) * std::numbers::pi / 180.0;
			const double flYaw = ( pYaw ? ( *pYaw )[ 0 ] : 0.0 ) * std::numbers::pi / 180.0;
			const xv::map::Point_t direction{ std::cos( flPitch ) * std::cos( flYaw ), std::cos( flPitch ) * std::sin( flYaw ), -std::sin( flPitch ) };
			const auto hit = pSurface ? pSurface->Hit( from, direction, TRACER_REACH ) : std::nullopt;
			const xv::map::Point_t to = hit.value_or( xv::map::Add( from, xv::map::Scale( direction, TRACER_REACH ) ) );
			if ( !pSurface )
			{
				frame.m_Lines.push_back( { from, to, TRACER_COLOR, false } );
				continue;
			}
			end = m_ShotEnds.insert_or_assign( nIndex, to ).first;
		}
		frame.m_Lines.push_back( { from, end->second, TRACER_COLOR, false } );
	}
	// Deaths: the player_death events of the last few seconds, at the player's last place.
	const int nDeathKind = pContents->FindEventKind( "player_death" );
	const int nDeathTicks = static_cast<int>( std::lround( DEATH_SECONDS / flInterval ) );
	const std::vector<xv::demo::GameEvent_t> &events = pContents->GetEvents();
	for ( auto it = std::ranges::lower_bound( events, nTick - nDeathTicks, {}, &xv::demo::GameEvent_t::m_nTick ); nDeathKind >= 0 && it != events.end() && it->m_nTick <= nTick; ++it )
	{
		if ( it->m_nKind != nDeathKind )
		{
			continue;
		}
		const xv::demo::Value_t *pUser = pContents->EventValue( *it, "userid" );
		if ( !pUser )
		{
			continue;
		}
		for ( const xv::demo::PlayerTrack_t &track : m_Viewer.GetPlayers().GetTracks() )
		{
			const xv::demo::PlayerIdentity_t *pIdentity = track.IdentityAt( it->m_nTick );
			if ( !pIdentity || pIdentity->m_nUserId != static_cast<int>( pUser->m_flNumber ) )
			{
				continue;
			}
			if ( const auto player = m_Viewer.GetPlayers().At( track.m_nEntity, it->m_nTick - 1 ) )
			{
				const xv::map::Point_t feet = xv::map::ToPoint( player->m_vecOrigin );
				const double flArm = 14.0;
				const auto at = [ & ]( double x, double y, double z ) { return xv::map::Point_t{ feet[ 0 ] + x, feet[ 1 ] + y, feet[ 2 ] + z }; };
				frame.m_Lines.push_back( { at( -flArm, -flArm, 2.0 ), at( flArm, flArm, 2.0 ), DEATH_COLOR, false } );
				frame.m_Lines.push_back( { at( -flArm, flArm, 2.0 ), at( flArm, -flArm, 2.0 ), DEATH_COLOR, false } );
				frame.m_Lines.push_back( { at( 0.0, 0.0, 2.0 ), at( 0.0, 0.0, 48.0 ), DEATH_COLOR, false } );
			}
			break;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: The life a handle names at a tick: its entity's, if the serial is the same.
//-----------------------------------------------------------------------------
const xv::demo::EntityLife_t *CDemoWorld::HandleLife( std::uint32_t nHandle, int nTick ) const
{
	const xv::demo::CContents *pContents = m_Viewer.GetContents();
	if ( !pContents || nHandle == xv::demo::ENTITY_NO_HANDLE )
	{
		return nullptr;
	}
	const xv::demo::EntityLife_t *pLife = pContents->LifeAt( static_cast<int>( nHandle & xv::demo::ENTITY_HANDLE_INDEX_MASK ), nTick );
	return pLife && pLife->m_nSerial == static_cast<int>( nHandle >> HANDLE_SERIAL_SHIFT ) ? pLife : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: The compiled faces on the scene's materials. The map's entities the demo draws
//          itself (its props) are hidden, with the templates its props are copies of.
//-----------------------------------------------------------------------------
void CDemoWorld::BuildCompiledMeshes()
{
	m_PropTemplates.clear();
	m_HiddenObjects.clear();
	m_Placed.clear();
	const auto pScene = m_pDocument->GetSharedScene();
	if ( !pScene )
	{
		return;
	}
	const xv::demo::CContents *pContents = m_Viewer.GetContents();
	for ( std::size_t nEntity = 0; nEntity < pScene->m_Map.m_Entities.size(); nEntity++ )
	{
		const xv::map::Entity_t &entity = pScene->m_Map.m_Entities[ nEntity ];
		const std::string_view szModel = entity.GetValue( "model" );
		if ( entity.GetValue( "targetname" ) == PROP_TEMPLATE_NAME )
		{
			int nSkin = 0;
			const std::string_view szSkin = entity.GetValue( "skin" );
			(void)std::from_chars( szSkin.data(), szSkin.data() + szSkin.size(), nSkin );
			m_PropTemplates[ { Lower( szModel ), nSkin } ] = pScene->EntityObject( nEntity );
			m_HiddenObjects.push_back( pScene->EntityObject( nEntity ) );
		}
		else if ( pContents && IsStudioModel( szModel ) && entity.GetClassname() != "prop_static" )
		{
			// Networked: the demo has it where it was, if it was there at all.
			m_HiddenObjects.push_back( pScene->EntityObject( nEntity ) );
		}
	}
	std::ranges::sort( m_HiddenObjects );
	if ( m_pCompiled )
	{
		m_pCompiled->Build( *pScene );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Where an entity is: its place and angles at the tick (between snapshots), after
//          its parent's (a parented entity's are its parent's way of seeing it). The
//          attachment it may hang from isn't followed (that needs the parent's pose).
//-----------------------------------------------------------------------------
std::optional<xv::map::Transform_t> CDemoWorld::EntityTransform( const xv::demo::EntityLife_t &life, double flTick, int nDepth ) const
{
	const xv::demo::CContents *pContents = m_Viewer.GetContents();
	const auto state = pContents ? pContents->StateOf( life, flTick ) : std::nullopt;
	if ( !state )
	{
		return std::nullopt;
	}
	const xv::map::Transform_t local = xv::map::Transform_t::FromAngles( xv::map::ToPoint( state->m_vecOrigin ), { state->m_vecAngles.m_flX, state->m_vecAngles.m_flY, state->m_vecAngles.m_flZ } );
	if ( state->m_nParent > 0 && nDepth < PARENT_DEPTH )
	{
		if ( const xv::demo::EntityLife_t *pParent = pContents->LifeAt( state->m_nParent, static_cast<int>( std::floor( flTick ) ) ) )
		{
			if ( const auto parent = EntityTransform( *pParent, flTick, nDepth + 1 ) )
			{
				return *parent * local;
			}
		}
	}
	return local;
}

//-----------------------------------------------------------------------------
// Purpose: The brush models and props as the demo has them at the playhead: each entity whose
//          model is the map's ("*3") or a studio model, drawn where it is and only while the
//          game draws it. Without the demo's entities, the map's brush models stand where the
//          entity lump puts them. A placement that hasn't moved keeps its mesh.
//-----------------------------------------------------------------------------
void CDemoWorld::PlaceEntities( std::vector<CMapCompiled::Drawn_t> &drawn )
{
	std::map<std::pair<const void *, int>, Placed_t> placed;
	m_LastPlaced.clear();
	m_nPropsPosed = 0;
	const std::uint32_t nObject = m_pCompiled->GetObject();
	const auto place = [ & ]( const void *pKey, int nPart, const std::shared_ptr<const xv::map::CMapMesh> &pMesh, const xv::map::Transform_t &transform, const std::shared_ptr<const std::vector<xv::map::LightVertex_t>> &pLights ) {
		std::shared_ptr<const xv::map::CMapMesh> pOut;
		std::shared_ptr<const std::vector<xv::map::LightVertex_t>> pOutLights = pLights;
		if ( const auto found = m_Placed.find( { pKey, nPart } ); found != m_Placed.end() && found->second.m_pSource == pMesh && SameTransform( found->second.m_Transform, transform ) )
		{
			pOut = found->second.m_pMesh;
			pOutLights = found->second.m_pLights;
		}
		else
		{
			auto pNew = std::make_shared<xv::map::CMapMesh>();
			xv::map::AppendPlaced( *pNew, *pMesh, transform, nObject );
			// A prop is lit where it now stands; a brush model keeps its lightmaps.
			if ( !pOutLights )
			{
				pOutLights = m_pCompiled->LightPlaced( *pNew );
			}
			pOut = std::move( pNew );
		}
		placed[ { pKey, nPart } ] = { transform, pOut, pOutLights, pMesh };
		drawn.push_back( { pOut, pOutLights } );
	};

	const xv::demo::CContents *pContents = m_Viewer.GetContents();
	if ( !pContents )
	{
		drawn = m_pCompiled->GetLumpPlacements();
		for ( const xv::bsp::ModelPlacement_t &placement : m_pCompiled->GetMap().m_Placements )
		{
			if ( placement.m_nModel > 0 && m_pCompiled->GetModelMesh( static_cast<std::size_t>( placement.m_nModel ) ) )
			{
				m_LastPlaced.emplace_back( 0, "*" + std::to_string( placement.m_nModel ) );
			}
		}
	}
	else
	{
		const double flTick = m_Viewer.GetPlayTick();
		const int nTick = static_cast<int>( std::floor( flTick ) );
		const int nMaxClients = pContents->GetServerInfo() ? pContents->GetServerInfo()->m_nMaxClients : 0;
		// Where something carried by a player is (through its parents): on the attachment
		// it hangs from, else in the hand when what it hangs from is a held weapon, else where
		// the player stands; its own place and its parents' below the player added to that.
		// nullopt when it isn't carried; a held weapon itself is the figure's to draw.
		struct Carried_t
		{
			std::optional<xv::map::Transform_t> m_Transform;
			bool m_bHeldWeapon = false;
		};
		const auto carried = [ & ]( const xv::demo::EntityLife_t &life, const xv::demo::EntityState_t &state ) -> std::optional<Carried_t> {
			const auto localOf = []( const xv::demo::EntityState_t &at ) { return xv::map::Transform_t::FromAngles( xv::map::ToPoint( at.m_vecOrigin ), { at.m_vecAngles.m_flX, at.m_vecAngles.m_flY, at.m_vecAngles.m_flZ } ); };
			const auto isWeapon = [ & ]( const xv::demo::EntityLife_t &of ) { return pContents->GetExtras( of.m_nClass )[ 0 ] == "m_iWorldModelIndex"; };
			xv::map::Transform_t below = localOf( state );
			xv::demo::EntityState_t child = state;
			const xv::demo::EntityLife_t *pChild = &life;
			for ( int nDepth = 0; child.m_nParent > 0 && nDepth < PARENT_DEPTH; nDepth++ )
			{
				const int nParent = child.m_nParent;
				if ( nParent <= nMaxClients )
				{
					Carried_t out;
					out.m_bHeldWeapon = pChild == &life && isWeapon( life );
					std::optional<xv::map::Transform_t> anchor;
					if ( child.m_nParentAttachment > 0 )
					{
						anchor = m_Figures.AttachmentOf( nParent, child.m_nParentAttachment - 1 );
					}
					else if ( isWeapon( *pChild ) )
					{
						anchor = m_Figures.HandOf( nParent );
					}
					if ( !anchor )
					{
						if ( const auto player = m_Viewer.GetPlayers().At( nParent, flTick ) )
						{
							anchor = xv::map::Transform_t::FromAngles( xv::map::ToPoint( player->m_vecOrigin ), { 0.0, player->m_flYaw, 0.0 } );
						}
					}
					if ( anchor )
					{
						out.m_Transform = *anchor * below;
					}
					return out;
				}
				const xv::demo::EntityLife_t *pParent = pContents->LifeAt( nParent, nTick );
				const auto parent = pParent ? pContents->StateOf( *pParent, flTick ) : std::nullopt;
				if ( !parent )
				{
					return std::nullopt;
				}
				// Hanging from a held weapon: in the hand, wherever the weapon says it is.
				below = isWeapon( *pParent ) && parent->m_nParent > 0 && parent->m_nParent <= nMaxClients ? below : localOf( *parent ) * below;
				child = *parent;
				pChild = pParent;
			}
			return std::nullopt;
		};
		const std::shared_ptr<const xv::map::MeshParts_t> pParts = m_pResources->GetPropParts();
		for ( const xv::demo::EntityLife_t &life : pContents->GetLives() )
		{
			// One out of the recorder's sight (a player's own demo) still stands where it was
			// last seen: a brush entity making a floor doesn't vanish behind the player.
			double flAt = flTick;
			if ( !life.IsThere( nTick ) )
			{
				const auto seen = life.LastSeen( nTick );
				if ( !seen )
				{
					continue;
				}
				flAt = *seen;
			}
			const auto state = pContents->StateOf( life, flAt );
			if ( !state || !IsDrawnState( *state ) )
			{
				continue;
			}
			const std::string_view szModel = pContents->ModelName( state->m_nModel );
			const int nModel = BrushModel( szModel );
			if ( nModel > 0 && static_cast<std::size_t>( nModel ) < m_pCompiled->GetModelCount() )
			{
				const auto &pModelMesh = m_pCompiled->GetModelMesh( static_cast<std::size_t>( nModel ) );
				if ( !pModelMesh )
				{
					continue;
				}
				if ( const auto transform = EntityTransform( life, flAt ) )
				{
					place( &life, nModel, pModelMesh, *transform, m_pCompiled->GetMap().m_Models[ static_cast<std::size_t>( nModel ) ].m_pLights );
					m_LastPlaced.emplace_back( life.m_nEntity, std::string( szModel ) );
				}
				continue;
			}
			if ( !IsStudioModel( szModel ) || !IsPropClass( pContents->ClassName( life.m_nClass ) ) || !pParts )
			{
				continue;
			}
			const auto carrier = carried( life, *state );
			if ( carrier && ( carrier->m_bHeldWeapon || !carrier->m_Transform ) )
			{
				continue;
			}
			// A weapon shows its world model (its own is its holder's view model while held).
			const bool bWeapon = pContents->GetExtras( life.m_nClass )[ 0 ] == "m_iWorldModelIndex";
			const std::string szShown = bWeapon ? WorldModelOf( *pContents, life, *state ) : Lower( szModel );
			const int nSkin = pContents->GetExtras( life.m_nClass )[ 1 ] == "m_nSkin" ? static_cast<int>( state->m_flExtra[ 1 ] ) : 0;
			const auto found = m_PropTemplates.find( { szShown, nSkin } );
			if ( found == m_PropTemplates.end() )
			{
				continue;
			}
			const auto part = pParts->find( found->second );
			if ( part == pParts->end() || !part->second )
			{
				continue;
			}
			// A prop_dynamic in its sequence, at the cycle the game would have it in.
			std::shared_ptr<const xv::map::CMapMesh> pShape = part->second;
			if ( pContents->GetExtras( life.m_nClass )[ 0 ] == "m_nSequence" && m_Figures.HasStudio( szShown ) )
			{
				const int nSequence = static_cast<int>( state->m_flExtra[ 0 ] );
				CDemoFigures::PropPose_t pose;
				pose.m_szModel = szShown;
				pose.m_pModel = m_pResources->FindPropModel( { szShown, nSkin } );
				pose.m_nSkin = nSkin;
				pose.m_nSequence = nSequence;
				pose.m_flCycle = PropCycle( life, szShown, nSequence, flAt );
				if ( auto pPosed = m_Figures.PoseProp( pose, nObject ) )
				{
					pShape = std::move( pPosed );
					m_nPropsPosed++;
				}
			}
			if ( const auto transform = carrier ? carrier->m_Transform : EntityTransform( life, flAt ) )
			{
				place( &life, -1 - static_cast<int>( found->second ), pShape, *transform, nullptr );
				m_LastPlaced.emplace_back( life.m_nEntity, szShown );
			}
		}
	}
	m_Placed = std::move( placed );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetShowingEntitySprites( bool bShow )
{
	m_bEntitySprites = bShow;
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: The sprite's VMT and texture read as the particles read theirs; added (glows) or
//          blended, unlit, coloured by the vertices.
//-----------------------------------------------------------------------------
std::optional<std::uint32_t> CDemoWorld::SpriteMaterial( const std::string &szMaterial, bool bAdditive )
{
	const auto key = std::pair{ szMaterial, bAdditive };
	if ( const auto found = m_SpriteMaterialOf.find( key ); found != m_SpriteMaterialOf.end() )
		return found->second;
	std::optional<std::uint32_t> nMaterial;
	if ( const std::shared_ptr<const xv::vfs::CFileSystem> pFiles = m_pMapFiles )
	{
		xv::particles::Sprite_t sprite = xv::particles::LoadSprite( szMaterial, [ pFiles ]( std::string_view szPath ) { return pFiles->Read( szPath ); } );
		if ( sprite.m_szProblem.empty() && !sprite.m_Mips.empty() && sprite.m_bVisible )
		{
			auto pTexture = std::make_shared<xv::map::RasterTexture_t>();
			pTexture->m_Mips = std::move( sprite.m_Mips );
			pTexture->m_nWidth = pTexture->m_Mips.front().m_nWidth;
			pTexture->m_nHeight = pTexture->m_Mips.front().m_nHeight;
			pTexture->m_bClampS = pTexture->m_bClampT = true;
			xv::map::RasterMaterial_t material;
			material.m_Shade.m_pBase = pTexture.get();
			material.m_Shade.m_nWidth = pTexture->m_nWidth;
			material.m_Shade.m_nHeight = pTexture->m_nHeight;
			material.m_Shade.m_bUnlit = true;
			material.m_Shade.m_bVertexColor = true;
			material.m_Shade.m_bAdditive = bAdditive || sprite.m_nBlend != xv::particles::Sprite_t::Blend_t::BLEND_ALPHA;
			material.m_Shade.m_bTranslucent = !material.m_Shade.m_bAdditive;
			material.m_Shade.m_bPremultiplied = true;
			material.m_bNoCull = true;
			auto pMaterials = std::make_shared<std::vector<xv::map::RasterMaterial_t>>( *m_pSprayMaterials );
			nMaterial = static_cast<std::uint32_t>( pMaterials->size() );
			pMaterials->push_back( material );
			m_pSprayMaterials = std::move( pMaterials );
			m_SprayTextures.push_back( std::move( pTexture ) );
		}
	}
	m_SpriteMaterialOf[ key ] = nMaterial;
	return nMaterial;
}

//-----------------------------------------------------------------------------
// Purpose: Each CSprite the demo has drawn at the playhead: where its entity is (after its
//          parents), its texture's size times its scale, its colour times its brightness
//          (premultiplied, so a glow adds that much), facing the camera.
//-----------------------------------------------------------------------------
std::shared_ptr<const xv::map::CMapMesh> CDemoWorld::BuildEntitySpriteMesh( const xv::map::CViewCamera &camera, std::uint32_t nFirstMaterial )
{
	m_nEntitySpritesDrawn = 0;
	const xv::demo::CContents *pContents = m_Viewer.GetContents();
	if ( !pContents || !m_pCompiled )
		return nullptr;
	const double flTick = m_Viewer.GetPlayTick();
	const int nTick = static_cast<int>( std::floor( flTick ) );
	const Point_t right = camera.GetRight();
	const Point_t up = camera.GetUp();
	auto pMesh = std::make_shared<xv::map::CMapMesh>();
	for ( const xv::demo::EntityLife_t &life : pContents->GetLives() )
	{
		const auto &extras = pContents->GetExtras( life.m_nClass );
		if ( ( extras[ 0 ] != "m_flBrightness" && extras[ 0 ] != "m_nBrightness" ) || !life.IsThere( nTick ) )
			continue;
		const auto state = pContents->StateOf( life, flTick );
		if ( !state || !IsDrawnState( *state ) )
			continue;
		std::string szMaterial = Lower( pContents->ModelName( state->m_nModel ) );
		if ( szMaterial.ends_with( ".vmt" ) || szMaterial.ends_with( ".spr" ) )
			szMaterial.resize( szMaterial.size() - 4 );
		if ( szMaterial.starts_with( "materials/" ) )
			szMaterial.erase( 0, 10 );
		if ( szMaterial.empty() )
			continue;
		const bool bAdditive = state->m_nRenderMode == RENDER_MODE_GLOW || state->m_nRenderMode == RENDER_MODE_ADDITIVE || state->m_nRenderMode == RENDER_MODE_WORLD_GLOW;
		const auto nMaterial = SpriteMaterial( szMaterial, bAdditive );
		const auto transform = EntityTransform( life, flTick );
		if ( !nMaterial || !transform )
			continue;
		const float flBrightness = std::clamp( state->m_flExtra[ 0 ], 0.0f, 255.0f ) / 255.0f;
		const float flScale = state->m_flExtra[ 1 ] > 0.0f ? state->m_flExtra[ 1 ] : 1.0f;
		const xv::map::RasterTexture_t *pTexture = ( *m_pSprayMaterials )[ *nMaterial ].m_Shade.m_pBase;
		const double flHalfW = pTexture->m_nWidth * flScale * 0.5;
		const double flHalfH = pTexture->m_nHeight * flScale * 0.5;
		const float flAlpha = flBrightness * ( state->m_nRenderMode == RENDER_MODE_NORMAL ? 1.0f : state->GetAlpha() / 255.0f );
		if ( flAlpha <= 0.0f )
			continue;
		const Point_t centre = transform->Apply( { 0, 0, 0 } );
		const float flR = static_cast<float>( state->m_nColor & 0xff ) / 255.0f, flG = static_cast<float>( ( state->m_nColor >> 8 ) & 0xff ) / 255.0f, flB = static_cast<float>( ( state->m_nColor >> 16 ) & 0xff ) / 255.0f;
		const auto nBase = static_cast<std::uint32_t>( pMesh->m_Vertices.size() );
		const std::array<std::array<double, 2>, 4> corners = { { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, -1 } } };
		for ( const auto &corner : corners )
		{
			xv::map::MeshVertex_t vertex;
			vertex.m_vecPosition = xv::map::ToVec3( xv::map::Add( centre, xv::map::Add( xv::map::Scale( right, corner[ 0 ] * flHalfW ), xv::map::Scale( up, corner[ 1 ] * flHalfH ) ) ) );
			// Premultiplied: the colour carries the alpha.
			vertex.m_vecNormal = { flR * flAlpha, flG * flAlpha, flB * flAlpha };
			vertex.m_flAlpha = flAlpha;
			vertex.m_Texel = { corner[ 0 ] < 0 ? 0.0f : static_cast<float>( pTexture->m_nWidth ), corner[ 1 ] > 0 ? 0.0f : static_cast<float>( pTexture->m_nHeight ) };
			vertex.m_nObject = m_pCompiled->GetObject();
			pMesh->m_Vertices.push_back( vertex );
		}
		const auto nFirst = static_cast<std::uint32_t>( pMesh->m_Indices.size() );
		pMesh->m_Indices.insert( pMesh->m_Indices.end(), { nBase, nBase + 1, nBase + 2, nBase, nBase + 2, nBase + 3 } );
		pMesh->m_Batches.push_back( { nFirstMaterial + *nMaterial, ~0u, nFirst, 6 } );
		m_nEntitySpritesDrawn++;
	}
	return pMesh->m_Indices.empty() ? nullptr : pMesh;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetShowingSprays( bool bShow )
{
	m_bSprays = bShow;
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: The logo's VTF from the game's download folder, its mips made, as an unlit
//          see-through material (sprays are drawn as they are, not lit).
//-----------------------------------------------------------------------------
std::optional<std::uint32_t> CDemoWorld::SprayMaterial( std::uint32_t nLogo )
{
	if ( const auto found = m_SprayMaterialOf.find( nLogo ); found != m_SprayMaterialOf.end() )
		return found->second;
	std::optional<std::uint32_t> nMaterial;
	if ( m_Game && nLogo != 0 )
	{
		QFile file( QString::fromStdU16String( ( m_Game->m_GameDir / xv::demo::SprayFile( nLogo ) ).u16string() ) );
		if ( file.open( QIODevice::ReadOnly ) )
		{
			const QByteArray bytes = file.readAll();
			const std::vector<std::byte> data( reinterpret_cast<const std::byte *>( bytes.constData() ), reinterpret_cast<const std::byte *>( bytes.constData() ) + bytes.size() );
			const auto vtf = xv::texture::ReadVtf( data );
			const auto image = vtf ? vtf->Decode() : std::nullopt;
			if ( image )
			{
				auto pTexture = std::make_shared<xv::map::RasterTexture_t>();
				pTexture->m_Mips = { *image };
				while ( pTexture->m_Mips.back().m_nWidth > 1 || pTexture->m_Mips.back().m_nHeight > 1 )
					pTexture->m_Mips.push_back( xv::texture::HalfSize( pTexture->m_Mips.back() ) );
				pTexture->m_nWidth = image->m_nWidth;
				pTexture->m_nHeight = image->m_nHeight;
				pTexture->m_bClampS = pTexture->m_bClampT = true;
				xv::map::RasterMaterial_t material;
				material.m_Shade.m_pBase = pTexture.get();
				material.m_Shade.m_nWidth = pTexture->m_nWidth;
				material.m_Shade.m_nHeight = pTexture->m_nHeight;
				material.m_Shade.m_bUnlit = true;
				material.m_Shade.m_bVertexColor = true;
				material.m_Shade.m_bTranslucent = true;
				material.m_bNoCull = true;
				auto pMaterials = std::make_shared<std::vector<xv::map::RasterMaterial_t>>( *m_pSprayMaterials );
				nMaterial = static_cast<std::uint32_t>( pMaterials->size() );
				pMaterials->push_back( material );
				m_pSprayMaterials = std::move( pMaterials );
				m_SprayTextures.push_back( std::move( pTexture ) );
			}
		}
	}
	m_SprayMaterialOf[ nLogo ] = nMaterial;
	return nMaterial;
}

//-----------------------------------------------------------------------------
// Purpose: Each spray up as a square SPRAY_SIZE across on the surface: its way found from
//          where rays along the axes meet the world beside the point, its top toward the
//          world's up (on a floor, toward north), a little off the surface.
//-----------------------------------------------------------------------------
std::shared_ptr<const xv::map::CMapMesh> CDemoWorld::BuildSprayMesh( std::uint32_t nFirstMaterial )
{
	m_nSpraysDrawn = 0;
	const xv::demo::CContents *pContents = m_Viewer.GetContents();
	const xv::bsp::CSurfaceLight *pSurface = m_pCompiled ? m_pCompiled->GetSurfaceLight() : nullptr;
	if ( !pContents || !pSurface )
		return nullptr;
	if ( !m_pSprays )
		m_pSprays = std::make_unique<xv::demo::CSprays>( *pContents, m_Viewer.GetPlayers() );
	auto pMesh = std::make_shared<xv::map::CMapMesh>();
	for ( const xv::demo::DemoSpray_t &spray : m_pSprays->At( static_cast<int>( m_Viewer.GetPlayTick() ) ) )
	{
		const auto nMaterial = SprayMaterial( spray.m_nLogo );
		if ( !nMaterial )
			continue;
		const Point_t at = xv::map::ToPoint( spray.m_vecOrigin );
		// The axis whose ray from just before the point meets the surface nearest it.
		std::optional<Point_t> way;
		double flNearest = SPRAY_REACH;
		for ( const Point_t direction : { Point_t{ 1, 0, 0 }, Point_t{ -1, 0, 0 }, Point_t{ 0, 1, 0 }, Point_t{ 0, -1, 0 }, Point_t{ 0, 0, 1 }, Point_t{ 0, 0, -1 } } )
		{
			const Point_t from = xv::map::Subtract( at, xv::map::Scale( direction, SPRAY_REACH ) );
			if ( const auto hit = pSurface->Hit( from, direction, SPRAY_REACH * 2.0 ) )
			{
				const double flAway = xv::map::Length( xv::map::Subtract( *hit, at ) );
				if ( flAway < flNearest )
				{
					flNearest = flAway;
					way = direction;
				}
			}
		}
		if ( !way )
			continue;
		// The surface's own facing, from three hits along that axis near the point.
		const Point_t side = std::abs( ( *way )[ 2 ] ) > 0.5 ? Point_t{ 1, 0, 0 } : Point_t{ 0, 0, 1 };
		const Point_t other = xv::map::Normalized( xv::map::Cross( *way, side ) );
		const auto hitAt = [ & ]( Point_t offset ) { return pSurface->Hit( xv::map::Subtract( xv::map::Add( at, offset ), xv::map::Scale( *way, SPRAY_REACH ) ), *way, SPRAY_REACH * 2.0 ); };
		const auto p0 = hitAt( { 0, 0, 0 } );
		const auto p1 = hitAt( xv::map::Scale( side, 4.0 ) );
		const auto p2 = hitAt( xv::map::Scale( other, 4.0 ) );
		Point_t normal = xv::map::Scale( *way, -1.0 );
		if ( p0 && p1 && p2 )
		{
			const Point_t found = xv::map::Normalized( xv::map::Cross( xv::map::Subtract( *p1, *p0 ), xv::map::Subtract( *p2, *p0 ) ) );
			if ( xv::map::Length( found ) > 0.5 )
				normal = xv::map::Dot( found, normal ) < 0.0 ? xv::map::Scale( found, -1.0 ) : found;
		}
		const Point_t centre = xv::map::Add( p0 ? *p0 : at, xv::map::Scale( normal, SPRAY_LIFT ) );
		Point_t up = std::abs( normal[ 2 ] ) > 0.9 ? Point_t{ 0, 1, 0 } : Point_t{ 0, 0, 1 };
		up = xv::map::Normalized( xv::map::Subtract( up, xv::map::Scale( normal, xv::map::Dot( up, normal ) ) ) );
		const Point_t right = xv::map::Normalized( xv::map::Cross( up, normal ) );
		const xv::map::RasterTexture_t *pTexture = ( *m_pSprayMaterials )[ *nMaterial ].m_Shade.m_pBase;
		const float flW = static_cast<float>( pTexture->m_nWidth ), flH = static_cast<float>( pTexture->m_nHeight );
		const double flHalf = SPRAY_SIZE * 0.5;
		const auto nBase = static_cast<std::uint32_t>( pMesh->m_Vertices.size() );
		const std::array<std::array<double, 2>, 4> corners = { { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, -1 } } };
		for ( const auto &corner : corners )
		{
			xv::map::MeshVertex_t vertex;
			vertex.m_vecPosition = xv::map::ToVec3( xv::map::Add( centre, xv::map::Add( xv::map::Scale( right, corner[ 0 ] * flHalf ), xv::map::Scale( up, corner[ 1 ] * flHalf ) ) ) );
			vertex.m_vecNormal = { 1.0f, 1.0f, 1.0f }; // the colour: as it is
			vertex.m_flAlpha = 1.0f;
			vertex.m_Texel = { corner[ 0 ] < 0 ? 0.0f : flW, corner[ 1 ] > 0 ? 0.0f : flH };
			// The compiled faces' object, which is always shown (the scene's own are hidden).
			vertex.m_nObject = m_pCompiled->GetObject();
			pMesh->m_Vertices.push_back( vertex );
		}
		const auto nFirst = static_cast<std::uint32_t>( pMesh->m_Indices.size() );
		pMesh->m_Indices.insert( pMesh->m_Indices.end(), { nBase, nBase + 1, nBase + 2, nBase, nBase + 2, nBase + 3 } );
		pMesh->m_Batches.push_back( { nFirstMaterial + *nMaterial, ~0u, nFirst, 6 } );
		m_nSpraysDrawn++;
	}
	return pMesh->m_Indices.empty() ? nullptr : pMesh;
}

//-----------------------------------------------------------------------------
// Purpose: The cycle as the game's client works it out: from when the sequence (or the parity
//          that starts it again) last changed, at the playback rate, wrapping when the
//          sequence loops; the server's own cycle when it sends one that moves.
//-----------------------------------------------------------------------------
double CDemoWorld::PropCycle( const xv::demo::EntityLife_t &life, const std::string &szModel, int nSequence, double flAt ) const
{
	const xv::demo::CContents *pContents = m_Viewer.GetContents();
	const auto timing = m_Figures.SequenceTiming( szModel, nSequence );
	if ( !pContents || !timing || timing->m_flSeconds <= 0.0 )
		return 0.0;
	const int nTick = static_cast<int>( std::floor( flAt ) );
	const auto stat = [ & ]( std::string_view szName ) -> const std::vector<xv::demo::StatKey_t> * {
		const int nStat = pContents->FindStat( life.m_nClass, szName );
		return nStat >= 0 && static_cast<std::size_t>( nStat ) < life.m_Stats.size() ? &life.m_Stats[ static_cast<std::size_t>( nStat ) ] : nullptr;
	};
	if ( const auto *pCycles = stat( "m_flCycle" ); pCycles && pCycles->size() > 2 )
	{
		if ( const auto flCycle = pContents->StatOf( life, "m_flCycle", nTick ) )
			return *flCycle;
	}
	double flStart = life.m_nFrom;
	if ( const auto nStart = RunStart( life.m_Fields[ xv::demo::ENTITY_FIELD_EXTRA_0 ], flAt, []( const xv::demo::EntityKey_t &key ) { return key.m_flValue[ 0 ]; } ) )
		flStart = std::max( flStart, static_cast<double>( *nStart ) );
	if ( const auto *pParities = stat( "m_nNewSequenceParity" ) )
	{
		if ( const auto nStart = RunStart( *pParities, flAt, []( const xv::demo::StatKey_t &key ) { return key.m_flValue; } ) )
			flStart = std::max( flStart, static_cast<double>( *nStart ) );
	}
	const double flRate = pContents->StatOf( life, "m_flPlaybackRate", nTick ).value_or( 1.0f );
	const double flInterval = pContents->GetTickInterval() > 0.0 ? pContents->GetTickInterval() : 0.015;
	const double flCycle = std::max( 0.0, flAt - flStart ) * flInterval * flRate / timing->m_flSeconds;
	return timing->m_bLoops ? flCycle - std::floor( flCycle ) : std::min( flCycle, 1.0 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::vector<std::pair<int, std::string>> CDemoWorld::GetPlacedModels()
{
	(void)MakeFrame( QSize( 64, 64 ) );
	return m_LastPlaced;
}

//-----------------------------------------------------------------------------
// Purpose: A measured exposure: drawn again with it while it still moves, and measured again
//          a little later, so it settles as the game's does when the eye adapts.
//-----------------------------------------------------------------------------
void CDemoWorld::ExposureMeasured( float flExposure )
{
	if ( std::abs( std::log2( flExposure / std::max( m_flExposure, 1.0e-6f ) ) ) < 0.02f )
	{
		return;
	}
	m_flExposure = flExposure;
	Redraw();
	QTimer::singleShot( 200, this, [ this ] { Redraw(); } );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::Redraw()
{
	if ( m_pGpu )
	{
		m_pGpu->update();
		if ( m_pOverlay )
		{
			m_pOverlay->update();
		}
		return;
	}
	// Software draws half as fine: a whole map at the widget's size is slow to fill.
	const QSize size = ( QSizeF( this->size() ) * devicePixelRatioF() ).toSize() / 2;
	if ( size.isEmpty() || !isVisible() )
	{
		update();
		return;
	}
	MapFrame_t frame = MakeFrame( size );
	{
		const std::lock_guard lock( m_Mutex );
		m_Pending = std::move( frame );
	}
	m_Wake.notify_all();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::WorkerLoop()
{
	xv::texture::CImage image;
	for ( ;; )
	{
		MapFrame_t frame;
		{
			std::unique_lock lock( m_Mutex );
			m_Wake.wait( lock, [ this ] { return m_bQuit || m_Pending.has_value(); } );
			if ( m_bQuit )
			{
				return;
			}
			frame = std::move( *m_Pending );
			m_Pending.reset();
		}
		xv::map::RenderView( frame.MakeScene(), frame.m_Camera, frame.m_Options, image );
		QImage picture = ToQImage( image );
		// HDR's next exposure, from this picture (every fourth pixel each way).
		const float flExposure = frame.m_flExposure > 0.0f ? xv::map::NextExposure( xv::map::ShownLuminance( image.m_Pixels.data(), image.m_nWidth, image.m_nHeight, static_cast<std::size_t>( image.m_nWidth ) * 4, 4, frame.m_Options.m_Shade.m_flMonitorGamma ), frame.m_flExposure, frame.m_flExposureMin, frame.m_flExposureMax ) : 0.0f;
		QMetaObject::invokeMethod(
			this, [ this, picture = std::move( picture ), flExposure ]() mutable {
				m_Image = std::move( picture );
				update();
				if ( flExposure > 0.0f )
				{
					ExposureMeasured( flExposure );
				}
			},
			Qt::QueuedConnection );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
QImage CDemoWorld::RenderNow( QSize size )
{
	const MapFrame_t frame = MakeFrame( size );
	xv::texture::CImage image;
	xv::map::RenderView( frame.MakeScene(), frame.m_Camera, frame.m_Options, image );
	QImage picture = ToQImage( image );
	if ( const QImage viewModel = RenderViewModel( frame.m_Camera ); !viewModel.isNull() )
	{
		QPainter painter( &picture );
		painter.setRenderHint( QPainter::SmoothPixmapTransform );
		painter.drawImage( picture.rect(), viewModel );
	}
	return picture;
}

//-----------------------------------------------------------------------------
// Purpose: A figure is clicked when the point is inside its body's outline on screen.
//-----------------------------------------------------------------------------
int CDemoWorld::PlayerAt( QPointF at )
{
	const xv::map::CViewCamera camera = SizedCamera();
	int nBest = 0;
	double flBestDepth = 2.0;
	for ( const PlayerState_t &player : GetPlayersShown() )
	{
		QRectF bounds;
		double flDepth = 2.0;
		bool bWhole = true;
		const auto boxes = FigureBoxes( player );
		for ( const Point_t &corner : boxes[ 0 ] )
		{
			const auto projected = camera.Project( corner );
			if ( !projected )
			{
				bWhole = false;
				break;
			}
			const QPointF point( ( *projected )[ 0 ], ( *projected )[ 1 ] );
			bounds = bounds.isNull() ? QRectF( point, QSizeF( 0.01, 0.01 ) ) : bounds.united( QRectF( point, QSizeF( 0.01, 0.01 ) ) );
			flDepth = std::min( flDepth, ( *projected )[ 2 ] );
		}
		if ( bWhole && bounds.adjusted( -2, -2, 2, 2 ).contains( at ) && flDepth < flBestDepth )
		{
			flBestDepth = flDepth;
			nBest = player.m_nEntity;
		}
	}
	return nBest;
}

//-----------------------------------------------------------------------------
// Purpose: Over the picture: each player's name above its figure; the figures' edges where
//          they are to show through walls (and always while there is no map to hide them);
//          and what the map is doing, while it is read.
//-----------------------------------------------------------------------------
void CDemoWorld::PaintLabels( QPainter &painter )
{
	const xv::map::CViewCamera camera = SizedCamera();
	if ( const QImage viewModel = RenderViewModel( camera ); !viewModel.isNull() )
	{
		painter.save();
		painter.setRenderHint( QPainter::SmoothPixmapTransform );
		painter.drawImage( rect(), viewModel );
		painter.restore();
	}
	painter.setRenderHint( QPainter::Antialiasing );
	const std::vector<PlayerState_t> players = GetPlayersShown();
	const std::vector<int> speakers = m_Viewer.GetSpeakers();
	const bool bEdges = m_bThroughWalls || !HasMap();
	QFont font = painter.font();
	font.setBold( true );
	painter.setFont( font );
	const QFontMetrics metrics( font );
	for ( const PlayerState_t &player : players )
	{
		if ( player.m_nEntity == GetEarPlayer() )
		{
			continue;
		}
		const QColor color = DemoTeamColor( player.m_nTeam );
		if ( bEdges )
		{
			painter.setPen( QPen( color, player.m_nEntity == m_nFollowed ? 2.0 : 1.0 ) );
			for ( const auto &box : FigureBoxes( player ) )
			{
				for ( const auto &edge : BOX_EDGES )
				{
					if ( const auto line = camera.ProjectLine( box[ static_cast<std::size_t>( edge[ 0 ] ) ], box[ static_cast<std::size_t>( edge[ 1 ] ) ] ) )
					{
						painter.drawLine( QPointF( line->first[ 0 ], line->first[ 1 ] ), QPointF( line->second[ 0 ], line->second[ 1 ] ) );
					}
				}
			}
		}
		if ( !m_bNames || !player.m_pIdentity )
		{
			continue;
		}
		const Point_t top{ player.m_vecOrigin.m_flX, player.m_vecOrigin.m_flY, player.m_vecOrigin.m_flZ + FigureHeight( player ) + NAME_RISE };
		const Point_t away = xv::map::Subtract( top, camera.m_Eye );
		if ( xv::map::Dot( away, away ) > NAME_DISTANCE * NAME_DISTANCE && player.m_nEntity != m_nFollowed )
		{
			continue;
		}
		const auto at = camera.Project( top );
		if ( !at || ( *at )[ 0 ] < -200 || ( *at )[ 0 ] > width() + 200 || ( *at )[ 1 ] < -20 || ( *at )[ 1 ] > height() + 20 )
		{
			continue;
		}
		const QString szName = QString::fromUtf8( player.m_pIdentity->m_szName );
		const QPointF origin( ( *at )[ 0 ] - metrics.horizontalAdvance( szName ) / 2.0, ( *at )[ 1 ] - metrics.descent() );
		// Dark behind the letters, so a name reads over any wall.
		QPainterPath path;
		path.addText( origin, font, szName );
		painter.setPen( QPen( QColor( 0, 0, 0, 200 ), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
		painter.setBrush( Qt::NoBrush );
		painter.drawPath( path );
		painter.setPen( Qt::NoPen );
		painter.setBrush( player.m_nEntity == m_nFollowed ? QColor( 255, 255, 255 ) : color.lighter( 130 ) );
		painter.drawPath( path );
		if ( std::ranges::find( speakers, player.m_nEntity ) != speakers.end() )
		{
			// Talking: a speaker before the name.
			const double flU = metrics.height() / 6.0;
			const QPointF at( origin.x() - flU * 5.0, origin.y() - metrics.ascent() / 2.0 + flU * 0.5 );
			QPolygonF speaker;
			speaker << at + QPointF( 0, -flU ) << at + QPointF( flU * 1.5, -flU ) << at + QPointF( flU * 3.5, -flU * 2.5 ) << at + QPointF( flU * 3.5, flU * 2.5 ) << at + QPointF( flU * 1.5, flU ) << at + QPointF( 0, flU );
			painter.setPen( QPen( QColor( 0, 0, 0, 200 ), 2.0 ) );
			painter.setBrush( QColor( 255, 255, 255 ) );
			painter.drawPolygon( speaker );
		}
	}
	PaintHighlights( painter, camera );
	// The HUD of the player seen through or followed; the crosshair only through its eyes.
	m_Viewer.PaintHud( painter, rect(), GetHudPlayer(), m_nMode == DemoCamera_t::DEMO_CAMERA_FIRST_PERSON || m_nMode == DemoCamera_t::DEMO_CAMERA_RECORDER );
	if ( m_bScoreboard )
		m_Viewer.PaintScoreboard( painter, rect(), m_nFollowed );
	if ( !m_szStatus.isEmpty() )
	{
		font.setBold( false );
		painter.setFont( font );
		const QRect box = QFontMetrics( font ).boundingRect( rect().adjusted( 12, 10, -12, -10 ), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_szStatus );
		painter.fillRect( box.adjusted( -6, -4, 6, 4 ), QColor( 0, 0, 0, 160 ) );
		painter.setPen( Qt::white );
		painter.drawText( box, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_szStatus );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::paintEvent( QPaintEvent * )
{
	if ( m_pGpu )
	{
		return; // the card draws, the overlay labels
	}
	QPainter painter( this );
	if ( m_Image.isNull() )
	{
		painter.fillRect( rect(), QColor( 30, 30, 30 ) );
	}
	else
	{
		painter.drawImage( rect(), m_Image );
	}
	PaintLabels( painter );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::resizeEvent( QResizeEvent * )
{
	if ( m_pGpu )
	{
		m_pGpu->setGeometry( rect() );
	}
	if ( m_pOverlay )
	{
		m_pOverlay->setGeometry( rect() );
		m_pOverlay->raise();
	}
	if ( m_pLoading && !m_pLoading->isHidden() )
		m_pLoading->raise();
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: Turning or moving a camera that follows something makes it the hand's.
//-----------------------------------------------------------------------------
void CDemoWorld::TakeCamera()
{
	if ( m_nMode != DemoCamera_t::DEMO_CAMERA_FREE )
	{
		SetCameraMode( DemoCamera_t::DEMO_CAMERA_FREE );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::mousePressEvent( QMouseEvent *pEvent )
{
	setFocus();
	if ( m_bLooking )
	{
		return;
	}
	m_LastMouse = m_PressAt = pEvent->pos();
	m_bDragged = false;
}

//-----------------------------------------------------------------------------
// Purpose: A drag with either button turns the camera: in third person, around the player.
//          Once it is a drag and not a click, the pointer is pinned (as the map viewer's
//          mouselook does) until the buttons are let go.
//-----------------------------------------------------------------------------
void CDemoWorld::mouseMoveEvent( QMouseEvent *pEvent )
{
	if ( !( pEvent->buttons() & ( Qt::LeftButton | Qt::RightButton ) ) )
	{
		return;
	}
	if ( !m_bDragged && ( pEvent->pos() - m_PressAt ).manhattanLength() <= CLICK_SLACK )
	{
		return;
	}
	if ( !m_bDragged )
	{
		m_bDragged = true;
		CapturePointer();
	}
	// A pinned pointer's movement comes through the lock's callback.
	if ( m_PointerLock.IsActive() )
	{
		return;
	}
	const QPoint delta = pEvent->pos() - m_LastMouse;
	if ( delta.isNull() )
	{
		return;
	}
	Turn( QPointF( delta ) );
	// Not pinned (X11, Windows): the cursor goes back where the drag began after each move, so
	// it never reaches an edge.
	m_LastMouse = m_PressAt;
	QCursor::setPos( mapToGlobal( m_PressAt ) );
}

//-----------------------------------------------------------------------------
// Purpose: The camera turned by a pointer's movement, in widget pixels.
//-----------------------------------------------------------------------------
void CDemoWorld::Turn( QPointF delta )
{
	if ( m_nMode == DemoCamera_t::DEMO_CAMERA_CHASE )
	{
		// Third person: the drag turns the camera around the player, which it keeps following.
		m_flOrbitYaw = std::remainder( m_flOrbitYaw - delta.x() * LOOK_SPEED, 360.0 );
		m_flOrbitPitch = std::clamp( m_flOrbitPitch + delta.y() * LOOK_SPEED, -89.0, 89.0 );
		Redraw();
		return;
	}
	TakeCamera();
	m_Camera.m_flYaw = std::remainder( m_Camera.m_flYaw - delta.x() * LOOK_SPEED, 360.0 );
	m_Camera.m_flPitch = std::clamp( m_Camera.m_flPitch + delta.y() * LOOK_SPEED, -89.0, 89.0 );
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: Hides the cursor and keeps it where the drag began while the camera turns: pinned
//          by the compositor where it can, else warped back at each move.
//-----------------------------------------------------------------------------
void CDemoWorld::CapturePointer()
{
	m_bLooking = true;
	setCursor( Qt::BlankCursor );
	grabMouse();
	m_PointerLock.Lock( this, [ this ]( QPointF delta ) { Turn( delta ); } );
}

//-----------------------------------------------------------------------------
// Purpose: Lets the pointer go, the cursor back where the drag began.
//-----------------------------------------------------------------------------
void CDemoWorld::ReleasePointer()
{
	if ( !m_bLooking )
	{
		return;
	}
	m_bLooking = false;
	releaseMouse();
	if ( m_PointerLock.IsActive() )
	{
		m_PointerLock.Unlock( m_PressAt );
	}
	else
	{
		QCursor::setPos( mapToGlobal( m_PressAt ) );
	}
	unsetCursor();
}

//-----------------------------------------------------------------------------
// Purpose: A click on a player follows it in third person; on the one already followed, looks
//          through its eyes.
//-----------------------------------------------------------------------------
void CDemoWorld::mouseReleaseEvent( QMouseEvent *pEvent )
{
	if ( m_bLooking )
	{
		// The drag ends when no turning button is held any more.
		if ( !( pEvent->buttons() & ( Qt::LeftButton | Qt::RightButton ) ) )
		{
			ReleasePointer();
		}
		return;
	}
	if ( m_bDragged || pEvent->button() != Qt::LeftButton )
	{
		return;
	}
	if ( const int nEntity = PlayerAt( pEvent->position() ) )
	{
		const bool bAgain = nEntity == m_nFollowed && m_nMode == DemoCamera_t::DEMO_CAMERA_CHASE;
		const bool bOther = nEntity != m_nFollowed;
		SetFollowed( nEntity );
		if ( !bAgain && bOther )
			ResetOrbit();
		SetCameraMode( bAgain ? DemoCamera_t::DEMO_CAMERA_FIRST_PERSON : DemoCamera_t::DEMO_CAMERA_CHASE );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetHighlights( std::vector<Highlight_t> highlights )
{
	m_Highlights = std::move( highlights );
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: Each marked thing's box as lines, its label over it; a demo entity where it is at
//          the playhead (its brush model's box turned as it is), when it is there.
//-----------------------------------------------------------------------------
void CDemoWorld::PaintHighlights( QPainter &painter, const xv::map::CViewCamera &camera )
{
	const xv::demo::CContents *pContents = m_Viewer.GetContents();
	const double flTick = m_Viewer.GetPlayTick();
	for ( const Highlight_t &highlight : m_Highlights )
	{
		std::array<Point_t, 8> corners;
		bool bHave = false;
		if ( highlight.m_nEntity && pContents )
		{
			const xv::demo::EntityLife_t *pLife = pContents->LifeAt( highlight.m_nEntity, static_cast<int>( flTick ) );
			const auto transform = pLife && pLife->IsThere( static_cast<int>( flTick ) ) ? EntityTransform( *pLife, flTick ) : std::nullopt;
			if ( transform )
			{
				xv::map::Box_t box;
				box.Add( Point_t{ -8, -8, -8 } );
				box.Add( Point_t{ 8, 8, 8 } );
				const std::string_view szModel = pContents->ModelName( static_cast<int>( pLife->ValueAt( xv::demo::ENTITY_FIELD_MODEL, static_cast<int>( flTick ) )[ 0 ] ) );
				const std::size_t nModel = szModel.starts_with( '*' ) ? static_cast<std::size_t>( std::atoi( std::string( szModel.substr( 1 ) ).c_str() ) ) : 0;
				if ( nModel > 0 && nModel < m_LogicMap.m_ModelBounds.size() && !m_LogicMap.m_ModelBounds[ nModel ].IsEmpty() )
					box = m_LogicMap.m_ModelBounds[ nModel ];
				for ( int i = 0; i < 8; i++ )
					corners[ static_cast<std::size_t>( i ) ] = transform->Apply( { ( i & 1 ) ? box.m_Maxs[ 0 ] : box.m_Mins[ 0 ], ( i & 2 ) ? box.m_Maxs[ 1 ] : box.m_Mins[ 1 ], ( i & 4 ) ? box.m_Maxs[ 2 ] : box.m_Mins[ 2 ] } );
				bHave = true;
			}
		}
		if ( !bHave && highlight.m_Box && !highlight.m_Box->IsEmpty() )
		{
			const xv::map::Box_t &box = *highlight.m_Box;
			for ( int i = 0; i < 8; i++ )
				corners[ static_cast<std::size_t>( i ) ] = { ( i & 1 ) ? box.m_Maxs[ 0 ] : box.m_Mins[ 0 ], ( i & 2 ) ? box.m_Maxs[ 1 ] : box.m_Mins[ 1 ], ( i & 4 ) ? box.m_Maxs[ 2 ] : box.m_Mins[ 2 ] };
			bHave = true;
		}
		if ( !bHave )
			continue;
		painter.setPen( QPen( highlight.m_Color, 2.0 ) );
		for ( const auto &edge : BOX_EDGES )
		{
			if ( const auto line = camera.ProjectLine( corners[ static_cast<std::size_t>( edge[ 0 ] ) ], corners[ static_cast<std::size_t>( edge[ 1 ] ) ] ) )
				painter.drawLine( QPointF( line->first[ 0 ], line->first[ 1 ] ), QPointF( line->second[ 0 ], line->second[ 1 ] ) );
		}
		Point_t top{ 0, 0, -1e30 };
		Point_t middle{ 0, 0, 0 };
		for ( const Point_t &corner : corners )
		{
			middle = xv::map::Add( middle, xv::map::Scale( corner, 1.0 / 8.0 ) );
			top[ 2 ] = std::max( top[ 2 ], corner[ 2 ] );
		}
		top[ 0 ] = middle[ 0 ];
		top[ 1 ] = middle[ 1 ];
		if ( const auto at = camera.Project( top ); at && !highlight.m_szLabel.isEmpty() )
		{
			const QFontMetrics metrics( painter.font() );
			const QRectF box( ( *at )[ 0 ] - metrics.horizontalAdvance( highlight.m_szLabel ) / 2.0 - 4, ( *at )[ 1 ] - metrics.height() - 6, metrics.horizontalAdvance( highlight.m_szLabel ) + 8, metrics.height() + 2 );
			painter.fillRect( box, QColor( 0, 0, 0, 170 ) );
			painter.setPen( highlight.m_Color );
			painter.drawText( box, Qt::AlignCenter, highlight.m_szLabel );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::ResetOrbit()
{
	// Behind the player, looking its way now, a little down.
	m_flOrbitYaw = 0.0;
	m_flOrbitPitch = 15.0;
	if ( const auto player = m_Viewer.GetPlayers().At( m_nFollowed, m_Viewer.GetPlayTick() ) )
		m_flOrbitYaw = player->m_flYaw;
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CDemoWorld::GetEarPlayer() const
{
	if ( m_nMode == DemoCamera_t::DEMO_CAMERA_RECORDER )
		return m_Viewer.GetPlayers().GetRecorderEntity();
	return m_nMode == DemoCamera_t::DEMO_CAMERA_FIRST_PERSON ? m_nFollowed : 0;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CDemoWorld::GetHudPlayer() const
{
	switch ( m_nMode )
	{
		case DemoCamera_t::DEMO_CAMERA_RECORDER:
			return m_Viewer.GetPlayers().GetRecorderEntity();
		case DemoCamera_t::DEMO_CAMERA_FIRST_PERSON:
		case DemoCamera_t::DEMO_CAMERA_CHASE:
			return m_nFollowed;
		case DemoCamera_t::DEMO_CAMERA_FREE:
			break;
	}
	return m_nFollowed ? m_nFollowed : m_Viewer.GetPlayers().GetRecorderEntity();
}

//-----------------------------------------------------------------------------
// Purpose: The wheel moves the free camera along its view, and the chasing one nearer or
//          further.
//-----------------------------------------------------------------------------
void CDemoWorld::wheelEvent( QWheelEvent *pEvent )
{
	const double flSteps = pEvent->angleDelta().y() / 120.0;
	if ( m_nMode == DemoCamera_t::DEMO_CAMERA_CHASE )
	{
		m_flChaseDistance = std::clamp( m_flChaseDistance * std::pow( 0.85, flSteps ), CHASE_NEAREST, CHASE_FURTHEST );
	}
	else
	{
		TakeCamera();
		const Point_t forward = m_Camera.GetForward();
		m_Camera.m_Eye = xv::map::Add( m_Camera.m_Eye, xv::map::Scale( forward, flSteps * WHEEL_STEP ) );
	}
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose: W, A, S and D fly the camera, E and Q raise and lower it, F finds the players;
//          the rest are the viewer's (Space, the arrows).
//-----------------------------------------------------------------------------
void CDemoWorld::keyPressEvent( QKeyEvent *pEvent )
{
	const bool bArrow = pEvent->key() == Qt::Key_Left || pEvent->key() == Qt::Key_Right;
	if ( bArrow && ( m_bLooking || !m_Held.empty() ) )
	{
		pEvent->accept();
		return;
	}
	if ( m_Viewer.StepKey( pEvent ) )
		return;
	switch ( pEvent->key() )
	{
		case Qt::Key_W:
		case Qt::Key_A:
		case Qt::Key_S:
		case Qt::Key_D:
		case Qt::Key_Q:
		case Qt::Key_E:
			if ( !pEvent->isAutoRepeat() && std::ranges::find( m_Held, pEvent->key() ) == m_Held.end() )
			{
				m_Held.push_back( pEvent->key() );
				TakeCamera();
				if ( !m_pFly->isActive() )
				{
					m_FlyClock.start();
					m_pFly->start();
				}
			}
			break;
		case Qt::Key_F:
			LookAtPlayers();
			break;
		default:
			QWidget::keyPressEvent( pEvent );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Tab is the scoreboard while held, as in the game, not a move to the next widget.
//-----------------------------------------------------------------------------
bool CDemoWorld::event( QEvent *pEvent )
{
	if ( pEvent->type() == QEvent::ToolTip && m_bScoreboard )
	{
		const auto *pHelp = static_cast<QHelpEvent *>( pEvent );
		const QString szTimes = m_Viewer.ScoreboardConnections();
		if ( !szTimes.isEmpty() )
			QToolTip::showText( pHelp->globalPos(), QStringLiteral( "<qt>" ) + szTimes.toHtmlEscaped().replace( u'\n', QStringLiteral( "<br>" ) ) + QStringLiteral( "</qt>" ), this );
		return true;
	}
	if ( pEvent->type() == QEvent::KeyPress || pEvent->type() == QEvent::KeyRelease )
	{
		auto *pKey = static_cast<QKeyEvent *>( pEvent );
		if ( pKey->key() == Qt::Key_Tab || pKey->key() == Qt::Key_Backtab )
		{
			if ( !pKey->isAutoRepeat() )
				SetShowingScoreboard( pEvent->type() == QEvent::KeyPress );
			pEvent->accept();
			return true;
		}
	}
	return QWidget::event( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetUsingVisibility( bool bUse )
{
	m_bVisibility = bUse;
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::SetShowingScoreboard( bool bShow )
{
	if ( bShow == m_bScoreboard )
		return;
	m_bScoreboard = bShow;
	if ( !bShow )
		QToolTip::hideText();
	Redraw();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::keyReleaseEvent( QKeyEvent *pEvent )
{
	if ( pEvent->isAutoRepeat() )
	{
		return;
	}
	std::erase( m_Held, pEvent->key() );
	if ( m_Held.empty() )
	{
		m_pFly->stop();
	}
	QWidget::keyReleaseEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::focusOutEvent( QFocusEvent *pEvent )
{
	ReleasePointer();
	SetShowingScoreboard( false );
	m_Held.clear();
	m_pFly->stop();
	QWidget::focusOutEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoWorld::Fly()
{
	const double flSeconds = std::min( static_cast<double>( m_FlyClock.restart() ) / 1000.0, 0.1 );
	const double flStep = FLY_SPEED * flSeconds * ( QGuiApplication::keyboardModifiers() & Qt::ShiftModifier ? FLY_FAST : 1.0 );
	const Point_t forward = m_Camera.GetForward();
	const Point_t right = m_Camera.GetRight();
	Point_t move{};
	for ( const int nKey : m_Held )
	{
		switch ( nKey )
		{
			case Qt::Key_W:
				move = xv::map::Add( move, forward );
				break;
			case Qt::Key_S:
				move = xv::map::Subtract( move, forward );
				break;
			case Qt::Key_D:
				move = xv::map::Add( move, right );
				break;
			case Qt::Key_A:
				move = xv::map::Subtract( move, right );
				break;
			case Qt::Key_E:
				move[ 2 ] += 1.0;
				break;
			case Qt::Key_Q:
				move[ 2 ] -= 1.0;
				break;
			default:
				break;
		}
	}
	m_Camera.m_Eye = xv::map::Add( m_Camera.m_Eye, xv::map::Scale( move, flStep ) );
	Redraw();
}
