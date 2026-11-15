//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: Source demos (.dem, "HL2DEMO") as data: the header (server, recorder, map, length)
//          and the list of frames after it, each with its tick, kind and size. Packets also
//          give the recorder's camera, and console frames the commands typed. What is inside
//          a packet (the network messages) isn't read.
//
//===========================================================================//
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "util/Vec3.h"

namespace xv::demo
{

// The demo protocol whose frames this reads (Source 2007 to 2013: Half-Life 2, Counter-Strike:
// Source, Team Fortress 2...). Other protocols give the header only.
constexpr int FRAME_PROTOCOL = 3;
constexpr std::size_t HEADER_SIZE = 1072;

// The values are the file's.
enum class FrameKind_t : std::uint8_t
{
	FRAME_KIND_SIGNON = 1,   // a packet from before the game began
	FRAME_KIND_PACKET,       // what the server sent in one go
	FRAME_KIND_SYNC_TICK,    // the clock starts here
	FRAME_KIND_CONSOLE,      // a console command
	FRAME_KIND_USER_COMMAND, // the recorder's input
	FRAME_KIND_DATA_TABLES,  // the entity classes' network tables
	FRAME_KIND_STOP,         // the end
	FRAME_KIND_STRING_TABLES,
};
constexpr int FRAME_KIND_COUNT = 8;

// "Packet", "Console command"...
[[nodiscard]] std::string_view FrameKindName( FrameKind_t nKind );

struct Header_t
{
	int m_nDemoProtocol = 0;
	int m_nNetworkProtocol = 0;
	std::string m_szServer;
	std::string m_szClient; // who recorded it
	std::string m_szMap;
	std::string m_szGameDir;
	float m_flSeconds = 0.0f;
	int m_nTicks = 0;
	int m_nFrames = 0;
	int m_nSignonLength = 0; // bytes of frames before the game began
};

struct Frame_t
{
	FrameKind_t m_nKind = FrameKind_t::FRAME_KIND_PACKET;
	int m_nTick = 0;
	std::uint64_t m_nOffset = 0; // of the frame's first byte in the file
	std::uint32_t m_nSize = 0;   // the whole frame's
};

// Where the recorder looked from at a packet.
struct View_t
{
	int m_nTick = 0;
	CVec3 m_vecOrigin;
	CVec3 m_vecAngles; // pitch, yaw, roll
};

struct Command_t
{
	int m_nTick = 0;
	std::string m_szText;
};

class CDemo
{
public:
	// nullopt (with the reason in pError) when the file isn't a demo.
	[[nodiscard]] static std::optional<CDemo> Load( const std::filesystem::path &file, std::string *pError = nullptr );
	[[nodiscard]] static std::optional<CDemo> Parse( std::span<const std::byte> bytes, std::string *pError = nullptr );
	// Whether bytes begin as a demo does.
	[[nodiscard]] static bool IsDemo( std::span<const std::byte> bytes );

	[[nodiscard]] const Header_t &GetHeader() const { return m_Header; }
	[[nodiscard]] const std::vector<Frame_t> &GetFrames() const { return m_Frames; }
	// The packets' cameras, in file order; empty when the demo has none (see HasCamera).
	[[nodiscard]] const std::vector<View_t> &GetViews() const { return m_Views; }
	[[nodiscard]] const std::vector<Command_t> &GetCommands() const { return m_Commands; }
	[[nodiscard]] std::uint64_t GetFileSize() const { return m_nFileSize; }

	// Why the frames stop short of the file's end (cut off while recording, or a protocol
	// this doesn't read); empty when they were all read.
	[[nodiscard]] const std::string &GetWarning() const { return m_szWarning; }
	// Recorded by the server's SourceTV, not by a player: it has no camera of its own.
	[[nodiscard]] bool IsSourceTv() const;
	// False when no packet says where its camera was (SourceTV's demos).
	[[nodiscard]] bool HasCamera() const { return m_bCamera; }
	// The last tick a frame has; the header's count when it is the larger (a demo cut off
	// while recording has 0 there).
	[[nodiscard]] int GetLastTick() const { return m_nLastTick; }
	// Seconds a tick lasts, from the header's time and ticks; 0 when it gives neither.
	[[nodiscard]] double GetTickInterval() const;
	[[nodiscard]] double SecondsAt( int nTick ) const { return nTick * GetTickInterval(); }
	// How many frames of a kind there are, and their bytes.
	[[nodiscard]] int CountOf( FrameKind_t nKind ) const;
	[[nodiscard]] std::uint64_t BytesOf( FrameKind_t nKind ) const;
	// The camera at a tick: the last packet's at or before it. Null before the first.
	[[nodiscard]] const View_t *ViewAt( int nTick ) const;
	// Bytes of packets in each of nBuckets equal spans of the ticks from 0 to the last: how
	// much the server sent over the demo's length.
	[[nodiscard]] std::vector<std::uint64_t> PacketBytesOverTime( int nBuckets ) const;

private:
	Header_t m_Header;
	std::vector<Frame_t> m_Frames;
	std::vector<View_t> m_Views;
	std::vector<Command_t> m_Commands;
	std::string m_szWarning;
	std::uint64_t m_nFileSize = 0;
	int m_nLastTick = 0;
	bool m_bCamera = false;
};

} // namespace xv::demo
