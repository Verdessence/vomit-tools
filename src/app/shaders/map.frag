// SPDX-License-Identifier: GPL-3.0-or-later
// The colour of a point on a face, step by step as xv::map::ShadeFragment (core/map/Shade.cpp)
// works it out; keep the two in step.
#version 440

layout(location = 0) in vec2 v_texel;
layout(location = 1) in vec2 v_luxel;
layout(location = 2) in float v_blend;
layout(location = 3) in vec3 v_normal;
layout(location = 4) flat in vec4 v_state;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Frame {
	mat4 viewProjection;
	vec4 eye;
	vec4 mode;          // draw mode, selection tint, darker luxels, lines: 1 for the selection only
	vec4 selection;     // rgb
	vec4 selectionEdge; // rgb
	vec4 faceShade;     // xyz
	vec4 state;         // the state texture's width
} frame;

layout(std140, binding = 1) uniform Material {
	vec4 color;   // $color, and $alpha in w
	vec4 options; // alpha test reference (below 0: none), see-through (1 blended, 2 additive), blend to base2, missing
	vec4 size;    // the base texture's size, then base2's, in texels
	vec4 kind;    // 1: the object's colour (entity boxes), has a base texture, has base2
} material;

layout(binding = 2) uniform sampler2D base;
layout(binding = 3) uniform sampler2D base2;

const float MODE_WIREFRAME = 0.0;
const float MODE_FLAT = 1.0;
const float MODE_TEXTURED_SHADED = 3.0;
const float MODE_LIGHTMAP_GRID = 4.0;

float faceShade(vec3 n) {
	return n.x * n.x * frame.faceShade.x + n.y * n.y * frame.faceShade.y + n.z * n.z * frame.faceShade.z;
}

void main() {
	float mode = material.kind.x > 0.5 ? MODE_FLAT : frame.mode.x;
	vec4 color = vec4(v_state.rgb, 1.0);
	vec3 n = normalize(v_normal);
	if (mode != MODE_FLAT && mode != MODE_WIREFRAME) {
		vec2 uv = v_texel / max(material.size.xy, vec2(1.0));
		if (material.options.w > 0.5) {
			// Source's missing texture: magenta and black, four squares a texture.
			ivec2 square = ivec2(floor(uv * 4.0));
			color = ((square.x + square.y) & 1) == 0 ? vec4(1.0, 0.0, 1.0, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
		} else if (material.kind.y > 0.5) {
			color = texture(base, uv);
			if (material.options.z > 0.5 && material.kind.z > 0.5) {
				vec4 second = texture(base2, v_texel / max(material.size.zw, vec2(1.0)));
				color = mix(color, second, v_blend);
			}
		} else {
			color = vec4(1.0);
		}
		if (material.options.x >= 0.0 && color.a < material.options.x) {
			discard;
		}
		color.rgb *= material.color.rgb;
		color.a = material.options.y > 0.5 ? color.a * material.color.a : material.color.a;
		if (mode == MODE_LIGHTMAP_GRID) {
			ivec2 square = ivec2(floor(v_luxel));
			color.rgb *= ((square.x + square.y) & 1) != 0 ? frame.mode.z : 1.0;
		}
	}
	if (mode == MODE_FLAT || mode == MODE_TEXTURED_SHADED) {
		color.rgb *= faceShade(n);
	}
	int flags = int(v_state.a * 255.0 + 0.5);
	if ((flags & 2) != 0) {
		color.rgb = mix(color.rgb, frame.selection.rgb, frame.mode.y);
	}
	fragColor = color;
}
