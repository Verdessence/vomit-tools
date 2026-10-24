//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: See Builtin.h. The tables are written from the library's data by a script kept with
//          the local tests; edit the script, not the tables.
//
//===========================================================================//
#include "Builtin.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>

#include "util/Numbers.h"

namespace xv::particles
{

namespace
{

using enum dmx::AttrType_t;
using enum SettingHint_t;

constexpr BuiltinSetting_t SYSTEM[]{
	{ "max_particles", ATTR_TYPE_INT, "1000" },
	{ "initial_particles", ATTR_TYPE_INT, "0" },
	{ "material", ATTR_TYPE_STRING, "vgui/white", SETTING_HINT_MATERIAL },
	{ "bounding_box_min", ATTR_TYPE_VECTOR3, "-10 -10 -10" },
	{ "bounding_box_max", ATTR_TYPE_VECTOR3, "10 10 10" },
	{ "cull_radius", ATTR_TYPE_FLOAT, "0" },
	{ "cull_cost", ATTR_TYPE_FLOAT, "1" },
	{ "cull_control_point", ATTR_TYPE_INT, "0" },
	{ "cull_replacement_definition", ATTR_TYPE_STRING, "" },
	{ "radius", ATTR_TYPE_FLOAT, "5" },
	{ "color", ATTR_TYPE_COLOR, "255 255 255 255" },
	{ "rotation", ATTR_TYPE_FLOAT, "0" },
	{ "rotation_speed", ATTR_TYPE_FLOAT, "0" },
	{ "sequence_number", ATTR_TYPE_INT, "0" },
	{ "sequence_number 1", ATTR_TYPE_INT, "0" },
	{ "group id", ATTR_TYPE_INT, "0" },
	{ "maximum time step", ATTR_TYPE_FLOAT, "0.1" },
	{ "maximum sim tick rate", ATTR_TYPE_FLOAT, "0.0" },
	{ "minimum sim tick rate", ATTR_TYPE_FLOAT, "0.0" },
	{ "minimum rendered frames", ATTR_TYPE_INT, "0" },
	{ "control point to disable rendering if it is the camera", ATTR_TYPE_INT, "-1" },
	{ "maximum draw distance", ATTR_TYPE_FLOAT, "100000.0" },
	{ "time to sleep when not drawn", ATTR_TYPE_FLOAT, "8" },
	{ "Sort particles", ATTR_TYPE_BOOL, "1" },
	{ "batch particle systems", ATTR_TYPE_BOOL, "0" },
	{ "view model effect", ATTR_TYPE_BOOL, "0" },
	// Read by the library's own code, not its table; the game's files have it as a bool.
	{ "preventNameBasedLookup", ATTR_TYPE_BOOL, "0" },
};

constexpr BuiltinSetting_t CHILD[]{
	{ "delay", ATTR_TYPE_FLOAT, "0.0" },
};

constexpr BuiltinSetting_t WHEN_IT_RUNS[]{
	{ "operator start fadein", ATTR_TYPE_FLOAT, "0" },
	{ "operator end fadein", ATTR_TYPE_FLOAT, "0" },
	{ "operator start fadeout", ATTR_TYPE_FLOAT, "0" },
	{ "operator end fadeout", ATTR_TYPE_FLOAT, "0" },
	{ "operator fade oscillate", ATTR_TYPE_FLOAT, "0" },
};

constexpr BuiltinSetting_t VISIBILITY[]{
	{ "Visibility Proxy Input Control Point Number", ATTR_TYPE_INT, "-1" },
	{ "Visibility Proxy Radius", ATTR_TYPE_FLOAT, "1.0" },
	{ "Visibility input minimum", ATTR_TYPE_FLOAT, "0" },
	{ "Visibility input maximum", ATTR_TYPE_FLOAT, "1" },
	{ "Visibility Alpha Scale minimum", ATTR_TYPE_FLOAT, "0" },
	{ "Visibility Alpha Scale maximum", ATTR_TYPE_FLOAT, "1" },
	{ "Visibility Radius Scale minimum", ATTR_TYPE_FLOAT, "1" },
	{ "Visibility Radius Scale maximum", ATTR_TYPE_FLOAT, "1" },
	{ "Visibility Camera Depth Bias", ATTR_TYPE_FLOAT, "0" },
};

// emit_continuously
constexpr BuiltinSetting_t EMITTER_EMIT_CONTINUOUSLY[]{
	{ "emission_start_time", ATTR_TYPE_FLOAT, "0" },
	{ "emission_rate", ATTR_TYPE_FLOAT, "100" },
	{ "emission_duration", ATTR_TYPE_FLOAT, "0" },
	{ "scale emission to used control points", ATTR_TYPE_FLOAT, "0.0" },
	{ "use parent particles for emission scaling", ATTR_TYPE_BOOL, "0" },
};

// emit_instantaneously
constexpr BuiltinSetting_t EMITTER_EMIT_INSTANTANEOUSLY[]{
	{ "emission_start_time", ATTR_TYPE_FLOAT, "0" },
	{ "num_to_emit_minimum", ATTR_TYPE_INT, "-1" },
	{ "num_to_emit", ATTR_TYPE_INT, "100" },
	{ "maximum emission per frame", ATTR_TYPE_INT, "-1" },
	{ "emission count scale control point", ATTR_TYPE_INT, "-1" },
	{ "emission count scale control point field", ATTR_TYPE_INT, "0" },
};

// emit noise
constexpr BuiltinSetting_t EMITTER_EMIT_NOISE[]{
	{ "emission_start_time", ATTR_TYPE_FLOAT, "0" },
	{ "emission_duration", ATTR_TYPE_FLOAT, "0" },
	{ "scale emission to used control points", ATTR_TYPE_FLOAT, "0.0" },
	{ "time noise coordinate scale", ATTR_TYPE_FLOAT, "0.1" },
	{ "time coordinate offset", ATTR_TYPE_FLOAT, "0" },
	{ "absolute value", ATTR_TYPE_BOOL, "0" },
	{ "invert absolute value", ATTR_TYPE_BOOL, "0" },
	{ "emission minimum", ATTR_TYPE_FLOAT, "0" },
	{ "emission maximum", ATTR_TYPE_FLOAT, "100" },
	{ "world time noise coordinate scale", ATTR_TYPE_FLOAT, "0" },
};

// Position Along Path Random
constexpr BuiltinSetting_t INITIALIZER_POSITION_ALONG_PATH_RANDOM[]{
	{ "maximum distance", ATTR_TYPE_FLOAT, "0" },
	{ "bulge", ATTR_TYPE_FLOAT, "0" },
	{ "start control point number", ATTR_TYPE_INT, "0" },
	{ "end control point number", ATTR_TYPE_INT, "0" },
	{ "bulge control 0=random 1=orientation of start pnt 2=orientation of end point", ATTR_TYPE_INT, "0" },
	{ "mid point position", ATTR_TYPE_FLOAT, "0.5" },
};

// Move Particles Between 2 Control Points
constexpr BuiltinSetting_t INITIALIZER_MOVE_PARTICLES_BETWEEN_2_CONTROL_POINTS[]{
	{ "minimum speed", ATTR_TYPE_FLOAT, "1" },
	{ "maximum speed", ATTR_TYPE_FLOAT, "1" },
	{ "end spread", ATTR_TYPE_FLOAT, "0" },
	{ "start offset", ATTR_TYPE_FLOAT, "0" },
	{ "end control point", ATTR_TYPE_INT, "1" },
};

// Position Within Sphere Random
constexpr BuiltinSetting_t INITIALIZER_POSITION_WITHIN_SPHERE_RANDOM[]{
	{ "distance_min", ATTR_TYPE_FLOAT, "0" },
	{ "distance_max", ATTR_TYPE_FLOAT, "0" },
	{ "distance_bias", ATTR_TYPE_VECTOR3, "1 1 1" },
	{ "distance_bias_absolute_value", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "bias in local system", ATTR_TYPE_BOOL, "0" },
	{ "control_point_number", ATTR_TYPE_INT, "0" },
	{ "speed_min", ATTR_TYPE_FLOAT, "0" },
	{ "speed_max", ATTR_TYPE_FLOAT, "0" },
	{ "speed_random_exponent", ATTR_TYPE_FLOAT, "1" },
	{ "speed_in_local_coordinate_system_min", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "speed_in_local_coordinate_system_max", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "create in model", ATTR_TYPE_INT, "0" },
	{ "randomly distribute to highest supplied Control Point", ATTR_TYPE_BOOL, "0" },
	{ "randomly distribution growth time", ATTR_TYPE_FLOAT, "0" },
};

// Velocity Random
constexpr BuiltinSetting_t INITIALIZER_VELOCITY_RANDOM[]{
	{ "control_point_number", ATTR_TYPE_INT, "0" },
	{ "random_speed_min", ATTR_TYPE_FLOAT, "0" },
	{ "random_speed_max", ATTR_TYPE_FLOAT, "0" },
	{ "speed_in_local_coordinate_system_min", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "speed_in_local_coordinate_system_max", ATTR_TYPE_VECTOR3, "0 0 0" },
};

// Position on Model Random
constexpr BuiltinSetting_t INITIALIZER_POSITION_ON_MODEL_RANDOM[]{
	{ "control_point_number", ATTR_TYPE_INT, "0" },
	{ "force to be inside model", ATTR_TYPE_INT, "0" },
	{ "hitbox scale", ATTR_TYPE_INT, "1.0" },
	{ "direction bias", ATTR_TYPE_VECTOR3, "0 0 0" },
};

// Position Within Box Random
constexpr BuiltinSetting_t INITIALIZER_POSITION_WITHIN_BOX_RANDOM[]{
	{ "min", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "max", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "control point number", ATTR_TYPE_INT, "0" },
};

// Rotation Speed Random
constexpr BuiltinSetting_t INITIALIZER_ROTATION_SPEED_RANDOM[]{
	{ "rotation_speed_constant", ATTR_TYPE_FLOAT, "0" },
	{ "rotation_speed_random_min", ATTR_TYPE_FLOAT, "0" },
	{ "rotation_speed_random_max", ATTR_TYPE_FLOAT, "360" },
	{ "rotation_speed_random_exponent", ATTR_TYPE_FLOAT, "1" },
	{ "randomly_flip_direction", ATTR_TYPE_BOOL, "1" },
};

// Lifetime Random
constexpr BuiltinSetting_t INITIALIZER_LIFETIME_RANDOM[]{
	{ "lifetime_min", ATTR_TYPE_FLOAT, "0" },
	{ "lifetime_max", ATTR_TYPE_FLOAT, "0" },
	{ "lifetime_random_exponent", ATTR_TYPE_FLOAT, "1" },
};

// Alpha Random
constexpr BuiltinSetting_t INITIALIZER_ALPHA_RANDOM[]{
	{ "alpha_min", ATTR_TYPE_INT, "255" },
	{ "alpha_max", ATTR_TYPE_INT, "255" },
	{ "alpha_random_exponent", ATTR_TYPE_FLOAT, "1" },
};

// Radius Random
constexpr BuiltinSetting_t INITIALIZER_RADIUS_RANDOM[]{
	{ "radius_min", ATTR_TYPE_FLOAT, "1" },
	{ "radius_max", ATTR_TYPE_FLOAT, "1" },
	{ "radius_random_exponent", ATTR_TYPE_FLOAT, "1" },
};

// Rotation Random
constexpr BuiltinSetting_t INITIALIZER_ROTATION_RANDOM[]{
	{ "rotation_initial", ATTR_TYPE_FLOAT, "0" },
	{ "rotation_offset_min", ATTR_TYPE_FLOAT, "0" },
	{ "rotation_offset_max", ATTR_TYPE_FLOAT, "360" },
	{ "rotation_random_exponent", ATTR_TYPE_FLOAT, "1" },
};

// Rotation Yaw Random
constexpr BuiltinSetting_t INITIALIZER_ROTATION_YAW_RANDOM[]{
	{ "yaw_initial", ATTR_TYPE_FLOAT, "0" },
	{ "yaw_offset_min", ATTR_TYPE_FLOAT, "0" },
	{ "yaw_offset_max", ATTR_TYPE_FLOAT, "360" },
	{ "yaw_random_exponent", ATTR_TYPE_FLOAT, "1" },
};

// Color Random
constexpr BuiltinSetting_t INITIALIZER_COLOR_RANDOM[]{
	{ "color1", ATTR_TYPE_COLOR, "255 255 255 255" },
	{ "color2", ATTR_TYPE_COLOR, "255 255 255 255" },
	{ "tint_perc", ATTR_TYPE_FLOAT, "0.0" },
	{ "tint control point", ATTR_TYPE_INT, "0" },
	{ "tint clamp min", ATTR_TYPE_COLOR, "0 0 0 0" },
	{ "tint clamp max", ATTR_TYPE_COLOR, "255 255 255 255" },
	{ "tint update movement threshold", ATTR_TYPE_FLOAT, "32" },
};

// Trail Length Random
constexpr BuiltinSetting_t INITIALIZER_TRAIL_LENGTH_RANDOM[]{
	{ "length_min", ATTR_TYPE_FLOAT, "0.1" },
	{ "length_max", ATTR_TYPE_FLOAT, "0.1" },
	{ "length_random_exponent", ATTR_TYPE_FLOAT, "1" },
};

// Sequence Random
constexpr BuiltinSetting_t INITIALIZER_SEQUENCE_RANDOM[]{
	{ "sequence_min", ATTR_TYPE_INT, "0" },
	{ "sequence_max", ATTR_TYPE_INT, "0" },
};

// Position Modify Offset Random
constexpr BuiltinSetting_t INITIALIZER_POSITION_MODIFY_OFFSET_RANDOM[]{
	{ "control_point_number", ATTR_TYPE_INT, "0" },
	{ "offset min", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "offset max", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "offset in local space 0/1", ATTR_TYPE_BOOL, "0" },
	{ "offset proportional to radius 0/1", ATTR_TYPE_BOOL, "0" },
};

// Position Modify Warp Random
constexpr BuiltinSetting_t INITIALIZER_POSITION_MODIFY_WARP_RANDOM[]{
	{ "control point number", ATTR_TYPE_INT, "0" },
	{ "warp min", ATTR_TYPE_VECTOR3, "1 1 1" },
	{ "warp max", ATTR_TYPE_VECTOR3, "1 1 1" },
	{ "warp transition time (treats min/max as start/end sizes)", ATTR_TYPE_FLOAT, "0" },
	{ "warp transition start time", ATTR_TYPE_FLOAT, "0" },
	{ "reverse warp (0/1)", ATTR_TYPE_BOOL, "0" },
};

// Remap Noise to Scalar
constexpr BuiltinSetting_t INITIALIZER_REMAP_NOISE_TO_SCALAR[]{
	{ "time noise coordinate scale", ATTR_TYPE_FLOAT, "0.1" },
	{ "spatial noise coordinate scale", ATTR_TYPE_FLOAT, "0.001" },
	{ "output field", ATTR_TYPE_INT, "3", SETTING_HINT_SCALAR_FIELD },
	{ "time coordinate offset", ATTR_TYPE_FLOAT, "0" },
	{ "spatial coordinate offset", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "absolute value", ATTR_TYPE_BOOL, "0" },
	{ "invert absolute value", ATTR_TYPE_BOOL, "0" },
	{ "output minimum", ATTR_TYPE_FLOAT, "0" },
	{ "output maximum", ATTR_TYPE_FLOAT, "1" },
	{ "world time noise coordinate scale", ATTR_TYPE_FLOAT, "0" },
};

// Velocity Noise
constexpr BuiltinSetting_t INITIALIZER_VELOCITY_NOISE[]{
	{ "Control Point Number", ATTR_TYPE_INT, "0" },
	{ "Time Noise Coordinate Scale", ATTR_TYPE_FLOAT, "1" },
	{ "Spatial Noise Coordinate Scale", ATTR_TYPE_FLOAT, "0.01" },
	{ "Time Coordinate Offset", ATTR_TYPE_FLOAT, "0" },
	{ "Spatial Coordinate Offset", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "Absolute Value", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "Invert Abs Value", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "output minimum", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "output maximum", ATTR_TYPE_VECTOR3, "1 1 1" },
	{ "Apply Velocity in Local Space (0/1)", ATTR_TYPE_BOOL, "0" },
};

// Remap Initial Scalar
constexpr BuiltinSetting_t INITIALIZER_REMAP_INITIAL_SCALAR[]{
	{ "emitter lifetime start time (seconds)", ATTR_TYPE_FLOAT, "-1" },
	{ "emitter lifetime end time (seconds)", ATTR_TYPE_FLOAT, "-1" },
	{ "input field", ATTR_TYPE_INT, "8", SETTING_HINT_SCALAR_FIELD },
	{ "input minimum", ATTR_TYPE_FLOAT, "0" },
	{ "input maximum", ATTR_TYPE_FLOAT, "1" },
	{ "output field", ATTR_TYPE_INT, "3", SETTING_HINT_SCALAR_FIELD },
	{ "output minimum", ATTR_TYPE_FLOAT, "0" },
	{ "output maximum", ATTR_TYPE_FLOAT, "1" },
	{ "output is scalar of initial random range", ATTR_TYPE_BOOL, "0" },
	{ "only active within specified input range", ATTR_TYPE_BOOL, "0" },
};

// Velocity Inherit from Control Point
constexpr BuiltinSetting_t INITIALIZER_VELOCITY_INHERIT_FROM_CONTROL_POINT[]{
	{ "control point number", ATTR_TYPE_INT, "0" },
	{ "velocity scale", ATTR_TYPE_FLOAT, "1" },
};

// Lifetime Pre-Age Noise
constexpr BuiltinSetting_t INITIALIZER_LIFETIME_PRE_AGE_NOISE[]{
	{ "time noise coordinate scale", ATTR_TYPE_FLOAT, "1.0" },
	{ "spatial noise coordinate scale", ATTR_TYPE_FLOAT, "1.0" },
	{ "time coordinate offset", ATTR_TYPE_FLOAT, "0" },
	{ "spatial coordinate offset", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "absolute value", ATTR_TYPE_BOOL, "0" },
	{ "invert absolute value", ATTR_TYPE_BOOL, "0" },
	{ "start age minimum", ATTR_TYPE_FLOAT, "0" },
	{ "start age maximum", ATTR_TYPE_FLOAT, "1" },
};

// Lifetime From Sequence
constexpr BuiltinSetting_t INITIALIZER_LIFETIME_FROM_SEQUENCE[]{
	{ "Frames Per Second", ATTR_TYPE_FLOAT, "30" },
};

// Position In CP Hierarchy
constexpr BuiltinSetting_t INITIALIZER_POSITION_IN_CP_HIERARCHY[]{
	{ "maximum distance", ATTR_TYPE_FLOAT, "0" },
	{ "bulge", ATTR_TYPE_FLOAT, "0" },
	{ "start control point number", ATTR_TYPE_INT, "0" },
	{ "end control point number", ATTR_TYPE_INT, "1" },
	{ "bulge control 0=random 1=orientation of start pnt 2=orientation of end point", ATTR_TYPE_INT, "0" },
	{ "mid point position", ATTR_TYPE_FLOAT, "0.5" },
	{ "growth time", ATTR_TYPE_FLOAT, "0.0" },
	{ "use highest supplied end point", ATTR_TYPE_BOOL, "0" },
	{ "distance_bias", ATTR_TYPE_VECTOR3, "1 1 1" },
	{ "distance_bias_absolute_value", ATTR_TYPE_VECTOR3, "0 0 0" },
};

// Remap Scalar to Vector
constexpr BuiltinSetting_t INITIALIZER_REMAP_SCALAR_TO_VECTOR[]{
	{ "emitter lifetime start time (seconds)", ATTR_TYPE_FLOAT, "-1" },
	{ "emitter lifetime end time (seconds)", ATTR_TYPE_FLOAT, "-1" },
	{ "input field", ATTR_TYPE_INT, "8", SETTING_HINT_SCALAR_FIELD },
	{ "input minimum", ATTR_TYPE_FLOAT, "0" },
	{ "input maximum", ATTR_TYPE_FLOAT, "1" },
	{ "output field", ATTR_TYPE_INT, "0", SETTING_HINT_VECTOR_FIELD },
	{ "output minimum", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "output maximum", ATTR_TYPE_VECTOR3, "1 1 1" },
	{ "output is scalar of initial random range", ATTR_TYPE_BOOL, "0" },
	{ "use local system", ATTR_TYPE_BOOL, "1" },
	{ "control_point_number", ATTR_TYPE_INT, "0" },
};

// Position Along Path Sequential
constexpr BuiltinSetting_t INITIALIZER_POSITION_ALONG_PATH_SEQUENTIAL[]{
	{ "maximum distance", ATTR_TYPE_FLOAT, "0" },
	{ "bulge", ATTR_TYPE_FLOAT, "0" },
	{ "start control point number", ATTR_TYPE_INT, "0" },
	{ "end control point number", ATTR_TYPE_INT, "0" },
	{ "bulge control 0=random 1=orientation of start pnt 2=orientation of end point", ATTR_TYPE_INT, "0" },
	{ "mid point position", ATTR_TYPE_FLOAT, "0.5" },
	{ "particles to map from start to end", ATTR_TYPE_FLOAT, "100" },
	{ "restart behavior (0 = bounce, 1 = loop )", ATTR_TYPE_BOOL, "1" },
	{ "Use sequential CP pairs between start and end point", ATTR_TYPE_BOOL, "0" },
	{ "Save Offset", ATTR_TYPE_BOOL, "0" },
};

// Velocity Repulse from World
constexpr BuiltinSetting_t INITIALIZER_VELOCITY_REPULSE_FROM_WORLD[]{
	{ "minimum velocity", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "maximum velocity", ATTR_TYPE_VECTOR3, "1 1 1" },
	{ "collision group", ATTR_TYPE_STRING, "NONE" },
	{ "control_point_number", ATTR_TYPE_INT, "0" },
	{ "Per Particle World Collision Tests", ATTR_TYPE_BOOL, "0" },
	{ "Use radius for Per Particle Trace Length", ATTR_TYPE_BOOL, "0" },
	{ "Offset instead of accelerate", ATTR_TYPE_BOOL, "0" },
	{ "Offset proportional to radius 0/1", ATTR_TYPE_BOOL, "0" },
	{ "Trace Length", ATTR_TYPE_FLOAT, "64.0" },
	{ "Inherit from Parent", ATTR_TYPE_BOOL, "0" },
	{ "control points to broadcast to children (n + 1)", ATTR_TYPE_INT, "-1" },
	{ "Child Group ID to affect", ATTR_TYPE_INT, "0" },
};

// Rotation Yaw Flip Random
constexpr BuiltinSetting_t INITIALIZER_ROTATION_YAW_FLIP_RANDOM[]{
	{ "Flip Percentage", ATTR_TYPE_FLOAT, ".5" },
};

// Sequence Two Random
constexpr BuiltinSetting_t INITIALIZER_SEQUENCE_TWO_RANDOM[]{
	{ "sequence_min", ATTR_TYPE_INT, "0" },
	{ "sequence_max", ATTR_TYPE_INT, "0" },
};

// Remap Control Point to Scalar
constexpr BuiltinSetting_t INITIALIZER_REMAP_CONTROL_POINT_TO_SCALAR[]{
	{ "emitter lifetime start time (seconds)", ATTR_TYPE_FLOAT, "-1" },
	{ "emitter lifetime end time (seconds)", ATTR_TYPE_FLOAT, "-1" },
	{ "input control point number", ATTR_TYPE_INT, "0" },
	{ "input minimum", ATTR_TYPE_FLOAT, "0" },
	{ "input maximum", ATTR_TYPE_FLOAT, "1" },
	{ "input field 0-2 X/Y/Z", ATTR_TYPE_INT, "0" },
	{ "output field", ATTR_TYPE_INT, "3", SETTING_HINT_SCALAR_FIELD },
	{ "output minimum", ATTR_TYPE_FLOAT, "0" },
	{ "output maximum", ATTR_TYPE_FLOAT, "1" },
	{ "output is scalar of initial random range", ATTR_TYPE_BOOL, "0" },
};

// Remap Control Point to Vector
constexpr BuiltinSetting_t INITIALIZER_REMAP_CONTROL_POINT_TO_VECTOR[]{
	{ "emitter lifetime start time (seconds)", ATTR_TYPE_FLOAT, "-1" },
	{ "emitter lifetime end time (seconds)", ATTR_TYPE_FLOAT, "-1" },
	{ "input control point number", ATTR_TYPE_INT, "0" },
	{ "input minimum", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "input maximum", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "output field", ATTR_TYPE_INT, "0", SETTING_HINT_VECTOR_FIELD },
	{ "output minimum", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "output maximum", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "output is scalar of initial random range", ATTR_TYPE_BOOL, "0" },
	{ "offset position", ATTR_TYPE_BOOL, "0" },
	{ "accelerate position", ATTR_TYPE_BOOL, "0" },
	{ "local space CP", ATTR_TYPE_INT, "-1" },
};

// Position From Parent Particles
constexpr BuiltinSetting_t INITIALIZER_POSITION_FROM_PARENT_PARTICLES[]{
	{ "Inherited Velocity Scale", ATTR_TYPE_FLOAT, "0" },
	{ "Random Parent Particle Distribution", ATTR_TYPE_BOOL, "0" },
};

// Remap Initial Distance to Control Point to Scalar
constexpr BuiltinSetting_t INITIALIZER_REMAP_INITIAL_DISTANCE_TO_CONTROL_POINT_TO_SCALAR[]{
	{ "distance minimum", ATTR_TYPE_FLOAT, "0" },
	{ "distance maximum", ATTR_TYPE_FLOAT, "128" },
	{ "output field", ATTR_TYPE_INT, "3", SETTING_HINT_SCALAR_FIELD },
	{ "output minimum", ATTR_TYPE_FLOAT, "0" },
	{ "output maximum", ATTR_TYPE_FLOAT, "1" },
	{ "control point", ATTR_TYPE_INT, "0" },
	{ "ensure line of sight", ATTR_TYPE_BOOL, "0" },
	{ "LOS collision group", ATTR_TYPE_STRING, "NONE" },
	{ "Maximum Trace Length", ATTR_TYPE_FLOAT, "-1" },
	{ "LOS Failure Scalar", ATTR_TYPE_FLOAT, "0" },
	{ "output is scalar of initial random range", ATTR_TYPE_BOOL, "0" },
	{ "only active within specified distance", ATTR_TYPE_BOOL, "0" },
};

// Remap Distance to Control Point to Vector
constexpr BuiltinSetting_t INITIALIZER_REMAP_DISTANCE_TO_CONTROL_POINT_TO_VECTOR[]{
	{ "distance minimum", ATTR_TYPE_FLOAT, "0" },
	{ "distance maximum", ATTR_TYPE_FLOAT, "128" },
	{ "output field", ATTR_TYPE_INT, "3", SETTING_HINT_VECTOR_FIELD },
	{ "output minimum", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "output maximum", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "control point", ATTR_TYPE_INT, "0" },
	{ "only active within specified distance", ATTR_TYPE_BOOL, "0" },
	{ "local space CP", ATTR_TYPE_INT, "-1" },
};

// Lifetime from Time to Impact
constexpr BuiltinSetting_t INITIALIZER_LIFETIME_FROM_TIME_TO_IMPACT[]{
	{ "trace collision group", ATTR_TYPE_STRING, "NONE" },
	{ "maximum trace length", ATTR_TYPE_FLOAT, "1024" },
	{ "trace offset", ATTR_TYPE_FLOAT, "0" },
	{ "trace recycle tolerance", ATTR_TYPE_FLOAT, "64" },
	{ "maximum points to cache", ATTR_TYPE_INT, "16" },
	{ "bias distance", ATTR_TYPE_VECTOR3, "1 1 1" },
};

// Random position within a curved cylinder
constexpr BuiltinSetting_t INITIALIZER_RANDOM_POSITION_WITHIN_A_CURVED_CYLINDER[]{
	{ "starting control point for cylinder", ATTR_TYPE_INT, "0" },
	{ "maximum end control point for cylinder", ATTR_TYPE_INT, "10" },
	{ "min scale factor for mapping cp velocity to particle velocity", ATTR_TYPE_FLOAT, "0" },
	{ "max scale factor for mapping cp velocity to particle velocity", ATTR_TYPE_FLOAT, "0" },
	{ "min scale factor for particle velocity along path", ATTR_TYPE_FLOAT, "0" },
	{ "max scale factor for particle velocity along path", ATTR_TYPE_FLOAT, "0" },
};

// Assign target CP
constexpr BuiltinSetting_t INITIALIZER_ASSIGN_TARGET_CP[]{
	{ "starting control point", ATTR_TYPE_INT, "0" },
	{ "maximum end control point", ATTR_TYPE_INT, "10" },
};

// Lifetime From Control Point Life Time
constexpr BuiltinSetting_t INITIALIZER_LIFETIME_FROM_CONTROL_POINT_LIFE_TIME[]{
	{ "starting control point", ATTR_TYPE_INT, "0" },
	{ "maximum end control point", ATTR_TYPE_INT, "10" },
};

// Movement Basic
constexpr BuiltinSetting_t OPERATOR_MOVEMENT_BASIC[]{
	{ "gravity", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "drag", ATTR_TYPE_FLOAT, "0" },
	{ "max constraint passes", ATTR_TYPE_INT, "3" },
};

// Lifespan Minimum Velocity Decay
constexpr BuiltinSetting_t OPERATOR_LIFESPAN_MINIMUM_VELOCITY_DECAY[]{
	{ "minimum velocity", ATTR_TYPE_FLOAT, "1" },
};

// Alpha Fade and Decay
constexpr BuiltinSetting_t OPERATOR_ALPHA_FADE_AND_DECAY[]{
	{ "start_alpha", ATTR_TYPE_FLOAT, "1" },
	{ "end_alpha", ATTR_TYPE_FLOAT, "0" },
	{ "start_fade_in_time", ATTR_TYPE_FLOAT, "0" },
	{ "end_fade_in_time", ATTR_TYPE_FLOAT, "0.5" },
	{ "start_fade_out_time", ATTR_TYPE_FLOAT, "0.5" },
	{ "end_fade_out_time", ATTR_TYPE_FLOAT, "1" },
};

// Alpha Fade In Random
constexpr BuiltinSetting_t OPERATOR_ALPHA_FADE_IN_RANDOM[]{
	{ "fade in time min", ATTR_TYPE_FLOAT, ".25" },
	{ "fade in time max", ATTR_TYPE_FLOAT, ".25" },
	{ "fade in time exponent", ATTR_TYPE_FLOAT, "1" },
	{ "proportional 0/1", ATTR_TYPE_BOOL, "1" },
	{ "fade in curve exponent", ATTR_TYPE_FLOAT, "0" },
};

// Alpha Fade Out Random
constexpr BuiltinSetting_t OPERATOR_ALPHA_FADE_OUT_RANDOM[]{
	{ "fade out time min", ATTR_TYPE_FLOAT, ".25" },
	{ "fade out time max", ATTR_TYPE_FLOAT, ".25" },
	{ "fade out time exponent", ATTR_TYPE_FLOAT, "1" },
	{ "proportional 0/1", ATTR_TYPE_BOOL, "1" },
	{ "ease in and out", ATTR_TYPE_BOOL, "1" },
	{ "fade bias", ATTR_TYPE_FLOAT, "0.5" },
};

// Rotation Spin Roll
constexpr BuiltinSetting_t OPERATOR_ROTATION_SPIN_ROLL[]{
	{ "spin_rate_degrees", ATTR_TYPE_INT, "0" },
	{ "spin_stop_time", ATTR_TYPE_FLOAT, "0" },
	{ "spin_rate_min", ATTR_TYPE_INT, "0" },
};

// Rotation Spin Yaw
constexpr BuiltinSetting_t OPERATOR_ROTATION_SPIN_YAW[]{
	{ "yaw_rate_degrees", ATTR_TYPE_INT, "0" },
	{ "yaw_stop_time", ATTR_TYPE_FLOAT, "0" },
	{ "yaw_rate_min", ATTR_TYPE_INT, "0" },
};

// Rotation Orient to 2D Direction
constexpr BuiltinSetting_t OPERATOR_ROTATION_ORIENT_TO_2D_DIRECTION[]{
	{ "Rotation Offset", ATTR_TYPE_FLOAT, "0" },
	{ "Spin Strength", ATTR_TYPE_FLOAT, "1" },
};

// Rotation Orient Relative to CP
constexpr BuiltinSetting_t OPERATOR_ROTATION_ORIENT_RELATIVE_TO_CP[]{
	{ "Rotation Offset", ATTR_TYPE_FLOAT, "0" },
	{ "Spin Strength", ATTR_TYPE_FLOAT, "1" },
	{ "Control Point", ATTR_TYPE_INT, "0" },
};

// Radius Scale
constexpr BuiltinSetting_t OPERATOR_RADIUS_SCALE[]{
	{ "start_time", ATTR_TYPE_FLOAT, "0" },
	{ "end_time", ATTR_TYPE_FLOAT, "1" },
	{ "radius_start_scale", ATTR_TYPE_FLOAT, "1" },
	{ "radius_end_scale", ATTR_TYPE_FLOAT, "1" },
	{ "ease_in_and_out", ATTR_TYPE_BOOL, "0" },
	{ "scale_bias", ATTR_TYPE_FLOAT, "0.5" },
};

// Color Fade
constexpr BuiltinSetting_t OPERATOR_COLOR_FADE[]{
	{ "color_fade", ATTR_TYPE_COLOR, "255 255 255 255" },
	{ "fade_start_time", ATTR_TYPE_FLOAT, "0" },
	{ "fade_end_time", ATTR_TYPE_FLOAT, "1" },
	{ "ease_in_and_out", ATTR_TYPE_BOOL, "1" },
};

// Oscillate Scalar
constexpr BuiltinSetting_t OPERATOR_OSCILLATE_SCALAR[]{
	{ "oscillation field", ATTR_TYPE_INT, "7", SETTING_HINT_SCALAR_FIELD },
	{ "oscillation rate min", ATTR_TYPE_FLOAT, "0" },
	{ "oscillation rate max", ATTR_TYPE_FLOAT, "0" },
	{ "oscillation frequency min", ATTR_TYPE_FLOAT, "1" },
	{ "oscillation frequency max", ATTR_TYPE_FLOAT, "1" },
	{ "proportional 0/1", ATTR_TYPE_BOOL, "1" },
	{ "start time min", ATTR_TYPE_FLOAT, "0" },
	{ "start time max", ATTR_TYPE_FLOAT, "0" },
	{ "end time min", ATTR_TYPE_FLOAT, "1" },
	{ "end time max", ATTR_TYPE_FLOAT, "1" },
	{ "start/end proportional", ATTR_TYPE_BOOL, "1" },
	{ "oscillation multiplier", ATTR_TYPE_FLOAT, "2" },
	{ "oscillation start phase", ATTR_TYPE_FLOAT, ".5" },
};

// Oscillate Vector
constexpr BuiltinSetting_t OPERATOR_OSCILLATE_VECTOR[]{
	{ "oscillation field", ATTR_TYPE_INT, "0", SETTING_HINT_VECTOR_FIELD },
	{ "oscillation rate min", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "oscillation rate max", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "oscillation frequency min", ATTR_TYPE_VECTOR3, "1 1 1" },
	{ "oscillation frequency max", ATTR_TYPE_VECTOR3, "1 1 1" },
	{ "proportional 0/1", ATTR_TYPE_BOOL, "1" },
	{ "start time min", ATTR_TYPE_FLOAT, "0" },
	{ "start time max", ATTR_TYPE_FLOAT, "0" },
	{ "end time min", ATTR_TYPE_FLOAT, "1" },
	{ "end time max", ATTR_TYPE_FLOAT, "1" },
	{ "start/end proportional", ATTR_TYPE_BOOL, "1" },
	{ "oscillation multiplier", ATTR_TYPE_FLOAT, "2" },
	{ "oscillation start phase", ATTR_TYPE_FLOAT, ".5" },
};

// Movement Dampen Relative to Control Point
constexpr BuiltinSetting_t OPERATOR_MOVEMENT_DAMPEN_RELATIVE_TO_CONTROL_POINT[]{
	{ "control_point_number", ATTR_TYPE_INT, "0" },
	{ "falloff range", ATTR_TYPE_FLOAT, "100" },
	{ "dampen scale", ATTR_TYPE_FLOAT, "1" },
};

// Movement Lock to Control Point
constexpr BuiltinSetting_t OPERATOR_MOVEMENT_LOCK_TO_CONTROL_POINT[]{
	{ "control_point_number", ATTR_TYPE_INT, "0" },
	{ "start_fadeout_min", ATTR_TYPE_FLOAT, "1" },
	{ "start_fadeout_max", ATTR_TYPE_FLOAT, "1" },
	{ "start_fadeout_exponent", ATTR_TYPE_FLOAT, "1" },
	{ "end_fadeout_min", ATTR_TYPE_FLOAT, "1" },
	{ "end_fadeout_max", ATTR_TYPE_FLOAT, "1" },
	{ "end_fadeout_exponent", ATTR_TYPE_FLOAT, "1" },
	{ "distance fade range", ATTR_TYPE_FLOAT, "0" },
	{ "lock rotation", ATTR_TYPE_BOOL, "0" },
};

// Movement Lock to Bone
constexpr BuiltinSetting_t OPERATOR_MOVEMENT_LOCK_TO_BONE[]{
	{ "control_point_number", ATTR_TYPE_INT, "0" },
	{ "lifetime fade start", ATTR_TYPE_FLOAT, "0" },
	{ "lifetime fade end", ATTR_TYPE_FLOAT, "0" },
};

// Remap Distance Between Two Control Points to Scalar
constexpr BuiltinSetting_t OPERATOR_REMAP_DISTANCE_BETWEEN_TWO_CONTROL_POINTS_TO_SCALAR[]{
	{ "distance minimum", ATTR_TYPE_FLOAT, "0" },
	{ "distance maximum", ATTR_TYPE_FLOAT, "128" },
	{ "output field", ATTR_TYPE_INT, "3", SETTING_HINT_SCALAR_FIELD },
	{ "output minimum", ATTR_TYPE_FLOAT, "0" },
	{ "output maximum", ATTR_TYPE_FLOAT, "1" },
	{ "starting control point", ATTR_TYPE_INT, "0" },
	{ "ending control point", ATTR_TYPE_INT, "1" },
	{ "ensure line of sight", ATTR_TYPE_BOOL, "0" },
	{ "LOS collision group", ATTR_TYPE_STRING, "NONE" },
	{ "Maximum Trace Length", ATTR_TYPE_FLOAT, "-1" },
	{ "LOS Failure Scalar", ATTR_TYPE_FLOAT, "0" },
	{ "output is scalar of initial random range", ATTR_TYPE_BOOL, "0" },
};

// Remap Distance to Control Point to Scalar
constexpr BuiltinSetting_t OPERATOR_REMAP_DISTANCE_TO_CONTROL_POINT_TO_SCALAR[]{
	{ "distance minimum", ATTR_TYPE_FLOAT, "0" },
	{ "distance maximum", ATTR_TYPE_FLOAT, "128" },
	{ "output field", ATTR_TYPE_INT, "3", SETTING_HINT_SCALAR_FIELD },
	{ "output minimum", ATTR_TYPE_FLOAT, "0" },
	{ "output maximum", ATTR_TYPE_FLOAT, "1" },
	{ "control point", ATTR_TYPE_INT, "0" },
	{ "ensure line of sight", ATTR_TYPE_BOOL, "0" },
	{ "LOS collision group", ATTR_TYPE_STRING, "NONE" },
	{ "Maximum Trace Length", ATTR_TYPE_FLOAT, "-1" },
	{ "LOS Failure Scalar", ATTR_TYPE_FLOAT, "0" },
	{ "output is scalar of initial random range", ATTR_TYPE_BOOL, "0" },
	{ "only active within specified distance", ATTR_TYPE_BOOL, "0" },
};

// Remap Distance to Control Point to Vector
constexpr BuiltinSetting_t OPERATOR_REMAP_DISTANCE_TO_CONTROL_POINT_TO_VECTOR[]{
	{ "distance minimum", ATTR_TYPE_FLOAT, "0" },
	{ "distance maximum", ATTR_TYPE_FLOAT, "128" },
	{ "output field", ATTR_TYPE_INT, "3", SETTING_HINT_VECTOR_FIELD },
	{ "output minimum", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "output maximum", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "control point", ATTR_TYPE_INT, "0" },
	{ "only active within specified distance", ATTR_TYPE_BOOL, "0" },
	{ "local space CP", ATTR_TYPE_INT, "-1" },
};

// Set Control Point To Player
constexpr BuiltinSetting_t OPERATOR_SET_CONTROL_POINT_TO_PLAYER[]{
	{ "Control Point Number", ATTR_TYPE_INT, "1" },
	{ "Control Point Offset", ATTR_TYPE_VECTOR3, "0 0 0" },
};

// Set Control Point To Particles' Center
constexpr BuiltinSetting_t OPERATOR_SET_CONTROL_POINT_TO_PARTICLES_CENTER[]{
	{ "Control Point Number to Set", ATTR_TYPE_INT, "1" },
	{ "Center Offset", ATTR_TYPE_VECTOR3, "0 0 0" },
};

// Set child control points from particle positions
constexpr BuiltinSetting_t OPERATOR_SET_CHILD_CONTROL_POINTS_FROM_PARTICLE_POSITIONS[]{
	{ "Group ID to affect", ATTR_TYPE_INT, "0" },
	{ "First control point to set", ATTR_TYPE_INT, "0" },
	{ "# of control points to set", ATTR_TYPE_INT, "1" },
	{ "first particle to copy", ATTR_TYPE_INT, "0" },
	{ "Set cp orientation for particles", ATTR_TYPE_BOOL, "0" },
	{ "Set cp density for particles", ATTR_TYPE_BOOL, "0" },
	{ "Set cp velocity for particles", ATTR_TYPE_BOOL, "0" },
	{ "Set cp radius for particles", ATTR_TYPE_BOOL, "0" },
};

// Set Control Point Positions
constexpr BuiltinSetting_t OPERATOR_SET_CONTROL_POINT_POSITIONS[]{
	{ "First Control Point Number", ATTR_TYPE_INT, "1" },
	{ "First Control Point Parent", ATTR_TYPE_INT, "0" },
	{ "First Control Point Location", ATTR_TYPE_VECTOR3, "128 0 0" },
	{ "Second Control Point Number", ATTR_TYPE_INT, "2" },
	{ "Second Control Point Parent", ATTR_TYPE_INT, "0" },
	{ "Second Control Point Location", ATTR_TYPE_VECTOR3, "0 128 0" },
	{ "Third Control Point Number", ATTR_TYPE_INT, "3" },
	{ "Third Control Point Parent", ATTR_TYPE_INT, "0" },
	{ "Third Control Point Location", ATTR_TYPE_VECTOR3, "-128 0 0" },
	{ "Fourth Control Point Number", ATTR_TYPE_INT, "4" },
	{ "Fourth Control Point Parent", ATTR_TYPE_INT, "0" },
	{ "Fourth Control Point Location", ATTR_TYPE_VECTOR3, "0 -128 0" },
	{ "Set positions in world space", ATTR_TYPE_BOOL, "0" },
	{ "Control Point to offset positions from", ATTR_TYPE_INT, "0" },
};

// Cull when crossing plane
constexpr BuiltinSetting_t OPERATOR_CULL_WHEN_CROSSING_PLANE[]{
	{ "Control Point for point on plane", ATTR_TYPE_INT, "0" },
	{ "Cull plane offset", ATTR_TYPE_FLOAT, "0" },
	{ "Plane Normal", ATTR_TYPE_VECTOR3, "0 0 1" },
};

// Cull relative to model
constexpr BuiltinSetting_t OPERATOR_CULL_RELATIVE_TO_MODEL[]{
	{ "control_point_number", ATTR_TYPE_INT, "0" },
	{ "use only bounding box", ATTR_TYPE_BOOL, "0" },
	{ "cull outside instead of inside", ATTR_TYPE_BOOL, "0" },
};

// Cull Random
constexpr BuiltinSetting_t OPERATOR_CULL_RANDOM[]{
	{ "Cull Start Time", ATTR_TYPE_FLOAT, "0" },
	{ "Cull End Time", ATTR_TYPE_FLOAT, "1" },
	{ "Cull Time Exponent", ATTR_TYPE_FLOAT, "1" },
	{ "Cull Percentage", ATTR_TYPE_FLOAT, "0.5" },
};

// Color Light from Control Point
constexpr BuiltinSetting_t OPERATOR_COLOR_LIGHT_FROM_CONTROL_POINT[]{
	{ "Light 1 Control Point", ATTR_TYPE_INT, "0" },
	{ "Light 1 Control Point Offset", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "Light 1 Type 0=Point 1=Spot", ATTR_TYPE_BOOL, "0" },
	{ "Light 1 Color", ATTR_TYPE_COLOR, "0 0 0 255" },
	{ "Light 1 Dynamic Light", ATTR_TYPE_BOOL, "0" },
	{ "Light 1 Direction", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "Light 1 50% Distance", ATTR_TYPE_FLOAT, "100" },
	{ "Light 1 0% Distance", ATTR_TYPE_FLOAT, "200" },
	{ "Light 1 Spot Inner Cone", ATTR_TYPE_FLOAT, "30.0" },
	{ "Light 1 Spot Outer Cone", ATTR_TYPE_FLOAT, "45.0" },
	{ "Light 2 Control Point", ATTR_TYPE_INT, "0" },
	{ "Light 2 Control Point Offset", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "Light 2 Type 0=Point 1=Spot", ATTR_TYPE_BOOL, "0" },
	{ "Light 2 Color", ATTR_TYPE_COLOR, "0 0 0 255" },
	{ "Light 2 Dynamic Light", ATTR_TYPE_BOOL, "0" },
	{ "Light 2 Direction", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "Light 2 50% Distance", ATTR_TYPE_FLOAT, "100" },
	{ "Light 2 0% Distance", ATTR_TYPE_FLOAT, "200" },
	{ "Light 2 Spot Inner Cone", ATTR_TYPE_FLOAT, "30.0" },
	{ "Light 2 Spot Outer Cone", ATTR_TYPE_FLOAT, "45.0" },
	{ "Light 3 Control Point", ATTR_TYPE_INT, "0" },
	{ "Light 3 Control Point Offset", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "Light 3 Type 0=Point 1=Spot", ATTR_TYPE_BOOL, "0" },
	{ "Light 3 Color", ATTR_TYPE_COLOR, "0 0 0 255" },
	{ "Light 3 Dynamic Light", ATTR_TYPE_BOOL, "0" },
	{ "Light 3 Direction", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "Light 3 50% Distance", ATTR_TYPE_FLOAT, "100" },
	{ "Light 3 0% Distance", ATTR_TYPE_FLOAT, "200" },
	{ "Light 3 Spot Inner Cone", ATTR_TYPE_FLOAT, "30.0" },
	{ "Light 3 Spot Outer Cone", ATTR_TYPE_FLOAT, "45.0" },
	{ "Light 4 Control Point", ATTR_TYPE_INT, "0" },
	{ "Light 4 Control Point Offset", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "Light 4 Type 0=Point 1=Spot", ATTR_TYPE_BOOL, "0" },
	{ "Light 4 Color", ATTR_TYPE_COLOR, "0 0 0 255" },
	{ "Light 4 Dynamic Light", ATTR_TYPE_BOOL, "0" },
	{ "Light 4 Direction", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "Light 4 50% Distance", ATTR_TYPE_FLOAT, "100" },
	{ "Light 4 0% Distance", ATTR_TYPE_FLOAT, "200" },
	{ "Light 4 Spot Inner Cone", ATTR_TYPE_FLOAT, "30.0" },
	{ "Light 4 Spot Outer Cone", ATTR_TYPE_FLOAT, "45.0" },
	{ "Initial Color Bias", ATTR_TYPE_FLOAT, "0.0" },
	{ "Clamp Minimum Light Value to Initial Color", ATTR_TYPE_BOOL, "0" },
	{ "Clamp Maximum Light Value to Initial Color", ATTR_TYPE_BOOL, "0" },
	{ "Compute Normals From Control Points", ATTR_TYPE_BOOL, "0" },
	{ "Half-Lambert Normals", ATTR_TYPE_BOOL, "1" },
};

// Remap Scalar
constexpr BuiltinSetting_t OPERATOR_REMAP_SCALAR[]{
	{ "input field", ATTR_TYPE_INT, "7", SETTING_HINT_SCALAR_FIELD },
	{ "input minimum", ATTR_TYPE_FLOAT, "0" },
	{ "input maximum", ATTR_TYPE_FLOAT, "1" },
	{ "output field", ATTR_TYPE_INT, "3", SETTING_HINT_SCALAR_FIELD },
	{ "output minimum", ATTR_TYPE_FLOAT, "0" },
	{ "output maximum", ATTR_TYPE_FLOAT, "1" },
};

// Noise Scalar
constexpr BuiltinSetting_t OPERATOR_NOISE_SCALAR[]{
	{ "noise coordinate scale", ATTR_TYPE_FLOAT, "0.1" },
	{ "output field", ATTR_TYPE_INT, "3", SETTING_HINT_SCALAR_FIELD },
	{ "output minimum", ATTR_TYPE_FLOAT, "0" },
	{ "output maximum", ATTR_TYPE_FLOAT, "1" },
};

// Noise Vector
constexpr BuiltinSetting_t OPERATOR_NOISE_VECTOR[]{
	{ "noise coordinate scale", ATTR_TYPE_FLOAT, "0.1" },
	{ "output field", ATTR_TYPE_INT, "6", SETTING_HINT_VECTOR_FIELD },
	{ "output minimum", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "output maximum", ATTR_TYPE_VECTOR3, "1 1 1" },
};

// Movement Match Particle Velocities
constexpr BuiltinSetting_t OPERATOR_MOVEMENT_MATCH_PARTICLE_VELOCITIES[]{
	{ "Direction Matching Strength", ATTR_TYPE_FLOAT, "0.25" },
	{ "Speed Matching Strength", ATTR_TYPE_FLOAT, "0.25" },
	{ "Control Point to Broadcast Speed and Direction To", ATTR_TYPE_INT, "-1" },
};

// Movement Max Velocity
constexpr BuiltinSetting_t OPERATOR_MOVEMENT_MAX_VELOCITY[]{
	{ "Maximum Velocity", ATTR_TYPE_FLOAT, "0" },
};

// Movement Maintain Position Along Path
constexpr BuiltinSetting_t OPERATOR_MOVEMENT_MAINTAIN_POSITION_ALONG_PATH[]{
	{ "maximum distance", ATTR_TYPE_FLOAT, "0" },
	{ "bulge", ATTR_TYPE_FLOAT, "0" },
	{ "start control point number", ATTR_TYPE_INT, "0" },
	{ "end control point number", ATTR_TYPE_INT, "0" },
	{ "bulge control 0=random 1=orientation of start pnt 2=orientation of end point", ATTR_TYPE_INT, "0" },
	{ "mid point position", ATTR_TYPE_FLOAT, "0.5" },
	{ "particles to map from start to end", ATTR_TYPE_FLOAT, "100" },
	{ "restart behavior (0 = bounce, 1 = loop )", ATTR_TYPE_BOOL, "1" },
	{ "cohesion strength", ATTR_TYPE_FLOAT, "1" },
	{ "use existing particle count", ATTR_TYPE_BOOL, "0" },
	{ "control point movement tolerance", ATTR_TYPE_FLOAT, "0" },
};

// Movement Lock to Saved Position Along Path
constexpr BuiltinSetting_t OPERATOR_MOVEMENT_LOCK_TO_SAVED_POSITION_ALONG_PATH[]{
	{ "bulge", ATTR_TYPE_FLOAT, "0" },
	{ "start control point number", ATTR_TYPE_INT, "0" },
	{ "end control point number", ATTR_TYPE_INT, "1" },
	{ "bulge control 0=random 1=orientation of start pnt 2=orientation of end point", ATTR_TYPE_INT, "0" },
	{ "mid point position", ATTR_TYPE_FLOAT, "0.5" },
	{ "Use sequential CP pairs between start and end point", ATTR_TYPE_BOOL, "0" },
};

// Remap Dot Product to Scalar
constexpr BuiltinSetting_t OPERATOR_REMAP_DOT_PRODUCT_TO_SCALAR[]{
	{ "use particle velocity for first input", ATTR_TYPE_BOOL, "0" },
	{ "first input control point", ATTR_TYPE_INT, "0" },
	{ "second input control point", ATTR_TYPE_INT, "0" },
	{ "input minimum (-1 to 1)", ATTR_TYPE_FLOAT, "0" },
	{ "input maximum (-1 to 1)", ATTR_TYPE_FLOAT, "1" },
	{ "output field", ATTR_TYPE_INT, "3", SETTING_HINT_SCALAR_FIELD },
	{ "output minimum", ATTR_TYPE_FLOAT, "0" },
	{ "output maximum", ATTR_TYPE_FLOAT, "1" },
	{ "output is scalar of initial random range", ATTR_TYPE_BOOL, "0" },
	{ "only active within specified input range", ATTR_TYPE_BOOL, "0" },
};

// Remap Control Point to Scalar
constexpr BuiltinSetting_t OPERATOR_REMAP_CONTROL_POINT_TO_SCALAR[]{
	{ "emitter lifetime start time (seconds)", ATTR_TYPE_FLOAT, "-1" },
	{ "emitter lifetime end time (seconds)", ATTR_TYPE_FLOAT, "-1" },
	{ "input control point number", ATTR_TYPE_INT, "0" },
	{ "input minimum", ATTR_TYPE_FLOAT, "0" },
	{ "input maximum", ATTR_TYPE_FLOAT, "1" },
	{ "input field 0-2 X/Y/Z", ATTR_TYPE_INT, "0" },
	{ "output field", ATTR_TYPE_INT, "3", SETTING_HINT_SCALAR_FIELD },
	{ "output minimum", ATTR_TYPE_FLOAT, "0" },
	{ "output maximum", ATTR_TYPE_FLOAT, "1" },
	{ "output is scalar of initial random range", ATTR_TYPE_BOOL, "0" },
};

// Movement Rotate Particle Around Axis
constexpr BuiltinSetting_t OPERATOR_MOVEMENT_ROTATE_PARTICLE_AROUND_AXIS[]{
	{ "Rotation Axis", ATTR_TYPE_VECTOR3, "0 0 1" },
	{ "Rotation Rate", ATTR_TYPE_FLOAT, "180" },
	{ "Control Point", ATTR_TYPE_INT, "0" },
	{ "Use Local Space", ATTR_TYPE_BOOL, "0" },
};

// Movement Follow CP
constexpr BuiltinSetting_t OPERATOR_MOVEMENT_FOLLOW_CP[]{
	{ "starting control point", ATTR_TYPE_INT, "0" },
	{ "maximum end control point", ATTR_TYPE_INT, "10" },
	{ "catch up speed", ATTR_TYPE_FLOAT, "0" },
	{ "lerp to CP radius speed", ATTR_TYPE_FLOAT, "0" },
	{ "update particle life time", ATTR_TYPE_BOOL, "0" },
};

// Remap CP Speed to CP
constexpr BuiltinSetting_t OPERATOR_REMAP_CP_SPEED_TO_CP[]{
	{ "input control point", ATTR_TYPE_INT, "0" },
	{ "input minimum", ATTR_TYPE_FLOAT, "0" },
	{ "input maximum", ATTR_TYPE_FLOAT, "1" },
	{ "output control point", ATTR_TYPE_INT, "-1" },
	{ "Output field 0-2 X/Y/Z", ATTR_TYPE_INT, "0" },
	{ "output minimum", ATTR_TYPE_FLOAT, "0" },
	{ "output maximum", ATTR_TYPE_FLOAT, "1" },
};

// render_animated_sprites
constexpr BuiltinSetting_t RENDERER_RENDER_ANIMATED_SPRITES[]{
	{ "animation rate", ATTR_TYPE_FLOAT, ".1" },
	{ "animation_fit_lifetime", ATTR_TYPE_BOOL, "0" },
	{ "orientation_type", ATTR_TYPE_INT, "0" },
	{ "orientation control point", ATTR_TYPE_INT, "-1" },
	{ "second sequence animation rate", ATTR_TYPE_FLOAT, "0" },
	{ "use animation rate as FPS", ATTR_TYPE_BOOL, "0" },
};

// render_sprite_trail
constexpr BuiltinSetting_t RENDERER_RENDER_SPRITE_TRAIL[]{
	{ "animation rate", ATTR_TYPE_FLOAT, ".1" },
	{ "length fade in time", ATTR_TYPE_FLOAT, "0" },
	{ "max length", ATTR_TYPE_FLOAT, "2000" },
	{ "min length", ATTR_TYPE_FLOAT, "0" },
};

// render_rope
constexpr BuiltinSetting_t RENDERER_RENDER_ROPE[]{
	{ "subdivision_count", ATTR_TYPE_INT, "3" },
	{ "texel_size", ATTR_TYPE_FLOAT, "4.0f" },
	{ "texture_scroll_rate", ATTR_TYPE_FLOAT, "0.0f" },
};

// render_screen_velocity_rotate
constexpr BuiltinSetting_t RENDERER_RENDER_SCREEN_VELOCITY_ROTATE[]{
	{ "rotate_rate(dps)", ATTR_TYPE_FLOAT, "0.0f" },
	{ "forward_angle", ATTR_TYPE_FLOAT, "-90.0f" },
};

// random force
constexpr BuiltinSetting_t FORCE_RANDOM_FORCE[]{
	{ "min force", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "max force", ATTR_TYPE_VECTOR3, "0 0 0" },
};

// twist around axis
constexpr BuiltinSetting_t FORCE_TWIST_AROUND_AXIS[]{
	{ "amount of force", ATTR_TYPE_FLOAT, "0" },
	{ "twist axis", ATTR_TYPE_VECTOR3, "0 0 1" },
	{ "object local space axis 0/1", ATTR_TYPE_BOOL, "0" },
};

// Pull towards control point
constexpr BuiltinSetting_t FORCE_PULL_TOWARDS_CONTROL_POINT[]{
	{ "amount of force", ATTR_TYPE_FLOAT, "0" },
	{ "falloff power", ATTR_TYPE_FLOAT, "2" },
	{ "control point number", ATTR_TYPE_INT, "0" },
};

// Constrain distance to control point
constexpr BuiltinSetting_t CONSTRAINT_CONSTRAIN_DISTANCE_TO_CONTROL_POINT[]{
	{ "minimum distance", ATTR_TYPE_FLOAT, "0" },
	{ "maximum distance", ATTR_TYPE_FLOAT, "100" },
	{ "control point number", ATTR_TYPE_INT, "0" },
	{ "offset of center", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "global center point", ATTR_TYPE_BOOL, "0" },
};

// Prevent passing through a plane
constexpr BuiltinSetting_t CONSTRAINT_PREVENT_PASSING_THROUGH_A_PLANE[]{
	{ "control point number", ATTR_TYPE_INT, "0" },
	{ "plane point", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "plane normal", ATTR_TYPE_VECTOR3, "0 0 1" },
	{ "global origin", ATTR_TYPE_BOOL, "0" },
	{ "global normal", ATTR_TYPE_BOOL, "0" },
};

// Collision via traces
constexpr BuiltinSetting_t CONSTRAINT_COLLISION_VIA_TRACES[]{
	{ "collision mode", ATTR_TYPE_INT, "0" },
	{ "amount of bounce", ATTR_TYPE_FLOAT, "0" },
	{ "amount of slide", ATTR_TYPE_FLOAT, "0" },
	{ "radius scale", ATTR_TYPE_FLOAT, "1" },
	{ "brush only", ATTR_TYPE_BOOL, "0" },
	{ "collision group", ATTR_TYPE_STRING, "NONE" },
	{ "control point offset for fast collisions", ATTR_TYPE_VECTOR3, "0 0 0" },
	{ "control point movement distance tolerance", ATTR_TYPE_FLOAT, "5" },
	{ "kill particle on collision", ATTR_TYPE_BOOL, "0" },
	{ "trace accuracy tolerance", ATTR_TYPE_FLOAT, "24" },
};

// Constrain distance to path between two control points
constexpr BuiltinSetting_t CONSTRAINT_CONSTRAIN_DISTANCE_TO_PATH_BETWEEN_TWO_CONTROL_POINTS[]{
	{ "minimum distance", ATTR_TYPE_FLOAT, "0" },
	{ "maximum distance", ATTR_TYPE_FLOAT, "100" },
	{ "maximum distance middle", ATTR_TYPE_FLOAT, "-1" },
	{ "maximum distance end", ATTR_TYPE_FLOAT, "-1" },
	{ "travel time", ATTR_TYPE_FLOAT, "10" },
	{ "random bulge", ATTR_TYPE_FLOAT, "0" },
	{ "start control point number", ATTR_TYPE_INT, "0" },
	{ "end control point number", ATTR_TYPE_INT, "0" },
	{ "bulge control 0=random 1=orientation of start pnt 2=orientation of end point", ATTR_TYPE_INT, "0" },
	{ "mid point position", ATTR_TYPE_FLOAT, "0.5" },
};

// Older names the game still takes, and the function each one is now.
constexpr std::pair<std::string_view, std::string_view> RENAMES[]{
	{ "alpha_fade", "Alpha Fade and Decay" },
	{ "alpha_fade_in_random", "Alpha Fade In Random" },
	{ "alpha_fade_out_random", "Alpha Fade Out Random" },
	{ "basic_movement", "Movement Basic" },
	{ "color_fade", "Color Fade" },
	{ "controlpoint_light", "Color Light From Control Point" },
	{ "Dampen Movement Relative to Control Point", "Movement Dampen Relative to Control Point" },
	{ "Distance Between Control Points Scale", "Remap Distance Between Two Control Points to Scalar" },
	{ "Distance to Control Points Scale", "Remap Distance to Control Point to Scalar" },
	{ "lifespan_decay", "Lifespan Decay" },
	{ "lock to bone", "Movement Lock to Bone" },
	{ "postion_lock_to_controlpoint", "Movement Lock to Control Point" },
	{ "maintain position along path", "Movement Maintain Position Along Path" },
	{ "lock to saved position along path", "Movement Lock to Saved Position Along Path" },
	{ "Match Particle Velocities", "Movement Match Particle Velocities" },
	{ "Max Velocity", "Movement Max Velocity" },
	{ "noise", "Noise Scalar" },
	{ "vector noise", "Noise Vector" },
	{ "oscillate_scalar", "Oscillate Scalar" },
	{ "oscillate_vector", "Oscillate Vector" },
	{ "Orient Rotation to 2D Direction", "Rotation Orient to 2D Direction" },
	{ "radius_scale", "Radius Scale" },
	{ "Random Cull", "Cull Random" },
	{ "remap_scalar", "Remap Scalar" },
	{ "rotation_movement", "Rotation Basic" },
	{ "rotation_spin", "Rotation Spin Roll" },
	{ "rotation_spin yaw", "Rotation Spin Yaw" },
	{ "alpha_random", "Alpha Random" },
	{ "color_random", "Color Random" },
	{ "create from parent particles", "Position From Parent Particles" },
	{ "Create In Hierarchy", "Position In CP Hierarchy" },
	{ "random position along path", "Position Along Path Random" },
	{ "random position on model", "Position on Model Random" },
	{ "sequential position along path", "Position Along Path Sequential" },
	{ "position_offset_random", "Position Modify Offset Random" },
	{ "position_warp_random", "Position Modify Warp Random" },
	{ "position_within_box", "Position Within Box Random" },
	{ "position_within_sphere", "Position Within Sphere Random" },
	{ "Inherit Velocity", "Velocity Inherit from Control Point" },
	{ "Initial Repulsion Velocity", "Velocity Repulse from World" },
	{ "Initial Velocity Noise", "Velocity Noise" },
	{ "Initial Scalar Noise", "Remap Noise to Scalar" },
	{ "Lifespan from distance to world", "Lifetime from Time to Impact" },
	{ "Pre-Age Noise", "Lifetime Pre-Age Noise" },
	{ "lifetime_random", "Lifetime Random" },
	{ "radius_random", "Radius Random" },
	{ "random yaw", "Rotation Yaw Random" },
	{ "Randomly Flip Yaw", "Rotation Yaw Flip Random" },
	{ "rotation_random", "Rotation Random" },
	{ "rotation_speed_random", "Rotation Speed Random" },
	{ "sequence_random", "Sequence Random" },
	{ "second_sequence_random", "Sequence Two Random" },
	{ "trail_length_random", "Trail Length Random" },
	{ "velocity_random", "Velocity Random" },
};

constexpr BuiltinFunction_t FUNCTIONS[]{
	{ Category_t::CATEGORY_EMITTERS, "emit_continuously", EMITTER_EMIT_CONTINUOUSLY },
	{ Category_t::CATEGORY_EMITTERS, "emit_instantaneously", EMITTER_EMIT_INSTANTANEOUSLY },
	{ Category_t::CATEGORY_EMITTERS, "emit noise", EMITTER_EMIT_NOISE },
	{ Category_t::CATEGORY_INITIALIZERS, "Position Along Path Random", INITIALIZER_POSITION_ALONG_PATH_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Move Particles Between 2 Control Points", INITIALIZER_MOVE_PARTICLES_BETWEEN_2_CONTROL_POINTS },
	{ Category_t::CATEGORY_INITIALIZERS, "Position Within Sphere Random", INITIALIZER_POSITION_WITHIN_SPHERE_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Velocity Random", INITIALIZER_VELOCITY_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Position on Model Random", INITIALIZER_POSITION_ON_MODEL_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Position Within Box Random", INITIALIZER_POSITION_WITHIN_BOX_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Rotation Speed Random", INITIALIZER_ROTATION_SPEED_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Lifetime Random", INITIALIZER_LIFETIME_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Alpha Random", INITIALIZER_ALPHA_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Radius Random", INITIALIZER_RADIUS_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Rotation Random", INITIALIZER_ROTATION_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Rotation Yaw Random", INITIALIZER_ROTATION_YAW_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Color Random", INITIALIZER_COLOR_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Trail Length Random", INITIALIZER_TRAIL_LENGTH_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Sequence Random", INITIALIZER_SEQUENCE_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Position Modify Offset Random", INITIALIZER_POSITION_MODIFY_OFFSET_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Position Modify Warp Random", INITIALIZER_POSITION_MODIFY_WARP_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Remap Noise to Scalar", INITIALIZER_REMAP_NOISE_TO_SCALAR },
	{ Category_t::CATEGORY_INITIALIZERS, "Velocity Noise", INITIALIZER_VELOCITY_NOISE },
	{ Category_t::CATEGORY_INITIALIZERS, "Remap Initial Scalar", INITIALIZER_REMAP_INITIAL_SCALAR },
	{ Category_t::CATEGORY_INITIALIZERS, "Velocity Inherit from Control Point", INITIALIZER_VELOCITY_INHERIT_FROM_CONTROL_POINT },
	{ Category_t::CATEGORY_INITIALIZERS, "Lifetime Pre-Age Noise", INITIALIZER_LIFETIME_PRE_AGE_NOISE },
	{ Category_t::CATEGORY_INITIALIZERS, "Lifetime From Sequence", INITIALIZER_LIFETIME_FROM_SEQUENCE },
	{ Category_t::CATEGORY_INITIALIZERS, "Position In CP Hierarchy", INITIALIZER_POSITION_IN_CP_HIERARCHY },
	{ Category_t::CATEGORY_INITIALIZERS, "Remap Scalar to Vector", INITIALIZER_REMAP_SCALAR_TO_VECTOR },
	{ Category_t::CATEGORY_INITIALIZERS, "Position Along Path Sequential", INITIALIZER_POSITION_ALONG_PATH_SEQUENTIAL },
	{ Category_t::CATEGORY_INITIALIZERS, "Velocity Repulse from World", INITIALIZER_VELOCITY_REPULSE_FROM_WORLD },
	{ Category_t::CATEGORY_INITIALIZERS, "Rotation Yaw Flip Random", INITIALIZER_ROTATION_YAW_FLIP_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Sequence Two Random", INITIALIZER_SEQUENCE_TWO_RANDOM },
	{ Category_t::CATEGORY_INITIALIZERS, "Remap Control Point to Scalar", INITIALIZER_REMAP_CONTROL_POINT_TO_SCALAR },
	{ Category_t::CATEGORY_INITIALIZERS, "Remap Control Point to Vector", INITIALIZER_REMAP_CONTROL_POINT_TO_VECTOR },
	{ Category_t::CATEGORY_INITIALIZERS, "Position From Parent Particles", INITIALIZER_POSITION_FROM_PARENT_PARTICLES },
	{ Category_t::CATEGORY_INITIALIZERS, "Remap Initial Distance to Control Point to Scalar", INITIALIZER_REMAP_INITIAL_DISTANCE_TO_CONTROL_POINT_TO_SCALAR },
	{ Category_t::CATEGORY_INITIALIZERS, "Remap Distance to Control Point to Vector", INITIALIZER_REMAP_DISTANCE_TO_CONTROL_POINT_TO_VECTOR },
	{ Category_t::CATEGORY_INITIALIZERS, "Lifetime from Time to Impact", INITIALIZER_LIFETIME_FROM_TIME_TO_IMPACT },
	{ Category_t::CATEGORY_INITIALIZERS, "Position from Parent Cache", {} },
	{ Category_t::CATEGORY_INITIALIZERS, "Random position within a curved cylinder", INITIALIZER_RANDOM_POSITION_WITHIN_A_CURVED_CYLINDER },
	{ Category_t::CATEGORY_INITIALIZERS, "Assign target CP", INITIALIZER_ASSIGN_TARGET_CP },
	{ Category_t::CATEGORY_INITIALIZERS, "Lifetime From Control Point Life Time", INITIALIZER_LIFETIME_FROM_CONTROL_POINT_LIFE_TIME },
	{ Category_t::CATEGORY_OPERATORS, "Movement Basic", OPERATOR_MOVEMENT_BASIC },
	{ Category_t::CATEGORY_OPERATORS, "Lifespan Decay", {} },
	{ Category_t::CATEGORY_OPERATORS, "Lifespan Minimum Velocity Decay", OPERATOR_LIFESPAN_MINIMUM_VELOCITY_DECAY },
	{ Category_t::CATEGORY_OPERATORS, "Alpha Fade and Decay", OPERATOR_ALPHA_FADE_AND_DECAY },
	{ Category_t::CATEGORY_OPERATORS, "Alpha Fade In Random", OPERATOR_ALPHA_FADE_IN_RANDOM },
	{ Category_t::CATEGORY_OPERATORS, "Alpha Fade Out Random", OPERATOR_ALPHA_FADE_OUT_RANDOM },
	{ Category_t::CATEGORY_OPERATORS, "Rotation Spin Roll", OPERATOR_ROTATION_SPIN_ROLL },
	{ Category_t::CATEGORY_OPERATORS, "Rotation Basic", {} },
	{ Category_t::CATEGORY_OPERATORS, "Rotation Spin Yaw", OPERATOR_ROTATION_SPIN_YAW },
	{ Category_t::CATEGORY_OPERATORS, "Rotation Orient to 2D Direction", OPERATOR_ROTATION_ORIENT_TO_2D_DIRECTION },
	{ Category_t::CATEGORY_OPERATORS, "Rotation Orient Relative to CP", OPERATOR_ROTATION_ORIENT_RELATIVE_TO_CP },
	{ Category_t::CATEGORY_OPERATORS, "Radius Scale", OPERATOR_RADIUS_SCALE },
	{ Category_t::CATEGORY_OPERATORS, "Color Fade", OPERATOR_COLOR_FADE },
	{ Category_t::CATEGORY_OPERATORS, "Oscillate Scalar", OPERATOR_OSCILLATE_SCALAR },
	{ Category_t::CATEGORY_OPERATORS, "Oscillate Vector", OPERATOR_OSCILLATE_VECTOR },
	{ Category_t::CATEGORY_OPERATORS, "Movement Dampen Relative to Control Point", OPERATOR_MOVEMENT_DAMPEN_RELATIVE_TO_CONTROL_POINT },
	{ Category_t::CATEGORY_OPERATORS, "Movement Lock to Control Point", OPERATOR_MOVEMENT_LOCK_TO_CONTROL_POINT },
	{ Category_t::CATEGORY_OPERATORS, "Movement Lock to Bone", OPERATOR_MOVEMENT_LOCK_TO_BONE },
	{ Category_t::CATEGORY_OPERATORS, "Remap Distance Between Two Control Points to Scalar", OPERATOR_REMAP_DISTANCE_BETWEEN_TWO_CONTROL_POINTS_TO_SCALAR },
	{ Category_t::CATEGORY_OPERATORS, "Remap Distance to Control Point to Scalar", OPERATOR_REMAP_DISTANCE_TO_CONTROL_POINT_TO_SCALAR },
	{ Category_t::CATEGORY_OPERATORS, "Remap Distance to Control Point to Vector", OPERATOR_REMAP_DISTANCE_TO_CONTROL_POINT_TO_VECTOR },
	{ Category_t::CATEGORY_OPERATORS, "Set Control Point To Player", OPERATOR_SET_CONTROL_POINT_TO_PLAYER },
	{ Category_t::CATEGORY_OPERATORS, "Set Control Point To Particles' Center", OPERATOR_SET_CONTROL_POINT_TO_PARTICLES_CENTER },
	{ Category_t::CATEGORY_OPERATORS, "Set child control points from particle positions", OPERATOR_SET_CHILD_CONTROL_POINTS_FROM_PARTICLE_POSITIONS },
	{ Category_t::CATEGORY_OPERATORS, "Set Control Point Positions", OPERATOR_SET_CONTROL_POINT_POSITIONS },
	{ Category_t::CATEGORY_OPERATORS, "Cull when crossing plane", OPERATOR_CULL_WHEN_CROSSING_PLANE },
	{ Category_t::CATEGORY_OPERATORS, "Cull relative to model", OPERATOR_CULL_RELATIVE_TO_MODEL },
	{ Category_t::CATEGORY_OPERATORS, "Cull Random", OPERATOR_CULL_RANDOM },
	{ Category_t::CATEGORY_OPERATORS, "Color Light from Control Point", OPERATOR_COLOR_LIGHT_FROM_CONTROL_POINT },
	{ Category_t::CATEGORY_OPERATORS, "Remap Scalar", OPERATOR_REMAP_SCALAR },
	{ Category_t::CATEGORY_OPERATORS, "Noise Scalar", OPERATOR_NOISE_SCALAR },
	{ Category_t::CATEGORY_OPERATORS, "Noise Vector", OPERATOR_NOISE_VECTOR },
	{ Category_t::CATEGORY_OPERATORS, "Movement Match Particle Velocities", OPERATOR_MOVEMENT_MATCH_PARTICLE_VELOCITIES },
	{ Category_t::CATEGORY_OPERATORS, "Movement Max Velocity", OPERATOR_MOVEMENT_MAX_VELOCITY },
	{ Category_t::CATEGORY_OPERATORS, "Movement Maintain Position Along Path", OPERATOR_MOVEMENT_MAINTAIN_POSITION_ALONG_PATH },
	{ Category_t::CATEGORY_OPERATORS, "Movement Lock to Saved Position Along Path", OPERATOR_MOVEMENT_LOCK_TO_SAVED_POSITION_ALONG_PATH },
	{ Category_t::CATEGORY_OPERATORS, "Remap Dot Product to Scalar", OPERATOR_REMAP_DOT_PRODUCT_TO_SCALAR },
	{ Category_t::CATEGORY_OPERATORS, "Remap Control Point to Scalar", OPERATOR_REMAP_CONTROL_POINT_TO_SCALAR },
	{ Category_t::CATEGORY_OPERATORS, "Movement Rotate Particle Around Axis", OPERATOR_MOVEMENT_ROTATE_PARTICLE_AROUND_AXIS },
	{ Category_t::CATEGORY_OPERATORS, "Movement Follow CP", OPERATOR_MOVEMENT_FOLLOW_CP },
	{ Category_t::CATEGORY_OPERATORS, "Remap CP Speed to CP", OPERATOR_REMAP_CP_SPEED_TO_CP },
	{ Category_t::CATEGORY_RENDERERS, "render_animated_sprites", RENDERER_RENDER_ANIMATED_SPRITES, true },
	{ Category_t::CATEGORY_RENDERERS, "render_sprite_trail", RENDERER_RENDER_SPRITE_TRAIL, true },
	{ Category_t::CATEGORY_RENDERERS, "render_rope", RENDERER_RENDER_ROPE },
	{ Category_t::CATEGORY_RENDERERS, "render_screen_velocity_rotate", RENDERER_RENDER_SCREEN_VELOCITY_ROTATE, true },
	{ Category_t::CATEGORY_FORCES, "random force", FORCE_RANDOM_FORCE },
	{ Category_t::CATEGORY_FORCES, "twist around axis", FORCE_TWIST_AROUND_AXIS },
	{ Category_t::CATEGORY_FORCES, "Pull towards control point", FORCE_PULL_TOWARDS_CONTROL_POINT },
	{ Category_t::CATEGORY_CONSTRAINTS, "Constrain distance to control point", CONSTRAINT_CONSTRAIN_DISTANCE_TO_CONTROL_POINT },
	{ Category_t::CATEGORY_CONSTRAINTS, "Prevent passing through a plane", CONSTRAINT_PREVENT_PASSING_THROUGH_A_PLANE },
	{ Category_t::CATEGORY_CONSTRAINTS, "Prevent passing through static part of world", {} },
	{ Category_t::CATEGORY_CONSTRAINTS, "Collision via traces", CONSTRAINT_COLLISION_VIA_TRACES },
	{ Category_t::CATEGORY_CONSTRAINTS, "Constrain distance to path between two control points", CONSTRAINT_CONSTRAIN_DISTANCE_TO_PATH_BETWEEN_TWO_CONTROL_POINTS },
};

//-----------------------------------------------------------------------------
// Purpose: ASCII only, as the names are.
//-----------------------------------------------------------------------------
bool EqualNoCase( std::string_view a, std::string_view b )
{
	return std::ranges::equal( a, b, []( unsigned char x, unsigned char y ) { return std::tolower( x ) == std::tolower( y ); } );
}

//-----------------------------------------------------------------------------
// Purpose: The numbers of a default string, as the game's atof reads each: "4.0f" is 4, and
//          anything it can't read is 0.
//-----------------------------------------------------------------------------
template <std::size_t N>
std::array<float, N> Numbers( std::string_view szText )
{
	std::array<float, N> out{};
	for ( std::size_t i = 0; i < N; i++ )
	{
		const std::size_t nStart = szText.find_first_not_of( ' ' );
		if ( nStart == std::string_view::npos )
		{
			break;
		}
		szText.remove_prefix( nStart );
		std::string_view szWord = szText.substr( 0, szText.find( ' ' ) );
		szText.remove_prefix( szWord.size() );
		if ( szWord.ends_with( 'f' ) )
		{
			szWord.remove_suffix( 1 );
		}
		out[ i ] = ParseNumber<float>( szWord ).value_or( 0.0f );
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const BuiltinSetting_t *FindIn( std::span<const BuiltinSetting_t> settings, std::string_view szName )
{
	const auto it = std::ranges::find_if( settings, [ szName ]( const BuiltinSetting_t &setting ) { return EqualNoCase( setting.m_szName, szName ); } );
	return it == settings.end() ? nullptr : &*it;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: The default as a value of the setting's type, read the way the game reads it.
//-----------------------------------------------------------------------------
dmx::Value_t BuiltinSetting_t::Default() const
{
	switch ( m_nType )
	{
		case ATTR_TYPE_INT: return static_cast<std::int32_t>( Numbers<1>( m_szDefault )[ 0 ] );
		case ATTR_TYPE_FLOAT: return Numbers<1>( m_szDefault )[ 0 ];
		case ATTR_TYPE_BOOL: return Numbers<1>( m_szDefault )[ 0 ] != 0.0f;
		case ATTR_TYPE_STRING: return std::string{ m_szDefault };
		case ATTR_TYPE_COLOR:
		{
			const auto rgba = Numbers<4>( m_szDefault );
			const auto channel = []( float flValue ) { return static_cast<std::uint8_t>( std::clamp( flValue, 0.0f, 255.0f ) ); };
			return dmx::CColor{ channel( rgba[ 0 ] ), channel( rgba[ 1 ] ), channel( rgba[ 2 ] ), channel( rgba[ 3 ] ) };
		}
		case ATTR_TYPE_VECTOR2: return Numbers<2>( m_szDefault );
		case ATTR_TYPE_VECTOR3:
		case ATTR_TYPE_QANGLE: return Numbers<3>( m_szDefault );
		case ATTR_TYPE_VECTOR4: return Numbers<4>( m_szDefault );
		default: return std::monostate{};
	}
}

//-----------------------------------------------------------------------------
// Purpose: Every setting it reads, in the game's order.
//-----------------------------------------------------------------------------
std::vector<const BuiltinSetting_t *> BuiltinFunction_t::Settings() const
{
	std::vector<const BuiltinSetting_t *> out;
	const auto add = [ &out ]( std::span<const BuiltinSetting_t> settings ) {
		for ( const BuiltinSetting_t &setting : settings )
		{
			out.push_back( &setting );
		}
	};
	add( WHEN_IT_RUNS );
	if ( m_bVisibility )
	{
		add( VISIBILITY );
	}
	add( m_Own );
	return out;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const BuiltinSetting_t *BuiltinFunction_t::Setting( std::string_view szName ) const
{
	if ( const BuiltinSetting_t *pFound = FindIn( m_Own, szName ) )
	{
		return pFound;
	}
	if ( m_bVisibility )
	{
		if ( const BuiltinSetting_t *pFound = FindIn( VISIBILITY, szName ) )
		{
			return pFound;
		}
	}
	return FindIn( WHEN_IT_RUNS, szName );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const BuiltinFunction_t> BuiltinFunctions()
{
	return FUNCTIONS;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const BuiltinFunction_t *FindBuiltin( Category_t nCategory, std::string_view szName )
{
	const std::string_view szCurrent = CurrentName( szName );
	const auto it = std::ranges::find_if( FUNCTIONS, [ & ]( const BuiltinFunction_t &function ) { return function.m_nCategory == nCategory && EqualNoCase( function.m_szName, szCurrent ); } );
	return it == std::end( FUNCTIONS ) ? nullptr : &*it;
}

//-----------------------------------------------------------------------------
// Purpose: The name a function is registered under, for the older names the game still takes.
//-----------------------------------------------------------------------------
std::string_view CurrentName( std::string_view szFunction )
{
	const auto it = std::ranges::find_if( RENAMES, [ szFunction ]( const auto &rename ) { return EqualNoCase( rename.first, szFunction ); } );
	return it == std::end( RENAMES ) ? szFunction : it->second;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const BuiltinSetting_t> SystemSettings()
{
	return SYSTEM;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
const BuiltinSetting_t *SystemSetting( std::string_view szName )
{
	return FindIn( SYSTEM, szName );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
std::span<const BuiltinSetting_t> ChildSettings()
{
	return CHILD;
}

//-----------------------------------------------------------------------------
// Purpose: Position, previous position, colour, place in hitbox, trace start, trace end and trace
//          hit normal hold three numbers each.
//-----------------------------------------------------------------------------
bool IsVectorField( int nField )
{
	return nField == 0 || nField == 2 || nField == 6 || nField == 15 || nField == 17 || nField == 18 || nField == 20;
}

} // namespace xv::particles
