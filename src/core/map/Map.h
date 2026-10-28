//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A Hammer map (.vmf) as read: solids with their sides and displacements, entities with
//          their keys and outputs, and the editor's groups, visgroups, cameras and cordons.
//          Strings are views into the file's text, which the map keeps, and every block records
//          the bytes it came from, so the parts nobody changes can be written back as they were.
//          Edits mark what they change (and keep new strings in the map's own store), and
//          map::WriteVmf patches the text as read with them.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "map/Geometry.h"
#include "util/CopyPtr.h"
#include "util/SharedVector.h"
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
	bool m_bChanged = false; // written again when saved
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
	std::string_view m_szMaterial;     // as written, which is how it is written back
	TextureAxis_t m_UAxis;
	TextureAxis_t m_VAxis;
	double m_flRotation = 0.0;
	int m_nLightmapScale = 16;
	std::uint32_t m_nSmoothingGroups = 0;
	std::vector<Point_t> m_PlusVertices; // Hammer++'s exact polygon, when it wrote one
	CCopyPtr<DispInfo_t> m_pDisp;
	Span_t m_Span;
};

struct Solid_t
{
	int m_nId = 0;
	int m_nEntity = -1;
	int m_nReadEntity = -1;            // its owner when the map was read (or when it was added): saving moves it when they differ                // index into CMap::m_Entities; -1 for the world
	std::vector<Side_t> m_Sides;
	EditorInfo_t m_Editor;
	bool m_bInHidden = false;          // stored in a "hidden" block
	Span_t m_Span;                     // empty for one made since the map was read
	bool m_bChanged = false;           // its sides or editor notes were edited
	bool m_bDeleted = false;

	[[nodiscard]] bool IsNew() const { return m_Span.m_nLength == 0; }
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
	Span_t m_Span;                     // empty for a key added since the map was read
	bool m_bChanged = false;
	bool m_bDeleted = false;           // kept in place so the others keep their order

	[[nodiscard]] bool IsNew() const { return m_Span.m_nLength == 0; }
};

struct Entity_t
{
	int m_nId = 0;
	std::vector<EntityKey_t> m_Keys;   // in file order, "id" left out
	std::vector<Connection_t> m_Connections;
	std::vector<std::uint32_t> m_Solids; // indices into CMap::m_Solids
	EditorInfo_t m_Editor;
	bool m_bInHidden = false;
	Span_t m_Span;                     // empty for one made since the map was read
	Span_t m_ConnectionsSpan;          // its "connections" block, if it had one
	bool m_bConnectionsChanged = false;
	bool m_bDeleted = false;

	[[nodiscard]] bool IsNew() const { return m_Span.m_nLength == 0; }
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

// Copies are cheap and safe to hand to other threads: they share the text, the string store and
// every solid and entity until one of them changes (see CSharedVector).
class CMap
{
public:
	[[nodiscard]] std::string_view GetText() const { return m_pText ? std::string_view{ m_pText->data(), m_pText->size() } : std::string_view{}; }
	[[nodiscard]] std::string_view GetSpanText( Span_t span ) const { return GetText().substr( span.m_nOffset, span.m_nLength ); }
	// The entity that owns a solid, or the world.
	[[nodiscard]] const Entity_t &GetOwner( const Solid_t &solid ) const { return solid.m_nEntity < 0 ? m_World : m_Entities[ static_cast<std::size_t>( solid.m_nEntity ) ]; }
	[[nodiscard]] bool HasErrors() const;
	// Keeps a string for as long as the map lives (edited keys and materials point into it). The
	// store is every copy's, so a const copy can add to it (only the thread that edits does).
	[[nodiscard]] std::string_view Store( std::string szText ) const;
	// A material's index in m_Materials, added if it is new.
	[[nodiscard]] std::uint32_t FindMaterial( std::string_view szName );
	// The text's line ending ("\r\n" as Hammer writes, or "\n").
	[[nodiscard]] std::string_view GetNewline() const;

	std::shared_ptr<const std::vector<char>> m_pText; // as read; views into it stay valid
	VersionInfo_t m_VersionInfo;
	ViewSettings_t m_ViewSettings;
	std::vector<Visgroup_t> m_Visgroups;
	Entity_t m_World;
	CSharedVector<Entity_t> m_Entities;
	CSharedVector<Solid_t> m_Solids;   // the world's and the entities' alike
	std::vector<Group_t> m_Groups;
	std::vector<Camera_t> m_Cameras;
	int m_nActiveCamera = -1;
	std::vector<Cordon_t> m_Cordons;
	bool m_bCordonsActive = false;
	std::vector<Span_t> m_CordonSpans;    // the "cordon" and "cordons" blocks as read
	bool m_bCordonsChanged = false;       // the writer puts a fresh "cordons" block in their place
	std::vector<RawBlock_t> m_RawBlocks;
	std::vector<std::string> m_Materials; // each once, lower case with '/'
	std::vector<Span_t> m_TopBlocks;      // every top-level block, in file order
	std::vector<Diagnostic_t> m_Diagnostics;
	std::size_t m_nMoreDiagnostics = 0;   // problems past the cap, counted only
	// What Store keeps: a deque, so they never move, shared by every copy of the map and only ever
	// added to (by the thread that edits).
	std::shared_ptr<std::deque<std::string>> m_pStrings = std::make_shared<std::deque<std::string>>();
};

} // namespace xv::map
