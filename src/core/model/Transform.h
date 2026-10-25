//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Edits that reach the vertex files (.vvd, .vtx) and the .phy as well as the .mdl:
//          scaling a whole model, turning it in quarter turns and moving it, removing meshes,
//          limiting its detail levels and working its normals out again. Each keeps every
//          file's size and the shared checksum, so the files still belong together; what can't be
//          done honestly in place is refused with the reason.
//
//===========================================================================//
#pragma once

#include "Edit.h"

namespace xv::model
{

// Bones, hitboxes, attachments, bounds, eyes, flexes, animations (their positions and IK rules),
// the vertices and the collision hulls, all by one factor.
void ScaleModel( Files_t &files, float flScale );

// Quarter turns about each axis (applied roll, then pitch, then yaw, as Source angles are) and
// then a move, in model space. Only for models with one bone and no IK rules (props): the bone
// stays, and what hangs off it turns around it.
struct Placement_t
{
	int m_nPitch = 0, m_nYaw = 0, m_nRoll = 0; // quarter turns
	CVec3 m_vecMove;
};
void PlaceModel( Files_t &files, const Placement_t &placement );
// Why PlaceModel would refuse the model, or empty when it can be placed.
[[nodiscard]] std::string CanPlace( const Files_t &files );

// Takes a mesh out of every detail level of every .vtx; the .mdl and .vvd keep its data.
void RemoveMesh( Files_t &files, int nBodygroup, int nModel, int nMesh );

// The detail levels of the first model with any, by the distance each starts at (the shadow
// level's is negative).
[[nodiscard]] std::vector<float> ReadLodSwitchPoints( const Files_t &files );
// Levels from nKeep on are never switched to (their switch point becomes unreachable); the
// shadow level stays.
void LimitLods( Files_t &files, int nKeep );

// Smooth normals from the triangles of the root detail level: faces meeting at a point are
// averaged when they're within flHardAngle degrees of the vertex's own faces, so seams stay
// hard. Tangents are worked out again to match.
void RecomputeNormals( Files_t &files, float flHardAngle );

} // namespace xv::model
