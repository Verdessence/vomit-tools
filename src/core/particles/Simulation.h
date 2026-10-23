//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: A preview simulation of particle systems: the common operators, worked out from what
//          their names and settings say, not the engine's code. It is close enough to see what an
//          effect does, not exact. Units are the game's: inches, seconds; angles are stored in
//          radians.
//
//===========================================================================//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dmx/Dmx.h"
#include "particles/Particles.h"
#include "util/Vec3.h"

namespace xv::particles
{

// One operator of a compiled system, with its settings looked up by name.
struct CCompiledOperator
{
	Category_t m_nCategory = Category_t::CATEGORY_OPERATORS;
	std::string m_szFunction;
	std::int32_t m_nElement = -1; // in the document it came from
	std::map<std::string, dmx::Value_t, std::less<>> m_Values;
	bool m_bSimulated = false;
	// The window of system time the operator works in (all zero: always).
	float m_flStartFadeIn = 0.0f;
	float m_flEndFadeIn = 0.0f;
	float m_flStartFadeOut = 0.0f;
	float m_flEndFadeOut = 0.0f;

	[[nodiscard]] float Number( std::string_view szName, float flFallback = 0.0f ) const; // float, int or bool
	[[nodiscard]] int Integer( std::string_view szName, int nFallback = 0 ) const;
	[[nodiscard]] bool Flag( std::string_view szName, bool bFallback = false ) const;
	[[nodiscard]] CVec3 Vector( std::string_view szName, CVec3 vecFallback = {} ) const;
	[[nodiscard]] std::array<float, 4> Color( std::string_view szName, std::array<float, 4> fallback = { 1, 1, 1, 1 } ) const; // 0-1
	// How strongly it applies at a system time: 0 outside its window.
	[[nodiscard]] float Weight( float flTime ) const;
};

struct CDefinition
{
	std::string m_szName;
	std::int32_t m_nElement = -1;
	int m_nMaxParticles = 1000;
	int m_nInitialParticles = 0;
	float m_flRadius = 5.0f;
	std::array<float, 4> m_Color{ 1, 1, 1, 1 };
	float m_flRotation = 0.0f;      // radians
	float m_flRotationSpeed = 0.0f; // radians per second
	int m_nSequence = 0;
	std::string m_szMaterial;
	CVec3 m_vecBoundsMin{ -10, -10, -10 };
	CVec3 m_vecBoundsMax{ 10, 10, 10 };
	int m_nBoundsControlPoint = 0; // the box is around this control point
	bool m_bSort = true;
	std::vector<CCompiledOperator> operators; // every category, each in its own order
	struct Child_t
	{
		std::size_t m_nDefinition = 0;
		float m_flDelay = 0.0f;
	};
	std::vector<Child_t> m_Children;

	[[nodiscard]] std::vector<const CCompiledOperator *> Of( Category_t nCategory ) const;
	// The control points its operators use (settings named "control point ..."), plus 0.
	[[nodiscard]] std::vector<int> GetControlPoints() const;
};

struct CLibrary
{
	std::vector<CDefinition> m_Definitions;

	[[nodiscard]] std::optional<std::size_t> Find( std::string_view szName ) const;
};
[[nodiscard]] CLibrary Compile( const dmx::Document_t &document );

// Whether the preview runs this function. Others are left out (and shown as such).
[[nodiscard]] bool IsSimulated( Category_t nCategory, std::string_view szFunction );

struct Particle_t
{
	CVec3 m_vecPosition;
	CVec3 m_vecVelocity;
	float m_flAge = 0.0f;
	float m_flLifetime = 1.0f;
	float m_flRadius = 5.0f;
	float m_flInitialRadius = 5.0f;
	std::array<float, 3> m_vecColor{ 1, 1, 1 };
	std::array<float, 3> m_vecInitialColor{ 1, 1, 1 };
	float m_flAlpha = 1.0f;
	float m_flInitialAlpha = 1.0f;
	float m_flAlpha2 = 1.0f;
	float m_flRotation = 0.0f;
	float m_flRotationSpeed = 0.0f;
	float m_flYaw = 0.0f;
	int m_nSequence = 0;
	int m_nSequence2 = 0;
	float m_flTrailLength = 0.1f;
	float m_flCreationTime = 0.0f;
	std::uint32_t m_nId = 0;
	bool m_bAlive = true;
};

class CSimulation
{
public:
	static constexpr int CONTROL_POINTS = 8;
	static constexpr float STEP = 1.0f / 60.0f;

	struct Instance_t
	{
		const CDefinition *m_pDefinition = nullptr;
		float m_flTime = 0.0f; // since it started; negative while a child waits for its delay
		std::vector<Particle_t> m_Particles;
		std::vector<float> m_Carry;     // per emitter: particles owed but not yet emitted
		std::vector<bool> m_Done;       // per emitter: finished emitting
		bool m_bInitialEmitted = false;
		std::vector<Instance_t> m_Children;
	};

	CSimulation( std::shared_ptr<const CLibrary> pLibrary, std::size_t nDefinition, std::uint32_t nSeed = 1 );

	// How many frames a sprite sheet sequence of a system's material has, for "lifetime from
	// sequence"; 0 when unknown.
	using SequenceFrames_t = std::function<int( std::size_t nDefinition, int nSequence )>;
	void SetSequenceFrames( SequenceFrames_t pfnFrames ) { m_pfnSequenceFrames = std::move( pfnFrames ); }

	void SetControlPoint( int nIndex, CVec3 vecPosition );
	[[nodiscard]] CVec3 ControlPoint( int nIndex ) const;
	// Advances by seconds, in steps of STEP (the remainder carries over to the next call).
	void Advance( float flSeconds );
	void Restart();

	[[nodiscard]] float GetTime() const { return m_flTime; }
	[[nodiscard]] const Instance_t &GetRoot() const { return m_Root; }
	[[nodiscard]] const CLibrary &GetLibrary() const { return *m_pLibrary; }
	[[nodiscard]] std::size_t GetParticleCount() const;
	// Nothing alive, and nothing left to emit.
	[[nodiscard]] bool GetFinished() const;

private:
	void Step( Instance_t &instance, float flDt );
	void Spawn( Instance_t &instance, int nCount );
	void Initialize( Instance_t &instance, Particle_t &particle, const CCompiledOperator &op, std::size_t nOpIndex );
	void Operate( Instance_t &instance, const CCompiledOperator &op, std::size_t nOpIndex, float flDt );
	[[nodiscard]] float Random(); // 0-1, from the simulation's sequence
	[[nodiscard]] Instance_t MakeInstance( std::size_t nDefinition, float flDelay ) const;

	std::shared_ptr<const CLibrary> m_pLibrary;
	std::size_t m_nDefinition;
	std::uint32_t m_nSeed;
	std::uint64_t m_nState;
	float m_flTime = 0.0f;
	float m_flPending = 0.0f;
	std::uint32_t m_nNextId = 0;
	SequenceFrames_t m_pfnSequenceFrames;
	std::array<CVec3, CONTROL_POINTS> m_ControlPoints{};
	std::array<CVec3, CONTROL_POINTS> m_UserControlPoints{};
	std::array<CVec3, CONTROL_POINTS> m_PreviousControlPoints{}; // a step ago, for what follows them
	Instance_t m_Root;
};

// A value from 0 to 1 that is fixed for one particle and one use (the operator and what for).
[[nodiscard]] float ParticleRandom( std::uint32_t nParticle, std::size_t nOp, std::uint32_t nSalt );

} // namespace xv::particles
