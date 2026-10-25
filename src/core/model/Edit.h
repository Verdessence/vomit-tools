//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Editing compiled studio models in place, without decompiling. Two kinds of edit:
//          - same-size: a field is patched where it is (flags, mass, hitbox bounds, attachment
//            transforms, the skin table, sequence timings, event frames...);
//          - moving: a name, a list or a block that no longer fits is written again at the end
//            of the .mdl and its offset (and the file's length) pointed at it. Nothing else
//            moves, so every other offset stays right, and a file written without changes is
//            byte for byte what was read.
//          Every edit checks what it touches and throws std::runtime_error, leaving the bytes
//          as they were, when the file doesn't add up or the value can't be stored.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "Mdl.h"

namespace xv::model
{

using Bytes_t = std::vector<std::byte>;
// A model's compiled files by kind: ".mdl", ".vvd", ".dx90.vtx", ".dx80.vtx", ".sw.vtx", ".vtx",
// ".phy" and ".ani" (its animation blocks, whatever the file is called).
using Files_t = std::map<std::string, Bytes_t>;

// What changed between two versions of a set of files: per file, the runs of bytes that differ
// with both versions of each, so a change can be undone and redone without keeping whole files.
struct FileChange_t
{
	struct Run_t
	{
		std::size_t m_nAt = 0;
		Bytes_t m_Old, m_New;
	};
	std::size_t m_nOldSize = 0, m_nNewSize = 0;
	std::vector<Run_t> m_Runs;
};
using Change_t = std::map<std::string, FileChange_t>;
[[nodiscard]] Change_t Diff( const Files_t &before, const Files_t &after );
// Forward turns before into after; backward the other way.
void ApplyChange( Files_t &files, const Change_t &change, bool bForward );

//-----------------------------------------------------------------------------
// The header
//-----------------------------------------------------------------------------
enum class HeaderVector_t
{
	HEADER_VECTOR_EYE,
	HEADER_VECTOR_ILLUMINATION,
	HEADER_VECTOR_HULL_MIN,
	HEADER_VECTOR_HULL_MAX,
	HEADER_VECTOR_VIEW_MIN,
	HEADER_VECTOR_VIEW_MAX
};
void SetHeaderVector( Bytes_t &mdl, HeaderVector_t nWhich, CVec3 vecValue );
void SetFlags( Bytes_t &mdl, int nFlags );
void SetContents( Bytes_t &mdl, int nContents );
void SetMass( Bytes_t &mdl, float flMass );
// The internal name (header, 63 characters at most).
void SetInternalName( Bytes_t &mdl, const std::string &szName );
// $surfaceprop and $keyvalues; they move to the end when they don't fit.
void SetSurfaceProp( Bytes_t &mdl, const std::string &szName );
void SetKeyValues( Bytes_t &mdl, const std::string &szText );
// The .ani the animation blocks are in ("models/props/crate.ani"), for a model saved under a new
// name. Only models with animation blocks have one.
void SetAnimationFile( Bytes_t &mdl, const std::string &szPath );
// $includemodel: the models whose sequences this one uses.
void SetIncludes( Bytes_t &mdl, const std::vector<std::string> &includes );

//-----------------------------------------------------------------------------
// Names
//-----------------------------------------------------------------------------
enum class Name_t
{
	NAME_BONE,           // nIndex
	NAME_ATTACHMENT,     // nIndex
	NAME_HITBOX_SET,     // nIndex
	NAME_HITBOX,         // nIndex in set nSub
	NAME_SEQUENCE,       // nIndex
	NAME_ACTIVITY,       // nIndex, the sequence's activity
	NAME_ANIMATION,      // nIndex
	NAME_BODYGROUP,      // nIndex
	NAME_BODYGROUP_MODEL, // nIndex in bodygroup nSub (63 characters at most, kept in place)
	NAME_TEXTURE,        // nIndex: a material, under the material folders
	NAME_POSE_PARAMETER, // nIndex
	NAME_EVENT,          // nIndex in sequence nSub
	NAME_FLEX            // nIndex
};
void Rename( Bytes_t &mdl, Name_t nWhat, int nIndex, const std::string &szName, int nSub = 0 );
[[nodiscard]] std::string GetName( const Bytes_t &mdl, Name_t nWhat, int nIndex, int nSub = 0 );

//-----------------------------------------------------------------------------
// Materials and skins
//-----------------------------------------------------------------------------
// $cdmaterials: the folders texture names are looked for under.
void SetFolders( Bytes_t &mdl, const std::vector<std::string> &folders );
// Adds a texture name and gives its index.
int AddTexture( Bytes_t &mdl, const std::string &szName );
// Refused while a skin family uses it; later textures move down one in the skin table.
void RemoveTexture( Bytes_t &mdl, int nTexture );
void SetSkinTexture( Bytes_t &mdl, int nFamily, int nSlot, int nTexture );
// A new family copied from nFrom (-1 for the default), added last.
void AddSkinFamily( Bytes_t &mdl, int nFrom );
// The default family (0) stays.
void RemoveSkinFamily( Bytes_t &mdl, int nFamily );
// Which skin slot a mesh uses: the column of the skin table that picks its material.
void SetMeshMaterial( Bytes_t &mdl, int nBodygroup, int nModel, int nMesh, int nSlot );
struct MeshRef_t
{
	int m_nBodygroup = 0, m_nModel = 0, m_nMesh = 0;
	int m_nSlot = 0; // its column in the skin table
	std::string m_szModel;
};
[[nodiscard]] std::vector<MeshRef_t> ListMeshes( const Bytes_t &mdl );

//-----------------------------------------------------------------------------
// Hitboxes and attachments
//-----------------------------------------------------------------------------
void SetHitbox( Bytes_t &mdl, int nSet, int nBox, const Hitbox_t &box );
// The whole set: boxes added, removed, reordered or copied in from another model.
void SetHitboxes( Bytes_t &mdl, int nSet, const std::vector<Hitbox_t> &boxes );
void SetAttachment( Bytes_t &mdl, int nAttachment, int nBone, const CTransform &local );
void SetAttachments( Bytes_t &mdl, const std::vector<Attachment_t> &attachments );
[[nodiscard]] std::vector<Attachment_t> ReadAttachments( const Bytes_t &mdl );

//-----------------------------------------------------------------------------
// Sequences, animations and their controls
//-----------------------------------------------------------------------------
// Sequence flags shown as switches; the others are how it was compiled and stay as they are.
constexpr int SEQUENCE_LOOPING = 0x0001;
constexpr int SEQUENCE_SNAP = 0x0002;
constexpr int SEQUENCE_AUTOPLAY = 0x0008;
constexpr int SEQUENCE_REALTIME = 0x0100;
constexpr int SEQUENCE_HIDDEN = 0x0400;
constexpr int SEQUENCE_EDITABLE_FLAGS = SEQUENCE_LOOPING | SEQUENCE_SNAP | SEQUENCE_AUTOPLAY | SEQUENCE_REALTIME | SEQUENCE_HIDDEN;
struct SequenceFields_t
{
	int m_nFlags = 0;
	int m_nWeight = 0;
	float m_flFadeIn = 0.2f, m_flFadeOut = 0.2f;
};
void SetSequence( Bytes_t &mdl, int nSequence, const SequenceFields_t &fields );
void SetAnimationFps( Bytes_t &mdl, int nAnimation, float flFps );
// One event's frame, number and options (63 characters at most) in place.
void SetEvent( Bytes_t &mdl, int nSequence, int nEvent, const Event_t &event );
// The sequence's whole list, sorted by cycle: events added, removed or renamed.
void SetEvents( Bytes_t &mdl, int nSequence, std::vector<Event_t> events );
void SetController( Bytes_t &mdl, int nController, float flStart, float flEnd );
void SetPoseParameter( Bytes_t &mdl, int nParameter, float flStart, float flEnd, float flLoop );

//-----------------------------------------------------------------------------
// The .phy's text: solids' mass and damping, ragdoll constraints, break and collision rules.
//-----------------------------------------------------------------------------
void SetPhysicsText( Bytes_t &phy, const std::string &szText );

} // namespace xv::model
