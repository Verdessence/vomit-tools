//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Source studio models (MDL versions 44-49, the 2013 branch and its games), read for the
//          particle preview: the reference pose's bones, hitboxes and attachments, and the
//          triangles of the default body with their materials. The .mdl holds the skeleton and
//          names, the .vvd the vertices and the .dx90.vtx (or another .vtx) the triangles.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "util/Vec3.h"

namespace xv::model
{

// A rotation (with scale) and an offset, as Source stores bone and attachment transforms: three
// rows of x, y, z and the offset.
struct CTransform
{
	std::array<float, 12> m_Values{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };

	[[nodiscard]] static CTransform Translation( CVec3 vecOffset );
	[[nodiscard]] CVec3 Apply( CVec3 vecPoint ) const;
	[[nodiscard]] CVec3 Rotate( CVec3 vecDirection ) const;
	[[nodiscard]] CVec3 GetOrigin() const { return { m_Values[ 3 ], m_Values[ 7 ], m_Values[ 11 ] }; }
	// This after other: other is applied first.
	[[nodiscard]] CTransform operator*( const CTransform &other ) const;
	// nullopt when the rotation can't be undone (no volume).
	[[nodiscard]] std::optional<CTransform> Inverse() const;
};

struct Bone_t
{
	std::string m_szName;
	int m_nParent = -1;
	CTransform m_ToModel; // the reference pose: bone space to model space
};

struct Hitbox_t
{
	int m_nBone = 0;
	int m_nGroup = 0;
	CVec3 m_vecMin;
	CVec3 m_vecMax;
	std::string m_szName;
};

struct Attachment_t
{
	std::string m_szName;
	int m_nBone = 0;
	CTransform m_Local; // attachment space to bone space
};

struct Vertex_t
{
	CVec3 m_vecPosition; // model space, in the reference pose
	CVec3 m_vecNormal;
	std::array<float, 2> m_Uv{};
};

struct Mesh_t
{
	int m_nMaterial = -1;                // into CModel::m_Materials, after the default skin
	std::vector<std::uint32_t> m_Indices; // three per triangle, into CModel::m_Vertices
};

class CModel
{
public:
	std::string m_szName;
	int m_nVersion = 0;
	std::vector<Bone_t> m_Bones;
	std::vector<Hitbox_t> m_Hitboxes; // the first hitbox set, which models use by default
	std::vector<Attachment_t> m_Attachments;
	std::vector<Vertex_t> m_Vertices;
	std::vector<Mesh_t> m_Meshes;
	// Per texture, the material it is, as a path under materials/ ("models/player/ct_gign.vmt"):
	// the first of the model's material folders that has it, else the first folder.
	std::vector<std::string> m_Materials;
	CVec3 m_vecMin; // around the vertices
	CVec3 m_vecMax;

	[[nodiscard]] int GetTriangleCount() const;
	// Where an attachment is, in model space (reference pose).
	[[nodiscard]] CTransform AttachmentToModel( std::size_t nAttachment ) const;
	// A point inside a hitbox, from its box's corners by u, v and w (0-1 each), in model space.
	[[nodiscard]] CVec3 HitboxPoint( std::size_t nHitbox, CVec3 vecUvw ) const;
};

// Reads a file of the game ("models/player/ct_gign.vvd"); nullopt when it isn't there.
using Reader_t = std::function<std::optional<std::vector<std::byte>>( std::string_view szGamePath )>;

// Reads models/<...>.mdl and its .vvd and .vtx. The files are untrusted: every count and offset is
// checked, and nullopt (with the reason in error) comes back for anything that doesn't add up.
[[nodiscard]] std::optional<CModel> LoadModel( std::string_view szMdlPath, const Reader_t &pfnRead, std::string *pError = nullptr );

} // namespace xv::model
