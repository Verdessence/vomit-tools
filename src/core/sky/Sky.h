//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Source 2D skyboxes: six images on the inside of a cube around the player. A map names
//          its sky (worldspawn "skyname", or sv_skyname), and face <f> of sky <name> is the
//          material skybox/<name><f>, e.g. skybox/sky_dustrt.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "texture/Image.h"
#include "texture/Vtf.h"
#include "util/Vec3.h"

namespace xv::vfs
{
class CFileSystem;
}

namespace xv::sky
{

enum class Face_t
{
	FACE_RT,
	FACE_BK,
	FACE_LF,
	FACE_FT,
	FACE_UP,
	FACE_DN
};
inline constexpr std::array<Face_t, 6> FACES{ Face_t::FACE_RT, Face_t::FACE_BK, Face_t::FACE_LF, Face_t::FACE_FT, Face_t::FACE_UP, Face_t::FACE_DN };
[[nodiscard]] constexpr std::size_t IndexOf( Face_t nFace )
{
	return static_cast<std::size_t>( nFace );
}

[[nodiscard]] std::string_view Suffix( Face_t nFace ); // "rt"
// The face a file is for, from the suffix just before its extension: "mysky_rt.tga", "skyUP.png".
[[nodiscard]] std::optional<Face_t> FaceFromFileName( std::string_view szFileName );

using xv::Cross;
using xv::CVec3;
using xv::Dot;
using xv::Normalized;

// Where a face sits in world coordinates (Hammer's: +X is east and yaw 0, +Y north and yaw 90, +Z
// up), and which way its image's right and top edges point, seen from inside the cube. Sky.cpp
// says how this table was established.
struct FaceBasis_t
{
	CVec3 m_vecNormal;
	CVec3 m_vecRight;
	CVec3 m_vecUp;
};
[[nodiscard]] const FaceBasis_t &Basis( Face_t nFace );

enum class Edge_t
{
	EDGE_TOP,
	EDGE_RIGHT,
	EDGE_BOTTOM,
	EDGE_LEFT
};
// The face on the other side of an edge of this face's image.
[[nodiscard]] Face_t Neighbor( Face_t nFace, Edge_t nEdge );

// Where a direction from the centre hits the cube: u and v run 0-1 from the image's top-left.
struct FacePoint_t
{
	Face_t m_nFace = Face_t::FACE_RT;
	float m_flU = 0.0f;
	float m_flV = 0.0f;
};
[[nodiscard]] FacePoint_t Project( CVec3 vecDirection );
[[nodiscard]] CVec3 Direction( Face_t nFace, float u, float v );

// Source view angles in degrees, as setang takes them: positive pitch looks down, yaw 0 looks
// along +X and yaw 90 along +Y.
struct Angles_t
{
	float m_flPitch = 0.0f;
	float m_flYaw = 0.0f;
};
[[nodiscard]] CVec3 Forward( Angles_t angles );
[[nodiscard]] Angles_t AnglesOf( CVec3 vecDirection );

// A texture-coordinate transform, as $basetexturetransform: u' = a*u + b*v + tx, v' = c*u + d*v + ty.
struct CUvTransform
{
	float m_flA = 1.0f;
	float m_flB = 0.0f;
	float m_flC = 0.0f;
	float m_flD = 1.0f;
	float m_flTx = 0.0f;
	float m_flTy = 0.0f;

	[[nodiscard]] bool IsIdentity() const;
	// "center 0 0 scale 1 2 rotate 0 translate 0 0"; parts left out keep their defaults.
	[[nodiscard]] static CUvTransform Parse( std::string_view szText );
};

struct CCube
{
	std::array<texture::CImage, 6> m_Faces;   // by indexOf(Face); a missing face reads as black
	std::array<CUvTransform, 6> m_Transforms; // HL2's half-height sides are stretched to fill

	[[nodiscard]] const texture::CImage &Face( Face_t f ) const { return m_Faces[ IndexOf( f ) ]; }
	[[nodiscard]] texture::CImage &Face( Face_t f ) { return m_Faces[ IndexOf( f ) ]; }
};

// The colour seen in a direction, 0-255 per channel: bilinear, clamped at the face edges.
[[nodiscard]] std::array<float, 3> Sample( const CCube &cube, CVec3 vecDirection );

struct CView
{
	Angles_t m_Angles;
	float m_flFov = 90.0f; // as Source's fov: horizontal degrees at 4:3; wider screens see more at the sides
	int m_nWidth = 0;
	int m_nHeight = 0;

