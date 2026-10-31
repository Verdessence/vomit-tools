// SPDX-License-Identifier: GPL-3.0-or-later
// The colour of a point on a face, step by step as xv::map::ShadeFragment (core/map/Shade.cpp)
// works it out; keep the two in step.
#version 440

layout(location = 0) in vec2 v_texel;
layout(location = 1) in vec2 v_luxel;
layout(location = 2) in float v_blend;
layout(location = 3) in vec3 v_normal;
layout(location = 4) flat in vec4 v_state;
layout(location = 5) in vec3 v_world;
layout(location = 6) in vec4 v_light;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Frame {
	mat4 viewProjection; // looking from the origin: positions go in relative to the eye
	vec4 eye;            // the eye, to float precision, and the rest of it in eyeLow
	vec4 mode;          // draw mode, selection tint, darker luxels, 1 when the selection's edges show
	vec4 selection;     // rgb
	vec4 selectionEdge; // rgb
	vec4 faceShade;     // xyz
	vec4 state;         // the state texture's width, and the most samples anisotropic filtering takes
	vec4 instance;      // rgb, and how much of it func_instances' contents take
	vec4 pass;          // x 1: the 3D skybox, drawn from its own camera first; y 1: a mesh as read
	vec4 invalid;       // rgb, and how much of it broken solids' faces take
	vec4 eyeLow;        // what float precision leaves of the eye after eye
	vec4 lighting;      // the lighting preview's atlas width and height, its brightness, 1 when there is one
} frame;

layout(std140, binding = 1) uniform Material {
	vec4 color;   // $color, and $alpha in w
	vec4 options; // alpha test reference (below 0: none), see-through (1 blended, 2 additive), blend to base2, missing
	vec4 size;    // the base texture's size, then base2's, in texels
	vec4 kind;    // 1: the object's colour (entity boxes), has a base texture, has base2
	vec4 extra;   // 1: a sky face, showing the map's sky; times the object's colour; unlit
	vec4 selfIllum; // $selfillumtint, and 1 when it glows by its base texture's alpha
} material;

layout(binding = 2) uniform sampler2D base;
layout(binding = 3) uniform sampler2D base2;

// The map's sky, as sky::Sample reads it (and sky.frag draws it).
layout(std140, binding = 6) uniform Sky {
	vec4 faceRight[6];  // each face's image right edge direction (sky::Basis)
	vec4 faceUp[6];     // and its top
	vec4 transform[6];  // a b c d: u' = a*u + b*v + tx, v' = c*u + d*v + ty
	vec4 translate[6];  // tx, ty; 1 when the face has an image; [0].w: 1 when there's a sky
} sky;

layout(binding = 7) uniform sampler2D skyFace0;
layout(binding = 8) uniform sampler2D skyFace1;
layout(binding = 9) uniform sampler2D skyFace2;
layout(binding = 10) uniform sampler2D skyFace3;
layout(binding = 11) uniform sampler2D skyFace4;
layout(binding = 12) uniform sampler2D skyFace5;
layout(binding = 13) uniform sampler2D lightmap; // the lighting preview's atlas: linear light, 1 fully lit

const float MODE_WIREFRAME = 0.0;
const float MODE_FLAT = 1.0;
const float MODE_TEXTURED_SHADED = 3.0;
const float MODE_LIGHTMAP_GRID = 4.0;
const float MODE_LIGHTING = 5.0;

// The face a direction hits: rt bk lf ft up dn as 0-5, as sky::Project picks it.
int skyFaceOf(vec3 d, out float major) {
	vec3 a = abs(d);
	if (a.x >= a.y && a.x >= a.z) {
		major = a.x;
		return d.x >= 0.0 ? 0 : 2;
	}
	if (a.y >= a.z) {
		major = a.y;
		return d.y >= 0.0 ? 1 : 3;
	}
	major = a.z;
	return d.z >= 0.0 ? 4 : 5;
}

vec3 skyColour(vec3 d) {
	float major;
	int f = skyFaceOf(d, major);
	if (major <= 0.0 || sky.translate[f].z < 0.5) {
		return vec3(0.0);
	}
	float s = dot(d, sky.faceRight[f].xyz) / major;
	float t = dot(d, sky.faceUp[f].xyz) / major;
	vec2 uv = vec2((s + 1.0) * 0.5, (1.0 - t) * 0.5);
	vec4 m = sky.transform[f];
	vec2 shown = vec2(m.x * uv.x + m.y * uv.y, m.z * uv.x + m.w * uv.y) + sky.translate[f].xy;
	if (f == 0) return textureLod(skyFace0, shown, 0.0).rgb;
	if (f == 1) return textureLod(skyFace1, shown, 0.0).rgb;
	if (f == 2) return textureLod(skyFace2, shown, 0.0).rgb;
	if (f == 3) return textureLod(skyFace3, shown, 0.0).rgb;
	if (f == 4) return textureLod(skyFace4, shown, 0.0).rgb;
	return textureLod(skyFace5, shown, 0.0).rgb;
}

