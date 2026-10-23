//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Names.h.
//
//===========================================================================//
#include "Names.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

#include "particles/Simulation.h"

namespace xv::particles
{

namespace
{

struct Entry_t
{
	std::string_view m_szName;
	std::string_view m_szMeaning;
};

//-----------------------------------------------------------------------------
// Purpose: ASCII lower case, as the game compares names.
//-----------------------------------------------------------------------------
std::string Lower( std::string_view szText )
{
	std::string szOut{ szText };
	std::ranges::transform( szOut, szOut.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: Functions by category and current name (lower case).
//-----------------------------------------------------------------------------
const std::map<std::pair<Category_t, std::string>, Entry_t> &Functions()
{
	using enum Category_t;
	static const std::map<std::pair<Category_t, std::string>, Entry_t> functions = [] {
		const std::initializer_list<std::tuple<Category_t, std::string_view, Entry_t>> entries{
			// Emitters
			{ CATEGORY_EMITTERS, "emit_continuously", { "Stream", "Makes particles steadily, so many a second, for a while or for good." } },
			{ CATEGORY_EMITTERS, "emit_instantaneously", { "Burst", "Makes a number of particles all at once." } },
			{ CATEGORY_EMITTERS, "emit noise", { "Wavering stream", "Makes particles at a rate that drifts between a low and a high over time." } },
			// Initializers
			{ CATEGORY_INITIALIZERS, "Position Within Sphere Random", { "Start in a sphere", "Puts each new particle somewhere in a ball around a control point, and can send it outward." } },
			{ CATEGORY_INITIALIZERS, "Position Within Box Random", { "Start in a box", "Puts each new particle somewhere in a box around a control point." } },
			{ CATEGORY_INITIALIZERS, "Position Modify Offset Random", { "Shift start place", "Moves where a particle starts by a random amount, after the others placed it." } },
			{ CATEGORY_INITIALIZERS, "Position Modify Warp Random", { "Stretch start places", "Stretches or squashes where particles start (and their speed) around a control point." } },
			{ CATEGORY_INITIALIZERS, "Position Along Path Sequential", { "Start along a path, in turn", "Puts particles one after another along the line between two control points." } },
			{ CATEGORY_INITIALIZERS, "Position Along Path Random", { "Start along a path", "Puts each particle somewhere on the line between two control points." } },
			{ CATEGORY_INITIALIZERS, "Position In CP Hierarchy", { "Start along a chain", "Puts particles along the control points from the start one to the end one, growing over time." } },
			{ CATEGORY_INITIALIZERS, "Position From Parent Particles", { "Start at the parent's particles", "Starts each particle where a particle of the system that spawned this one is." } },
			{ CATEGORY_INITIALIZERS, "Position from Parent Cache", { "Start near the parent's particles", "Starts each particle near a particle of the parent system." } },
			{ CATEGORY_INITIALIZERS, "Position on Model Random", { "Start on a model", "Starts particles inside the hitboxes of the model on a control point (a player, a prop)." } },
			{ CATEGORY_INITIALIZERS, "Position Along Ring", { "Start on a ring", "Starts particles on a circle around a control point." } },
			{ CATEGORY_INITIALIZERS, "Move Particles Between 2 Control Points", { "Fly to a control point", "Sends each particle toward another control point, living just long enough to get there." } },
			{ CATEGORY_INITIALIZERS, "Random position within a curved cylinder", { "Start in a curved tube", "Starts particles in a tube along control points the game sets up (TF2's flamethrower)." } },
			{ CATEGORY_INITIALIZERS, "Lifetime Random", { "Lifetime", "How long each particle lives, picked between two values." } },
			{ CATEGORY_INITIALIZERS, "Lifetime Pre-Age Noise", { "Start partly aged", "Makes particles start as if some of their life had already passed." } },
			{ CATEGORY_INITIALIZERS, "lifetime from sequence", { "Lifetime from animation", "Lives exactly as long as its sprite sheet animation takes at the given frame rate." } },
			{ CATEGORY_INITIALIZERS, "Lifetime from Time to Impact", { "Live until it hits", "Lives until it would hit the world along its way (rain that stops at the ground)." } },
			{ CATEGORY_INITIALIZERS, "Lifetime From Control Point Life Time", { "Lifetime from control points", "Takes its life from control points the game sets up (TF2's flamethrower)." } },
			{ CATEGORY_INITIALIZERS, "Radius Random", { "Size", "How big each particle is, picked between two values." } },
			{ CATEGORY_INITIALIZERS, "Color Random", { "Colour", "Each particle's colour, picked between two colours." } },
			{ CATEGORY_INITIALIZERS, "Color Lit Per Particle", { "Colour, lit by the world", "A colour between two, then lit by the light where the particle starts." } },
			{ CATEGORY_INITIALIZERS, "Alpha Random", { "Opacity", "How see-through each particle is, picked between two values (0 clear, 255 solid)." } },
			{ CATEGORY_INITIALIZERS, "Rotation Random", { "Starting roll", "How far each particle is turned on screen when it starts." } },
			{ CATEGORY_INITIALIZERS, "Rotation Speed Random", { "Spin speed", "How fast each particle turns; Turn by spin speed makes it turn." } },
			{ CATEGORY_INITIALIZERS, "Rotation Yaw Random", { "Starting yaw", "How far each particle is turned around the up axis when it starts." } },
			{ CATEGORY_INITIALIZERS, "Rotation Yaw Flip Random", { "Random flip", "Turns some particles half way around, so they don't all face one way." } },
			{ CATEGORY_INITIALIZERS, "Sequence Random", { "Sprite sequence", "Which of the sprite sheet's sequences (or frames) each particle shows." } },
			{ CATEGORY_INITIALIZERS, "Sequence Two Random", { "Second sprite sequence", "The sequence of the second texture, for materials that blend two." } },
			{ CATEGORY_INITIALIZERS, "Trail Length Random", { "Trail length", "How long each particle's trail is, for the trail renderer." } },
			{ CATEGORY_INITIALIZERS, "Velocity Noise", { "Noisy starting speed", "A starting speed that varies smoothly with time and place, not at random." } },
			{ CATEGORY_INITIALIZERS, "Velocity Random", { "Starting speed", "A random starting speed: so fast in a random direction, plus a range on each axis." } },
			{ CATEGORY_INITIALIZERS, "Velocity Inherit from Control Point", { "Speed from a control point", "Gives particles some of a moving control point's speed." } },
			{ CATEGORY_INITIALIZERS, "Inherit Velocity", { "Inherit movement", "Gives particles some of the speed of what the effect is on." } },
			{ CATEGORY_INITIALIZERS, "Velocity Repulse from World", { "Push away from walls", "Pushes new particles away from nearby walls and floors." } },
			{ CATEGORY_INITIALIZERS, "Remap Noise to Scalar", { "Setting from noise", "Sets a setting from noise that varies smoothly with time and place." } },
			{ CATEGORY_INITIALIZERS, "remap initial scalar", { "Setting from another (at start)", "When a particle starts, sets one setting from another (size from creation time, say)." } },
			{ CATEGORY_INITIALIZERS, "Remap Control Point to Scalar", { "Setting from a control point", "Sets a setting from where a control point is along one axis." } },
			{ CATEGORY_INITIALIZERS, "Remap Initial Distance to Control Point to Scalar", { "Setting from distance (at start)", "Sets a setting from how far a new particle is from a control point." } },
			{ CATEGORY_INITIALIZERS, "remap scalar to vector", { "Position or colour from a setting", "Sets a position or colour from one of the particle's settings." } },
			{ CATEGORY_INITIALIZERS, "Remap Particle Count to Scalar", { "Setting from particle count", "Sets a setting from how many particles the system has made so far." } },
			{ CATEGORY_INITIALIZERS, "Scalar Random", { "Random value", "Sets any one setting to a random value between two." } },
			{ CATEGORY_INITIALIZERS, "Vector Random", { "Random position or colour", "Sets a position or colour to a random one between two." } },
			{ CATEGORY_INITIALIZERS, "random_number", { "Random number", "Gives each particle a random number other functions can read." } },
			{ CATEGORY_INITIALIZERS, "Inherit Initial Value From Parent Particle", { "Copy from a parent particle (at start)", "Starts a setting at a particle of the parent system's value." } },
			{ CATEGORY_INITIALIZERS, "Cull relative to model", { "Remove inside a model", "Removes new particles inside (or outside) the model on a control point." } },
			{ CATEGORY_INITIALIZERS, "Cull relative to Ray Trace Environment", { "Remove where blocked", "Removes particles the map's blockers cover (rain under a roof)." } },
			{ CATEGORY_INITIALIZERS, "Assign target CP", { "Pick a control point to follow", "Gives each particle one of a chain of control points the game sets up (TF2's flamethrower)." } },
			{ CATEGORY_INITIALIZERS, "Movement Lock to Control Point", { "Follow a control point (misplaced)", "An operator, put among the initializers, where the game doesn't run it." } },
			// Operators
			{ CATEGORY_OPERATORS, "Lifespan Decay", { "Die at end of life", "Removes particles when their lifetime is up; without it they never go." } },
			{ CATEGORY_OPERATORS, "Movement Basic", { "Move", "Moves particles by their speed, with gravity and drag; without it they stay put." } },
			{ CATEGORY_OPERATORS, "Alpha Fade In Random", { "Fade in", "Fades particles in from clear over the start of their life." } },
			{ CATEGORY_OPERATORS, "Alpha Fade Out Random", { "Fade out", "Fades particles out to clear over the end of their life." } },
			{ CATEGORY_OPERATORS, "Alpha Fade and Decay", { "Fade in and out, then die", "Fades particles in and out over their life, and removes them at the end." } },
			{ CATEGORY_OPERATORS, "Alpha Fade In Simple", { "Fade in (simple)", "Fades particles in from clear." } },
			{ CATEGORY_OPERATORS, "Alpha Fade Out Simple", { "Fade out (simple)", "Fades particles out to clear." } },
			{ CATEGORY_OPERATORS, "Color Fade", { "Colour change", "Changes particles' colour to another over part of their life." } },
			{ CATEGORY_OPERATORS, "Radius Scale", { "Grow or shrink", "Scales particles' size from one amount to another over part of their life." } },
			{ CATEGORY_OPERATORS, "Rotation Spin Roll", { "Spin", "Turns particles on screen at a rate that can slow down." } },
			{ CATEGORY_OPERATORS, "Rotation Spin Yaw", { "Spin around up", "Turns particles around the up axis at a rate that can slow down." } },
			{ CATEGORY_OPERATORS, "Rotation Basic", { "Turn by spin speed", "Turns each particle by its own spin speed (from Spin speed)." } },
			{ CATEGORY_OPERATORS, "Oscillate Scalar", { "Wobble a setting", "Makes a setting rise and fall in waves: pulsing size, flickering opacity." } },
			{ CATEGORY_OPERATORS, "Oscillate Vector", { "Wobble position or colour", "Makes a position or colour swing back and forth in waves." } },
			{ CATEGORY_OPERATORS, "oscillating_movement", { "Sway", "Gives particles a swaying drift along each axis." } },
			{ CATEGORY_OPERATORS, "Movement Lock to Control Point", { "Follow a control point", "Carries particles along when a control point moves, letting go over time." } },
			{ CATEGORY_OPERATORS, "Movement Lock to Bone", { "Stick to the model", "Keeps particles started on a model with it as it moves." } },
			{ CATEGORY_OPERATORS, "Movement Dampen Relative to Control Point", { "Slow near a control point", "Slows particles down as they get close to a control point." } },
			{ CATEGORY_OPERATORS, "Movement Rotate Particle Around Axis", { "Orbit", "Turns particles around an axis through a control point." } },
			{ CATEGORY_OPERATORS, "Movement Max Velocity", { "Speed limit", "Keeps particles from going faster than a speed." } },
			{ CATEGORY_OPERATORS, "Movement Maintain Position Along Path", { "Hold a path", "Keeps particles at their places along the line between two control points." } },
			{ CATEGORY_OPERATORS, "Movement Lock to Saved Position Along Path", { "Follow the path", "Moves particles with the path they were started on as its control points move." } },
			{ CATEGORY_OPERATORS, "Movement Follow CP", { "Chase control points", "Moves particles toward control points the game sets up (TF2's flamethrower)." } },
			{ CATEGORY_OPERATORS, "Cull Random", { "Remove some at random", "Removes a share of the particles at random times in their life." } },
			{ CATEGORY_OPERATORS, "Cull when crossing plane", { "Remove past a plane", "Removes particles that go past a flat plane." } },
			{ CATEGORY_OPERATORS, "Cull when crossing sphere", { "Remove outside a sphere", "Removes particles that leave (or enter) a ball around a control point." } },
			{ CATEGORY_OPERATORS, "Cull relative to model", { "Remove inside a model", "Removes particles inside (or outside) the model on a control point." } },
			{ CATEGORY_OPERATORS, "Noise Scalar", { "Noisy setting", "Sets a setting from noise that varies smoothly with place and time." } },
			{ CATEGORY_OPERATORS, "Noise Vector", { "Noisy position or colour", "Sets a position or colour from noise that varies smoothly with place and time." } },
			{ CATEGORY_OPERATORS, "Remap Distance to Control Point to Scalar", { "Setting from distance", "Sets a setting from how far each particle is from a control point." } },
			{ CATEGORY_OPERATORS, "Remap Distance to Control Point to Vector", { "Colour from distance", "Sets a colour (or position) from how far each particle is from a control point." } },
			{ CATEGORY_OPERATORS, "Remap Distance Between Two Control Points to Scalar", { "Setting from two points' distance", "Sets a setting from how far apart two control points are." } },
			{ CATEGORY_OPERATORS, "Remap Dot Product to Scalar", { "Setting from facing", "Sets a setting from how much two directions agree." } },
			{ CATEGORY_OPERATORS, "Remap Scalar", { "Setting from another", "Sets one setting from another as the particle lives." } },
			{ CATEGORY_OPERATORS, "Remap Speed To Scalar", { "Setting from speed", "Sets a setting from how fast each particle moves." } },
			{ CATEGORY_OPERATORS, "Set Control Point Positions", { "Place control points", "Puts up to four control points at set places from another one." } },
			{ CATEGORY_OPERATORS, "Set child control points from particle positions", { "Child control points from particles", "Puts the child systems' control points where this system's particles are." } },
			{ CATEGORY_OPERATORS, "Set Control Point To Particles' Center", { "Control point at the middle", "Puts a control point at the middle of the particles." } },
			{ CATEGORY_OPERATORS, "Set Control Point To Player", { "Control point on the player", "Puts a control point where the player is." } },
			{ CATEGORY_OPERATORS, "Color Light from Control Point", { "Light from control points", "Lights particles with up to four coloured lights on control points." } },
			{ CATEGORY_OPERATORS, "Rotation Orient Relative to CP", { "Face a control point", "Turns particles to point at a control point." } },
			{ CATEGORY_OPERATORS, "Rotation Orient to 2D Direction", { "Face the way it moves", "Turns particles to point the way they move." } },
			{ CATEGORY_OPERATORS, "Clamp Scalar", { "Keep a setting in range", "Keeps a setting between a low and a high." } },
			{ CATEGORY_OPERATORS, "Lerp Initial Scalar", { "Blend a setting to a value", "Changes a setting from where it started to a value over part of the life." } },
			{ CATEGORY_OPERATORS, "Lerp Initial Vector", { "Blend position or colour to a value", "Changes a position or colour from where it started to a value over part of the life." } },
			{ CATEGORY_OPERATORS, "Lifespan Minimum Radius Decay", { "Die when tiny", "Removes particles that get smaller than a size." } },
			{ CATEGORY_OPERATORS, "Lifespan Minimum Velocity Decay", { "Die when slow", "Removes particles that get slower than a speed." } },
			{ CATEGORY_OPERATORS, "Lifespan Maintain Count Decay", { "Keep a count", "Removes the oldest particles when there are more than a number." } },
			{ CATEGORY_OPERATORS, "Normalize Vector", { "Normalize a vector", "Makes a position or colour one unit long, then scales it." } },
			{ CATEGORY_OPERATORS, "Ramp Scalar Linear Simple", { "Ramp a setting", "Raises (or lowers) a setting steadily over part of the life." } },
			{ CATEGORY_OPERATORS, "Stop Effect after Duration", { "Stop after a time", "Stops making particles (and can clear them) after so many seconds." } },
			{ CATEGORY_OPERATORS, "Inherit Attribute From Parent Particle", { "Copy from a parent particle", "Keeps a setting at a parent system particle's value." } },
			// Renderers
			{ CATEGORY_RENDERERS, "render_animated_sprites", { "Sprites", "Draws each particle as a flat picture, facing you, upright or lying flat." } },
			{ CATEGORY_RENDERERS, "render_sprite_trail", { "Trails", "Draws each particle stretched along the way it moves." } },
			{ CATEGORY_RENDERERS, "render_rope", { "Rope", "Draws the particles joined into one line, in the order they were made." } },
			{ CATEGORY_RENDERERS, "render_screen_velocity_rotate", { "Sprites facing their motion", "Draws sprites turned to point the way they move on screen (sparks)." } },
			// Forces
			{ CATEGORY_FORCES, "random force", { "Random push", "Pushes particles a random amount each moment." } },
			{ CATEGORY_FORCES, "twist around axis", { "Twist", "Pushes particles around an axis, like a whirlwind." } },
			{ CATEGORY_FORCES, "Pull towards control point", { "Pull to a control point", "Pulls particles toward a control point." } },
			{ CATEGORY_FORCES, "turbulent force", { "Turbulence", "Pushes particles with layers of smooth noise, like gusts of wind." } },
			// Constraints
			{ CATEGORY_CONSTRAINTS, "Constrain distance to control point", { "Stay near a control point", "Keeps particles within a distance of a control point." } },
			{ CATEGORY_CONSTRAINTS, "Constrain distance to path between two control points", { "Stay near a path", "Keeps particles near a point travelling from one control point to another." } },
			{ CATEGORY_CONSTRAINTS, "Prevent passing through a plane", { "Wall", "Keeps particles on one side of a flat plane." } },
			{ CATEGORY_CONSTRAINTS, "Prevent passing through static part of world", { "Stop at the world", "Keeps particles from going through walls and floors." } },
			{ CATEGORY_CONSTRAINTS, "Collision via traces", { "Bounce off the world", "Makes particles hit walls and floors, bouncing and sliding." } },
		};
		std::map<std::pair<Category_t, std::string>, Entry_t> out;
		for ( const auto &[ nCategory, szName, entry ] : entries )
		{
			out.emplace( std::pair{ nCategory, Lower( szName ) }, entry );
		}
		return out;
	}();
	return functions;
}

//-----------------------------------------------------------------------------
// Purpose: Settings by key (lower case), and by function and key where a key means something else
//          in one function.
//-----------------------------------------------------------------------------
const std::map<std::string, Entry_t> &Settings()
{
	static const std::map<std::string, Entry_t> settings = [] {
		const std::initializer_list<std::pair<std::string_view, Entry_t>> entries{
			// The system
			{ "max_particles", { "Most particles at once", "No more are made while this many are alive." } },
			{ "initial_particles", { "Particles at the start", "How many are made the moment the effect starts." } },
			{ "material", { "Material", "The material (a .vmt under materials/) the particles are drawn with." } },
			{ "color", { "Colour", "The colour particles start with, unless an initializer sets one." } },
			{ "radius", { "Size", "The radius particles start with, unless an initializer sets one." } },
			{ "rotation", { "Roll", "Degrees particles start turned, unless an initializer sets it." } },
			{ "rotation_speed", { "Spin speed", "Degrees a second particles start spinning at." } },
			{ "sequence_number", { "Sprite sequence", "The sprite sheet sequence particles start on." } },
			{ "sequence_number 1", { "Second sprite sequence", "The sequence of the second texture." } },
			{ "bounding_box_min", { "Box, low corner", "With the high corner, the box the game checks against the view to skip drawing." } },
			{ "bounding_box_max", { "Box, high corner", "With the low corner, the box the game checks against the view to skip drawing." } },
			{ "bounding_box_control_point", { "Box centred on", "The control point the box is around." } },
			{ "cull_radius", { "Cut-down distance", "Beyond this, the game swaps in the replacement system (0: never)." } },
			{ "cull_cost", { "Cost", "How costly the effect is, for the game's cutting down." } },
			{ "cull_control_point", { "Cut-down point", "The control point the cut-down distance is measured from." } },
			{ "cull_replacement_definition", { "Replacement system", "The cheaper system shown instead when the effect is cut down." } },
			{ "maximum draw distance", { "Draw distance", "Not drawn farther away than this." } },
			{ "control point to disable rendering if it is the camera", { "Hidden from this point's view", "Not drawn for the camera on this control point (a first-person effect)." } },
			{ "maximum time step", { "Longest step", "The longest stretch of time simulated in one go." } },
			{ "maximum sim tick rate", { "Most steps a second", "Simulates at most this many times a second (0: no limit)." } },
			{ "minimum sim tick rate", { "Fewest steps a second", "Simulates at least this many times a second." } },
			{ "minimum rendered frames", { "Frames drawn before sleeping", "Drawn at least this many frames before it may stop updating out of view." } },
			{ "time to sleep when not drawn", { "Sleep when unseen after", "Seconds out of view before the game stops updating it." } },
			{ "sort particles", { "Sort back to front", "Draws far particles first, so near ones cover them." } },
			{ "batch particle systems", { "Batch", "Draws it together with others like it." } },
			{ "view model effect", { "First-person effect", "Drawn with the player's weapon, in front of the world." } },
			{ "group id", { "Group", "A number other systems' settings can refer to it by." } },
			{ "preventnamebasedlookup", { "Hide from name lookup", "The game can't start it by name." } },
			{ "child", { "System", "The system this link starts." } },
			{ "delay", { "Delay", "Seconds after the parent starts that the child starts." } },
			// Common
			{ "control_point_number", { "Control point", "Which control point it works from." } },
			{ "control point number", { "Control point", "Which control point it works from." } },
			{ "control point", { "Control point", "Which control point it works from." } },
			{ "start control point number", { "Start point", "The control point the path starts at." } },
			{ "end control point number", { "End point", "The control point the path ends at." } },
			{ "output field", { "Sets", "Which of the particle's settings it changes." } },
			{ "input field", { "Reads", "Which of the particle's settings it reads." } },
			{ "output minimum", { "Lowest result", "What the low end of the input becomes." } },
			{ "output maximum", { "Highest result", "What the high end of the input becomes." } },
			{ "input minimum", { "Input from", "The low end of what it reads." } },
			{ "input maximum", { "Input to", "The high end of what it reads." } },
			{ "output is scalar of initial random range", { "Multiply instead", "Multiplies the setting by the result instead of replacing it." } },
			{ "output is scalar of current value", { "Multiply current value", "Multiplies the setting's current value by the result." } },
			{ "only active within specified input range", { "Only inside the range", "Leaves particles alone when the input is outside its range." } },
			{ "only active within specified distance", { "Only inside the distances", "Leaves particles alone outside the distance range." } },
			{ "distance minimum", { "Distance from", "The near end of the distances it maps." } },
			{ "distance maximum", { "Distance to", "The far end of the distances it maps." } },
			{ "emitter lifetime start time (seconds)", { "Only from (seconds)", "Works on particles made after this time (-1: always)." } },
			{ "emitter lifetime end time (seconds)", { "Only until (seconds)", "Works on particles made before this time (-1: always)." } },
			{ "proportional 0/1", { "Share of life", "Times are shares of each particle's life (on) or seconds (off)." } },
			{ "start/end proportional", { "Window as share of life", "The start and end times are shares of life (on) or seconds (off)." } },
			{ "ease_in_and_out", { "Ease in and out", "Starts and ends the change gently instead of evenly." } },
			{ "ease in and out", { "Ease in and out", "Starts and ends the change gently instead of evenly." } },
			// Emitters
			{ "emission_rate", { "Particles per second", "How many it makes each second." } },
			{ "emission_duration", { "For (seconds)", "How long it keeps making them (0: for good)." } },
			{ "emission_start_time", { "Starts after (seconds)", "Seconds after the effect starts that it begins." } },
			{ "num_to_emit", { "How many", "How many particles the burst makes." } },
			{ "num_to_emit_minimum", { "At least", "With this set (not -1), a random number from this to How many." } },
			{ "maximum emission per frame", { "Most per frame", "Spreads the burst over frames, this many each (-1: all at once)." } },
			{ "scale emission to used control points", { "Scale by control points", "Makes more when the game supplies more control points." } },
			{ "emission minimum", { "Lowest rate", "Fewest particles a second." } },
			{ "emission maximum", { "Highest rate", "Most particles a second." } },
			{ "time noise coordinate scale", { "Change speed", "How quickly the noise changes over time; higher is choppier." } },
			{ "time coordinate offset", { "Noise offset", "Moves to another part of the noise, so two noisy settings differ." } },
			{ "spatial noise coordinate scale", { "Change over space", "How quickly the noise changes from place to place." } },
			{ "spatial coordinate offset", { "Noise offset in space", "Moves to another part of the noise in space." } },
			{ "absolute value", { "Bounce off zero", "Folds the noise at zero, for sharp dips." } },
			{ "invert absolute value", { "Sharp peaks", "With Bounce off zero, turns the sharp dips into sharp peaks." } },
			{ "noise coordinate scale", { "Noise scale", "How quickly the noise changes from place to place." } },
			// Initializers
			{ "distance_min", { "Nearest", "The least distance from the centre a particle starts at." } },
			{ "distance_max", { "Farthest", "The most distance from the centre a particle starts at." } },
			{ "distance_bias", { "Shape", "Stretches the sphere on each axis; 1 1 0 makes a flat disc." } },
			{ "distance_bias_absolute_value", { "Half only", "1 on an axis keeps only that axis's positive half (a dome)." } },
			{ "bias in local system", { "Shape turns with the point", "The shape follows the control point's turning." } },
			{ "speed_min", { "Slowest outward", "Least speed away from the centre." } },
			{ "speed_max", { "Fastest outward", "Most speed away from the centre." } },
			{ "speed_random_exponent", { "Speed bias", "Above 1 favours the slow end; below 1 the fast end." } },
			{ "speed_in_local_coordinate_system_min", { "Extra speed, low", "Low end of extra speed on each axis." } },
			{ "speed_in_local_coordinate_system_max", { "Extra speed, high", "High end of extra speed on each axis." } },
			{ "create in model", { "In the model", "Starts particles in the model on the control point instead." } },
			{ "offset min", { "Shift, low", "Low end of the shift on each axis." } },
			{ "offset max", { "Shift, high", "High end of the shift on each axis." } },
			{ "offset in local space 0/1", { "Shift turns with the point", "The shift follows the control point's turning." } },
			{ "offset proportional to radius 0/1", { "Shift by size", "Multiplies the shift by the particle's size." } },
			{ "warp min", { "Stretch, low", "Low end of the stretch on each axis (1: none)." } },
			{ "warp max", { "Stretch, high", "High end of the stretch on each axis." } },
			{ "warp transition time (treats min/max as start/end sizes)", { "Stretch over (seconds)", "Changes the stretch from low to high over this time instead of at random." } },
			{ "warp transition start time", { "Stretch starts after", "Seconds before the stretch starts changing." } },
			{ "reverse warp (0/1)", { "Stretch backwards", "Changes the stretch from high to low instead." } },
			{ "lifetime_min", { "Shortest life (s)", "" } },
			{ "lifetime_max", { "Longest life (s)", "" } },
			{ "lifetime_random_exponent", { "Lifetime bias", "Above 1 favours short lives; below 1 long ones." } },
			{ "start age minimum", { "Least head start", "Least share of its life a particle starts with already gone." } },
			{ "start age maximum", { "Most head start", "Most share of its life a particle starts with already gone." } },
			{ "frames per second", { "Frames per second", "How fast the animation plays, which sets how long it lasts." } },
			{ "radius_min", { "Smallest", "" } },
			{ "radius_max", { "Largest", "" } },
			{ "radius_random_exponent", { "Size bias", "Above 1 favours small; below 1 large." } },
			{ "color1", { "Colour from", "" } },
			{ "color2", { "Colour to", "" } },
			{ "tint_perc", { "World light tint", "How much the light where a particle starts tints it (0 to 1)." } },
			{ "alpha_min", { "Least opaque", "0 clear, 255 solid." } },
			{ "alpha_max", { "Most opaque", "0 clear, 255 solid." } },
			{ "alpha_random_exponent", { "Opacity bias", "Above 1 favours the low end." } },
			{ "rotation_initial", { "Base roll", "Degrees every particle starts turned." } },
			{ "rotation_offset_min", { "Extra roll from", "Least random extra turn, in degrees." } },
			{ "rotation_offset_max", { "Extra roll to", "Most random extra turn, in degrees." } },
			{ "rotation_random_exponent", { "Roll bias", "" } },
			{ "rotation_speed_constant", { "Base spin", "Degrees a second every particle spins." } },
			{ "rotation_speed_random_min", { "Extra spin from", "Least random extra spin, degrees a second." } },
			{ "rotation_speed_random_max", { "Extra spin to", "Most random extra spin, degrees a second." } },
			{ "rotation_speed_random_exponent", { "Spin bias", "" } },
			{ "randomly_flip_direction", { "Either way", "Spins some particles the other way." } },
			{ "yaw_initial", { "Base yaw", "Degrees every particle starts turned around the up axis." } },
			{ "yaw_offset_min", { "Extra yaw from", "" } },
			{ "yaw_offset_max", { "Extra yaw to", "" } },
			{ "flip percentage", { "Share flipped", "The share of particles turned half way around (0 to 1)." } },
			{ "sequence_min", { "Sequence from", "Lowest sequence (or frame) number." } },
			{ "sequence_max", { "Sequence to", "Highest sequence (or frame) number." } },
			{ "length_min", { "Shortest trail", "" } },
			{ "length_max", { "Longest trail", "" } },
			{ "random_speed_min", { "Slowest", "Least speed in the random direction." } },
			{ "random_speed_max", { "Fastest", "Most speed in the random direction." } },
			{ "velocity scale", { "Share of its speed", "How much of the speed it passes on (1: all)." } },
			{ "particles to map from start to end", { "Steps along the path", "How many particles it takes to reach the end." } },
			{ "restart behavior (0 = bounce, 1 = loop )", { "Loop", "At the end, start again (on) or come back (off)." } },
			{ "bulge", { "Bulge", "How far the path bows out sideways." } },
			{ "mid point position", { "Bulge at", "Where along the path (0 to 1) it bows out most." } },
			{ "bulge control 0=random 1=orientation of start pnt 2=orientation of end point", { "Bulge direction", "0 random, 1 the start point's facing, 2 the end point's." } },
			{ "maximum distance", { "Spread", "How far off the path particles may start." } },
			{ "growth time", { "Grows over (seconds)", "How long the chain takes to reach the end point." } },
			{ "use highest supplied end point", { "To the last point given", "Ends at the highest control point the game supplies." } },
			{ "random parent particle distribution", { "Random parent particle", "Picks parent particles at random instead of in turn." } },
			{ "inherited velocity scale", { "Share of their speed", "How much of the parent particle's speed it takes." } },
			{ "hitbox scale", { "Hitbox share", "How much of each hitbox to use (1 or 0: all, 0.5: the centre)." } },
			{ "force to be inside model", { "Tries", "Attempts at a good point; more with a direction bias." } },
			{ "direction bias", { "Favour direction", "Prefers points on the model's side this way." } },
			{ "end control point", { "Goes to", "The control point particles fly to." } },
			{ "end spread", { "Spread at the end", "How far around the end point they aim." } },
			{ "start offset", { "Starts ahead by", "How far along the way particles start." } },
			{ "minimum speed", { "Slowest", "" } },
			{ "maximum speed", { "Fastest", "" } },
			{ "input control point number", { "Control point", "The control point whose place it reads." } },
			{ "input field 0-2 x/y/z", { "Axis", "0 its X, 1 its Y, 2 its Z." } },
			{ "initial radius", { "Ring radius", "" } },
			{ "local offset min", { "Shift, low", "" } },
			{ "local offset max", { "Shift, high", "" } },
			{ "min", { "From", "" } },
			{ "max", { "To", "" } },
			{ "exponent", { "Bias", "Above 1 favours the low end." } },
			{ "inherited field", { "Copies", "Which setting it copies." } },
			{ "particle increment amount", { "Particles per parent", "How many particles share each parent particle." } },
			{ "scale", { "Scale", "Multiplies what it copies." } },
			{ "maximum trace length", { "Longest look", "The farthest it checks ahead." } },
			{ "cull outside instead of inside", { "Remove outside", "Removes particles outside the model instead of inside." } },
			{ "use only bounding box", { "Box only", "Uses the model's whole box instead of its hitboxes." } },
			// Operators
			{ "gravity", { "Gravity", "Pull each second on each axis; 0 0 -800 is roughly the game's." } },
			{ "drag", { "Drag", "Share of speed lost (0 none, 1 stops at once)." } },
			{ "max constraint passes", { "Limit passes", "How many times the limits are applied each step." } },
			{ "fade in time min", { "Fade in, shortest", "" } },
			{ "fade in time max", { "Fade in, longest", "" } },
			{ "fade in time exponent", { "Fade bias", "" } },
			{ "fade out time min", { "Fade out, shortest", "" } },
			{ "fade out time max", { "Fade out, longest", "" } },
			{ "fade out time exponent", { "Fade bias", "" } },
			{ "start_alpha", { "Fade in from", "Opacity at the start (0 to 1)." } },
			{ "end_alpha", { "Fade out to", "Opacity at the end (0 to 1)." } },
			{ "start_fade_in_time", { "Fade in starts", "Share of life." } },
			{ "end_fade_in_time", { "Fade in ends", "Share of life." } },
			{ "start_fade_out_time", { "Fade out starts", "Share of life." } },
			{ "end_fade_out_time", { "Fade out ends", "Share of life." } },
			{ "color_fade", { "Change to", "The colour particles end up." } },
			{ "fade_start_time", { "Starts at", "Share of life when the change begins." } },
			{ "fade_end_time", { "Done at", "Share of life when it is complete." } },
			{ "start_time", { "Starts at", "Share of life when the change begins." } },
			{ "end_time", { "Done at", "Share of life when it is complete." } },
			{ "radius_start_scale", { "Scale at start", "1 keeps the size." } },
			{ "radius_end_scale", { "Scale at end", "" } },
			{ "scale_bias", { "Curve", "0.5 even; lower changes late, higher early." } },
			{ "spin_rate_degrees", { "Spin (degrees a second)", "" } },
			{ "spin_rate_min", { "Slows to", "Degrees a second it ends at." } },
			{ "spin_stop_time", { "Slowed by", "Share of life when it reaches the slower rate (0: never slows)." } },
			{ "yaw_rate_degrees", { "Spin (degrees a second)", "" } },
			{ "yaw_rate_min", { "Slows to", "" } },
			{ "yaw_stop_time", { "Slowed by", "" } },
			{ "oscillation field", { "Wobbles", "Which setting goes up and down." } },
			{ "oscillation rate min", { "Amount, low", "How much it changes, low end." } },
			{ "oscillation rate max", { "Amount, high", "How much it changes, high end." } },
			{ "oscillation frequency min", { "Waves, low", "Waves a second, or per life with Share of life." } },
			{ "oscillation frequency max", { "Waves, high", "" } },
			{ "oscillation multiplier", { "Amount multiplier", "" } },
			{ "oscillation start phase", { "Start on the wave at", "Where on the wave (0 to 1) it begins." } },
			{ "start time min", { "From, earliest", "" } },
			{ "start time max", { "From, latest", "" } },
			{ "end time min", { "Until, earliest", "" } },
			{ "end time max", { "Until, latest", "" } },
			{ "start_fadeout_min", { "Lets go from, earliest", "Share of life when it starts letting go." } },
			{ "start_fadeout_max", { "Lets go from, latest", "" } },
			{ "end_fadeout_min", { "Let go by, earliest", "Share of life when it has let go." } },
			{ "end_fadeout_max", { "Let go by, latest", "" } },
			{ "distance fade range", { "Lets go at distance", "Lets go as particles get this far from the point (0: never)." } },
			{ "lock rotation", { "Turn with it", "Also turns particles when the point turns." } },
			{ "lifetime fade start", { "Lets go from", "Share of life." } },
			{ "lifetime fade end", { "Let go by", "Share of life." } },
			{ "falloff range", { "Slows within", "Distance at which slowing starts." } },
			{ "dampen scale", { "Slowing", "How strongly it slows." } },
			{ "rotation axis", { "Axis", "The axis it turns around." } },
			{ "rotation rate", { "Degrees a second", "" } },
			{ "use local space", { "Axis turns with the point", "" } },
			{ "maximum velocity", { "Top speed", "" } },
			{ "cohesion strength", { "Pull", "How strongly particles are pulled back (1: held exactly)." } },
			{ "cull percentage", { "Share removed", "0 to 1." } },
			{ "cull start time", { "From", "Share of life." } },
			{ "cull end time", { "Until", "Share of life." } },
			{ "cull plane offset", { "Plane offset", "Moves the plane along its normal." } },
			{ "control point for point on plane", { "Plane through", "The control point the plane passes through." } },
			{ "plane normal", { "Plane facing", "Which way the plane faces." } },
			{ "control point number to set", { "Sets point", "The control point it moves." } },
			{ "center offset", { "Offset", "Added to the middle." } },
			{ "control point offset", { "Offset", "Added to the place." } },
			{ "# of control points to set", { "How many points", "" } },
			{ "first control point to set", { "First point", "" } },
			{ "first particle to copy", { "First particle", "" } },
			{ "set positions in world space", { "World places", "Places are in the world, not from the offset point." } },
			{ "control point to offset positions from", { "From point", "The control point the places are measured from." } },
			{ "rotation offset", { "Extra turn", "Degrees added." } },
			{ "spin strength", { "Turn rate", "How much of the way it turns each step (1: at once)." } },
			{ "rotation field", { "Turns", "Which rotation it sets." } },
			{ "initial color bias", { "Unlit share", "How much of the particle's own colour stays unlit." } },
			{ "clamp minimum light value to initial color", { "No darker than its colour", "" } },
			{ "clamp maximum light value to initial color", { "No brighter than its colour", "" } },
			{ "half-lambert normals", { "Soft light", "" } },
			{ "compute normals from control points", { "Light from points' shape", "" } },
			{ "value to lerp to", { "Blend to", "" } },
			{ "minimum radius", { "Smallest size", "" } },
			{ "minimum velocity", { "Slowest speed", "" } },
			{ "count to maintain", { "Keep", "How many to keep." } },
			{ "decay delay", { "Not before (seconds)", "Particles younger than this aren't removed." } },
			{ "scale factor", { "Length", "" } },
			{ "rate field", { "Ramps", "Which setting it changes." } },
			{ "ramp rate", { "Per second", "How much it changes each second." } },
			{ "duration at which to stop", { "Stop after (seconds)", "" } },
			{ "destroy all particles immediately 0/1", { "Clear at once", "Removes every particle when it stops." } },
			{ "cull distance", { "Radius", "" } },
			{ "cull inside instead of outside 0/1", { "Remove inside", "Removes particles inside instead of outside." } },
			// Renderers
			{ "orientation_type", { "Facing", "0 faces you, 1 stands upright, 2 lies flat." } },
			{ "animation rate", { "Animation speed", "How fast the sprite sheet plays." } },
			{ "use animation rate as fps", { "Speed is frames a second", "" } },
			{ "animation_fit_lifetime", { "Animation fits the life", "Plays the whole animation once over each particle's life." } },
			{ "second sequence animation rate", { "Second animation speed", "" } },
			{ "orientation control point", { "Face with point", "Particles take this control point's facing." } },
			{ "min length", { "Shortest", "" } },
			{ "max length", { "Longest", "" } },
			{ "length fade in time", { "Grow in over", "Seconds for a trail to reach full length." } },
			{ "subdivision_count", { "Smoothness", "Points between particles along the rope." } },
			{ "texel_size", { "Texture length", "Share of the texture across one stretch of rope." } },
			{ "texture_scroll_rate", { "Texture scroll", "How fast the texture slides along the rope." } },
			{ "forward_angle", { "Texture forward", "The texture's forward, in degrees from its X axis (-90: up)." } },
			{ "rotate_rate(dps)", { "Extra spin", "Degrees a second added." } },
			// Forces and constraints
			{ "min force", { "Push, low", "" } },
			{ "max force", { "Push, high", "" } },
			{ "amount of force", { "Strength", "" } },
			{ "falloff power", { "Falloff", "0 the same everywhere; higher weakens with distance." } },
			{ "twist axis", { "Axis", "" } },
			{ "object local space axis 0/1", { "Axis turns with the effect", "" } },
			{ "minimum distance", { "Nearest", "" } },
			{ "offset of center", { "Centre offset", "" } },
			{ "global center point", { "World centre", "" } },
			{ "maximum distance middle", { "Farthest, middle", "-1: the same as Spread." } },
			{ "maximum distance end", { "Farthest, end", "-1: the same as the middle." } },
			{ "travel time", { "Travels over (seconds)", "" } },
			{ "random bulge", { "Random bulge", "" } },
			{ "plane point", { "Plane through", "" } },
			{ "global origin", { "World plane", "The plane is in the world, not at the control point." } },
			{ "global normal", { "World facing", "" } },
			{ "amount of bounce", { "Bounce", "Share of speed kept bouncing off (0 to 1)." } },
			{ "amount of slide", { "Slide", "Share of speed kept sliding along." } },
			{ "radius scale", { "Hit size", "Multiplies the size used for hitting." } },
			{ "collision mode", { "Accuracy", "0 most accurate; 1 and 2 cheap guesses; 3 a balance." } },
			{ "collision group", { "Hits", "What it collides with (DEBRIS: the world and static props)." } },
			{ "brush only", { "Brushes only", "Ignores props." } },
			{ "kill particle on collision", { "Die on hit", "" } },
		};
		std::map<std::string, Entry_t> out;
		for ( const auto &[ szKey, entry ] : entries )
		{
			out.emplace( Lower( szKey ), entry );
		}
		return out;
	}();
	return settings;
}

//-----------------------------------------------------------------------------
// Purpose: Where a key means something else in one function.
//-----------------------------------------------------------------------------
const std::map<std::pair<std::string, std::string>, Entry_t> &FunctionSettings()
{
	static const std::map<std::pair<std::string, std::string>, Entry_t> settings = [] {
		const std::initializer_list<std::tuple<std::string_view, std::string_view, Entry_t>> entries{
			{ "Position Within Box Random", "min", { "Low corner", "From the control point." } },
			{ "Position Within Box Random", "max", { "High corner", "From the control point." } },
			{ "Constrain distance to control point", "maximum distance", { "Farthest", "" } },
			{ "Constrain distance to path between two control points", "maximum distance", { "Farthest, start", "" } },
			{ "Position In CP Hierarchy", "maximum distance", { "Spread", "How far off the chain particles may start." } },
			{ "Movement Rotate Particle Around Axis", "Control Point", { "Around point", "" } },
			{ "Remap Distance Between Two Control Points to Scalar", "distance minimum", { "Apart from", "" } },
			{ "Remap Distance Between Two Control Points to Scalar", "distance maximum", { "Apart to", "" } },
		};
		std::map<std::pair<std::string, std::string>, Entry_t> out;
		for ( const auto &[ szFunction, szKey, entry ] : entries )
		{
			out.emplace( std::pair{ Lower( szFunction ), Lower( szKey ) }, entry );
		}
		return out;
	}();
	return settings;
}

//-----------------------------------------------------------------------------
// Purpose: A key without a plain name, made readable: underscores to spaces, switch hints dropped,
//          a capital first letter.
//-----------------------------------------------------------------------------
std::string Readable( std::string_view szKey )
{
	std::string szOut;
	for ( char c : szKey )
	{
		szOut += c == '_' ? ' ' : c;
	}
	for ( std::string_view szHint : { " 0/1", " (0/1)" } )
	{
		if ( const auto nAt = szOut.find( szHint ); nAt != std::string::npos )
		{
			szOut.erase( nAt, szHint.size() );
		}
	}
	if ( !szOut.empty() )
	{
		szOut[ 0 ] = static_cast<char>( std::toupper( static_cast<unsigned char>( szOut[ 0 ] ) ) );
	}
	return szOut;
}

//-----------------------------------------------------------------------------
// Purpose: The settings that come in numbered sets: lights, control point slots, noise layers.
//-----------------------------------------------------------------------------
std::optional<PlainName_t> Patterned( std::string_view szAttribute )
{
	const std::string szLower = Lower( szAttribute );
	// "Light 1 Color" and the like.
	if ( szLower.starts_with( "light " ) && szLower.size() > 8 && std::isdigit( static_cast<unsigned char>( szLower[ 6 ] ) ) )
	{
		static const std::map<std::string, Entry_t> parts{
			{ "color", { "colour", "Its colour; black turns it off." } },
			{ "control point", { "on point", "The control point it is on." } },
			{ "control point offset", { "offset", "Where it is from its control point." } },
			{ "50% distance", { "half strength at", "Distance where it is half as bright." } },
			{ "0% distance", { "reach", "Distance where it fades out." } },
			{ "type 0=point 1=spot", { "spotlight", "Shines in a cone instead of all round." } },
			{ "direction", { "spot direction", "" } },
			{ "spot inner cone", { "spot full within", "Degrees from its direction lit fully." } },
			{ "spot outer cone", { "spot edge", "Degrees from its direction where it fades out." } },
			{ "dynamic light", { "lights the world", "Also lights the map around it." } },
		};
		const auto found = parts.find( szLower.substr( 8 ) );
		const std::string szWhich = "Light " + std::string( 1, szAttribute[ 6 ] );
		return found == parts.end() ? PlainName_t{ szWhich + ": " + Readable( szAttribute.substr( 8 ) ), {} }
									: PlainName_t{ szWhich + ": " + std::string{ found->second.m_szName }, std::string{ found->second.m_szMeaning } };
	}
	// "First Control Point Number" and the like.
	static const std::array<std::pair<std::string_view, std::string_view>, 4> SLOTS{ { { "first ", "Point 1" }, { "second ", "Point 2" }, { "third ", "Point 3" }, { "fourth ", "Point 4" } } };
	for ( const auto &[ szPrefix, szSlot ] : SLOTS )
	{
		if ( szLower.starts_with( szPrefix ) && szLower.find( "control point " ) == szPrefix.size() )
		{
			const std::string szWhat = szLower.substr( szPrefix.size() + 14 );
			if ( szWhat == "number" )
			{
				return PlainName_t{ std::string{ szSlot } + ": which", "The control point this slot places." };
			}
			if ( szWhat == "parent" )
			{
				return PlainName_t{ std::string{ szSlot } + ": moves with", "The control point it moves with in the game." };
			}
			if ( szWhat == "location" )
			{
				return PlainName_t{ std::string{ szSlot } + ": place", "Where it goes, from the offset point." };
			}
		}
	}
	// "Noise amount 0", "Noise scale 0".
	for ( const auto &[ szPrefix, szPlain ] : { std::pair<std::string_view, std::string_view>{ "noise amount ", "strength" }, { "noise scale ", "scale" } } )
	{
		if ( szLower.starts_with( szPrefix ) )
		{
			return PlainName_t{ "Layer " + szLower.substr( szPrefix.size() ) + " " + std::string{ szPlain }, {} };
		}
	}
	if ( szLower.starts_with( "visibility " ) )
	{
		return PlainName_t{ "Visibility: " + Readable( szAttribute.substr( 11 ) ), "Fades or shrinks particles as a point gets hidden from view (for glows)." };
	}
	return std::nullopt;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
PlainName_t PlainFunction( Category_t nCategory, std::string_view szFunction )
{
	const auto &functions = Functions();
	const auto found = functions.find( { nCategory, Lower( CurrentFunctionName( nCategory, szFunction ) ) } );
	if ( found == functions.end() )
	{
		return {};
	}
	return { std::string{ found->second.m_szName }, std::string{ found->second.m_szMeaning } };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
PlainName_t PlainSetting( std::string_view szFunction, std::string_view szAttribute )
{
	if ( !szFunction.empty() )
	{
		const auto &specific = FunctionSettings();
		std::string szCurrent{ szFunction };
		for ( Category_t nCategory : CATEGORIES )
		{
			if ( const std::string_view szName = CurrentFunctionName( nCategory, szFunction ); szName != szFunction )
			{
				szCurrent = szName;
				break;
			}
		}
		if ( const auto found = specific.find( { Lower( szCurrent ), Lower( szAttribute ) } ); found != specific.end() )
		{
			return { std::string{ found->second.m_szName }, std::string{ found->second.m_szMeaning } };
		}
	}
	if ( const auto found = Settings().find( Lower( szAttribute ) ); found != Settings().end() )
	{
		return { std::string{ found->second.m_szName }, std::string{ found->second.m_szMeaning } };
	}
	if ( auto patterned = Patterned( szAttribute ) )
	{
		return *patterned;
	}
	return { Readable( szAttribute ), {} };
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::string_view PlainListTitle( std::string_view szList )
{
	static const std::map<std::string_view, std::string_view> titles{
		{ "emitters", "Making particles" },
		{ "initializers", "How they start" },
		{ "operators", "Over their life" },
		{ "renderers", "Drawing" },
		{ "forces", "Forces" },
		{ "constraints", "Limits" },
		{ "children", "Child systems" },
	};
	const auto found = titles.find( szList );
	return found == titles.end() ? szList : found->second;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsFieldSetting( std::string_view szAttribute )
{
	const std::string szLower = Lower( szAttribute );
	return szLower == "output field" || szLower == "input field" || szLower == "oscillation field" || szLower == "rotation field" || szLower == "inherited field" ||
		szLower == "rate field";
}

//-----------------------------------------------------------------------------
// Purpose: The fields as the engine numbers them (public SDK particles.h).
//-----------------------------------------------------------------------------
std::string_view FieldName( int nField )
{
	static constexpr std::array<std::string_view, FIELD_COUNT> NAMES{
		"Position",
		"Lifetime",
		"Previous position",
		"Radius",
		"Roll",
		"Roll speed",
		"Colour",
		"Opacity",
		"Creation time",
		"Sequence",
		"Trail length",
		"Particle ID",
		"Yaw",
		"Second sequence",
		"Hitbox",
		"Place in hitbox",
		"Second opacity",
		"Trace start",
		"Trace end",
		"Trace hit",
		"Trace hit normal",
		"Control point",
	};
	return nField >= 0 && nField < FIELD_COUNT ? NAMES[ static_cast<std::size_t>( nField ) ] : std::string_view{};
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool IsAdvancedSetting( std::string_view szFunction, std::string_view szAttribute )
{
	static const std::set<std::string> advanced{
		"maximum time step",
		"maximum sim tick rate",
		"minimum sim tick rate",
		"minimum rendered frames",
		"time to sleep when not drawn",
		"sort particles",
		"batch particle systems",
		"view model effect",
		"group id",
		"preventnamebasedlookup",
		"cull_cost",
		"max constraint passes",
		"los collision group",
		"los failure scalar",
		"ensure line of sight",
		"maximum trace length",
		"emitter lifetime start time (seconds)",
		"emitter lifetime end time (seconds)",
		"control point to disable rendering if it is the camera",
		"orientation control point",
		"speed_random_exponent",
		"lifetime_random_exponent",
		"radius_random_exponent",
		"alpha_random_exponent",
		"rotation_random_exponent",
		"rotation_speed_random_exponent",
		"yaw_random_exponent",
		"length_random_exponent",
		"fade in time exponent",
		"fade out time exponent",
		"start_fadeout_exponent",
		"end_fadeout_exponent",
	};
	const std::string szLower = Lower( szAttribute );
	(void)szFunction;
	return IsCommonOperatorAttribute( szAttribute ) || szLower.starts_with( "visibility " ) || advanced.contains( szLower );
}

} // namespace xv::particles
