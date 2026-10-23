//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Model.h. Layouts are from the public Source SDK 2013 headers (studio.h,
//          optimize.h), as format facts only.
//
//===========================================================================//
#include "Model.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>

#include "vfs/Vfs.h"

namespace xv::model
{

namespace
{

constexpr std::int32_t MDL_ID = 0x54534449; // "IDST"
constexpr std::int32_t VVD_ID = 0x56534449; // "IDSV"
constexpr int VTX_VERSION = 7;

// Sizes of the records the counts below step over.
constexpr std::size_t BONE_SIZE = 216;
constexpr std::size_t HITBOX_SET_SIZE = 12;
constexpr std::size_t HITBOX_SIZE = 68;
constexpr std::size_t ATTACHMENT_SIZE = 92;
constexpr std::size_t TEXTURE_SIZE = 64;
constexpr std::size_t BODY_PART_SIZE = 16;
constexpr std::size_t MODEL_SIZE = 148;
constexpr std::size_t MESH_SIZE = 116;
constexpr std::size_t VVD_VERTEX_SIZE = 48;
constexpr std::size_t VVD_FIXUP_SIZE = 12;
constexpr std::size_t VTX_BODY_PART_SIZE = 8;
constexpr std::size_t VTX_MODEL_SIZE = 8;
constexpr std::size_t VTX_LOD_SIZE = 12;
constexpr std::size_t VTX_MESH_SIZE = 9;
constexpr std::size_t VTX_STRIP_GROUP_SIZE = 25;
constexpr std::size_t VTX_STRIP_SIZE = 27;
constexpr std::size_t VTX_VERTEX_SIZE = 9;
constexpr std::uint8_t STRIP_IS_TRISTRIP = 0x02;
// More than any real model has; stops a corrupt count from asking for gigabytes.
constexpr std::int32_t MAX_COUNT = 1 << 20;

// Little-endian reads that fail instead of running off the end.
class CReader
{
public:
	explicit CReader( std::span<const std::byte> data )
		: m_Data( data )
	{
	}

	[[nodiscard]] bool Has( std::size_t nOffset, std::size_t nSize ) const { return nOffset <= m_Data.size() && nSize <= m_Data.size() - nOffset; }

	template <typename T>
	[[nodiscard]] T Read( std::size_t nOffset )
	{
		T value{};
		if ( Has( nOffset, sizeof( T ) ) )
		{
			std::memcpy( &value, m_Data.data() + nOffset, sizeof( T ) );
		}
		else
		{
			m_bFailed = true;
		}
		return value;
	}

	[[nodiscard]] CVec3 Vector( std::size_t nOffset ) { return { Read<float>( nOffset ), Read<float>( nOffset + 4 ), Read<float>( nOffset + 8 ) }; }

	[[nodiscard]] CTransform Matrix( std::size_t nOffset )
	{
		CTransform t;
		for ( std::size_t i = 0; i < 12; i++ )
		{
			t.m_Values[ i ] = Read<float>( nOffset + i * 4 );
		}
		return t;
	}

	// A NUL-terminated string at an offset; empty when it runs off the end.
	[[nodiscard]] std::string String( std::size_t nOffset )
	{
		if ( nOffset >= m_Data.size() )
		{
			return {};
		}
		const auto *pStart = reinterpret_cast<const char *>( m_Data.data() + nOffset );
		const std::size_t nMax = m_Data.size() - nOffset;
		return std::string{ pStart, strnlen( pStart, nMax ) };
	}

	// Where an array of count records of size starts, if it fits; adds to the failure otherwise.
	[[nodiscard]] bool Array( std::size_t nOffset, std::int32_t nCount, std::size_t nSize )
	{
		if ( nCount < 0 || nCount > MAX_COUNT || !Has( nOffset, static_cast<std::size_t>( nCount ) * nSize ) )
		{
			m_bFailed = true;
			return false;
		}
		return true;
	}

