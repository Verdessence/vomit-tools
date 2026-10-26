//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A Hammer map (.vmf) as read: solids with their sides and displacements, entities with
//          their keys and outputs, and the editor's groups, visgroups, cameras and cordons.
//          Strings are views into the file's text, which the map keeps, and every block records
//          the bytes it came from, so the parts nobody changes can be written back as they were.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "map/Geometry.h"
#include "util/Vec3.h"

namespace xv::map
{

// Bytes of the map's text: a block from its name to its closing brace, or a key and its value.
struct Span_t
{
	std::uint32_t m_nOffset = 0;
	std::uint32_t m_nLength = 0;
};

struct Color_t
{
	std::uint8_t m_nR = 255;
	std::uint8_t m_nG = 255;
	std::uint8_t m_nB = 255;

	friend bool operator==( const Color_t &, const Color_t & ) = default;
};

// Hammer's own notes on an object (the "editor" block).
struct EditorInfo_t
{
	Color_t m_Color;
	bool m_bHasColor = false;
	std::vector<int> m_Visgroups; // visgroup ids; an object can be in several
	int m_nGroup = 0;             // the group id it belongs to; 0 for none
	bool m_bVisgroupShown = true;
	bool m_bAutoShown = true;
	std::string_view m_szComments;
	Span_t m_Span;
};

// A displacement on a four-sided face. Grids are (2^power + 1) points a side, row by row from
// the corner nearest m_Start.
struct DispInfo_t
{
	int m_nPower = 0;
	Point_t m_Start{};
	double m_flElevation = 0.0;
	bool m_bSubdivided = false;
	int m_nFlags = 0;
	std::vector<CVec3> m_Normals;
	std::vector<float> m_Distances;
	std::vector<CVec3> m_Offsets;
	std::vector<CVec3> m_OffsetNormals;
	std::vector<float> m_Alphas;       // 0-255: how much of the second texture shows
	std::vector<int> m_TriangleTags;   // two per grid square
	std::vector<int> m_AllowedVerts;
	bool m_bValid = false;             // the power and every row read cleanly
	Span_t m_Span;

	[[nodiscard]] int GetSize() const { return ( 1 << m_nPower ) + 1; }
};

struct Side_t
{
	int m_nId = 0;
	std::array<Point_t, 3> m_Plane{};
	bool m_bPlaneRead = false;         // the plane's text held three points
	std::uint32_t m_nMaterial = 0;     // index into CMap::m_Materials
	TextureAxis_t m_UAxis;
	TextureAxis_t m_VAxis;
	double m_flRotation = 0.0;
	int m_nLightmapScale = 16;
	std::uint32_t m_nSmoothingGroups = 0;
	std::vector<Point_t> m_PlusVertices; // Hammer++'s exact polygon, when it wrote one
	std::unique_ptr<DispInfo_t> m_pDisp;
	Span_t m_Span;
};

struct Solid_t
{
	int m_nId = 0;
	int m_nEntity = -1;                // index into CMap::m_Entities; -1 for the world
	std::vector<Side_t> m_Sides;
	EditorInfo_t m_Editor;
	bool m_bInHidden = false;          // stored in a "hidden" block
	Span_t m_Span;
};

// One output: when m_szOutput fires, send m_szInput to m_szTarget.
struct Connection_t
{
	std::string_view m_szOutput;
	std::string_view m_szTarget;
	std::string_view m_szInput;
	std::string_view m_szParameter;
	float m_flDelay = 0.0f;
	int m_nTimes = -1;                 // -1: every time
	char m_chSeparator = ',';          // ',' before Left 4 Dead 2's tools, then ESC (0x1B)
	bool m_bMalformed = false;
	Span_t m_Span;
};

struct EntityKey_t
{
	std::string_view m_szKey;
	std::string_view m_szValue;
	Span_t m_Span;
};

struct Entity_t
{
	int m_nId = 0;
	std::vector<EntityKey_t> m_Keys;   // in file order, "id" left out
	std::vector<Connection_t> m_Connections;
	std::vector<std::uint32_t> m_Solids; // indices into CMap::m_Solids
	EditorInfo_t m_Editor;
	bool m_bInHidden = false;
	Span_t m_Span;