// A texture at a texel position (in its full-size texels) as xv::map::SampleFiltered samples it:
// once at the gradient's mip level, or with anisotropy up to that many times along the pixel's
// footprint at a finer level, averaged.
vec4 sampleFiltered(sampler2D tex, vec2 texel, vec2 size, vec2 across, vec2 down) {
	vec2 full = max(size, vec2(1.0));
	float most = frame.state.y;
	if (most <= 1.0) {
		return texture(tex, texel / full);
	}
	float lengthAcross = length(across);
	float lengthDown = length(down);
	float major = max(lengthAcross, lengthDown);
	float minor = min(lengthAcross, lengthDown);
	vec2 axis = lengthAcross >= lengthDown ? across : down;
	float count = max(1.0, minor > 0.0 ? min(ceil(major / minor), most) : most);
	float span = max(major / count, minor);
	// The level in the texture as uploaded, which may start below its full size.
	float lod = (span > 1.0 ? log2(span) : 0.0) - log2(full.x / float(textureSize(tex, 0).x));
	vec4 sum = vec4(0.0);
	for (int i = 0; i < 16; i++) {
		if (float(i) >= count) {
			break;
		}
		float t = (float(i) + 0.5) / count - 0.5;
		sum += textureLod(tex, (texel + axis * t) / full, lod);
	}
	return sum / count;
}

float faceShade(vec3 n) {
	return n.x * n.x * frame.faceShade.x + n.y * n.y * frame.faceShade.y + n.z * n.z * frame.faceShade.z;
}

void main() {
	// How far the texture moves a pixel across and down, taken here where every pixel of the
	// quad runs the same code.
	vec2 across = dFdx(v_texel);
	vec2 down = dFdy(v_texel);
	float mode = material.kind.x > 0.5 ? MODE_FLAT : frame.mode.x;
	vec4 color = vec4(v_state.rgb, 1.0);
	float glow = 0.0;    // $selfillum's mask: the base texture's alpha
	vec3 albedo = vec3(0.0); // the texture's colour, before light
	vec3 n = normalize(v_normal);
	bool textured = mode != MODE_FLAT && mode != MODE_WIREFRAME;
	if (textured && material.extra.x > 0.5 && sky.translate[0].w > 0.5) {
		// Hammer++ shows the map's sky where its sky faces are, the way the game does.
		color = vec4(skyColour(v_world - frame.eye.xyz), 1.0);
	} else if (textured) {
		vec2 uv = v_texel / max(material.size.xy, vec2(1.0));
		if (material.options.w > 0.5) {
			// Source's missing texture: magenta and black, four squares a texture.
			ivec2 square = ivec2(floor(uv * 4.0));
			color = ((square.x + square.y) & 1) == 0 ? vec4(1.0, 0.0, 1.0, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
		} else if (material.kind.y > 0.5) {
			color = sampleFiltered(base, v_texel, material.size.xy, across, down);
			if (material.options.z > 0.5 && material.kind.z > 0.5) {
				vec4 second = sampleFiltered(base2, v_texel, material.size.zw, across, down);
				color = mix(color, second, v_blend);
			}
		} else {
			color = vec4(1.0);
		}
		if (material.options.x >= 0.0 && color.a < material.options.x) {
			discard;
		}
		color.rgb *= material.color.rgb;
		if (material.extra.y > 0.5) {
			color.rgb *= v_state.rgb;
		}
		glow = material.selfIllum.w > 0.5 ? color.a : 0.0;
		albedo = color.rgb;
		color.a = material.options.y > 0.5 ? color.a * material.color.a : material.color.a;
		if (mode == MODE_LIGHTMAP_GRID && material.extra.z < 0.5) {
			ivec2 square = ivec2(floor(v_luxel));
			color.rgb *= ((square.x + square.y) & 1) != 0 ? frame.mode.z : 1.0;
		}
	}
	// The kinds of xv::map::LightVertex_t: none, the atlas, the vertex's own, fullbright.
	float kind = v_light.w;
	bool vertexLit = kind > 1.5 && kind < 2.5;
	if (mode == MODE_LIGHTING && (material.extra.z < 0.5 || vertexLit) && !(material.extra.x > 0.5 && sky.translate[0].w > 0.5)) {
		vec3 light = vec3(0.0);
		bool lit = false;
		if (kind > 0.5 && kind < 1.5 && frame.lighting.w > 0.5) {
			light = texture(lightmap, v_light.xy / frame.lighting.xy).rgb;
			lit = true;
		} else if (vertexLit) {
			light = v_light.rgb;
			lit = true;
		}
		if (lit) {
			// xv::map::LightFactor: Source's overbright of 2, in gamma 2.2.
			color.rgb *= 2.0 * pow(max(light * frame.lighting.z * 0.5, vec3(0.0)), vec3(1.0 / 2.2));
			// Self-illuminated: toward the texture times the tint, by the mask, as the game.
			color.rgb = mix(color.rgb, albedo * material.selfIllum.rgb, glow);
		} else if (kind < 0.5 || frame.lighting.w < 0.5) {
			// Not baked (yet): the shaded look. Fullbright faces (tool textures) stay as they are.
			color.rgb *= faceShade(n);
		}
	}
	if ((mode == MODE_FLAT || mode == MODE_TEXTURED_SHADED) && material.extra.z < 0.5) {
		color.rgb *= faceShade(n);
	}
	int flags = int(v_state.a * 255.0 + 0.5);
	if ((flags & 8) != 0) {
		color.rgb = mix(color.rgb, frame.instance.rgb, frame.instance.a);
	}
	if ((flags & 4) != 0) {
		color.rgb = mix(color.rgb, frame.invalid.rgb, frame.invalid.a);
	}
	if ((flags & 2) != 0) {
		color.rgb = mix(color.rgb, frame.selection.rgb, frame.mode.y);
	}
	fragColor = color;
}
