//===========================================================================//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Purpose: The map views' look and feel, matched to Hammer++. Values marked "Hammer++ settings"
//          are its own defaults, from hammerplusplus_settings.ini; values marked "unconfirmed"
//          aren't in any of its files and are still to be checked against it by eye
//          (docs/plan/hammer-check.md).
//
//===========================================================================//
#pragma once

#include <QColor>

namespace mapstyle
{

// Colours (Hammer++ settings, [Color]).
inline const QColor AXIS{ 0, 100, 100 };
inline const QColor GRID{ 60, 60, 60 };
inline const QColor GRID_DOT{ 115, 115, 115 };
inline const QColor GRID_10{ 115, 115, 115 };    // every GRID_HIGHLIGHT_EVERY lines, and every 64 units
inline const QColor GRID_1024{ 100, 50, 5 };
inline const QColor BACKGROUND{ 0, 0, 0 };
inline const QColor BRUSH{ 255, 255, 255 };
inline const QColor ENTITY{ 220, 30, 220 };
inline const QColor SELECTION{ 255, 0, 0 };
inline const QColor VERTEX{ 255, 255, 255 };
inline const QColor TOOL_HANDLE{ 255, 255, 255 };
inline const QColor TOOL_SELECTION{ 255, 255, 0 };
inline const QColor MODEL_BOUNDING_BOX{ 0, 200, 255 };
inline const QColor BACKGROUND_3D{ 30, 30, 30 };
inline const QColor SELECTION_3D{ 220, 0, 0 };
inline const QColor SELECTION_EDGE_3D{ 255, 255, 0 };
inline const QColor FILL_SELECTION{ 255, 255, 255, 10 };
inline const QColor INVALID{ 255, 0, 0 };
// Func_instance's VDC article: instances are drawn with an orange tint by default.
inline const QColor INSTANCE_TINT{ 255, 140, 0 }; // unconfirmed: the exact orange
inline constexpr float INSTANCE_TINT_AMOUNT = 0.35f; // how much of it the contents take; unconfirmed

// 2D views (Hammer++ settings, [View2D]).
inline constexpr int GRID_DEFAULT_SIZE = 64;
inline constexpr int GRID_HIGHLIGHT_EVERY = 8; // GridHighSpec
inline constexpr int HANDLE_SIZE = 5;          // [General] HandleSize, in pixels
inline constexpr double HIDE_GRID_BELOW_PIXELS = 4.0; // HideSmallGrid; unconfirmed: the threshold
inline constexpr int GRID_SMALLEST = 1;
inline constexpr int GRID_LARGEST = 512;
inline constexpr double WORLD_LIMIT = 16384.0;    // the scroll range, the @mapsize of Source games
inline constexpr double ZOOM_SMALLEST = 0.004;    // pixels per unit: the whole +/-16384 in about 130 pixels; unconfirmed
inline constexpr double ZOOM_LARGEST = 256.0;     // unconfirmed
inline constexpr double WHEEL_ZOOM_STEP = 1.2;    // per wheel notch; unconfirmed
inline constexpr double KEY_ZOOM_STEP = 2.0;      // numpad +/-; unconfirmed
inline constexpr int SCROLL_STEP_PIXELS = 64;     // arrow keys; unconfirmed
inline constexpr int VERTEX_SIZE = 2;             // DrawVertices dots, in pixels; unconfirmed
inline constexpr int MODELS_FROM_PIXELS = 24;     // models drawn in 2D when at least this big; unconfirmed
inline constexpr int NAMES_FROM_PIXELS = 32;      // entity names shown at this zoom; unconfirmed
// The 3D view's camera, drawn in the 2D views (Hammer++ settings: CameraFrustum=0, so without
// its frustum): a ring at the eye and a line the way it looks.
inline const QColor CAMERA{ 0, 255, 255 };        // unconfirmed
inline constexpr int CAMERA_EYE_PIXELS = 4;       // the ring's radius; unconfirmed
inline constexpr int CAMERA_LOOK_PIXELS = 32;     // the line when it looks across the view; unconfirmed

// 3D view (Hammer++ settings, [View3D]).
inline constexpr float FOV = 90.0f;               // horizontal, in degrees; unconfirmed: horizontal or vertical
inline constexpr float NEAR_PLANE = 4.0f;
inline constexpr float FAR_PLANE = 32768.0f;      // ClippingPlane: the render distance
inline constexpr float FAR_PLANE_STEP = 1.25f;    // keys 1 and 2; unconfirmed
inline constexpr float MODEL_DISTANCE = 32768.0f;
inline constexpr float DETAIL_DISTANCE = 1500.0f;
inline constexpr float MOUSE_SENSITIVITY = 0.4f;  // degrees per pixel at Hammer's default; unconfirmed: the scale
inline constexpr float MAX_FORWARD_SPEED = 1000.0f; // units a second
inline constexpr float MAX_SPEED_TIME = 0.5f;     // seconds to reach it
inline constexpr float WHEEL_STEP = 64.0f;        // units a wheel notch moves the camera; unconfirmed
inline constexpr float PAN_SCALE = 1.0f;          // units a pixel of right-drag moves; unconfirmed
inline constexpr float FACE_SHADE[ 3 ] = { 0.65f, 0.8f, 1.0f }; // textured shaded: by facing X, Y, Z; unconfirmed

} // namespace mapstyle
