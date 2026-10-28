//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The map views' look and feel, matched to Hammer++. Values marked "Hammer++ settings"
//          are its own defaults, from hammerplusplus_settings.ini; the rest were checked against
//          it by eye (docs/plan/hammer-check.md). Most are only defaults: Preferences > Maps
//          (MapPreferences_t) changes them.
//
//===========================================================================//
#pragma once

#include <QColor>

namespace mapstyle
{

// Colours (Hammer++ settings, [Color]).
inline const QColor AXIS{ 0, 100, 100 };
inline const QColor GRID{ 60, 60, 60 };
inline const QColor GRID_10{ 115, 115, 115 };    // every GRID_HIGHLIGHT_EVERY lines, and every 64 units
inline const QColor GRID_1024{ 100, 50, 5 };
inline const QColor BACKGROUND{ 0, 0, 0 };
inline const QColor BRUSH{ 255, 255, 255 };
inline const QColor ENTITY{ 220, 30, 220 };
inline const QColor SELECTION{ 255, 0, 0 };
inline const QColor VERTEX{ 255, 255, 255 };
inline const QColor CENTER_HANDLE{ 255, 255, 0 }; // the × on every object
inline const QColor TOOL_HANDLE{ 255, 255, 255 };
inline const QColor TOOL_SELECTION{ 255, 255, 0 };
inline const QColor TOOL_BLOCK{ 255, 255, 255 };  // the block tool's box, the entity tool's marker
inline constexpr double ROTATION_SNAP = 15.0;      // Hammer++'s RotationDegreeSnap
inline const QColor BACKGROUND_3D{ 30, 30, 30 };
inline const QColor SELECTION_3D{ 220, 0, 0 };
inline const QColor SELECTION_EDGE_3D{ 255, 255, 0 };
inline const QColor FILL_SELECTION{ 255, 255, 255, 10 };
// Broken solids: red edges over everything (2D and 3D), and red-tinted faces.
inline const QColor INVALID{ 255, 0, 0 };
inline constexpr float INVALID_TINT = 0.5f;
// Func_instance's VDC article: instances are drawn with an orange tint by default.
inline const QColor INSTANCE_TINT{ 255, 140, 0 };
inline constexpr float INSTANCE_TINT_AMOUNT = 0.35f; // how much of it the contents take
inline constexpr float SELECTION_TINT = 0.5f;        // how far selected faces go toward SELECTION_3D

// 2D views (Hammer++ settings, [View2D]).
inline constexpr int GRID_DEFAULT_SIZE = 64;
inline constexpr int GRID_HIGHLIGHT_EVERY = 8; // GridHighSpec
inline constexpr int HANDLE_SIZE = 5;          // [General] HandleSize, in pixels
inline constexpr double HIDE_GRID_BELOW_PIXELS = 4.0; // HideSmallGrid
inline constexpr int GRID_SMALLEST = 1;
inline constexpr int GRID_LARGEST = 512;
inline constexpr double WORLD_LIMIT = 16384.0;    // where the grid and scrolling stop without an FGD @mapsize (Source's own limit)
inline constexpr double ZOOM_SMALLEST = 0.004;    // pixels per unit: the whole +/-16384 in about 130 pixels
inline constexpr double ZOOM_LARGEST = 256.0;
inline constexpr double WHEEL_ZOOM_STEP = 1.2;    // per wheel notch
inline constexpr double KEY_ZOOM_STEP = 2.0;      // numpad +/-
inline constexpr int SCROLL_STEP_PIXELS = 64;     // arrow keys
inline constexpr int VERTEX_SIZE = 2;             // DrawVertices dots, in pixels
inline constexpr int VERTICES_FROM_PIXELS = 8;    // on brushes at least this big
inline constexpr int CENTER_HANDLES_FROM_PIXELS = 15; // the × on objects at least this big
inline constexpr int MODELS_FROM_PIXELS = 24;     // models drawn in 2D when at least this big
inline constexpr int NAMES_FROM_PIXELS = 32;      // entity names shown once 16 units are this many pixels
// The 3D view's camera, drawn in the 2D views as Blender draws one: a pyramid from the eye to a
// rectangle the shape of the 3D view, with a triangle on top for which way is up.
inline const QColor CAMERA{ 255, 0, 0 };
inline constexpr int CAMERA_PIXELS = 48;          // from the eye to the rectangle

// The 3D view's overlay: the axes gizmo in the corner (Hammer++ settings: GizmoCameraAxes=1) and
// the statistics' backing.
inline const QColor AXIS_X{ 255, 64, 64 };
inline const QColor AXIS_Y{ 64, 255, 64 };
inline const QColor AXIS_Z{ 64, 128, 255 };
inline constexpr int GIZMO_SIZE = 28;             // the axes' length in pixels
inline constexpr double GIZMO_PIXELS = 90.0;      // the selection gizmo's axes, on screen
inline constexpr double GIZMO_SLACK = 7.0;        // how near the mouse must be to an axis
inline constexpr int GIZMO_MARGIN = 14;           // from the bottom left corner
inline const QColor OVERLAY_BACKING{ 0, 0, 0, 150 };

// 3D view (Hammer++ settings, [View3D]).
inline constexpr float FOV = 90.0f;               // horizontal, in degrees
inline constexpr float NEAR_PLANE = 4.0f;
inline constexpr float FAR_PLANE = 32768.0f;      // ClippingPlane: the render distance
inline constexpr float FAR_PLANE_STEP = 1.25f;    // keys 1 and 2
inline constexpr float MODEL_DISTANCE = 32768.0f;
inline constexpr float DETAIL_DISTANCE = 1500.0f;
inline constexpr float MOUSE_SENSITIVITY = 0.4f;  // degrees per pixel
inline constexpr float MAX_FORWARD_SPEED = 1000.0f; // units a second
inline constexpr float MAX_SPEED_TIME = 0.5f;     // seconds to reach it
inline constexpr float WHEEL_STEP = 64.0f;        // units a wheel notch moves the camera
inline constexpr float PAN_SCALE = 1.0f;          // units a pixel of right-drag moves
inline constexpr float FACE_SHADE[ 3 ] = { 0.65f, 0.8f, 1.0f }; // textured shaded: by facing X, Y, Z
inline constexpr float LIGHTMAP_GRID_SHADE = 0.75f; // the lightmap grid's darker squares: how bright

} // namespace mapstyle
