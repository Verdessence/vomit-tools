// SPDX-License-Identifier: GPL-3.0-or-later
// The world grid's plane: a square of it around the point nearest the eye, out past the far plane. The
// fragment shader draws the lines on it (mapgrid.frag).
#version 440

layout(location = 0) out vec2 v_plane; // where on the plane, from the point of it nearest the eye

layout(std140, binding = 0) uniform Frame {
	mat4 viewProjection; // looking from the origin: positions go in relative to the eye
	vec4 eye;            // the eye, to float precision, and the rest of it in eyeLow
	vec4 mode;          // draw mode, selection tint, darker luxels, 1 when the selection's edges show
	vec4 selection;     // rgb
	vec4 selectionEdge; // rgb
	vec4 faceShade;     // xyz
	vec4 state;         // the state texture's width, and the most samples anisotropic filtering takes
	vec4 instance;      // rgb, and how much of it func_instances' contents take
	vec4 pass;          // x 1: the 3D skybox, drawn from its own camera first; y 1: a mesh as read; z 1: sky faces show their own texture
	vec4 invalid;       // rgb, and how much of it broken solids' faces take
	vec4 eyeLow;        // what float precision leaves of the eye after eye
	vec4 lighting;      // the lighting preview's atlas width and height, brightness (times exposure), 1 (LDR) or 2 (HDR) when there is one
	vec4 fogColor;      // the lighting preview's fog: rgb, and the most it covers
	vec4 fogColor2;     // rgb, and 1 to blend to it by the view's direction
	vec4 fogDirection;  // xyz, and 1 when measured from the eye (else along the view)
	vec4 fogRange;      // start, end, 1 when there is fog; w 1: the light alone (mat_fullbright 2)
	vec4 viewForward;   // xyz; w: the lighting preview's mat_monitorgamma / 2.2
	vec4 grid;          // the 3D grid: spacing (0: none), world units a pixel covers per unit of distance, and at any distance
	vec4 gridColor;     // its lines: rgb, and how far toward it a line takes the face
	vec4 gridColor64;   // those every 64 units
	vec4 gridColor1024; // those every 1024
	vec4 worldGrid;     // the world grid's plane: spacing (0: none), how far it reaches from the eye, and where the point of it nearest the eye is along its axes
	vec4 worldGridAt;   // that point, from the eye
	vec4 worldGridU;    // the plane's axes
	vec4 worldGridV;
} frame;

void main() {
	const vec2 corners[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
	v_plane = corners[gl_VertexIndex] * frame.worldGrid.y;
	// Relative to the eye, as map.vert's faces are.
	gl_Position = frame.viewProjection * vec4(frame.worldGridAt.xyz + frame.worldGridU.xyz * v_plane.x + frame.worldGridV.xyz * v_plane.y, 1.0);
}
