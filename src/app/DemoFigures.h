//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The demo view's players as their own models: each standing where the demo has it,
//          turned its way, with the weapon it holds in its right hand. CS:S doesn't network a
//          player's animation (its clients work it out from how the player moves), so the pose
//          is a likeness made the same way: the legs idle, walk, run or crouch from the
//          player's speed and ducking, along the way it moves, and the upper body holds the
//          weapon as the player model's own sequence for that weapon does.
//
//===========================================================================//
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "map/Instance.h"
#include "map/Mesh.h"
#include "map/Props.h"
#include "model/Mdl.h"
#include "util/Vec3.h"

class CDemoFigures
{
public:
	// A model's studio files: the model first, then the ones it includes (its animations).
	using Studio_t = std::vector<xv::model::CStudioFile>;
	using Studios_t = std::map<std::string, std::shared_ptr<const Studio_t>>;

	// Each model's studio files, by its lower-case name (a thread may call this); a model that
	// won't read is left out.
	[[nodiscard]] static Studios_t ReadStudios( const std::set<std::string> &models, const xv::model::Reader_t &pfnRead );
	void SetStudios( Studios_t studios );
	void Clear();

	// What a figure is doing at a moment, as the demo has it.
	struct Figure_t
	{
		int m_nEntity = 0; // whose: a figure that hasn't changed keeps its mesh
		std::string m_szModel;                          // lower case
		const xv::map::PropModel_t *m_pModel = nullptr; // as the map's resources loaded it
		int m_nSkin = 0;
		std::string m_szWeapon; // the held weapon's world model, lower case; empty for none
		const xv::map::PropModel_t *m_pWeapon = nullptr;
		xv::CVec3 m_vecOrigin;   // the feet
		float m_flYaw = 0.0f;    // the way the player looks
		float m_flPitch = 0.0f;
		xv::CVec3 m_vecVelocity; // units a second
		bool m_bDucked = false;
		double m_flSeconds = 0.0; // the demo's time, for the animations' cycles
	};
	// The figure posed and placed in the world, its vertices object nObject; null when its
	// model isn't loaded.
	[[nodiscard]] std::shared_ptr<const xv::map::CMapMesh> Build( const Figure_t &figure, std::uint32_t nObject );

	// Where an attachment of a figure's model (by index) is in the world, as last built: its
	// place, its bone in the pose, the attachment's own place on the bone. nullopt when the
	// figure wasn't built or its model has no such attachment.
	[[nodiscard]] std::optional<xv::map::Transform_t> AttachmentOf( int nEntity, int nAttachment ) const;
	// Its right hand: the attachment there, else the hand's bone (where a held weapon is).
	[[nodiscard]] std::optional<xv::map::Transform_t> HandOf( int nEntity ) const;

	// A prop's model (a prop_dynamic's) in one of its sequences (the demo's m_nSequence: the
	// model's own sequences, then its included models'), at a cycle (0 to 1), in model space;
	// null when its studio files weren't read or it hasn't the sequence.
	struct PropPose_t
	{
		std::string m_szModel; // lower case
		const xv::map::PropModel_t *m_pModel = nullptr;
		int m_nSkin = 0;
		int m_nSequence = 0;
		double m_flCycle = 0.0;
	};
	[[nodiscard]] std::shared_ptr<const xv::map::CMapMesh> PoseProp( const PropPose_t &prop, std::uint32_t nObject );
	// A model's sequence: how long one cycle of it is (seconds) and whether it loops; nullopt
	// when its studio files weren't read or it hasn't the sequence.
	struct Timing_t
	{
		double m_flSeconds = 0.0;
		bool m_bLoops = false;
	};
	[[nodiscard]] std::optional<Timing_t> SequenceTiming( const std::string &szModel, int nSequence ) const;
	[[nodiscard]] int ViewModelSequence( const std::string &szModel, std::string_view szActivity, std::string_view szName ) const;
	[[nodiscard]] bool HasStudio( const std::string &szModel ) const { return m_Studios.contains( szModel ); }

	// The sequence a figure's legs and upper body show (for tests): their names, empty when
	// the model has none such.
	[[nodiscard]] std::pair<std::string, std::string> SequencesFor( const Figure_t &figure ) const;

private:
	// A sequence of a model's studio files: the file and its index there.
	struct SequenceRef_t
	{
		int m_nFile = -1;
		int m_nSequence = -1;
		[[nodiscard]] bool IsValid() const { return m_nFile >= 0; }
	};
	struct Choice_t
	{
		SequenceRef_t m_Lower, m_Upper;
		float m_flMoveX = 0.0f, m_flMoveY = 0.0f;
	};
	[[nodiscard]] SequenceRef_t FindSequence( const Studio_t &studio, std::string_view szName ) const;
	// The demo's sequence number in the model's files.
	[[nodiscard]] static SequenceRef_t SequenceAt( const Studio_t &studio, int nSequence );
	[[nodiscard]] std::optional<Choice_t> Choose( const Figure_t &figure, const Studio_t &studio ) const;
	// A posed model in model space, and the pose (its bones' transforms) it is in.
	struct Posed_t
	{
		std::shared_ptr<const xv::map::CMapMesh> m_pMesh;
		std::shared_ptr<const std::vector<xv::model::CTransform>> m_pPose;
	};
	[[nodiscard]] Posed_t Pose( const Figure_t &figure, const Studio_t &studio, const Choice_t &choice, int nCycleStep, int nPitchStep, std::uint32_t nObject );
	[[nodiscard]] std::optional<xv::map::Transform_t> BoneOf( int nEntity, int nBone, const xv::model::CTransform &local ) const;

	// A model with its weapon in the reference pose, in model space, and where each vertex of
	// it comes from: the model (0) or the weapon (1), and the vertex there.
	struct Template_t
	{
		std::shared_ptr<const xv::map::CMapMesh> m_pMesh;
		std::vector<std::pair<std::uint32_t, std::uint32_t>> m_Sources;
	};
	[[nodiscard]] const Template_t &TemplateFor( const Figure_t &figure, std::uint32_t nObject );

	Studios_t m_Studios;
	// Posed models in model space, shared by every figure in the same pose.
	using PoseKey_t = std::tuple<std::string, int, std::string, int, int, int, int, int, int, int, int>;
	std::map<PoseKey_t, Posed_t> m_Posed;
	std::map<std::tuple<std::string, int, std::string>, Template_t> m_Templates;
	// Each figure's last placing, kept while it is in the same pose at the same place.
	struct Placed_t
	{
		std::shared_ptr<const xv::map::CMapMesh> m_pPosed, m_pPlaced;
		xv::CVec3 m_vecOrigin;
		float m_flYaw = 0.0f;
		std::shared_ptr<const std::vector<xv::model::CTransform>> m_pPose; // null: the reference pose
		std::shared_ptr<const xv::model::CModel> m_pModel;
	};
	std::map<int, Placed_t> m_Placed;
	// Props' poses in model space: model, skin, sequence, cycle step.
	std::map<std::tuple<std::string, int, int, int>, std::shared_ptr<const xv::map::CMapMesh>> m_PropPoses;
};

// The upper-body sequences' name for a weapon, from its world model ("models/weapons/
// w_rif_ak47.mdl" holds an AK: "AK"); empty for one the player models have none for.
[[nodiscard]] std::string DemoWeaponFamily( std::string_view szWorldModel );
