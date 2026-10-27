// SPDX-License-Identifier: GPL-3.0-or-later
// Makes the picture opaque once it's drawn. With a 3D skybox the picture starts see-through, so
// the world's sky faces can tell where the skybox showed (map.frag); only alpha is written here.
#version 440

layout(location = 0) out vec4 fragColor;

void main() {
	fragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
