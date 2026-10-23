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
	vec2 viewport;    // pixels
	float yUp;        // 1 when normalized device coordinates have y up, -1 when down
	float zeroToOne;  // 1 when clip space depth runs 0 to 1, 0 when -1 to 1
} frame;

const float NEAR = 1.0;
const float FAR = 100000.0;

void main() {
	vec2 ndc = vec2(position.x / frame.viewport.x * 2.0 - 1.0, (1.0 - position.y / frame.viewport.y * 2.0) * frame.yUp);
	float w = max(position.z, 0.001);
	// Depth as a perspective projection makes it, so the model hides what's behind it.
	float depth = FAR / (FAR - NEAR) * (1.0 - NEAR / max(w, NEAR)); // 0 at the near plane, 1 at the far
	float z = frame.zeroToOne > 0.5 ? depth : depth * 2.0 - 1.0;
	gl_Position = vec4(ndc * w, z * w, w);
	v_uv = uv;
	v_color = color;
	v_addSelf = addSelf;
}
