// SPDX-License-Identifier: GPL-3.0-or-later
// The map's edges: every shown object's, in its line colour (the wireframe view), or only the
// selection's and broken solids', over everything (Hammer's selection edges and invalid solids).
#version 440

layout(location = 0) in vec3 position;
layout(location = 5) in uint object;

layout(location = 0) flat out vec4 v_color;

layout(std140, binding = 0) uniform Frame {
	mat4 viewProjection;
	vec4 eye;
	vec4 mode;          // draw mode, selection tint, darker luxels, 1 when the selection's edges show
	vec4 selection;     // rgb
	vec4 selectionEdge; // rgb
	vec4 faceShade;     // xyz
	vec4 state;         // the state texture's width, and the most samples anisotropic filtering takes
	vec4 instance;      // rgb, and how much of it func_instances' contents take
	vec4 pass;          // 1: the 3D skybox, drawn from its own camera first
	vec4 invalid;       // rgb, and how much of it broken solids' faces take
} frame;

layout(std140, binding = 1) uniform Material {
	vec4 color;
	vec4 options;
	vec4 size;
	vec4 kind; // w: 1 to draw only the selection's and broken solids' edges
} material;

layout(binding = 4) uniform sampler2D stateTexture;
layout(binding = 5) uniform sampler2D lineTexture;

void main() {
	int n = int(object);
	int width = int(frame.state.x);
	ivec2 at = ivec2(n % width, n / width);
	int flags = int(texelFetch(stateTexture, at, 0).a * 255.0 + 0.5);
	bool shown = (flags & 1) != 0;
	bool selected = (flags & 2) != 0 && frame.mode.w > 0.5;
	bool invalid = (flags & 4) != 0;
	bool onTop = selected || invalid;
	bool selectionOnly = material.kind.w > 0.5;
	bool wireframe = frame.mode.x < 0.5;
	// In the wireframe view the selection is red; elsewhere its edges are Hammer's yellow. Broken
	// solids' are in their own colour.
	v_color = selected ? (wireframe ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(frame.selectionEdge.rgb, 1.0)) : invalid ? vec4(frame.invalid.rgb, 1.0) : vec4(texelFetch(lineTexture, at, 0).rgb, 1.0);
	bool drawn = shown && (selectionOnly ? onTop : !onTop);
	gl_Position = drawn ? frame.viewProjection * vec4(position, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
}
