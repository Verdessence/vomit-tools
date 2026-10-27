// SPDX-License-Identifier: GPL-3.0-or-later
// One triangle over the whole picture, for mapalpha.frag.
#version 440

void main() {
	vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); // 0,0  2,0  0,2
	gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