	// A key's value, ignoring case; the last one wins when a key is repeated, as in Hammer.
	[[nodiscard]] std::string_view GetValue( std::string_view szKey ) const;
	[[nodiscard]] bool HasKey( std::string_view szKey ) const;
	[[nodiscard]] std::string_view GetClassname() const { return GetValue( "classname" ); }
};

struct Visgroup_t
{
	std::string_view m_szName;
	int m_nId = 0;
	Color_t m_Color;
	int m_nParent = -1;                // index into CMap::m_Visgroups
	Span_t m_Span;
};

struct Group_t
{
	int m_nId = 0;
	EditorInfo_t m_Editor;             // m_nGroup is the parent group
	Span_t m_Span;
};

struct Camera_t
{
	Point_t m_Position{};
	Point_t m_Look{};
};

struct Cordon_t
{
	std::string_view m_szName;
	bool m_bActive = false;
	std::vector<Box_t> m_Boxes;
};

struct VersionInfo_t
{
	int m_nEditorVersion = 0;
	int m_nEditorBuild = 0;
	int m_nMapVersion = 0;
	int m_nFormatVersion = 0;
	bool m_bPrefab = false;
};

struct ViewSettings_t
{
	bool m_bSnapToGrid = true;
	bool m_bShowGrid = true;
	bool m_bShowLogicalGrid = false;
	int m_nGridSpacing = 64;
	bool m_bShow3DGrid = false;
};

// A block the reader keeps but doesn't look into (Hammer++'s "palette_plus", "quickhide"...).
struct RawBlock_t
{
	std::string_view m_szName;
	Span_t m_Span;
};

struct Diagnostic_t
{
	enum class Severity_t
	{
		SEVERITY_WARNING,
		SEVERITY_ERROR
	};

	Severity_t m_nSeverity = Severity_t::SEVERITY_ERROR;
	std::size_t m_nOffset = 0;
	std::size_t m_nLine = 0;   // 1-based
	std::size_t m_nColumn = 0; // 1-based, in bytes
	int m_nObjectId = 0;       // the solid, side or entity it is about; 0 for none
	std::string m_szMessage;
};

class CMap
{
public:
	CMap() = default;
	CMap( const CMap & ) = delete;
	CMap &operator=( const CMap & ) = delete;
	// Moving keeps the text's buffer, so the views into it stay valid.
	CMap( CMap && ) = default;
	CMap &operator=( CMap && ) = default;

	[[nodiscard]] std::string_view GetText() const { return { m_Text.data(), m_Text.size() }; }
	[[nodiscard]] std::string_view GetSpanText( Span_t span ) const { return GetText().substr( span.m_nOffset, span.m_nLength ); }
	// The entity that owns a solid, or the world.
	[[nodiscard]] const Entity_t &GetOwner( const Solid_t &solid ) const { return solid.m_nEntity < 0 ? m_World : m_Entities[ static_cast<std::size_t>( solid.m_nEntity ) ]; }
	[[nodiscard]] bool HasErrors() const;

	std::vector<char> m_Text;
	VersionInfo_t m_VersionInfo;
	ViewSettings_t m_ViewSettings;
	std::vector<Visgroup_t> m_Visgroups;
	Entity_t m_World;
	std::vector<Entity_t> m_Entities;
	std::vector<Solid_t> m_Solids;     // the world's and the entities' alike
	std::vector<Group_t> m_Groups;
	std::vector<Camera_t> m_Cameras;
	int m_nActiveCamera = -1;
	std::vector<Cordon_t> m_Cordons;
	bool m_bCordonsActive = false;
	std::vector<RawBlock_t> m_RawBlocks;
	std::vector<std::string> m_Materials; // each once, lower case with '/'
	std::vector<Span_t> m_TopBlocks;      // every top-level block, in file order
	std::vector<Diagnostic_t> m_Diagnostics;
	std::size_t m_nMoreDiagnostics = 0;   // problems past the cap, counted only
};

} // namespace xv::map
