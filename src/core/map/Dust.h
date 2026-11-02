//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: func_dustmotes and func_dustcloud as the game plays them: motes of particle/sparkles
//          born at random inside the entity's brushes, drifting and fading in and out over their
//          lives, fading with distance; dust motes keep their size on screen, a dust cloud's
//          are as big as its keys say.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include "map/Geometry.h"
#include "map/Scene.h"
#include "map/Sprites.h"

namespace xv::map
{

// The material every mote is drawn with.
inline constexpr char DUST_MATERIAL[] = "particle/sparkles";

// What an entity's keys and brushes make of it. Missing keys take the FGD's defaults.
struct DustEmitter_t
{
	bool m_bMotes = true;                       // func_dustmotes: a constant size on screen
	std::array<float, 3> m_Color{ 1, 1, 1 };    // Color, 0-1
	float m_flAlpha = 1.0f;                     // Alpha, 0-1
	int m_nSpawnRate = 40;                      // motes a second
	float m_flSizeMin = 10.0f;
	float m_flSizeMax = 20.0f;
	float m_flSpeedMax = 13.0f;                 // on each axis, either way
	float m_flLifetimeMin = 3.0f;               // seconds
	float m_flLifetimeMax = 5.0f;
	float m_flDistMax = 1024.0f;                // hidden past this far from the eye, fading toward it
	float m_flFallSpeed = 0.0f;
	bool m_bFrozen = false;                     // its motes made at once, never moving or fading
	Box_t m_Bounds;
	std::vector<std::vector<Plane_t>> m_Brushes; // where motes may be born
};

// The emitter a func_dustmotes or func_dustcloud makes (started or not); nullopt for other
// entities and ones with no brushes.
[[nodiscard]] std::optional<DustEmitter_t> ReadDustEmitter( const CScene &scene, std::size_t nEntity );

class CDust
{
public:
	CDust( DustEmitter_t emitter, std::uint32_t nSeed );

	// Plays on by seconds, in steps of a tenth at most.
	void Advance( float flSeconds );
	[[nodiscard]] std::size_t GetCount() const { return m_Motes.size(); }
	[[nodiscard]] const DustEmitter_t &GetEmitter() const { return m_Emitter; }
	// The motes seen from an eye (right and up of unit length), far to near, into out.
	void Layout( Point_t eye, Point_t right, Point_t up, std::uint32_t nMaterial, std::array<int, 2> textureSize, std::uint32_t nObject, std::vector<ParticleSprite_t> &out ) const;

	// The most motes one entity keeps: a preview, not the game.
	static constexpr std::size_t MAX_MOTES = 2000;

private:
	struct Mote_t
	{
		Point_t m_Position{};
		Point_t m_Velocity{};
		float m_flAge = 0.0f;
		float m_flLife = 1.0f;
		float m_flSize = 1.0f;
	};

	void Spawn();
	[[nodiscard]] float Random( float flMin, float flMax );

	DustEmitter_t m_Emitter;
	std::mt19937 m_Random;
	std::vector<Mote_t> m_Motes;
	float m_flOwed = 0.0f; // motes due but not yet born
};

} // namespace xv::map
