// SPDX-License-Identifier: GPL-3.0-or-later
// The sky as a player sees it, pixel by pixel as sky::Render does it: the ray through the pixel,
// the face it hits and where (sky::Project), that face's texture transform, and a bilinear read,
// clamped or repeating as the face says. Faces' edges show in a bright line when asked.
#version 440

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Sky {
	vec4 forward;        // xyz; w: tangent of half the horizontal field of view
	vec4 right;          // xyz; w: tangent of half the vertical one
	vec4 up;             // xyz
	vec4 size;           // width, height (pixels); 1 when framebuffer rows run bottom to top; 1 to draw edges
	vec4 faceRight[6];   // each face's image right edge direction (sky::Basis)
	vec4 faceUp[6];      // and its top
	vec4 transform[6];   // a b c d: u' = a*u + b*v + tx, v' = c*u + d*v + ty
	vec4 translate[6];   // tx, ty; 1 when the face has an image
} sky;

layout(binding = 1) uniform sampler2D face0;
layout(binding = 2) uniform sampler2D face1;
layout(binding = 3) uniform sampler2D face2;
layout(binding = 4) uniform sampler2D face3;
layout(binding = 5) uniform sampler2D face4;
layout(binding = 6) uniform sampler2D face5;

// The direction through a point of the picture (pixels from the top-left).
vec3 rayAt(vec2 pixel) {
	float sx = (2.0 * pixel.x / max(sky.size.x, 1.0) - 1.0) * sky.forward.w;
	float sy = (1.0 - 2.0 * pixel.y / max(sky.size.y, 1.0)) * sky.right.w;
	return sky.forward.xyz + sky.right.xyz * sx + sky.up.xyz * sy;
}

// The face a direction hits: rt bk lf ft up dn as 0-5, as sky::Project picks it.
int faceOf(vec3 d, out float major) {
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

vec4 readFace(int f, vec2 uv) {
	if (f == 0) return textureLod(face0, uv, 0.0);
	if (f == 1) return textureLod(face1, uv, 0.0);
	if (f == 2) return textureLod(face2, uv, 0.0);
	if (f == 3) return textureLod(face3, uv, 0.0);
	if (f == 4) return textureLod(face4, uv, 0.0);
	return textureLod(face5, uv, 0.0);
}

void main() {
	vec2 pixel = vec2(gl_FragCoord.x, sky.size.z > 0.5 ? sky.size.y - gl_FragCoord.y : gl_FragCoord.y);
	vec3 d = rayAt(pixel);
	float major;
	int f = faceOf(d, major);
	vec3 colour = vec3(0.0);
	if (major > 0.0 && sky.translate[f].z > 0.5) {
		float s = dot(d, sky.faceRight[f].xyz) / major;
		float t = dot(d, sky.faceUp[f].xyz) / major;
		vec2 uv = vec2((s + 1.0) * 0.5, (1.0 - t) * 0.5);
		vec4 m = sky.transform[f];
		vec2 shown = vec2(m.x * uv.x + m.y * uv.y, m.z * uv.x + m.w * uv.y) + sky.translate[f].xy;
		colour = readFace(f, shown).rgb;
	}
	if (sky.size.w > 0.5) {
		// Within about a pixel of a face's border: the edge.
		float unused;
		if (faceOf(rayAt(pixel + vec2(1.0, 0.0)), unused) != f || faceOf(rayAt(pixel + vec2(0.0, 1.0)), unused) != f) {
			colour = vec3(255.0, 200.0, 0.0) / 255.0;
		}
	}
	fragColor = vec4(colour, 1.0);
}
