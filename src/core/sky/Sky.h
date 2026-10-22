// SPDX-License-Identifier: GPL-3.0-or-later
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

namespace xv::vfs {
class FileSystem;
}

// Source 2D skyboxes: six images on the inside of a cube around the player. A map names its sky
// (worldspawn "skyname", or sv_skyname), and face <f> of sky <name> is the material
// skybox/<name><f>, e.g. skybox/sky_dustrt.
namespace xv::sky {

enum class Face { Rt, Bk, Lf, Ft, Up, Dn };
inline constexpr std::array<Face, 6> FACES{Face::Rt, Face::Bk, Face::Lf, Face::Ft, Face::Up, Face::Dn};
[[nodiscard]] constexpr std::size_t indexOf(Face face) { return static_cast<std::size_t>(face); }

[[nodiscard]] std::string_view suffix(Face face); // "rt"
// The face a file is for, from the suffix just before its extension: "mysky_rt.tga", "skyUP.png".
[[nodiscard]] std::optional<Face> faceFromFileName(std::string_view fileName);

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	friend Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
	friend Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
	friend Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
	friend Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
	friend Vec3 operator*(float s, Vec3 a) { return a * s; }
	friend bool operator==(const Vec3&, const Vec3&) = default;
};
[[nodiscard]] float dot(Vec3 a, Vec3 b);
[[nodiscard]] Vec3 cross(Vec3 a, Vec3 b);
[[nodiscard]] Vec3 normalized(Vec3 v);

// Where a face sits in world coordinates (Hammer's: +X is east and yaw 0, +Y north and yaw 90, +Z
// up), and which way its image's right and top edges point, seen from inside the cube. Sky.cpp
// says how this table was established.
struct FaceBasis {
	Vec3 normal;
	Vec3 right;
	Vec3 up;
};
[[nodiscard]] const FaceBasis& basis(Face face);

enum class Edge { Top, Right, Bottom, Left };
// The face on the other side of an edge of this face's image.
[[nodiscard]] Face neighbor(Face face, Edge edge);

// Where a direction from the centre hits the cube: u and v run 0-1 from the image's top-left.
struct FacePoint {
	Face face = Face::Rt;
	float u = 0.0f;
	float v = 0.0f;
};
[[nodiscard]] FacePoint project(Vec3 direction);
[[nodiscard]] Vec3 direction(Face face, float u, float v);

// Source view angles in degrees, as setang takes them: positive pitch looks down, yaw 0 looks
// along +X and yaw 90 along +Y.
struct Angles {
	float pitch = 0.0f;
	float yaw = 0.0f;
};
[[nodiscard]] Vec3 forward(Angles angles);
[[nodiscard]] Angles anglesOf(Vec3 direction);

// A texture-coordinate transform, as $basetexturetransform: u' = a*u + b*v + tx, v' = c*u + d*v + ty.
struct UvTransform {
	float a = 1.0f;
	float b = 0.0f;
	float c = 0.0f;
	float d = 1.0f;
	float tx = 0.0f;
	float ty = 0.0f;

	[[nodiscard]] bool isIdentity() const;
	// "center 0 0 scale 1 2 rotate 0 translate 0 0"; parts left out keep their defaults.
	[[nodiscard]] static UvTransform parse(std::string_view text);
};

struct Cube {
	std::array<texture::Image, 6> faces;   // by indexOf(Face); a missing face reads as black
	std::array<UvTransform, 6> transforms; // HL2's half-height sides are stretched to fill

	[[nodiscard]] const texture::Image& face(Face f) const { return faces[indexOf(f)]; }
	[[nodiscard]] texture::Image& face(Face f) { return faces[indexOf(f)]; }
};

// The colour seen in a direction, 0-255 per channel: bilinear, clamped at the face edges.
[[nodiscard]] std::array<float, 3> sample(const Cube& cube, Vec3 direction);

struct View {
	Angles angles;
	float fov = 90.0f; // as Source's fov: horizontal degrees at 4:3; wider screens see more at the sides
	int width = 0;
	int height = 0;

	// The direction through a point of the image (pixel coordinates, 0,0 at the top-left corner).
	[[nodiscard]] Vec3 ray(float x, float y) const;
	// Where a direction appears in the image; nullopt when it is behind the viewer.
	[[nodiscard]] std::optional<std::array<float, 2>> toScreen(Vec3 direction) const;
};
// Renders what a player sees, into out (view.width x view.height). edges outlines the faces.
void render(const Cube& cube, const View& view, texture::Image& out, bool edges = false);

