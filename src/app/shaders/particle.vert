// SPDX-License-Identifier: GPL-3.0-or-later
// Particle quads, already laid out on the screen in pixels. Each corner carries its depth, which
// goes into w so the texture follows the perspective across the quad.
#version 440

layout(location = 0) in vec3 position; // x, y in pixels from the top-left; depth
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;    // tint (overbright included) and alpha
layout(location = 3) in float addSelf;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) out float v_addSelf;

layout(std140, binding = 0) uniform Frame {
	vec2 viewport; // pixels
	float yUp;     // 1 when normalized device coordinates have y up, -1 when down
} frame;

void main() {
	vec2 ndc = vec2(position.x / frame.viewport.x * 2.0 - 1.0, (1.0 - position.y / frame.viewport.y * 2.0) * frame.yUp);
	float w = max(position.z, 0.001);
	gl_Position = vec4(ndc * w, 0.5 * w, w);
	v_uv = uv;
	v_color = color;
	v_addSelf = addSelf;
}
