// SPDX-License-Identifier: GPL-3.0-or-later
// mapsky.frag: the map's 2D sky behind everything in a textured 3D view, drawn first, along each
// pixel's ray as map.frag shows the sky on its sky faces (xv::map's RasterOptions_t::m_bSkyBackground).
#version 440

layout(location = 0) in vec2 v_clip;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform SkyView {
	mat4 clipToWorld; // the view's inverse view-projection, from the eye
	vec4 options;     // x: mat_monitorgamma / 2.2 (0: none); y: the alpha written (254/255 for the HDR preview's exposure to leave out)
} view;

// The map's sky, as sky::Sample reads it (and sky.frag draws it).
layout(std140, binding = 6) uniform Sky {
	vec4 faceRight[6];  // each face's image right edge direction (sky::Basis)
	vec4 faceUp[6];     // and its top
	vec4 transform[6];  // a b c d: u' = a*u + b*v + tx, v' = c*u + d*v + ty
	vec4 translate[6];  // tx, ty; 1 when the face has an image; [0].w: 1 when there's a sky; [1].w: 1 when envAtlas holds env_cubemap's stand-in
} sky;

layout(binding = 7) uniform sampler2D skyFace0;
layout(binding = 8) uniform sampler2D skyFace1;
layout(binding = 9) uniform sampler2D skyFace2;
layout(binding = 10) uniform sampler2D skyFace3;
layout(binding = 11) uniform sampler2D skyFace4;
layout(binding = 12) uniform sampler2D skyFace5;

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

void main() {
	vec4 far = view.clipToWorld * vec4(v_clip, 0.5, 1.0);
	vec3 color = skyColour(far.xyz / far.w);
	if (view.options.x > 0.0 && view.options.x != 1.0) {
		color = pow(max(color, vec3(0.0)), vec3(view.options.x));
	}
	fragColor = vec4(color, view.options.y);
}