// How well the faces meet: the mean difference (0-255) between the pixels on either side of an edge.
struct Seam {
	Face a = Face::Rt;
	Face b = Face::Rt;
	float difference = 0.0f;
};
[[nodiscard]] std::vector<Seam> seams(const Cube& cube); // the 12 edges of the cube
[[nodiscard]] float seamDifference(const Cube& cube, Face face); // mean over the face's four edges

// A turn and/or mirror of a face's image (see texture::oriented) that makes its seams match better.
struct OrientationFix {
	Face face = Face::Up;
	int quarterTurns = 0; // clockwise
	bool mirror = false;
	float before = 0.0f;
	float after = 0.0f;
};
// Fixes for the up and down faces, which other tools often store turned. Only clear wins are
// suggested; faces with plain edges (most down faces) give none.
[[nodiscard]] std::vector<OrientationFix> suggestOrientation(const Cube& cube);

// The centre of the brightest spot, which is the sun in most skies.
[[nodiscard]] std::optional<Vec3> brightestSpot(const Cube& cube);

// Writing skies
struct ExportOptions {
	std::string name;                                        // "sky_mysky": materials/skybox/sky_myskyrt.vmt, ...
	texture::VtfFormat format = texture::VtfFormat::BGR888;  // BGR888 (no compression) or DXT1
	int size = 0;                                            // face width and height; 0 keeps each face's own
	bool mipmaps = false;
};
struct OutputFile {
	std::string path; // relative to the game folder, e.g. materials/skybox/sky_myskyrt.vtf
	std::vector<std::byte> data;
};
// Sky names become material and file names: letters, digits, '_', '-' and '.'.
[[nodiscard]] bool isValidName(std::string_view name);
// CLAMPS | CLAMPT so edges don't bleed, NOLOD so low texture detail settings don't blur the sky,
// and NOMIP when there are no mipmaps.
[[nodiscard]] std::uint32_t vtfFlags(bool mipmaps);
// The VMT of one face of an LDR sky.
[[nodiscard]] std::string material(std::string_view name, Face face);
// The VTF and VMT of every face (12 files). Faces are encoded in parallel. Empty if a face is missing.
[[nodiscard]] std::vector<OutputFile> exportSky(const Cube& cube, const ExportOptions& options);

// Reading skies from a game
// The sky names the game has materials for (every skybox/<name>rt.vmt), sorted.
[[nodiscard]] std::vector<std::string> skyNames(const vfs::FileSystem& fileSystem);
struct LoadedSky {
	Cube cube;
	std::array<std::string, 6> textures; // the VTF each face came from
	std::vector<std::string> problems;   // faces that could not be read, and why
};
// Uses each material's $basetexture (the LDR image), else its HDR texture. A face with no material
// falls back to a VTF of the same name.
[[nodiscard]] LoadedSky loadSky(const vfs::FileSystem& fileSystem, std::string_view name);

// Reading skies from files on disk
// A sky saved as files side by side, <name><face>.<ext>: the way materials/skybox holds them, or
// images exported from another tool ("mysky_rt.png").
struct SkyFiles {
	std::string name;                                // the sky's name, from the file names
	std::array<std::filesystem::path, 6> faces;      // by indexOf(Face); empty where no file was found
	bool inSkyboxFolder = false;                     // the files sit in a folder called skybox
	[[nodiscard]] int count() const;
};
// The sky a file belongs to, if its name ends in a face suffix: the files for the other faces
// beside it. For each face the same kind of file is preferred (a .vmt finds .vmts), then materials,
// VTFs and images. Material and VTF names are kept exactly (they are the sky's name in game);
// image names lose a trailing separator ("mysky_rt.png" is the sky mysky).
[[nodiscard]] std::optional<SkyFiles> findSkyFiles(const std::filesystem::path& file);

// Loads each face: a .vmt through its $basetexture, looked up under the materials folder the .vmt
// is in, then in fileSystem (if given), then beside the .vmt; a .vtf or image directly.
[[nodiscard]] LoadedSky loadSkyFiles(const SkyFiles& files, const vfs::FileSystem* fileSystem = nullptr);

} // namespace xv::sky
