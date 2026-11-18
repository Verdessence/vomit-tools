//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See DemoFigures.h.
//
//===========================================================================//
#include "DemoFigures.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <numbers>

#include "kv1/Kv1.h"
#include "map/Geometry.h"
#include "model/Anim.h"

namespace
{

// Speeds (units a second) the legs change at: still below the first, walking below the second.
constexpr float STILL_SPEED = 20.0f;
constexpr float WALK_SPEED = 150.0f;
// A cycle is cut into this many poses; the aim's pitch into steps of this many degrees.
constexpr int CYCLE_STEPS = 24;
// A prop's cycle is drawn in this many steps (props in the same step share the pose), and this
// many poses are kept.
constexpr int PROP_CYCLE_STEPS = 60;
constexpr std::size_t PROP_POSES_KEPT = 1024;
// mstudioseqdesc_t's flag: the sequence starts again at its end.
constexpr int STUDIO_LOOPING = 0x0001;
constexpr float PITCH_STEP = 15.0f;
// How finely the way it moves is kept (move_x, move_y run -1 to 1).
constexpr float MOVE_STEPS = 2.0f;
// Posed models kept; past this, they are all made again as needed.
constexpr std::size_t POSED_KEPT = 768;
// A sequence flag: its animation adds to another's rather than being a pose of its own.
constexpr int SEQUENCE_DELTA = 0x0004;

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
// Purpose: A pose's transforms each made relative to its bone's parent (the root's stays in
//          model space), so poses can be mixed bone by bone.
//-----------------------------------------------------------------------------
std::vector<xv::model::CTransform> ToLocal( const xv::model::CModel &model, const std::vector<xv::model::CTransform> &pose )
{
	std::vector<xv::model::CTransform> local( pose.size() );
	for ( std::size_t i = 0; i < pose.size() && i < model.m_Bones.size(); i++ )
	{
		const int nParent = model.m_Bones[ i ].m_nParent;
		const auto parent = nParent >= 0 && static_cast<std::size_t>( nParent ) < pose.size() ? pose[ static_cast<std::size_t>( nParent ) ].Inverse() : std::nullopt;
		local[ i ] = parent ? *parent * pose[ i ] : pose[ i ];
	}
	return local;
}

//-----------------------------------------------------------------------------
// Purpose: The parent-relative transforms put back in model space (parents come first).
//-----------------------------------------------------------------------------
std::vector<xv::model::CTransform> ToModel( const xv::model::CModel &model, const std::vector<xv::model::CTransform> &local )
{
	std::vector<xv::model::CTransform> pose( local.size() );
	for ( std::size_t i = 0; i < local.size() && i < model.m_Bones.size(); i++ )
	{
		const int nParent = model.m_Bones[ i ].m_nParent;
		pose[ i ] = nParent >= 0 && static_cast<std::size_t>( nParent ) < i ? pose[ static_cast<std::size_t>( nParent ) ] * local[ i ] : local[ i ];
	}
	return pose;
}

//-----------------------------------------------------------------------------
// Purpose: A weapon held as the game holds one (bone merged): each of its bones named as one
//          of the player's takes that bone's place, the others follow their parents as in the
//          weapon's own reference pose.
//-----------------------------------------------------------------------------
std::vector<xv::model::CTransform> MergedPose( const xv::model::CModel &weapon, const xv::model::CModel &player, const std::vector<xv::model::CTransform> &playerPose )
{
	std::vector<xv::model::CTransform> pose( weapon.m_Bones.size() );
	for ( std::size_t i = 0; i < weapon.m_Bones.size(); i++ )
	{
		const xv::model::Bone_t &bone = weapon.m_Bones[ i ];
		const auto match = std::ranges::find_if( player.m_Bones, [ & ]( const xv::model::Bone_t &other ) { return xv::kv1::EqualsIgnoreCase( other.m_szName, bone.m_szName ); } );
		if ( match != player.m_Bones.end() && static_cast<std::size_t>( match - player.m_Bones.begin() ) < playerPose.size() )
		{
			pose[ i ] = playerPose[ static_cast<std::size_t>( match - player.m_Bones.begin() ) ];
			continue;
		}
		const int nParent = bone.m_nParent;
		if ( nParent >= 0 && static_cast<std::size_t>( nParent ) < i )
		{
			const auto toParent = weapon.m_Bones[ static_cast<std::size_t>( nParent ) ].m_ToModel.Inverse();
			pose[ i ] = toParent ? pose[ static_cast<std::size_t>( nParent ) ] * *toParent * bone.m_ToModel : bone.m_ToModel;
		}
		else
		{
			pose[ i ] = bone.m_ToModel;
		}
	}
	return pose;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string DemoWeaponFamily( std::string_view szWorldModel )
{
	const std::string szName = Lower( szWorldModel.substr( szWorldModel.find_last_of( '/' ) == std::string_view::npos ? 0 : szWorldModel.find_last_of( '/' ) + 1 ) );
	// The player models' upper-body sequences, by the world model's name; the first that the
	// name contains wins.
	static constexpr std::array<std::pair<std::string_view, std::string_view>, 24> FAMILIES = { {
		{ "ak47", "AK" },
		{ "m4a1", "M4" },
		{ "aug", "AUG" },
		{ "awp", "AWP" },
		{ "c4", "C4" },
		{ "elite", "ELITES" },
		{ "famas", "FAMAS" },
		{ "g3sg1", "G3" },
		{ "galil", "GALIL" },
		{ "knife", "KNIFE" },
		{ "m249", "M249" },
		{ "m3super90", "M3S90" },
		{ "mac10", "MAC10" },
		{ "mp5", "MP5" },
		{ "p90", "P90" },
		{ "scout", "SCOUT" },
		{ "sg550", "SG550" },
		{ "sg552", "SG552" },
		{ "tmp", "TMP" },
		{ "ump45", "UMP45" },
		{ "xm1014", "XM1014" },
		{ "w_eq_", "GREN" },
		{ "w_pist_", "PISTOL" },
		{ "w_smg_", "MP5" },
	} };
	for ( const auto &[ szPart, szFamily ] : FAMILIES )
	{
		if ( szName.find( szPart ) != std::string::npos )
		{
			return std::string( szFamily );
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CDemoFigures::Studios_t CDemoFigures::ReadStudios( const std::set<std::string> &models, const xv::model::Reader_t &pfnRead )
{
	Studios_t studios;
	for ( const std::string &szModel : models )
	{
		auto main = xv::model::ReadStudio( szModel, pfnRead );
		if ( !main )
		{
			continue;
		}
		std::vector<std::string> problems;
		auto pStudio = std::make_shared<Studio_t>();
		std::vector<xv::model::CStudioFile> includes = xv::model::LoadIncludes( *main, pfnRead, problems );
		pStudio->push_back( std::move( *main ) );
		for ( xv::model::CStudioFile &include : includes )
		{
			pStudio->push_back( std::move( include ) );
		}
		studios.emplace( szModel, std::move( pStudio ) );
	}
	return studios;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoFigures::SetStudios( Studios_t studios )
{
	m_Studios = std::move( studios );
	m_Posed.clear();
	m_Templates.clear();
	m_Placed.clear();
	m_PropPoses.clear();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CDemoFigures::Clear()
{
	m_Studios.clear();
	m_Posed.clear();
	m_Templates.clear();
	m_Placed.clear();
	m_PropPoses.clear();
}

//-----------------------------------------------------------------------------
// Purpose: The model's own sequences first, then its includes', by name (any case).
//-----------------------------------------------------------------------------
CDemoFigures::SequenceRef_t CDemoFigures::FindSequence( const Studio_t &studio, std::string_view szName ) const
{
	for ( std::size_t nFile = 0; nFile < studio.size(); nFile++ )
	{
		const auto &sequences = studio[ nFile ].m_Sequences;
		for ( std::size_t i = 0; i < sequences.size(); i++ )
		{
			if ( xv::kv1::EqualsIgnoreCase( sequences[ i ].m_szName, szName ) )
			{
				return { static_cast<int>( nFile ), static_cast<int>( i ) };
			}
		}
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: The legs' sequence from the speed and ducking, the way it moves relative to where
//          the player looks (move_x ahead, move_y to the left); the upper body's from the
//          weapon, running or not, crouched if the model has that.
//-----------------------------------------------------------------------------
std::optional<CDemoFigures::Choice_t> CDemoFigures::Choose( const Figure_t &figure, const Studio_t &studio ) const
{
	Choice_t choice;
	const float flSpeed = std::hypot( figure.m_vecVelocity.m_flX, figure.m_vecVelocity.m_flY );
	const bool bMoving = flSpeed >= STILL_SPEED;
	const char *pszLower = figure.m_bDucked ? ( bMoving ? "Crouch_walk_lower" : "Crouch_Idle_Lower" ) : !bMoving ? "Idle_lower"
		: flSpeed < WALK_SPEED                                                                                   ? "walk_lower"
																												 : "Run_lower";
	choice.m_Lower = FindSequence( studio, pszLower );
	if ( !choice.m_Lower.IsValid() )
	{
		choice.m_Lower = FindSequence( studio, "Idle_lower" );
	}
	if ( bMoving )
	{
		const double flYaw = figure.m_flYaw * std::numbers::pi / 180.0;
		const float flAhead = static_cast<float>( ( figure.m_vecVelocity.m_flX * std::cos( flYaw ) + figure.m_vecVelocity.m_flY * std::sin( flYaw ) ) / flSpeed );
		const float flLeft = static_cast<float>( ( -figure.m_vecVelocity.m_flX * std::sin( flYaw ) + figure.m_vecVelocity.m_flY * std::cos( flYaw ) ) / flSpeed );
		choice.m_flMoveX = std::round( flAhead * MOVE_STEPS ) / MOVE_STEPS;
		choice.m_flMoveY = std::round( flLeft * MOVE_STEPS ) / MOVE_STEPS;
	}
	if ( const std::string szFamily = DemoWeaponFamily( figure.m_szWeapon ); !szFamily.empty() )
	{
		if ( figure.m_bDucked )
		{
			choice.m_Upper = FindSequence( studio, "Crouch_Idle_Upper_" + szFamily );
		}
		if ( !choice.m_Upper.IsValid() )
		{
			choice.m_Upper = FindSequence( studio, ( bMoving ? "Run_Upper_" : "Idle_Upper_" ) + szFamily );
		}
		if ( !choice.m_Upper.IsValid() )
		{
			choice.m_Upper = FindSequence( studio, "Idle_Upper_" + szFamily );
		}
		if ( choice.m_Upper.IsValid() && ( studio[ static_cast<std::size_t>( choice.m_Upper.m_nFile ) ].m_Sequences[ static_cast<std::size_t>( choice.m_Upper.m_nSequence ) ].m_nFlags & SEQUENCE_DELTA ) != 0 )
		{
			choice.m_Upper = {};
		}
	}
	if ( !choice.m_Lower.IsValid() && !choice.m_Upper.IsValid() )
	{
		return std::nullopt;
	}
	return choice;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::pair<std::string, std::string> CDemoFigures::SequencesFor( const Figure_t &figure ) const
{
	const auto found = m_Studios.find( figure.m_szModel );
	if ( found == m_Studios.end() )
	{
		return {};
	}
	const Studio_t &studio = *found->second;
	const auto choice = Choose( figure, studio );
	const auto name = [ & ]( const SequenceRef_t &ref ) { return ref.IsValid() ? studio[ static_cast<std::size_t>( ref.m_nFile ) ].m_Sequences[ static_cast<std::size_t>( ref.m_nSequence ) ].m_szName : std::string(); };
	return choice ? std::pair{ name( choice->m_Lower ), name( choice->m_Upper ) } : std::pair<std::string, std::string>{};
}

//-----------------------------------------------------------------------------
// Purpose: The model in a pose, with its weapon, in model space: the legs' sequence, the
//          upper body's over it where its weights say (bone by bone, relative to the parents,
//          so the upper body rides on the legs' hips), the weapon merged onto the hand.
//-----------------------------------------------------------------------------
CDemoFigures::Posed_t CDemoFigures::Pose( const Figure_t &figure, const Studio_t &studio, const Choice_t &choice, int nCycleStep, int nPitchStep, std::uint32_t nObject )
{
	const xv::model::CModel &model = *figure.m_pModel->m_pModel;
	const float flCycle = static_cast<float>( nCycleStep ) / static_cast<float>( CYCLE_STEPS );
	const auto sample = [ & ]( const SequenceRef_t &ref ) -> std::optional<std::vector<xv::model::CTransform>> {
		if ( !ref.IsValid() )
		{
			return std::nullopt;
		}
		const xv::model::CStudioFile &file = studio[ static_cast<std::size_t>( ref.m_nFile ) ];
		std::vector<float> parameters;
		for ( const xv::model::PoseParameter_t &param : file.m_Parameters )
		{
			float flValue = std::clamp( 0.0f, std::min( param.m_flStart, param.m_flEnd ), std::max( param.m_flStart, param.m_flEnd ) );
			if ( param.m_szName == "move_x" )
			{
				flValue = choice.m_flMoveX;
			}
			else if ( param.m_szName == "move_y" )
			{
				flValue = choice.m_flMoveY;
			}
			else if ( param.m_szName == "body_pitch" )
			{
				flValue = static_cast<float>( nPitchStep ) * PITCH_STEP;
			}
			parameters.push_back( flValue );
		}
		return xv::model::SampleSequence( file, static_cast<std::size_t>( ref.m_nSequence ), flCycle, parameters, model );
	};
	auto pose = sample( choice.m_Lower );
	if ( const auto upper = sample( choice.m_Upper ) )
	{
		if ( !pose )
		{
			pose = upper;
		}
		else
		{
			// Each of the model's bones takes the upper body's pose where that sequence weighs it
			// (by bone name: the sequence's file may number its bones differently).
			const xv::model::CStudioFile &file = studio[ static_cast<std::size_t>( choice.m_Upper.m_nFile ) ];
			const xv::model::Sequence_t &seq = file.m_Sequences[ static_cast<std::size_t>( choice.m_Upper.m_nSequence ) ];
			std::vector<xv::model::CTransform> local = ToLocal( model, *pose );
			const std::vector<xv::model::CTransform> upperLocal = ToLocal( model, *upper );
			for ( std::size_t i = 0; i < model.m_Bones.size() && i < local.size(); i++ )
			{
				for ( std::size_t j = 0; j < file.m_Bones.size() && j < seq.m_BoneWeights.size(); j++ )
				{
					if ( xv::kv1::EqualsIgnoreCase( file.m_Bones[ j ].m_szName, model.m_Bones[ i ].m_szName ) )
					{
						if ( seq.m_BoneWeights[ j ] > 0.5f )
						{
							local[ i ] = upperLocal[ i ];
						}
						break;
					}
				}
			}
			pose = ToModel( model, local );
		}
	}
	if ( !pose )
	{
		return {};
	}
	// The model and its weapon in this pose, written over the vertices of their mesh as made
	// in the reference pose.
	const Template_t &shape = TemplateFor( figure, nObject );
	const xv::model::CModel posed = xv::model::Deform( model, *pose );
	std::optional<xv::model::CModel> weaponPosed;
	if ( figure.m_pWeapon && figure.m_pWeapon->m_pModel )
	{
		weaponPosed = xv::model::Deform( *figure.m_pWeapon->m_pModel, MergedPose( *figure.m_pWeapon->m_pModel, model, *pose ) );
	}
	auto pMesh = std::make_shared<xv::map::CMapMesh>( *shape.m_pMesh );
	for ( std::size_t i = 0; i < pMesh->m_Vertices.size() && i < shape.m_Sources.size(); i++ )
	{
		const auto &[ nModel, nVertex ] = shape.m_Sources[ i ];
		const xv::model::CModel &from = nModel == 0 || !weaponPosed ? posed : *weaponPosed;
		if ( nVertex < from.m_Vertices.size() )
		{
			pMesh->m_Vertices[ i ].m_vecPosition = from.m_Vertices[ nVertex ].m_vecPosition;
			pMesh->m_Vertices[ i ].m_vecNormal = from.m_Vertices[ nVertex ].m_vecNormal;
		}
	}
	return { std::move( pMesh ), std::make_shared<const std::vector<xv::model::CTransform>>( std::move( *pose ) ) };
}

//-----------------------------------------------------------------------------
// Purpose: The model with its weapon in the reference pose, made once: the mesh the poses are
//          written over, and where each of its vertices comes from.
//-----------------------------------------------------------------------------
const CDemoFigures::Template_t &CDemoFigures::TemplateFor( const Figure_t &figure, std::uint32_t nObject )
{
	const auto key = std::tuple{ figure.m_szModel, figure.m_nSkin, figure.m_pWeapon ? figure.m_szWeapon : std::string() };
	if ( const auto found = m_Templates.find( key ); found != m_Templates.end() )
	{
		return found->second;
	}
	std::vector<xv::map::PropModel_t> models{ *figure.m_pModel };
	std::vector<xv::map::PropPlacement_t> placements{ { nObject, 0, {} } };
	if ( figure.m_pWeapon && figure.m_pWeapon->m_pModel )
	{
		models.push_back( *figure.m_pWeapon );
		placements.push_back( { nObject, 1, {} } );
	}
	Template_t shape;
	auto pMesh = std::make_shared<xv::map::CMapMesh>( xv::map::BuildPropMesh( models, placements, 1.0e9, &shape.m_Sources ) );
	// Without the wireframe's edges, which nothing here draws.
	pMesh->m_Lines.clear();
	shape.m_pMesh = std::move( pMesh );
	return m_Templates.emplace( key, std::move( shape ) ).first->second;
}

//-----------------------------------------------------------------------------
// Purpose: The model's own sequences first, then each included model's in turn.
//-----------------------------------------------------------------------------
CDemoFigures::SequenceRef_t CDemoFigures::SequenceAt( const Studio_t &studio, int nSequence )
{
	if ( nSequence < 0 )
		return {};
	for ( std::size_t nFile = 0; nFile < studio.size(); nFile++ )
	{
		const int nCount = static_cast<int>( studio[ nFile ].m_Sequences.size() );
		if ( nSequence < nCount )
			return { static_cast<int>( nFile ), nSequence };
		nSequence -= nCount;
	}
	return {};
}

//-----------------------------------------------------------------------------
// Purpose: Pick a view-model activity, with a sequence-name prefix for custom weapons.
//-----------------------------------------------------------------------------
int CDemoFigures::ViewModelSequence( const std::string &szModel, std::string_view szActivity, std::string_view szName ) const
{
	const auto found = m_Studios.find( szModel );
	if ( found == m_Studios.end() )
		return -1;
	int nIndex = 0, nNamed = -1;
	for ( const auto &file : *found->second )
	{
		for ( const auto &sequence : file.m_Sequences )
		{
			if ( sequence.m_szActivity == szActivity )
				return nIndex;
			if ( nNamed < 0 && sequence.m_szName.starts_with( szName ) )
				nNamed = nIndex;
			++nIndex;
		}
	}
	return nNamed;
}

//-----------------------------------------------------------------------------
// Purpose: The first animation's frames over its frame rate; the sequence's looping flag.
//-----------------------------------------------------------------------------
std::optional<CDemoFigures::Timing_t> CDemoFigures::SequenceTiming( const std::string &szModel, int nSequence ) const
{
	const auto found = m_Studios.find( szModel );
	if ( found == m_Studios.end() )
		return std::nullopt;
	const SequenceRef_t ref = SequenceAt( *found->second, nSequence );
	if ( !ref.IsValid() )
		return std::nullopt;
	const xv::model::CStudioFile &file = ( *found->second )[ static_cast<std::size_t>( ref.m_nFile ) ];
	const xv::model::Sequence_t &seq = file.m_Sequences[ static_cast<std::size_t>( ref.m_nSequence ) ];
	Timing_t timing;
	timing.m_bLoops = ( seq.m_nFlags & STUDIO_LOOPING ) != 0;
	if ( !seq.m_Animations.empty() && seq.m_Animations.front() >= 0 && static_cast<std::size_t>( seq.m_Animations.front() ) < file.m_Animations.size() )
	{
		const xv::model::Animation_t &animation = file.m_Animations[ static_cast<std::size_t>( seq.m_Animations.front() ) ];
		timing.m_flSeconds = animation.m_flFps > 0.0f ? std::max( 1, animation.m_nFrames - 1 ) / static_cast<double>( animation.m_flFps ) : 0.0;
	}
	return timing;
}

//-----------------------------------------------------------------------------
// Purpose: The sequence sampled at the cycle (in steps, so props in the same pose share it),
//          written over the model's reference mesh.
//-----------------------------------------------------------------------------
std::shared_ptr<const xv::map::CMapMesh> CDemoFigures::PoseProp( const PropPose_t &prop, std::uint32_t nObject )
{
	if ( !prop.m_pModel || !prop.m_pModel->m_pModel )
		return nullptr;
	const auto found = m_Studios.find( prop.m_szModel );
	if ( found == m_Studios.end() )
		return nullptr;
	const SequenceRef_t ref = SequenceAt( *found->second, prop.m_nSequence );
	if ( !ref.IsValid() )
		return nullptr;
	const int nStep = static_cast<int>( std::lround( std::clamp( prop.m_flCycle, 0.0, 1.0 ) * PROP_CYCLE_STEPS ) );
	const auto key = std::tuple{ prop.m_szModel, prop.m_nSkin, prop.m_nSequence, nStep };
	if ( const auto kept = m_PropPoses.find( key ); kept != m_PropPoses.end() )
		return kept->second;
	if ( m_PropPoses.size() >= PROP_POSES_KEPT )
		m_PropPoses.clear();
	const xv::model::CModel &model = *prop.m_pModel->m_pModel;
	const xv::model::CStudioFile &file = ( *found->second )[ static_cast<std::size_t>( ref.m_nFile ) ];
	std::vector<float> parameters;
	for ( const xv::model::PoseParameter_t &param : file.m_Parameters )
		parameters.push_back( std::clamp( 0.0f, std::min( param.m_flStart, param.m_flEnd ), std::max( param.m_flStart, param.m_flEnd ) ) );
	const auto pose = xv::model::SampleSequence( file, static_cast<std::size_t>( ref.m_nSequence ), static_cast<float>( nStep ) / PROP_CYCLE_STEPS, parameters, model );
	std::shared_ptr<const xv::map::CMapMesh> pOut;
	if ( pose )
	{
		Figure_t figure;
		figure.m_szModel = prop.m_szModel;
		figure.m_pModel = prop.m_pModel;
		figure.m_nSkin = prop.m_nSkin;
		const Template_t &shape = TemplateFor( figure, nObject );
		const xv::model::CModel posed = xv::model::Deform( model, *pose );
		auto pMesh = std::make_shared<xv::map::CMapMesh>( *shape.m_pMesh );
		for ( std::size_t i = 0; i < pMesh->m_Vertices.size() && i < shape.m_Sources.size(); i++ )
		{
			const std::uint32_t nVertex = shape.m_Sources[ i ].second;
			if ( nVertex < posed.m_Vertices.size() )
			{
				pMesh->m_Vertices[ i ].m_vecPosition = posed.m_Vertices[ nVertex ].m_vecPosition;
				pMesh->m_Vertices[ i ].m_vecNormal = posed.m_Vertices[ nVertex ].m_vecNormal;
			}
		}
		pOut = std::move( pMesh );
	}
	m_PropPoses[ key ] = pOut;
	return pOut;
}

//-----------------------------------------------------------------------------
// Purpose: The cycles run with the demo's time (each sequence at its own rate), the same for
//          every figure, so figures in the same pose share it.
//-----------------------------------------------------------------------------
std::shared_ptr<const xv::map::CMapMesh> CDemoFigures::Build( const Figure_t &figure, std::uint32_t nObject )
{
	if ( !figure.m_pModel || !figure.m_pModel->m_pModel )
	{
		return nullptr;
	}
	const auto found = m_Studios.find( figure.m_szModel );
	std::shared_ptr<const xv::map::CMapMesh> pPosed;
	std::shared_ptr<const std::vector<xv::model::CTransform>> pPose;
	if ( found != m_Studios.end() )
	{
		const Studio_t &studio = *found->second;
		if ( const auto choice = Choose( figure, studio ) )
		{
			// The legs' sequence sets the rate: its first animation's frames over its frame rate.
			double flLength = 1.0;
			const SequenceRef_t &timing = choice->m_Lower.IsValid() ? choice->m_Lower : choice->m_Upper;
			const xv::model::CStudioFile &file = studio[ static_cast<std::size_t>( timing.m_nFile ) ];
			const xv::model::Sequence_t &seq = file.m_Sequences[ static_cast<std::size_t>( timing.m_nSequence ) ];
			if ( !seq.m_Animations.empty() && seq.m_Animations.front() >= 0 && static_cast<std::size_t>( seq.m_Animations.front() ) < file.m_Animations.size() )
			{
				const xv::model::Animation_t &anim = file.m_Animations[ static_cast<std::size_t>( seq.m_Animations.front() ) ];
				flLength = anim.m_flFps > 0.0f ? std::max( 1, anim.m_nFrames - 1 ) / static_cast<double>( anim.m_flFps ) : 1.0;
			}
			const double flCycle = std::fmod( figure.m_flSeconds / std::max( flLength, 0.05 ), 1.0 );
			const int nCycleStep = static_cast<int>( std::floor( flCycle * CYCLE_STEPS ) ) % CYCLE_STEPS;
			const int nPitchStep = static_cast<int>( std::lround( std::clamp( figure.m_flPitch, -89.0f, 89.0f ) / PITCH_STEP ) );
			const PoseKey_t key{ figure.m_szModel, figure.m_nSkin, figure.m_szWeapon, choice->m_Lower.m_nFile, choice->m_Lower.m_nSequence, choice->m_Upper.m_nFile, choice->m_Upper.m_nSequence, nCycleStep, nPitchStep, static_cast<int>( choice->m_flMoveX * MOVE_STEPS ), static_cast<int>( choice->m_flMoveY * MOVE_STEPS ) };
			auto posed = m_Posed.find( key );
			if ( posed == m_Posed.end() )
			{
				if ( m_Posed.size() >= POSED_KEPT )
				{
					m_Posed.clear();
				}
				posed = m_Posed.emplace( key, Pose( figure, studio, *choice, nCycleStep, nPitchStep, nObject ) ).first;
			}
			pPosed = posed->second.m_pMesh;
			pPose = posed->second.m_pPose;
		}
	}
	if ( !pPosed )
	{
		// No sequences to be had: the model as it was made.
		pPosed = TemplateFor( figure, nObject ).m_pMesh;
	}
	if ( const auto last = m_Placed.find( figure.m_nEntity ); last != m_Placed.end() && last->second.m_pPosed == pPosed && last->second.m_vecOrigin == figure.m_vecOrigin && last->second.m_flYaw == figure.m_flYaw )
	{
		return last->second.m_pPlaced;
	}
	// Turned to where the player looks (about up) and put where it stands.
	auto pPlaced = std::make_shared<xv::map::CMapMesh>( *pPosed );
	const float flYaw = figure.m_flYaw * std::numbers::pi_v<float> / 180.0f;
	const float flCos = std::cos( flYaw );
	const float flSin = std::sin( flYaw );
	const auto turn = [ & ]( const xv::CVec3 &vec ) { return xv::CVec3{ vec.m_flX * flCos - vec.m_flY * flSin, vec.m_flX * flSin + vec.m_flY * flCos, vec.m_flZ }; };
	xv::map::Box_t bounds;
	for ( xv::map::MeshVertex_t &vertex : pPlaced->m_Vertices )
	{
		vertex.m_vecPosition = turn( vertex.m_vecPosition ) + figure.m_vecOrigin;
		vertex.m_vecNormal = turn( vertex.m_vecNormal );
		vertex.m_nObject = nObject;
		bounds.Add( xv::map::ToPoint( vertex.m_vecPosition ) );
	}
	for ( xv::map::MeshChunk_t &chunk : pPlaced->m_Chunks )
	{
		chunk.m_Bounds = bounds;
	}
	m_Placed[ figure.m_nEntity ] = { pPosed, pPlaced, figure.m_vecOrigin, figure.m_flYaw, pPose, figure.m_pModel->m_pModel };
	return pPlaced;
}

//-----------------------------------------------------------------------------
// Purpose: A bone of a figure as last built, with something placed on it, in the world.
//-----------------------------------------------------------------------------
std::optional<xv::map::Transform_t> CDemoFigures::BoneOf( int nEntity, int nBone, const xv::model::CTransform &local ) const
{
	const auto found = m_Placed.find( nEntity );
	if ( found == m_Placed.end() || !found->second.m_pModel || nBone < 0 || static_cast<std::size_t>( nBone ) >= found->second.m_pModel->m_Bones.size() )
	{
		return std::nullopt;
	}
	const Placed_t &placed = found->second;
	const auto nAt = static_cast<std::size_t>( nBone );
	const xv::model::CTransform bone = placed.m_pPose && nAt < placed.m_pPose->size() ? ( *placed.m_pPose )[ nAt ] : placed.m_pModel->m_Bones[ nAt ].m_ToModel;
	const xv::model::CTransform at = bone * local;
	xv::map::Transform_t onModel;
	const std::array<float, 12> &v = at.m_Values;
	onModel.m_Rotation = { v[ 0 ], v[ 1 ], v[ 2 ], v[ 4 ], v[ 5 ], v[ 6 ], v[ 8 ], v[ 9 ], v[ 10 ] };
	onModel.m_Offset = { v[ 3 ], v[ 7 ], v[ 11 ] };
	return xv::map::Transform_t::FromAngles( xv::map::ToPoint( placed.m_vecOrigin ), { 0.0, placed.m_flYaw, 0.0 } ) * onModel;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<xv::map::Transform_t> CDemoFigures::AttachmentOf( int nEntity, int nAttachment ) const
{
	const auto found = m_Placed.find( nEntity );
	if ( found == m_Placed.end() || !found->second.m_pModel || nAttachment < 0 || static_cast<std::size_t>( nAttachment ) >= found->second.m_pModel->m_Attachments.size() )
	{
		return std::nullopt;
	}
	const xv::model::Attachment_t &attachment = found->second.m_pModel->m_Attachments[ static_cast<std::size_t>( nAttachment ) ];
	return BoneOf( nEntity, attachment.m_nBone, attachment.m_Local );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<xv::map::Transform_t> CDemoFigures::HandOf( int nEntity ) const
{
	const auto found = m_Placed.find( nEntity );
	if ( found == m_Placed.end() || !found->second.m_pModel )
	{
		return std::nullopt;
	}
	const xv::model::CModel &model = *found->second.m_pModel;
	for ( std::size_t i = 0; i < model.m_Attachments.size(); i++ )
	{
		if ( xv::kv1::EqualsIgnoreCase( model.m_Attachments[ i ].m_szName, "anim_attachment_RH" ) )
		{
			return AttachmentOf( nEntity, static_cast<int>( i ) );
		}
	}
	for ( std::size_t i = 0; i < model.m_Bones.size(); i++ )
	{
		if ( xv::kv1::EqualsIgnoreCase( model.m_Bones[ i ].m_szName, "ValveBiped.Bip01_R_Hand" ) )
		{
			return BoneOf( nEntity, static_cast<int>( i ), {} );
		}
	}
	return std::nullopt;
}
