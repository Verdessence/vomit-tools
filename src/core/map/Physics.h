//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The physics tool's simulation (Hammer++'s Physics Simulation): the selected props and
//          brushes fall, tumble and settle against the rest of the map, on Box3D. The world's
//          brushes are its static hulls (but for those whose materials let physics through),
//          displacements its triangle meshes and the props not moving their collision models;
//          each moving prop is a body of its PHY's convex pieces, each moving brush (or brush
//          entity) a body of its solids. Inside it works in metres, z up; everything given and
//          taken is in Source's units.
//
//===========================================================================//
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "map/Edit.h"
#include "map/Geometry.h"
#include "map/Instance.h"
#include "map/Scene.h"
#include "model/Mdl.h"

namespace xv::map
{

struct PhysicsSettings_t
{
	Point_t m_vecGravity{ 0, 0, -600 }; // units/s², sv_gravity's 600 down
	double m_flAirDensity = 1.0;        // slows what moves through it (linear and angular damping)
	double m_flTimeScale = 1.0;         // simulated seconds a real one
	bool m_bSleepUntilTouched = false;  // the moving objects start asleep, waking when hit
	bool m_bAutoMass = true;            // a prop's mass from its PHY ($mass), else from its volume
	bool m_bPropsOnly = false;          // of the selection, only props move (brushes stay)

	friend bool operator==( const PhysicsSettings_t &, const PhysicsSettings_t & ) = default;
};

// A model's collision as the simulation takes it: its convex pieces in model space (the PHY's,
// placed by their bones' rest pose), and its mass from the PHY's text (0 when it says none).
struct PropCollision_t
{
	std::vector<std::vector<Point_t>> m_Pieces;
	double m_flMass = 0.0;
};
// A model's collision by its path ("models/props_junk/wood_crate001a.mdl"); null when it has none.
using PropCollisionLookup_t = std::function<std::shared_ptr<const PropCollision_t>( std::string_view szModel )>;

// Reads a model's collision: its .phy beside the .mdl, checked against the model's checksum.
[[nodiscard]] std::shared_ptr<const PropCollision_t> ReadPropCollision( std::string_view szModel, const model::Reader_t &pfnRead, std::string *pError = nullptr );
// The mass a PHY's text gives (its solids' "mass" summed, or "totalmass"); 0 when none.
[[nodiscard]] double PhysicsTextMass( std::string_view szText );

// Where a ray meets a moving object.
struct PhysicsHit_t
{
	std::size_t m_nBody = 0; // index into GetObjects()
	Point_t m_Point{};
	double m_flDistance = 0.0;
};

class CPhysicsScene
{
public:
	// Builds the simulation: moving are the objects that move (props and brushes, as the
	// selection names them); the rest of scene is what they hit.
	CPhysicsScene( const CScene &scene, const std::vector<std::uint32_t> &moving, const PropCollisionLookup_t &pfnCollision, const PhysicsSettings_t &settings );
	~CPhysicsScene();
	CPhysicsScene( const CPhysicsScene & ) = delete;
	CPhysicsScene &operator=( const CPhysicsScene & ) = delete;

	void SetSettings( const PhysicsSettings_t &settings );
	// Advances by real seconds (the time scale applies), in fixed steps.
	void Step( double flSeconds );
	// The objects that move (one body each), in the order bodies are numbered.
	[[nodiscard]] const std::vector<std::uint32_t> &GetObjects() const;
	// Each moving object's move since the start, as TransformObjects takes it.
	[[nodiscard]] std::vector<std::pair<std::uint32_t, Transform_t>> GetMoves() const;
	// The edit that leaves each moving object where it is now (texture lock on, as by hand).
	[[nodiscard]] MapEdit_t MakeEdit( const CScene &scene ) const;
	// Whether every moving body has come to rest.
	[[nodiscard]] bool IsSettled() const;
	// Selected objects that couldn't move (a prop without a model or collision), and why.
	[[nodiscard]] const std::vector<std::string> &GetProblems() const;

	// The nearest moving body along a ray (dir a unit vector), within flMax.
	[[nodiscard]] std::optional<PhysicsHit_t> CastRay( Point_t from, Point_t dir, double flMax ) const;
	// A bullet: an impulse along the ray at the first moving body it hits.
	bool Shoot( Point_t from, Point_t dir, double flMagnitude );
	// An explosion: every moving body within the radius pushed away from the point, less with
	// distance.
	void Explode( Point_t at, double flMagnitude );
	// Dragging: a body held at a point on it, pulled towards a target each step (a damped
	// spring as strong as the magnitude), until let go.
	void Grab( std::size_t nBody, Point_t at );
	void DragTo( Point_t target, double flMagnitude );
	void Release();
	[[nodiscard]] bool IsGrabbing() const;

	// The collision shapes as line segments in Source units (for drawing them): the moving
	// bodies' where they are now, or the static world's.
	[[nodiscard]] std::vector<std::pair<Point_t, Point_t>> GetEdges( bool bMoving ) const;

private:
	struct Impl_t;
	std::unique_ptr<Impl_t> m_pImpl;
};

} // namespace xv::map