	[[nodiscard]] bool IsFailed() const { return m_bFailed; }

private:
	std::span<const std::byte> m_Data;
	bool m_bFailed = false;
};

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::optional<CModel> Fail( std::string *pError, std::string szWhy )
{
	if ( pError )
	{
		*pError = std::move( szWhy );
	}
	return std::nullopt;
}

//-----------------------------------------------------------------------------
// Purpose: "models/a.mdl" -> "models/a" + extension.
//-----------------------------------------------------------------------------
std::string Sibling( std::string_view szMdlPath, std::string_view szExtension )
{
	const std::size_t nDot = szMdlPath.rfind( '.' );
	return std::string{ szMdlPath.substr( 0, nDot ) } + std::string{ szExtension };
}

//-----------------------------------------------------------------------------
// Purpose: A material folder as the model names it ("models\player\") to a path piece
//          ("models/player/").
//-----------------------------------------------------------------------------
std::string Folder( std::string szFolder )
{
	std::ranges::replace( szFolder, '\\', '/' );
	while ( !szFolder.empty() && szFolder.front() == '/' )
	{
		szFolder.erase( szFolder.begin() );
	}
	if ( !szFolder.empty() && szFolder.back() != '/' )
	{
		szFolder += '/';
	}
	return szFolder;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CTransform CTransform::Translation( CVec3 vecOffset )
{
	CTransform t;
	t.m_Values[ 3 ] = vecOffset.m_flX;
	t.m_Values[ 7 ] = vecOffset.m_flY;
	t.m_Values[ 11 ] = vecOffset.m_flZ;
	return t;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 CTransform::Rotate( CVec3 v ) const
{
	const auto &m = m_Values;
	return { m[ 0 ] * v.m_flX + m[ 1 ] * v.m_flY + m[ 2 ] * v.m_flZ, m[ 4 ] * v.m_flX + m[ 5 ] * v.m_flY + m[ 6 ] * v.m_flZ, m[ 8 ] * v.m_flX + m[ 9 ] * v.m_flY + m[ 10 ] * v.m_flZ };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CVec3 CTransform::Apply( CVec3 v ) const
{
	return Rotate( v ) + GetOrigin();
}

//-----------------------------------------------------------------------------
// Purpose: This after other.
//-----------------------------------------------------------------------------
CTransform CTransform::operator*( const CTransform &other ) const
{
	CTransform out;
	const auto &a = m_Values;
	const auto &b = other.m_Values;
	for ( int r = 0; r < 3; r++ )
	{
		for ( int c = 0; c < 4; c++ )
		{
			float flSum = c == 3 ? a[ static_cast<std::size_t>( r * 4 + 3 ) ] : 0.0f;
			for ( int k = 0; k < 3; k++ )
			{
				flSum += a[ static_cast<std::size_t>( r * 4 + k ) ] * b[ static_cast<std::size_t>( k * 4 + c ) ];
			}
			out.m_Values[ static_cast<std::size_t>( r * 4 + c ) ] = flSum;
		}
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: The 3x3 part by its adjugate, then the offset undone.
//-----------------------------------------------------------------------------
std::optional<CTransform> CTransform::Inverse() const
{
	const auto &m = m_Values;
	const float a = m[ 0 ], b = m[ 1 ], c = m[ 2 ], d = m[ 4 ], e = m[ 5 ], f = m[ 6 ], g = m[ 8 ], h = m[ 9 ], i = m[ 10 ];
	const float flDet = a * ( e * i - f * h ) - b * ( d * i - f * g ) + c * ( d * h - e * g );
	if ( std::abs( flDet ) < 1e-12f )
	{
		return std::nullopt;
	}
	const float s = 1.0f / flDet;
	CTransform out;
	auto &o = out.m_Values;
	o[ 0 ] = ( e * i - f * h ) * s;
	o[ 1 ] = ( c * h - b * i ) * s;
	o[ 2 ] = ( b * f - c * e ) * s;
	o[ 4 ] = ( f * g - d * i ) * s;
	o[ 5 ] = ( a * i - c * g ) * s;
	o[ 6 ] = ( c * d - a * f ) * s;
	o[ 8 ] = ( d * h - e * g ) * s;
	o[ 9 ] = ( b * g - a * h ) * s;
	o[ 10 ] = ( a * e - b * d ) * s;
	const CVec3 vecOffset = -out.Rotate( GetOrigin() );
	o[ 3 ] = vecOffset.m_flX;
	o[ 7 ] = vecOffset.m_flY;
	o[ 11 ] = vecOffset.m_flZ;
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CModel::GetTriangleCount() const
{
	std::size_t nCount = 0;
	for ( const Mesh_t &mesh : m_Meshes )
	{
		nCount += mesh.m_Indices.size() / 3;
	}
	return static_cast<int>( nCount );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CTransform CModel::AttachmentToModel( std::size_t nAttachment ) const
{
	if ( nAttachment >= m_Attachments.size() )
	{
		return {};
	}
	const Attachment_t &attachment = m_Attachments[ nAttachment ];
	const CTransform bone = attachment.m_nBone >= 0 && static_cast<std::size_t>( attachment.m_nBone ) < m_Bones.size() ? m_Bones[ static_cast<std::size_t>( attachment.m_nBone ) ].m_ToModel : CTransform{};
	return bone * attachment.m_Local;
}

//-----------------------------------------------------------------------------
// Purpose: A point inside a hitbox, from its box's corners by u, v and w.
//-----------------------------------------------------------------------------
CVec3 CModel::HitboxPoint( std::size_t nHitbox, CVec3 vecUvw ) const
{
	if ( nHitbox >= m_Hitboxes.size() )
	{
		return {};
	}
	const Hitbox_t &box = m_Hitboxes[ nHitbox ];
	const auto lerp = []( float a, float b, float t ) { return a + ( b - a ) * t; };
	const CVec3 vecLocal{ lerp( box.m_vecMin.m_flX, box.m_vecMax.m_flX, vecUvw.m_flX ), lerp( box.m_vecMin.m_flY, box.m_vecMax.m_flY, vecUvw.m_flY ), lerp( box.m_vecMin.m_flZ, box.m_vecMax.m_flZ, vecUvw.m_flZ ) };
	const CTransform bone = box.m_nBone >= 0 && static_cast<std::size_t>( box.m_nBone ) < m_Bones.size() ? m_Bones[ static_cast<std::size_t>( box.m_nBone ) ].m_ToModel : CTransform{};
	return bone.Apply( vecLocal );
}

//-----------------------------------------------------------------------------
// Purpose: Reads the .mdl, then its .vvd and .vtx.
//-----------------------------------------------------------------------------
std::optional<CModel> LoadModel( std::string_view szMdlPath, const Reader_t &pfnRead, std::string *pError )
{
	const std::string szMdl = vfs::NormalizePath( szMdlPath );
	const auto mdlData = pfnRead( szMdl );
	if ( !mdlData )
	{
		return Fail( pError, szMdl + " is missing" );
	}
	CReader mdl{ *mdlData };
	if ( mdl.Read<std::int32_t>( 0 ) != MDL_ID )
	{
		return Fail( pError, szMdl + " is not a model" );
	}
	CModel model;
	model.m_nVersion = mdl.Read<std::int32_t>( 4 );
	if ( model.m_nVersion < 44 || model.m_nVersion > 49 )
	{
		return Fail( pError, szMdl + ": model version " + std::to_string( model.m_nVersion ) + " isn't supported (44 to 49 are)" );
	}
	const std::int32_t nChecksum = mdl.Read<std::int32_t>( 8 );
	model.m_szName = mdl.String( 12 ).substr( 0, 64 );

	// Bones: the reference pose is the inverse of each bone's poseToBone (model to bone space).
	const auto nBones = mdl.Read<std::int32_t>( 156 );
	const auto nBoneIndex = static_cast<std::size_t>( mdl.Read<std::int32_t>( 160 ) );
	if ( !mdl.Array( nBoneIndex, nBones, BONE_SIZE ) )
	{
		return Fail( pError, szMdl + ": the bones run past the end of the file" );
	}
	for ( std::int32_t i = 0; i < nBones; i++ )
	{
		const std::size_t nAt = nBoneIndex + static_cast<std::size_t>( i ) * BONE_SIZE;
		Bone_t bone;
		bone.m_szName = mdl.String( nAt + static_cast<std::size_t>( mdl.Read<std::int32_t>( nAt ) ) );
		bone.m_nParent = mdl.Read<std::int32_t>( nAt + 4 );
		bone.m_ToModel = mdl.Matrix( nAt + 96 ).Inverse().value_or( CTransform{} );
		model.m_Bones.push_back( std::move( bone ) );
	}

	// The first hitbox set.
	const auto nSets = mdl.Read<std::int32_t>( 172 );
	const auto nSetIndex = static_cast<std::size_t>( mdl.Read<std::int32_t>( 176 ) );
	if ( nSets > 0 && mdl.Array( nSetIndex, 1, HITBOX_SET_SIZE ) )
	{
		const auto nBoxes = mdl.Read<std::int32_t>( nSetIndex + 4 );
		const std::size_t nBoxIndex = nSetIndex + static_cast<std::size_t>( mdl.Read<std::int32_t>( nSetIndex + 8 ) );
		if ( mdl.Array( nBoxIndex, nBoxes, HITBOX_SIZE ) )
		{
			for ( std::int32_t i = 0; i < nBoxes; i++ )
			{
				const std::size_t nAt = nBoxIndex + static_cast<std::size_t>( i ) * HITBOX_SIZE;
				Hitbox_t box;
				box.m_nBone = std::clamp( mdl.Read<std::int32_t>( nAt ), 0, std::max( 0, nBones - 1 ) );
				box.m_nGroup = mdl.Read<std::int32_t>( nAt + 4 );
				box.m_vecMin = mdl.Vector( nAt + 8 );
				box.m_vecMax = mdl.Vector( nAt + 20 );
				if ( const auto nName = mdl.Read<std::int32_t>( nAt + 32 ) )
				{
					box.m_szName = mdl.String( nAt + static_cast<std::size_t>( nName ) );
				}
				model.m_Hitboxes.push_back( std::move( box ) );
			}
		}
	}

	// Attachments.
	const auto nAttachments = mdl.Read<std::int32_t>( 240 );
	const auto nAttachmentIndex = static_cast<std::size_t>( mdl.Read<std::int32_t>( 244 ) );
	if ( nAttachments > 0 && mdl.Array( nAttachmentIndex, nAttachments, ATTACHMENT_SIZE ) )
	{
		for ( std::int32_t i = 0; i < nAttachments; i++ )
		{
			const std::size_t nAt = nAttachmentIndex + static_cast<std::size_t>( i ) * ATTACHMENT_SIZE;
			Attachment_t attachment;
			attachment.m_szName = mdl.String( nAt + static_cast<std::size_t>( mdl.Read<std::int32_t>( nAt ) ) );
			attachment.m_nBone = std::clamp( mdl.Read<std::int32_t>( nAt + 8 ), 0, std::max( 0, nBones - 1 ) );
			attachment.m_Local = mdl.Matrix( nAt + 12 );
			model.m_Attachments.push_back( std::move( attachment ) );
		}
	}

	// Materials: texture names, looked for in each material folder; the default skin maps a
	// mesh's material to a texture.
	std::vector<std::string> folders;
	const auto nFolders = mdl.Read<std::int32_t>( 212 );
	const auto nFolderIndex = static_cast<std::size_t>( mdl.Read<std::int32_t>( 216 ) );
	if ( nFolders > 0 && mdl.Array( nFolderIndex, nFolders, 4 ) )
	{
		for ( std::int32_t i = 0; i < nFolders; i++ )
		{
			folders.push_back( Folder( mdl.String( static_cast<std::size_t>( mdl.Read<std::int32_t>( nFolderIndex + static_cast<std::size_t>( i ) * 4 ) ) ) ) );
		}
	}
	if ( folders.empty() )
	{
		folders.emplace_back();
	}
	const auto nTextures = mdl.Read<std::int32_t>( 204 );
	const auto nTextureIndex = static_cast<std::size_t>( mdl.Read<std::int32_t>( 208 ) );
	if ( nTextures > 0 && mdl.Array( nTextureIndex, nTextures, TEXTURE_SIZE ) )
	{
		for ( std::int32_t i = 0; i < nTextures; i++ )
		{
			const std::size_t nAt = nTextureIndex + static_cast<std::size_t>( i ) * TEXTURE_SIZE;
			std::string szName = mdl.String( nAt + static_cast<std::size_t>( mdl.Read<std::int32_t>( nAt ) ) );
			std::ranges::replace( szName, '\\', '/' );
			std::string szFound;
			for ( const std::string &szFolder : folders )
			{
				const std::string szCandidate = vfs::NormalizePath( szFolder + szName + ".vmt" );
				if ( pfnRead( "materials/" + szCandidate ) )
				{
					szFound = szCandidate;
					break;
				}
			}
			model.m_Materials.push_back( szFound.empty() ? vfs::NormalizePath( folders.front() + szName + ".vmt" ) : szFound );
		}
	}
	std::vector<int> skin; // skin family 0: material reference -> texture
	const auto nSkinRefs = mdl.Read<std::int32_t>( 220 );
	const auto nSkinIndex = static_cast<std::size_t>( mdl.Read<std::int32_t>( 228 ) );
	if ( nSkinRefs > 0 && mdl.Array( nSkinIndex, nSkinRefs, 2 ) )
	{
		for ( std::int32_t i = 0; i < nSkinRefs; i++ )
		{
			skin.push_back( mdl.Read<std::int16_t>( nSkinIndex + static_cast<std::size_t>( i ) * 2 ) );
		}
	}
	if ( mdl.IsFailed() )
	{
		return Fail( pError, szMdl + " is damaged" );
	}

	if ( mdl.Read<std::int32_t>( 232 ) <= 0 )
	{
		return Fail( pError, szMdl + " has nothing to draw (it holds animations for other models)" );
	}

	// Vertices: the root LOD's, through the fixup table when there is one.
	const std::string szVvd = Sibling( szMdl, ".vvd" );
	const auto vvdData = pfnRead( szVvd );
	if ( !vvdData )
	{
		return Fail( pError, szVvd + " is missing" );
	}
	CReader vvd{ *vvdData };
	if ( vvd.Read<std::int32_t>( 0 ) != VVD_ID || vvd.Read<std::int32_t>( 8 ) != nChecksum )
	{
		return Fail( pError, szVvd + " doesn't belong to " + szMdl );
	}
	const auto nRootVertices = vvd.Read<std::int32_t>( 16 );
	const auto nFixups = vvd.Read<std::int32_t>( 48 );
	const auto nFixupStart = static_cast<std::size_t>( vvd.Read<std::int32_t>( 52 ) );
	const auto nVertexStart = static_cast<std::size_t>( vvd.Read<std::int32_t>( 56 ) );
	const auto readVertex = [ & ]( std::int32_t nIndex ) {
		const std::size_t nAt = nVertexStart + static_cast<std::size_t>( nIndex ) * VVD_VERTEX_SIZE;
		Vertex_t vertex;
		vertex.m_vecPosition = vvd.Vector( nAt + 16 );
		vertex.m_vecNormal = vvd.Vector( nAt + 28 );
		vertex.m_Uv = { vvd.Read<float>( nAt + 40 ), vvd.Read<float>( nAt + 44 ) };
		return vertex;
	};
	if ( nFixups > 0 )
	{
		if ( !vvd.Array( nFixupStart, nFixups, VVD_FIXUP_SIZE ) )
		{
			return Fail( pError, szVvd + " is damaged" );
		}
		for ( std::int32_t i = 0; i < nFixups; i++ )
		{
			const std::size_t nAt = nFixupStart + static_cast<std::size_t>( i ) * VVD_FIXUP_SIZE;
			const auto nSource = vvd.Read<std::int32_t>( nAt + 4 );
			const auto nCount = vvd.Read<std::int32_t>( nAt + 8 );
			if ( vvd.Read<std::int32_t>( nAt ) < 0 || nSource < 0 || !vvd.Array( nVertexStart + static_cast<std::size_t>( nSource ) * VVD_VERTEX_SIZE, nCount, VVD_VERTEX_SIZE ) )
			{
				continue; // LOD 0 keeps every fixup whose lod is 0 or more
			}
			for ( std::int32_t k = 0; k < nCount; k++ )
			{
				model.m_Vertices.push_back( readVertex( nSource + k ) );
			}
		}
	}
	else
	{
		if ( !vvd.Array( nVertexStart, nRootVertices, VVD_VERTEX_SIZE ) )
		{
			return Fail( pError, szVvd + " is damaged" );
		}
		for ( std::int32_t i = 0; i < nRootVertices; i++ )
		{
			model.m_Vertices.push_back( readVertex( i ) );
		}
	}

	// Triangles: the .vtx for DirectX 9 first, the others when it's missing.
	std::optional<std::vector<std::byte>> vtxData;
	std::string szVtx;
	for ( std::string_view szKind : { ".dx90.vtx", ".dx80.vtx", ".sw.vtx", ".vtx" } )
	{
		szVtx = Sibling( szMdl, szKind );
		if ( ( vtxData = pfnRead( szVtx ) ) )
		{
			break;
		}
	}
	if ( !vtxData )
	{
		return Fail( pError, Sibling( szMdl, ".dx90.vtx" ) + " is missing" );
	}
	CReader vtx{ *vtxData };
	if ( vtx.Read<std::int32_t>( 0 ) != VTX_VERSION || vtx.Read<std::int32_t>( 16 ) != nChecksum )
	{
		return Fail( pError, szVtx + " doesn't belong to " + szMdl );
	}
	const auto nBodyParts = mdl.Read<std::int32_t>( 232 );
	const auto nBodyPartIndex = static_cast<std::size_t>( mdl.Read<std::int32_t>( 236 ) );
	const auto nVtxBodyParts = vtx.Read<std::int32_t>( 28 );
	const auto nVtxBodyPartIndex = static_cast<std::size_t>( vtx.Read<std::int32_t>( 32 ) );
	if ( !mdl.Array( nBodyPartIndex, nBodyParts, BODY_PART_SIZE ) || !vtx.Array( nVtxBodyPartIndex, nVtxBodyParts, VTX_BODY_PART_SIZE ) )
	{
		return Fail( pError, szMdl + ": the body parts don't add up" );
	}
	for ( std::int32_t nPart = 0; nPart < std::min( nBodyParts, nVtxBodyParts ); nPart++ )
	{
		// The first model of each body part is its default.
		const std::size_t nPartAt = nBodyPartIndex + static_cast<std::size_t>( nPart ) * BODY_PART_SIZE;
		const std::size_t nModelAt = nPartAt + static_cast<std::size_t>( mdl.Read<std::int32_t>( nPartAt + 12 ) );
		const std::size_t nVtxPartAt = nVtxBodyPartIndex + static_cast<std::size_t>( nPart ) * VTX_BODY_PART_SIZE;
		const std::size_t nVtxModelAt = nVtxPartAt + static_cast<std::size_t>( vtx.Read<std::int32_t>( nVtxPartAt + 4 ) );
		if ( mdl.Read<std::int32_t>( nPartAt + 4 ) < 1 || vtx.Read<std::int32_t>( nVtxPartAt ) < 1 || !mdl.Array( nModelAt, 1, MODEL_SIZE ) || !vtx.Array( nVtxModelAt, 1, VTX_MODEL_SIZE ) )
		{
			continue;
		}
		// The model's vertices start at vertexindex (bytes into the vertex block).
		const std::size_t nModelFirst = static_cast<std::size_t>( std::max( 0, mdl.Read<std::int32_t>( nModelAt + 84 ) ) ) / VVD_VERTEX_SIZE;
		const auto nMeshes = mdl.Read<std::int32_t>( nModelAt + 72 );
		const std::size_t nMeshIndex = nModelAt + static_cast<std::size_t>( mdl.Read<std::int32_t>( nModelAt + 76 ) );
		if ( vtx.Read<std::int32_t>( nVtxModelAt ) < 1 )
		{
			continue;
		}
		const std::size_t nLodAt = nVtxModelAt + static_cast<std::size_t>( vtx.Read<std::int32_t>( nVtxModelAt + 4 ) );
		if ( !vtx.Array( nLodAt, 1, VTX_LOD_SIZE ) )
		{
			continue;
		}
		const auto nVtxMeshes = vtx.Read<std::int32_t>( nLodAt );
		const std::size_t nVtxMeshIndex = nLodAt + static_cast<std::size_t>( vtx.Read<std::int32_t>( nLodAt + 4 ) );
		if ( !mdl.Array( nMeshIndex, nMeshes, MESH_SIZE ) || !vtx.Array( nVtxMeshIndex, nVtxMeshes, VTX_MESH_SIZE ) )
		{
			continue;
		}
		for ( std::int32_t nMesh = 0; nMesh < std::min( nMeshes, nVtxMeshes ); nMesh++ )
		{
			const std::size_t nMeshAt = nMeshIndex + static_cast<std::size_t>( nMesh ) * MESH_SIZE;
			const auto nMaterialRef = mdl.Read<std::int32_t>( nMeshAt );
			const std::size_t nMeshFirst = nModelFirst + static_cast<std::size_t>( std::max( 0, mdl.Read<std::int32_t>( nMeshAt + 12 ) ) );
			Mesh_t mesh;
			if ( nMaterialRef >= 0 && static_cast<std::size_t>( nMaterialRef ) < skin.size() )
			{
				mesh.m_nMaterial = skin[ static_cast<std::size_t>( nMaterialRef ) ];
			}
			const std::size_t nVtxMeshAt = nVtxMeshIndex + static_cast<std::size_t>( nMesh ) * VTX_MESH_SIZE;
			const auto nGroups = vtx.Read<std::int32_t>( nVtxMeshAt );
			const std::size_t nGroupIndex = nVtxMeshAt + static_cast<std::size_t>( vtx.Read<std::int32_t>( nVtxMeshAt + 4 ) );
			if ( !vtx.Array( nGroupIndex, nGroups, VTX_STRIP_GROUP_SIZE ) )
			{
				continue;
			}
			for ( std::int32_t nGroup = 0; nGroup < nGroups; nGroup++ )
			{
				const std::size_t nGroupAt = nGroupIndex + static_cast<std::size_t>( nGroup ) * VTX_STRIP_GROUP_SIZE;
				const auto nVerts = vtx.Read<std::int32_t>( nGroupAt );
				const std::size_t nVertAt = nGroupAt + static_cast<std::size_t>( vtx.Read<std::int32_t>( nGroupAt + 4 ) );
				const auto nIndices = vtx.Read<std::int32_t>( nGroupAt + 8 );
				const std::size_t nIndexAt = nGroupAt + static_cast<std::size_t>( vtx.Read<std::int32_t>( nGroupAt + 12 ) );
				const auto nStrips = vtx.Read<std::int32_t>( nGroupAt + 16 );
				const std::size_t nStripAt = nGroupAt + static_cast<std::size_t>( vtx.Read<std::int32_t>( nGroupAt + 20 ) );
				if ( !vtx.Array( nVertAt, nVerts, VTX_VERTEX_SIZE ) || !vtx.Array( nIndexAt, nIndices, 2 ) || !vtx.Array( nStripAt, nStrips, VTX_STRIP_SIZE ) )
				{
					continue;
				}
				// A strip group index -> a vertex of the whole model.
				const auto vertexOf = [ & ]( std::int32_t nIndex ) -> std::optional<std::uint32_t> {
					if ( nIndex < 0 || nIndex >= nIndices )
					{
						return std::nullopt;
					}
					const auto nLocal = vtx.Read<std::uint16_t>( nIndexAt + static_cast<std::size_t>( nIndex ) * 2 );
					if ( nLocal >= nVerts )
					{
						return std::nullopt;
					}
					const auto nOriginal = vtx.Read<std::uint16_t>( nVertAt + static_cast<std::size_t>( nLocal ) * VTX_VERTEX_SIZE + 4 );
					const std::size_t nGlobal = nMeshFirst + nOriginal;
					if ( nGlobal >= model.m_Vertices.size() )
					{
						return std::nullopt;
					}
					return static_cast<std::uint32_t>( nGlobal );
				};
				const auto addTriangle = [ & ]( std::int32_t a, std::int32_t b, std::int32_t c ) {
					const auto va = vertexOf( a ), vb = vertexOf( b ), vc = vertexOf( c );
					if ( va && vb && vc && *va != *vb && *vb != *vc && *va != *vc )
					{
						mesh.m_Indices.insert( mesh.m_Indices.end(), { *va, *vb, *vc } );
					}
				};
				for ( std::int32_t nStrip = 0; nStrip < nStrips; nStrip++ )
				{
					const std::size_t nAt = nStripAt + static_cast<std::size_t>( nStrip ) * VTX_STRIP_SIZE;
					const auto nCount = vtx.Read<std::int32_t>( nAt );
					const auto nFirst = vtx.Read<std::int32_t>( nAt + 4 );
					const auto nFlags = vtx.Read<std::uint8_t>( nAt + 18 );
					if ( nCount < 0 || nFirst < 0 || nFirst > nIndices - nCount )
					{
						continue;
					}
					if ( nFlags & STRIP_IS_TRISTRIP )
					{
						for ( std::int32_t i = 0; i + 2 < nCount; i++ )
						{
							// Every other triangle of a strip is wound the other way.
							if ( i % 2 == 0 )
							{
								addTriangle( nFirst + i, nFirst + i + 1, nFirst + i + 2 );
							}
							else
							{
								addTriangle( nFirst + i + 1, nFirst + i, nFirst + i + 2 );
							}
						}
					}
					else
					{
						for ( std::int32_t i = 0; i + 2 < nCount; i += 3 )
						{
							addTriangle( nFirst + i, nFirst + i + 1, nFirst + i + 2 );
						}
					}
				}
			}
			if ( !mesh.m_Indices.empty() )
			{
				model.m_Meshes.push_back( std::move( mesh ) );
			}
		}
	}
	if ( vtx.IsFailed() || vvd.IsFailed() )
	{
		return Fail( pError, szMdl + ": its .vvd or .vtx is damaged" );
	}

	// The box around what's drawn.
	if ( !model.m_Vertices.empty() )
	{
		model.m_vecMin = model.m_vecMax = model.m_Vertices.front().m_vecPosition;
		for ( const Vertex_t &vertex : model.m_Vertices )
		{
			const CVec3 &p = vertex.m_vecPosition;
			model.m_vecMin = { std::min( model.m_vecMin.m_flX, p.m_flX ), std::min( model.m_vecMin.m_flY, p.m_flY ), std::min( model.m_vecMin.m_flZ, p.m_flZ ) };
			model.m_vecMax = { std::max( model.m_vecMax.m_flX, p.m_flX ), std::max( model.m_vecMax.m_flY, p.m_flY ), std::max( model.m_vecMax.m_flZ, p.m_flZ ) };
		}
	}
	return model;
}

} // namespace xv::model
