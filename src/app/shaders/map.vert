// SPDX-License-Identifier: GPL-3.0-or-later
// The map's faces: placed by the camera's view and projection; objects that are hidden are
// moved out of view. Each object's flat colour and flags come from the state texture, a texel an
// object, so selecting and hiding never touch the vertices.
#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texel;
layout(location = 3) in vec2 luxel;
layout(location = 4) in float blend;
layout(location = 5) in uint object;

layout(location = 0) out vec2 v_texel;
layout(location = 1) out vec2 v_luxel;
layout(location = 2) out float v_blend;
layout(location = 3) out vec3 v_normal;
layout(location = 4) flat out vec4 v_state; // flat colour, and the flags in alpha
layout(location = 5) out vec3 v_world;

layout(std140, binding = 0) uniform Frame {
	mat4 viewProjection;
	vec4 eye;
	vec4 mode;          // draw mode, selection tint, darker luxels, lines: 1 for the selection only
	vec4 selection;     // rgb
	vec4 selectionEdge; // rgb
	vec4 faceShade;     // xyz
	vec4 state;         // the state texture's width
	vec4 instance;      // rgb, and how much of it func_instances' contents take
	vec4 pass;          // 1: the 3D skybox, drawn from its own camera first
} frame;

layout(binding = 4) uniform sampler2D stateTexture;

void main() {
	int n = int(object);
	int width = int(frame.state.x);
	v_state = texelFetch(stateTexture, ivec2(n % width, n / width), 0);
	int flags = int(v_state.a * 255.0 + 0.5);
	v_texel = texel;
	v_luxel = luxel;
	v_blend = blend;
	v_normal = normal;
	v_world = position;
	// Not shown (or, while the 3D skybox is drawn, not in its room): outside every clip plane, so
	// the triangle is dropped.
	bool shown = (flags & 1) != 0 && (frame.pass.x < 0.5 || (flags & 16) != 0);
	gl_Position = shown ? frame.viewProjection * vec4(position, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
}
