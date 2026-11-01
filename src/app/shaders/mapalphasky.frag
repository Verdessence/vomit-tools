// SPDX-License-Identifier: GPL-3.0-or-later
// mapalpha.frag for the HDR lighting preview: what is still see-through (where nothing was drawn)
// is marked as sky as map.frag marks the sky (xv::map::UNMEASURED_ALPHA), through a Max blend that
// leaves both the marks and the opaque alone.
#version 440

layout(location = 0) out vec4 fragColor;

void main() {
	fragColor = vec4(0.0, 0.0, 0.0, 254.0 / 255.0);
}
