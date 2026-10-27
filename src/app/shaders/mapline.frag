// SPDX-License-Identifier: GPL-3.0-or-later
// An edge's colour, as the vertex shader chose it.
#version 440

layout(location = 0) flat in vec4 v_color;
layout(location = 0) out vec4 fragColor;

void main() {
	fragColor = v_color;
}
