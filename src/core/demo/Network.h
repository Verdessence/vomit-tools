//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: What is inside a demo's frames (network protocol 24: Counter-Strike: Source, Team
//          Fortress 2 and the other Source 2013 games): the server's messages, read one after
//          another, far enough to follow its entities. The network tables frame says what each
//          entity class sends; the string tables hold the players' names and each class's
//          starting values; the entity messages then change entities a few properties at a time,
//          each against the tick before, so a demo is followed from its start.
//
//          Only the properties asked for are kept (Watch_t); the rest are read past.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace xv::demo
{

class CBitReader;

// The network protocol whose messages this reads.
constexpr int NETWORK_PROTOCOL = 24;
// Entities are numbered below this.
constexpr int MAX_ENTITIES = 2048;

// What the server says about itself as a client joins.
struct ServerInfo_t
{
	int m_nProtocol = 0;
	bool m_bSourceTv = false;
	int m_nPlayerSlot = 0; // the recorder's entity, less one
	int m_nMaxClients = 0;
	float m_flTickInterval = 0.0f;
	std::string m_szGameDir;
	std::string m_szMap;
	std::string m_szSky;
	std::string m_szHost;
};

// The values are the file's.
enum class PropKind_t : std::uint8_t
{
	PROP_KIND_INT,
	PROP_KIND_FLOAT,
	PROP_KIND_VECTOR,
	PROP_KIND_VECTOR_XY,
	PROP_KIND_STRING,
	PROP_KIND_ARRAY,
	PROP_KIND_TABLE,
};

// One property of a class, in the order entity messages number them.
struct FlatProp_t
{
	std::string m_szTable; // the network table that declares it ("DT_BasePlayer")
	std::string m_szName;  // "m_vecOrigin"
	PropKind_t m_nKind = PropKind_t::PROP_KIND_INT;
	std::uint32_t m_nFlags = 0;
	int m_nBits = 0;
	float m_flLow = 0.0f;
	float m_flHigh = 0.0f;
	int m_nElements = 0;  // an array's most
	int m_nElement = -1;  // an array's element: where its description is in the class's m_Elements
};

struct ServerClass_t
{
	std::string m_szName;  // "CCSPlayer"
	std::string m_szTable; // "DT_CSPlayer"
};

struct StringEntry_t
{
	std::string m_szText;
	std::vector<std::byte> m_Data;
};

struct StringTable_t
{
	std::string m_szName;
	int m_nMaxEntries = 0;
	bool m_bFixedData = false;
	int m_nFixedBits = 0;
	std::vector<StringEntry_t> m_Entries;
	std::uint64_t m_nRevision = 0; // bumped whenever an entry changes
};

// A kept property: up to three numbers (a vector's; a whole number or a float in the first).
using PropValue_t = std::array<double, 3>;

struct Entity_t
{
	int m_nClass = -1; // -1: not there (never made, deleted, or out of the recorder's sight)
	int m_nSerial = 0;
	std::vector<PropValue_t> m_Values; // the kept properties, by slot; empty for a class not watched
};

class CNetworkState
{
public:
	// Asked once for each class when it first appears: where each of its properties' values
	// are kept, as slot * 4 + the first of the slot's three numbers the value fills (a vector
	// fills as many as it has), or -1 for a property not kept. An empty answer keeps nothing of
	// the class. Strings and arrays can't be kept.
	using Watch_t = std::function<std::vector<int>( const ServerClass_t &serverClass, std::span<const FlatProp_t> props )>;

	CNetworkState( Watch_t pfnWatch, int nSlots );

	// One frame's block each; false (with GetError saying why) when it couldn't be read, after
	// which nothing more should be.
	bool ReadDataTables( std::span<const std::byte> block );
	bool ReadStringTables( std::span<const std::byte> block );
	bool ReadPacket( std::span<const std::byte> block );

	[[nodiscard]] const std::string &GetError() const { return m_szError; }
	[[nodiscard]] const std::optional<ServerInfo_t> &GetServerInfo() const { return m_ServerInfo; }
	[[nodiscard]] const std::vector<ServerClass_t> &GetClasses() const { return m_Classes; }
	[[nodiscard]] std::span<const Entity_t> GetEntities() const { return m_Entities; }
	// Whether anything of a class is kept.
	[[nodiscard]] bool IsWatched( int nClass ) const;
	[[nodiscard]] const StringTable_t *FindTable( std::string_view szName ) const;
	// How many entity messages have been read: a snapshot of the entities is whole after each.
	[[nodiscard]] std::uint64_t GetSnapshotCount() const { return m_nSnapshots; }
	// A class's properties in message order (made on first use); null for a class not there.
	[[nodiscard]] const std::vector<FlatProp_t> *GetProps( int nClass );

private:
	struct SendProp_t
	{
		PropKind_t m_nKind = PropKind_t::PROP_KIND_INT;
		std::string m_szName;
		std::uint32_t m_nFlags = 0;
		std::string m_szTable; // the table it brings in, or the one an exclude names
		int m_nElements = 0;
		float m_flLow = 0.0f;
		float m_flHigh = 0.0f;
		int m_nBits = 0;
	};
	struct SendTable_t
	{
		std::string m_szName;
		std::vector<SendProp_t> m_Props;
	};
	struct Flat_t
	{
		std::vector<FlatProp_t> m_Props;
		std::vector<FlatProp_t> m_Elements; // the arrays' elements
		std::vector<int> m_Watch;           // per property, as Watch_t answers; empty: none kept
		bool m_bMade = false;
		// The class's starting values (its entry in the instancebaseline table), decoded when
		// first wanted and again when the entry changes.
		std::vector<PropValue_t> m_Baseline;
		std::uint64_t m_nBaselineRevision = ~std::uint64_t{ 0 };
	};
	struct Baseline_t
	{
		int m_nClass = -1;
		std::vector<PropValue_t> m_Values;
	};

	bool Fail( std::string szWhy );
	bool ReadMessage( int nType, CBitReader &reader );
	bool ReadSendTable( CBitReader &reader );
	bool ReadCreateStringTable( CBitReader &reader );
	bool ReadUpdateStringTable( CBitReader &reader );
	bool ReadTableEntries( StringTable_t &table, CBitReader &reader, int nEntries );
	bool ReadPacketEntities( CBitReader &reader );
	bool ReadProps( CBitReader &reader, const Flat_t &flat, std::vector<PropValue_t> *pValues );
	void ReadValue( CBitReader &reader, const FlatProp_t &prop, const Flat_t &flat, double *pOut, int nRoom );
	[[nodiscard]] float ReadFloatProp( CBitReader &reader, const FlatProp_t &prop );
	[[nodiscard]] Flat_t *Flatten( int nClass );
	[[nodiscard]] const std::vector<PropValue_t> *InstanceBaseline( int nClass, Flat_t &flat );
	[[nodiscard]] StringTable_t *FindTableToChange( std::string_view szName );

	Watch_t m_pfnWatch;
	int m_nSlots = 0;
	std::string m_szError;
	std::optional<ServerInfo_t> m_ServerInfo;
	std::vector<SendTable_t> m_Tables;
	std::unordered_map<std::string, std::size_t> m_TableByName;
	std::vector<ServerClass_t> m_Classes;
	std::vector<Flat_t> m_Flat; // by class
	int m_nClassBits = 0;
	std::vector<StringTable_t> m_StringTables;
	std::vector<Entity_t> m_Entities;
	// What an entity coming into sight starts from, when the server has said so: two sets, a
	// message naming the one it reads and writing the other.
	std::array<std::unordered_map<int, Baseline_t>, 2> m_Baselines;
	std::uint64_t m_nSnapshots = 0;
};

} // namespace xv::demo
