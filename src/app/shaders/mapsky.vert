// SPDX-License-Identifier: GPL-3.0-or-later
// One triangle over the whole picture, for mapsky.frag, with each corner's place in clip space.
#version 440

layout(location = 0) out vec2 v_clip;

void main() {
	vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); // 0,0  2,0  0,2
	v_clip = corner * 2.0 - 1.0;
	gl_Position = vec4(v_clip, 0.0, 1.0);
}
