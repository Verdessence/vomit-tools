// SPDX-License-Identifier: GPL-3.0-or-later
// The map's edges: every shown object's, in its line colour (the wireframe view), or only the
// selection's and broken solids', over everything (Hammer's selection edges and invalid solids).
#version 440

layout(location = 0) in vec3 position;
layout(location = 5) in uint object;

layout(location = 0) flat out vec4 v_color;

layout(std140, binding = 0) uniform Frame {
	mat4 viewProjection; // looking from the origin: positions go in relative to the eye
	vec4 eye;            // the eye, to float precision, and the rest of it in eyeLow
	vec4 mode;          // draw mode, selection tint, darker luxels, 1 when the selection's edges show
	vec4 selection;     // rgb
	vec4 selectionEdge; // rgb
	vec4 faceShade;     // xyz
	vec4 state;         // the state texture's width, and the most samples anisotropic filtering takes
	vec4 instance;      // rgb, and how much of it func_instances' contents take
	vec4 pass;          // x 1: the 3D skybox, drawn from its own camera first; y 1: a mesh as read
	vec4 invalid;       // rgb, and how much of it broken solids' faces take
	vec4 eyeLow;        // what float precision leaves of the eye after eye
	vec4 lighting;      // the lighting preview's atlas width and height, its brightness, 1 when there is one
} frame;

layout(std140, binding = 1) uniform Material {
	vec4 color;
	vec4 options;
	vec4 size;
	vec4 kind; // z: 1 for a models' mesh; w: 1 to draw only the selection's and broken solids' edges
} material;

layout(binding = 4) uniform sampler2D stateTexture;
layout(binding = 5) uniform sampler2D lineTexture;

void main() {
	int n = int(object);
	int width = int(frame.state.x);
	ivec2 at = ivec2(n % width, n / width);
	int flags = int(texelFetch(stateTexture, at, 0).a * 255.0 + 0.5);
	bool shown = (flags & 1) != 0 && (frame.pass.y < 0.5 || (frame.pass.y < 1.5 ? (flags & 32) == 0 : (flags & 128) == 0));
	// A model that shows its box when selected (flag 64) draws its own edges as unselected ones
	// in a models' mesh; its box, in a mesh of its own, is the selection's.
	bool boxed = (flags & 64) != 0 && material.kind.z > 0.5;
	bool selected = (flags & 2) != 0 && !boxed && frame.mode.w > 0.5;
	bool invalid = (flags & 4) != 0;
	bool onTop = selected || invalid;
	bool selectionOnly = material.kind.w > 0.5;
	bool wireframe = frame.mode.x < 0.5;
	// In the wireframe view the selection is red; elsewhere its edges are Hammer's yellow. Broken
	// solids' are in their own colour.
	v_color = selected ? (wireframe ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(frame.selectionEdge.rgb, 1.0)) : invalid ? vec4(frame.invalid.rgb, 1.0) : vec4(texelFetch(lineTexture, at, 0).rgb, 1.0);
	bool drawn = shown && (selectionOnly ? onTop : !onTop);
	// Relative to the eye, as map.vert's faces are.
	gl_Position = drawn ? frame.viewProjection * vec4((position - frame.eye.xyz) - frame.eyeLow.xyz, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
}