	// The direction through a point of the image (pixel coordinates, 0,0 at the top-left corner).
	[[nodiscard]] CVec3 Ray( float x, float y ) const;
	// Where a direction appears in the image; nullopt when it is behind the viewer.
	[[nodiscard]] std::optional<std::array<float, 2>> ToScreen( CVec3 vecDirection ) const;
};
// Renders what a player sees, into out (view.width x view.height). edges outlines the faces.
void Render( const CCube &cube, const CView &view, texture::CImage &out, bool bEdges = false );

// How well the faces meet: the mean difference (0-255) between the pixels on either side of an edge.
struct Seam_t
{
	Face_t m_nA = Face_t::FACE_RT;
	Face_t m_nB = Face_t::FACE_RT;
	float m_flDifference = 0.0f;
};
[[nodiscard]] std::vector<Seam_t> Seams( const CCube &cube ); // the 12 edges of the cube
[[nodiscard]] float SeamDifference( const CCube &cube, Face_t nFace ); // mean over the face's four edges

// A turn and/or mirror of a face's image (see texture::oriented) that makes its seams match better.
struct OrientationFix_t
{
	Face_t m_nFace = Face_t::FACE_UP;
	int m_nQuarterTurns = 0; // clockwise
	bool m_bMirror = false;
	float m_flBefore = 0.0f;
	float m_flAfter = 0.0f;
};
// Fixes for the up and down faces, which other tools often store turned. Only clear wins are
// suggested; faces with plain edges (most down faces) give none.
[[nodiscard]] std::vector<OrientationFix_t> SuggestOrientation( const CCube &cube );

// The centre of the brightest spot, which is the sun in most skies.
[[nodiscard]] std::optional<CVec3> BrightestSpot( const CCube &cube );

// Writing skies
struct ExportOptions_t
{
	std::string m_szName;                                        // "sky_mysky": materials/skybox/sky_myskyrt.vmt, ...
	texture::VtfFormat_t m_nFormat = texture::VtfFormat_t::BGR888;  // BGR888 (no compression) or DXT1
	int m_nSize = 0;                                            // face width and height; 0 keeps each face's own
	bool m_bMipmaps = false;
};
struct OutputFile_t
{
	std::string m_szPath; // relative to the game folder, e.g. materials/skybox/sky_myskyrt.vtf
	std::vector<std::byte> m_Data;
};
// Sky names become material and file names: letters, digits, '_', '-' and '.'.
[[nodiscard]] bool IsValidName( std::string_view szName );
// CLAMPS | CLAMPT so edges don't bleed, NOLOD so low texture detail settings don't blur the sky,
// and NOMIP when there are no mipmaps.
[[nodiscard]] std::uint32_t VtfFlags( bool bMipmaps );
// The VMT of one face of an LDR sky.
[[nodiscard]] std::string Material( std::string_view szName, Face_t nFace );
// The VTF and VMT of every face (12 files). Faces are encoded in parallel. Empty if a face is missing.
[[nodiscard]] std::vector<OutputFile_t> ExportSky( const CCube &cube, const ExportOptions_t &options );

// Cube maps (VTFs with the ENVMAP flag, such as a WindowImposter sky's $envmap) hold the six faces
// in the order rt lf bk ft up dn, each turned and mirrored from how a sky face is drawn. Sky.cpp
// says how this was established.
[[nodiscard]] bool IsCubeMap( const texture::CVtf &vtf );
// One face of a cube map as a sky face image (the largest mip), or nullopt with error set.
[[nodiscard]] std::optional<texture::CImage> CubeMapFace( const texture::CVtf &vtf, Face_t nFace, std::string *pError = nullptr );

// Reading skies from a game
// The sky names the game has materials for (every skybox/<name>rt.vmt), sorted.
[[nodiscard]] std::vector<std::string> SkyNames( const vfs::CFileSystem &fileSystem );
struct LoadedSky_t
{
	CCube m_Cube;
	std::array<std::string, 6> m_Textures; // the VTF each face came from
	std::vector<std::string> m_Problems;   // faces that could not be read, and why
};
// Uses each material's $basetexture (the LDR image), else its HDR texture. A face with no material
// falls back to a VTF of the same name.
[[nodiscard]] LoadedSky_t LoadSky( const vfs::CFileSystem &fileSystem, std::string_view szName );

// Reading skies from files on disk
// A sky saved as files side by side, <name><face>.<ext>: the way materials/skybox holds them, or
// images exported from another tool ("mysky_rt.png").
struct CSkyFiles
{
	std::string m_szName;                                // the sky's name, from the file names
	std::array<std::filesystem::path, 6> m_Faces;      // by indexOf(Face); empty where no file was found
	bool m_bInSkyboxFolder = false;                     // the files sit in a folder called skybox
	[[nodiscard]] int Count() const;
};
// The sky a file belongs to, if its name ends in a face suffix: the files for the other faces
// beside it. For each face the same kind of file is preferred (a .vmt finds .vmts), then materials,
// VTFs and images. Material and VTF names are kept exactly (they are the sky's name in game);
// image names lose a trailing separator ("mysky_rt.png" is the sky mysky).
[[nodiscard]] std::optional<CSkyFiles> FindSkyFiles( const std::filesystem::path &file );

// Loads each face: a .vmt through its $basetexture, looked up under the materials folder the .vmt
// is in, then in fileSystem (if given), then beside the .vmt; a .vtf or image directly.
[[nodiscard]] LoadedSky_t LoadSkyFiles( const CSkyFiles &files, const vfs::CFileSystem *pFileSystem = nullptr );

} // namespace xv::sky
