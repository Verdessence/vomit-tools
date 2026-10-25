#version 440
#extension GL_GOOGLE_include_directive : require
#include "material-shading.glsl"
void main() {
	vec2 frag = vec2(gl_FragCoord.x, m.light.w > 0.5 ? m.square.w - gl_FragCoord.y : gl_FragCoord.y);
	vec2 p = frag - m.square.xy;
	float size = m.square.z;
	if (p.x < 0.0 || p.y < 0.0 || p.x >= size || p.y >= size) {
		discard; // around the square: the clear colour
	}
	vec3 result = backdropAt(p, size);
	vec3 position;
	vec3 normal;
	vec3 tangent;
	vec3 bitangent;
	vec2 uv;
	if (surfaceAt(p, size, position, normal, tangent, bitangent, uv)) {
		Fragment f = shade(position, normal, tangent, bitangent, uv, p.x / size, size);
		if (f.refracts) {
			vec3 seen = backdropAt(p + f.refract, size);
			result = seen * (int(m.look.x + 0.5) == LOOK_WATER ? mix(vec3(1.0), f.color * 3.0, 0.6) : f.color);
			if (f.modulates) {
				result += f.modulate;
			}
		} else if (f.modulates) {
			result *= f.modulate;
		} else if (f.discarded) {
			// alpha-tested away: the backdrop shows
		} else if (f.additive) {
			result += f.color * f.alpha;
		} else if (f.blend) {
			result = mix(result, f.color, f.alpha);
		} else {
			result = f.color;
		}
	}
	fragColor = vec4(fromLinear(result.r), fromLinear(result.g), fromLinear(result.b), 1.0);
}
