// SPDX-License-Identifier: GPL-3.0-or-later
#include "Sky.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <numbers>
#include <set>
#include <sstream>
#include <thread>

#include "kv1/Kv1.h"
#include "util/Numbers.h"
#include "vfs/Vfs.h"

namespace xv::sky {

namespace {

using texture::Image;

// Which way each face looks and how its image is turned. Established from the game's own skies,
// not from engine code:
// - Placement relative to each other: in all 63 skies CS:S ships (its own and HL2's), the pixels on
//   either side of the cube's edges agree (a mean difference of at most about 2 out of 255 between
//   the sides), and turning or mirroring any face makes its edges match clearly worse. Stock down
//   faces are plain at their edges, so their turn follows what all the others show: no face is
//   mirrored seen from inside, and up's bottom edge and dn's top edge both meet rt.
// - Absolute direction: the sun drawn in stock skies sits where the map's light_environment puts it
//   (de_aztec, de_cbble, de_piranesi, cs_havana and cs_italy within 6 degrees, de_dust2 and
//   de_chateau within 18), which puts rt at +X. A turned or mirrored cube would put those suns tens
//   of degrees off.
// The corpus tests repeat these checks (seams, up and down turns, suns) against the game files.
constexpr std::array<FaceBasis, 6> BASES{{
	{{1, 0, 0}, {0, -1, 0}, {0, 0, 1}},  // rt: east, yaw 0
	{{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},   // bk: north, yaw 90
	{{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  // lf: west, yaw 180
	{{0, -1, 0}, {-1, 0, 0}, {0, 0, 1}}, // ft: south, yaw 270
	{{0, 0, 1}, {0, -1, 0}, {-1, 0, 0}}, // up: its top edge toward lf
	{{0, 0, -1}, {0, -1, 0}, {1, 0, 0}}, // dn: its top edge toward rt
}};

constexpr std::array<std::string_view, 6> SUFFIXES{"rt", "bk", "lf", "ft", "up", "dn"};

constexpr float DEGREES = 180.0f / std::numbers::pi_v<float>;

std::string lower(std::string_view text) {
	std::string out{text};
	std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

std::array<float, 3> bilinear(const Image& image, float u, float v) {
	const float fx = std::clamp(u * static_cast<float>(image.width) - 0.5f, 0.0f, static_cast<float>(image.width - 1));
	const float fy = std::clamp(v * static_cast<float>(image.height) - 0.5f, 0.0f, static_cast<float>(image.height - 1));
	const int x0 = static_cast<int>(fx);
	const int y0 = static_cast<int>(fy);
	const int x1 = std::min(x0 + 1, image.width - 1);
	const int y1 = std::min(y0 + 1, image.height - 1);
	const float wx = fx - static_cast<float>(x0);
	const float wy = fy - static_cast<float>(y0);
	const std::uint8_t* p00 = image.pixel(x0, y0);
	const std::uint8_t* p10 = image.pixel(x1, y0);
	const std::uint8_t* p01 = image.pixel(x0, y1);
	const std::uint8_t* p11 = image.pixel(x1, y1);
	std::array<float, 3> out{};
	for (std::size_t c = 0; c < 3; c++) {
		const float top = static_cast<float>(p00[c]) + (static_cast<float>(p10[c]) - static_cast<float>(p00[c])) * wx;
		const float bottom = static_cast<float>(p01[c]) + (static_cast<float>(p11[c]) - static_cast<float>(p01[c])) * wx;
		out[c] = top + (bottom - top) * wy;
	}
	return out;
}

// Samples the cube with one face's image swapped for another (for trying out orientations).
std::array<float, 3> sampleWith(const Cube& cube, Vec3 dir, Face swapped, const Image* replacement) {
	const FacePoint point = project(dir);
	const Image& image = replacement && point.face == swapped ? *replacement : cube.face(point.face);
	if (image.empty()) {
		return {};
	}
	const UvTransform& t = cube.transforms[indexOf(point.face)];
	return bilinear(image, t.a * point.u + t.b * point.v + t.tx, t.c * point.u + t.d * point.v + t.ty);
}

float edgeDifference(const Cube& cube, Face a, Face b, Face swapped, const Image* replacement) {
	constexpr int SAMPLES = 128;
	constexpr float INSET = 0.004f; // about a pixel of a 512 face into each side
	const Vec3 na = basis(a).normal;
	const Vec3 nb = basis(b).normal;
	const Vec3 along = cross(na, nb);
	float total = 0.0f;
	for (int i = 0; i < SAMPLES; i++) {
		const float t = -1.0f + (2.0f * static_cast<float>(i) + 1.0f) / SAMPLES;
		const Vec3 point = na + nb + along * t;
		const auto ca = sampleWith(cube, point - nb * INSET, swapped, replacement);
		const auto cb = sampleWith(cube, point - na * INSET, swapped, replacement);
		total += (std::abs(ca[0] - cb[0]) + std::abs(ca[1] - cb[1]) + std::abs(ca[2] - cb[2])) / 3.0f;
	}
	return total / SAMPLES;
}

float faceDifference(const Cube& cube, Face face, const Image* replacement) {
	float total = 0.0f;
	int count = 0;
	for (Face other : FACES) {
		if (std::abs(dot(basis(face).normal, basis(other).normal)) < 0.5f && !cube.face(other).empty()) {
			total += edgeDifference(cube, face, other, face, replacement);
			count++;
		}
	}
	return count ? total / static_cast<float>(count) : 0.0f;
}

// Runs body(begin, end) over rows split between the machine's cores. (std::thread rather than
// jthread: libc++ only has jthread from version 18.)
template<typename Body>
void parallelRows(int rows, Body&& body) {
	const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 32);
	if (threads == 1 || rows < 64) {
		body(0, rows);
		return;
	}
	std::vector<std::thread> workers;
	const int chunk = (rows + threads - 1) / threads;
	for (int begin = 0; begin < rows; begin += chunk) {
		workers.emplace_back([&body, begin, end = std::min(rows, begin + chunk)] { body(begin, end); });
	}
	for (std::thread& worker : workers) {
		worker.join();
	}
}

using Reader = std::function<std::optional<std::vector<std::byte>>(const std::string& gamePath)>;

std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
	std::ifstream stream{path, std::ios::binary};
	if (!stream) {
		return std::nullopt;
	}
	std::vector<char> chars{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	std::vector<std::byte> bytes(chars.size());
	std::memcpy(bytes.data(), chars.data(), chars.size());
	return bytes;
}

std::optional<Image> decodeTexture(std::span<const std::byte> bytes, std::string& error) {
	auto vtf = texture::readVtf(bytes, &error);
	return vtf ? vtf->decode(0, 0, 0, &error) : std::nullopt;
}

// One face from its material: its texture transform, and the first of its textures that reads
// (the LDR one first; the HDR one when that's missing, as the game would show with HDR on).
// Returns what went wrong if none does.
std::optional<std::string> loadMaterialFace(LoadedSky& sky, Face face, const std::string& materialPath, std::string_view text, const Reader& read) {
	const kv1::Document document = kv1::parse(text).document;
	const auto root = std::ranges::find_if(document.roots, &kv1::Node::isBlock);
	if (root == document.roots.end()) {
		return materialPath + " has no shader block";
	}
	if (const kv1::Node* transform = root->find("$basetexturetransform"); transform && transform->isPair()) {
		sky.cube.transforms[indexOf(face)] = UvTransform::parse(transform->value());
	}
	std::optional<std::string> firstProblem;
	for (std::string_view key : {"$basetexture", "$hdrbasetexture", "$hdrcompressedtexture"}) {
		const kv1::Node* texture = root->find(key);
		if (!texture || !texture->isPair()) {
			continue;
		}
		const std::string vtfPath = vfs::normalizePath("materials/" + texture->value() + ".vtf");
		const auto bytes = read(vtfPath);
		std::string error = "missing";
		auto image = bytes ? decodeTexture(*bytes, error) : std::nullopt;
		if (!image) {
			firstProblem = firstProblem.value_or(vtfPath + ": " + error);
			continue;
		}
		sky.cube.face(face) = std::move(*image);
		sky.textures[indexOf(face)] = vtfPath;
		return std::nullopt;
	}
	return firstProblem.value_or(materialPath + " names no texture");
}

// The folder called materials that path is in, if any.
std::filesystem::path materialsFolder(const std::filesystem::path& path) {
	for (std::filesystem::path dir = path.parent_path(); !dir.empty() && dir != dir.parent_path(); dir = dir.parent_path()) {
		if (lower(vfs::toUtf8(dir.filename())) == "materials") {
			return dir;
		}
	}
	return {};
}

// A face as a plain square image, with its texture transform (if any) applied.
Image bakedFace(const Cube& cube, Face face) {
	const Image& image = cube.face(face);
	const UvTransform& t = cube.transforms[indexOf(face)];
	if (t.isIdentity()) {
		return image;
	}
	const int size = std::max(image.width, image.height);
	Image out{size, size};
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
			const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
			const auto colour = bilinear(image, t.a * u + t.b * v + t.tx, t.c * u + t.d * v + t.ty);
			std::uint8_t* p = out.pixel(x, y);
			for (std::size_t c = 0; c < 3; c++) {
				p[c] = static_cast<std::uint8_t>(std::lround(colour[c]));
			}
			p[3] = 255;
		}
	}
	return out;
}

} // namespace

std::string_view suffix(Face face) {
	return SUFFIXES[indexOf(face)];
}

std::optional<Face> faceFromFileName(std::string_view fileName) {
	std::string stem = lower(fileName.substr(0, std::min(fileName.size(), fileName.rfind('.'))));
	for (Face face : FACES) {
		if (stem.ends_with(suffix(face))) {
			return face;
		}
	}
	return std::nullopt;
}

float dot(Vec3 a, Vec3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b) {
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 normalized(Vec3 v) {
	const float length = std::sqrt(dot(v, v));
	return length > 0.0f ? v * (1.0f / length) : v;
}

const FaceBasis& basis(Face face) {
	return BASES[indexOf(face)];
}

Face neighbor(Face face, Edge edge) {
	const FaceBasis& b = basis(face);
	const Vec3 toward = edge == Edge::Top ? b.up : edge == Edge::Bottom ? -b.up : edge == Edge::Right ? b.right : -b.right;
	for (Face other : FACES) {
		if (basis(other).normal == toward) {
			return other;
		}
	}
	return face;
}

FacePoint project(Vec3 d) {
	const float ax = std::abs(d.x);
	const float ay = std::abs(d.y);
	const float az = std::abs(d.z);
	Face face = Face::Rt;
	float major = ax;
	if (ax >= ay && ax >= az) {
		face = d.x >= 0.0f ? Face::Rt : Face::Lf;
	} else if (ay >= az) {
		face = d.y >= 0.0f ? Face::Bk : Face::Ft;
		major = ay;
	} else {
		face = d.z >= 0.0f ? Face::Up : Face::Dn;
		major = az;
	}
	if (major <= 0.0f) {
		return {Face::Rt, 0.5f, 0.5f};
	}
	const FaceBasis& b = basis(face);
	const float s = dot(d, b.right) / major;
	const float t = dot(d, b.up) / major;
	return {face, (s + 1.0f) * 0.5f, (1.0f - t) * 0.5f};
}

Vec3 direction(Face face, float u, float v) {
	const FaceBasis& b = basis(face);
	return b.normal + b.right * (u * 2.0f - 1.0f) + b.up * (1.0f - v * 2.0f);
}

Vec3 forward(Angles angles) {
	const float pitch = angles.pitch / DEGREES;
	const float yaw = angles.yaw / DEGREES;
	return {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), -std::sin(pitch)};
}

Angles anglesOf(Vec3 d) {
	d = normalized(d);
	float yaw = std::atan2(d.y, d.x) * DEGREES;
	if (yaw < 0.0f) {
		yaw += 360.0f;
	}
	return {-std::asin(std::clamp(d.z, -1.0f, 1.0f)) * DEGREES, yaw};
}

bool UvTransform::isIdentity() const {
	return a == 1.0f && b == 0.0f && c == 0.0f && d == 1.0f && tx == 0.0f && ty == 0.0f;
}

UvTransform UvTransform::parse(std::string_view text) {
	float cx = 0.5f; // Source's default centre
	float cy = 0.5f;
	float sx = 1.0f;
	float sy = 1.0f;
	float rotate = 0.0f;
	float tx = 0.0f;
	float ty = 0.0f;
	std::istringstream in{std::string{text}};
	std::string word;
	const auto number = [&in](float& out) {
		std::string token;
		if (in >> token) {
			if (const auto value = parseNumber<float>(token)) {
				out = *value;
			}
		}
	};
	while (in >> word) {
		word = lower(word);
		if (word == "center") {
			number(cx);
			number(cy);
		} else if (word == "scale") {
			number(sx);
			number(sy);
		} else if (word == "rotate") {
			number(rotate);
		} else if (word == "translate") {
			number(tx);
			number(ty);
		}
	}
	// uv' = centre + R * S * (uv - centre) + translate
	const float r = rotate / DEGREES;
	const float cosR = std::cos(r);
	const float sinR = std::sin(r);
	UvTransform t;
	t.a = cosR * sx;
	t.b = -sinR * sy;
	t.c = sinR * sx;
	t.d = cosR * sy;
	t.tx = cx - (t.a * cx + t.b * cy) + tx;
	t.ty = cy - (t.c * cx + t.d * cy) + ty;
	return t;
}

std::array<float, 3> sample(const Cube& cube, Vec3 dir) {
	return sampleWith(cube, dir, Face::Rt, nullptr);
}

Vec3 View::ray(float x, float y) const {
	const float tanV = std::tan(fov * 0.5f / DEGREES) * 0.75f;
	const float tanH = tanV * static_cast<float>(width) / static_cast<float>(std::max(height, 1));
	const Vec3 f = forward(angles);
	const float yaw = angles.yaw / DEGREES;
	const Vec3 right{std::sin(yaw), -std::cos(yaw), 0.0f};
	const Vec3 up = cross(right, f);
	const float sx = (2.0f * x / static_cast<float>(std::max(width, 1)) - 1.0f) * tanH;
	const float sy = (1.0f - 2.0f * y / static_cast<float>(std::max(height, 1))) * tanV;
	return f + right * sx + up * sy;
}

std::optional<std::array<float, 2>> View::toScreen(Vec3 dir) const {
	const float tanV = std::tan(fov * 0.5f / DEGREES) * 0.75f;
	const float tanH = tanV * static_cast<float>(width) / static_cast<float>(std::max(height, 1));
	const Vec3 f = forward(angles);
	const float yaw = angles.yaw / DEGREES;
	const Vec3 right{std::sin(yaw), -std::cos(yaw), 0.0f};
	const Vec3 up = cross(right, f);
	const float depth = dot(dir, f);
	if (depth <= 1e-4f) {
		return std::nullopt;
	}
	const float sx = dot(dir, right) / depth / tanH;
	const float sy = dot(dir, up) / depth / tanV;
	return std::array<float, 2>{(sx + 1.0f) * 0.5f * static_cast<float>(width), (1.0f - sy) * 0.5f * static_cast<float>(height)};
}

void render(const Cube& cube, const View& view, Image& out, bool edges) {
	if (out.width != view.width || out.height != view.height) {
		out = Image{view.width, view.height};
	}
	parallelRows(view.height, [&](int begin, int end) {
		for (int y = begin; y < end; y++) {
			for (int x = 0; x < view.width; x++) {
				const Vec3 dir = view.ray(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
				auto colour = sample(cube, dir);
				if (edges) {
					// Within about a pixel of a face's border: draw the edge.
					const FacePoint here = project(dir);
					const FacePoint next = project(view.ray(static_cast<float>(x) + 1.5f, static_cast<float>(y) + 0.5f));
					const FacePoint below = project(view.ray(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 1.5f));
					if (here.face != next.face || here.face != below.face) {
						colour = {255.0f, 200.0f, 0.0f};
					}
				}
				std::uint8_t* p = out.pixel(x, y);
				for (std::size_t c = 0; c < 3; c++) {
					p[c] = static_cast<std::uint8_t>(std::clamp(colour[c] + 0.5f, 0.0f, 255.0f));
				}
				p[3] = 255;
			}
		}
	});
}

std::vector<Seam> seams(const Cube& cube) {
	std::vector<Seam> out;
	for (std::size_t i = 0; i < FACES.size(); i++) {
		for (std::size_t j = i + 1; j < FACES.size(); j++) {
			const Face a = FACES[i];
			const Face b = FACES[j];
			if (std::abs(dot(basis(a).normal, basis(b).normal)) < 0.5f && !cube.face(a).empty() && !cube.face(b).empty()) {
				out.push_back({a, b, edgeDifference(cube, a, b, a, nullptr)});
			}
		}
	}
	return out;
}

float seamDifference(const Cube& cube, Face face) {
	return cube.face(face).empty() ? 0.0f : faceDifference(cube, face, nullptr);
}

std::vector<OrientationFix> suggestOrientation(const Cube& cube) {
	std::vector<OrientationFix> fixes;
	for (Face face : {Face::Up, Face::Dn}) {
		const Image& image = cube.face(face);
		if (image.empty()) {
			continue;
		}
		const float before = faceDifference(cube, face, nullptr);
		OrientationFix best{face, 0, false, before, before};
		for (int turns = 0; turns < 4; turns++) {
			for (bool mirror : {false, true}) {
				if (turns == 0 && !mirror) {
					continue;
				}
				const Image candidate = texture::oriented(image, turns, mirror);
				const float after = faceDifference(cube, face, &candidate);
				if (after < best.after) {
					best = {face, turns, mirror, before, after};
				}
			}
		}
		// Only clear wins: noisy or plain edges make small differences meaningless.
		if (best.after < before * 0.6f && before - best.after > 3.0f) {
			fixes.push_back(best);
		}
	}
	return fixes;
}

std::optional<Vec3> brightestSpot(const Cube& cube) {
	const auto luminance = [](const std::uint8_t* p) {
		return 0.299f * static_cast<float>(p[0]) + 0.587f * static_cast<float>(p[1]) + 0.114f * static_cast<float>(p[2]);
	};
	std::optional<Face> brightest;
	float peak = -1.0f;
	for (Face face : FACES) {
		const Image& image = cube.face(face);
		for (int y = 0; y < image.height; y++) {
			for (int x = 0; x < image.width; x++) {
				if (const float l = luminance(image.pixel(x, y)); l > peak + 1.0f) {
					peak = l;
					brightest = face;
				}
			}
		}
	}
	if (!brightest || peak <= 0.0f) {
		return std::nullopt;
	}
	const Image& image = cube.face(*brightest);
	double sumX = 0.0;
	double sumY = 0.0;
	int count = 0;
	for (int y = 0; y < image.height; y++) {
		for (int x = 0; x < image.width; x++) {
			if (luminance(image.pixel(x, y)) >= peak * 0.98f) {
				sumX += x + 0.5;
				sumY += y + 0.5;
				count++;
			}
		}
	}
	// Texture coordinates back to face coordinates (undoing a $basetexturetransform).
	const UvTransform& t = cube.transforms[indexOf(*brightest)];
	const float tu = static_cast<float>(sumX / count / image.width) - t.tx;
	const float tv = static_cast<float>(sumY / count / image.height) - t.ty;
	const float det = t.a * t.d - t.b * t.c;
	const float u = std::abs(det) > 1e-6f ? (t.d * tu - t.b * tv) / det : tu;
	const float v = std::abs(det) > 1e-6f ? (t.a * tv - t.c * tu) / det : tv;
	return normalized(direction(*brightest, std::clamp(u, 0.0f, 1.0f), std::clamp(v, 0.0f, 1.0f)));
}

bool isValidName(std::string_view name) {
	return !name.empty() && name.size() <= 64 && std::ranges::all_of(name, [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
	});
}

std::uint32_t vtfFlags(bool mipmaps) {
	using namespace texture::vtf_flag;
	return CLAMPS | CLAMPT | NOLOD | (mipmaps ? 0 : NOMIP);
}

std::string material(std::string_view name, Face face) {
	return std::string{"\"UnlitGeneric\"\n{\n\t\"$basetexture\" \"skybox/"} + std::string{name} + std::string{suffix(face)}
		+ "\"\n\t\"$nofog\" \"1\"\n\t\"$ignorez\" \"1\"\n}\n";
}

std::vector<OutputFile> exportSky(const Cube& cube, const ExportOptions& options) {
	if (!isValidName(options.name) || std::ranges::any_of(cube.faces, &Image::empty) || !texture::canEncode(options.format)) {
		return {};
	}
	std::array<std::vector<std::byte>, 6> textures;
	std::vector<std::thread> workers;
	for (Face face : FACES) {
		workers.emplace_back([&cube, &options, &textures, face] {
			Image image = bakedFace(cube, face);
			if (options.size > 0) {
				image = texture::resize(image, options.size, options.size);
			}
			textures[indexOf(face)] = texture::writeVtf(image, {.format = options.format, .flags = vtfFlags(options.mipmaps), .mipmaps = options.mipmaps, .lowRes = true, .versionMinor = 2});
		});
	}
	for (std::thread& worker : workers) {
		worker.join();
	}
	std::vector<OutputFile> files;
	for (Face face : FACES) {
		const std::string base = "materials/skybox/" + options.name + std::string{suffix(face)};
		const std::string vmt = material(options.name, face);
		files.push_back({base + ".vtf", std::move(textures[indexOf(face)])});
		files.push_back({base + ".vmt", {reinterpret_cast<const std::byte*>(vmt.data()), reinterpret_cast<const std::byte*>(vmt.data() + vmt.size())}});
	}
	return files;
}

std::vector<std::string> skyNames(const vfs::FileSystem& fileSystem) {
	std::set<std::string> names;
	constexpr std::string_view PREFIX = "materials/skybox/";
	for (const std::string& path : fileSystem.find(PREFIX, ".vmt")) {
		const std::string_view file = std::string_view{path}.substr(PREFIX.size());
		if (file.ends_with("rt.vmt") && file.size() > 6) {
			names.emplace(file.substr(0, file.size() - 6));
		}
	}
	return {names.begin(), names.end()};
}

LoadedSky loadSky(const vfs::FileSystem& fileSystem, std::string_view name) {
	LoadedSky sky;
	const Reader read = [&fileSystem](const std::string& path) { return fileSystem.read(path); };
	for (Face face : FACES) {
		const std::string base = "materials/skybox/" + lower(name) + std::string{suffix(face)};
		std::string materialPath = base + ".vmt";
		auto text = fileSystem.readText(materialPath);
		if (!text) {
			// Maps may name a sky whose materials only exist as <name>_hdr<face>.
			const std::string hdr = "materials/skybox/" + lower(name) + "_hdr" + std::string{suffix(face)} + ".vmt";
			if ((text = fileSystem.readText(hdr))) {
				materialPath = hdr;
			}
		}
		if (!text) {
			// No material: a texture of the same name, as a sky whose VMTs aren't written yet has.
			std::string error = "missing";
			const auto bytes = fileSystem.read(base + ".vtf");
			if (auto image = bytes ? decodeTexture(*bytes, error) : std::nullopt) {
				sky.cube.face(face) = std::move(*image);
				sky.textures[indexOf(face)] = base + ".vtf";
			} else {
				sky.problems.push_back(materialPath + " is missing");
			}
			continue;
		}
		if (auto problem = loadMaterialFace(sky, face, materialPath, *text, read)) {
			sky.problems.push_back(std::move(*problem));
		}
	}
	return sky;
}

int SkyFiles::count() const {
	return static_cast<int>(std::ranges::count_if(faces, [](const std::filesystem::path& face) { return !face.empty(); }));
}

std::optional<SkyFiles> findSkyFiles(const std::filesystem::path& file) {
	const std::string extension = lower(vfs::toUtf8(file.extension()));
	const std::string kind = extension.empty() ? std::string{} : extension.substr(1);
	const bool gameFile = kind == "vmt" || kind == "vtf";
	if (!gameFile && !texture::isImageExtension(kind)) {
		return std::nullopt;
	}
	const std::string stem = vfs::toUtf8(file.stem());
	const auto face = faceFromFileName(stem);
	if (!face || stem.size() <= 2) {
		return std::nullopt;
	}
	const std::string base = lower(stem.substr(0, stem.size() - 2));

	// The folder's files by lowercase name, as games match names without case.
	std::map<std::string, std::filesystem::path> entries;
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(file.parent_path(), ec)) {
		if (entry.is_regular_file(ec)) {
			entries.emplace(lower(vfs::toUtf8(entry.path().filename())), entry.path());
		}
	}
	std::vector<std::string> kinds{kind, "vmt", "vtf", "png", "tga", "jpg", "jpeg", "bmp", "psd", "hdr"};
	SkyFiles files;
	for (Face f : FACES) {
		for (const std::string& k : kinds) {
			if (const auto it = entries.find(base + std::string{suffix(f)} + "." + k); it != entries.end()) {
				files.faces[indexOf(f)] = it->second;
				break;
			}
		}
	}
	files.inSkyboxFolder = lower(vfs::toUtf8(file.parent_path().filename())) == "skybox";
	files.name = base;
	if (!gameFile) {
		while (!files.name.empty() && (files.name.back() == '_' || files.name.back() == '-' || files.name.back() == '.' || files.name.back() == ' ')) {
			files.name.pop_back();
		}
	}
	return files;
}

LoadedSky loadSkyFiles(const SkyFiles& files, const vfs::FileSystem* fileSystem) {
	LoadedSky sky;
	for (Face face : FACES) {
		const std::filesystem::path& path = files.faces[indexOf(face)];
		if (path.empty()) {
			sky.problems.push_back("no file for " + std::string{suffix(face)});
			continue;
		}
		const std::string shown = vfs::toUtf8(path);
		const auto bytes = readFile(path);
		if (!bytes) {
			sky.problems.push_back(shown + " can't be read");
			continue;
		}
		const std::string extension = lower(vfs::toUtf8(path.extension()));
		std::string error;
		if (extension == ".vmt") {
			// Texture names are relative to a materials folder: the one this material is in,
			// else the game's.
			const std::filesystem::path materials = materialsFolder(path);
			const Reader read = [&materials, fileSystem](const std::string& gamePath) -> std::optional<std::vector<std::byte>> {
				if (!materials.empty()) {
					if (auto data = readFile(vfs::findCaseInsensitive(materials.parent_path() / vfs::fromUtf8(gamePath)))) {
						return data;
					}
				}
				return fileSystem ? fileSystem->read(gamePath) : std::nullopt;
			};
			const std::string text{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
			if (auto problem = loadMaterialFace(sky, face, shown, text, read)) {
				// Its texture can't be found: a VTF beside the material will do.
				std::filesystem::path beside = path;
				beside.replace_extension(".vtf");
				const auto data = readFile(vfs::findCaseInsensitive(beside));
				if (auto image = data ? decodeTexture(*data, error) : std::nullopt) {
					sky.cube.face(face) = std::move(*image);
					sky.textures[indexOf(face)] = vfs::toUtf8(beside);
				} else {
					sky.problems.push_back(std::move(*problem));
				}
			}
			continue;
		}
		auto image = extension == ".vtf" ? decodeTexture(*bytes, error) : texture::loadImage(*bytes, &error);
		if (!image) {
			sky.problems.push_back(shown + ": " + error);
			continue;
		}
		sky.cube.face(face) = std::move(*image);
		sky.textures[indexOf(face)] = shown;
	}
	return sky;
}

} // namespace xv::sky
