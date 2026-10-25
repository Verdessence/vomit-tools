// SPDX-License-Identifier: GPL-3.0-or-later
// A material drawn the way material::RenderPreview draws it, a pixel at a time: the backdrop, the
// flat square or ball, the shader's look (lit, unlit, sprite, modulate, sky, window, refract,
// water), detail, bump, self-illumination, phong and reflections, then over the backdrop. Every
// step follows its counterpart in core/material/Preview.cpp; keep the two in step.

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Material {
	vec4 square;        // x, y of its top-left (pixels from the target's top-left), its side; target height
	vec4 options;       // 1 for the ball; times the texture repeats; backdrop (0 checkers, 1 dark, 2 light); exposure (stops)
	vec4 light;         // towards the light, normalized; 1 when framebuffer rows run bottom to top
	vec4 look;          // look; detail blend mode; half lambert; self-shadowing bump
	vec4 color;         // $color; $alpha
	vec4 color2;        // $color2; 1 when it tints too (VertexLitGeneric)
	vec4 blending;      // translucent; additive; alpha test; its reference
	vec4 selfIllum;     // tint; 1 when on
	vec4 envMasks;      // base alpha masks it; normal alpha masks it; $envmap on; a cube map is bound
	vec4 envTint;       // tint; contrast
	vec4 envSaturation; // saturation; $blendtintcoloroverbase
	vec4 detailTint;    // tint; factor
	vec4 phong;         // on; exponent; boost; base alpha masks it
	vec4 phongFresnel;  // ranges; the HDR texture is compressed (RGBS)
	vec4 phongTint;     // tint; $overbrightfactor
	vec4 sprite;        // $addself; mod2x; refract amount; the second texture multiplies
	vec4 refractTint;   // tint; the second texture fades in across (WorldVertexTransition)
	vec4 fog;           // water's fog colour; blend tint by base alpha
	vec4 hdr;           // scale; 1 when the HDR light values are bound
	vec4 valid[3];      // which of the ten textures there are, four to a vec4
	vec4 transform[10]; // a b c d: u' = a*u + b*v + tx, v' = c*u + d*v + ty
	vec4 translate[10]; // tx, ty; scale u, v (applied first)
	vec4 faceRight[6];  // the cube map's faces, as sky::Basis has them
	vec4 faceUp[6];
	vec4 faces[2];      // which of its faces have an image
} m;

layout(binding = 1) uniform sampler2D baseTexture;
layout(binding = 2) uniform sampler2D base2Texture;
layout(binding = 3) uniform sampler2D detailTexture;
layout(binding = 4) uniform sampler2D bumpTexture;
layout(binding = 5) uniform sampler2D envMaskTexture;
layout(binding = 6) uniform sampler2D selfIllumMaskTexture;
layout(binding = 7) uniform sampler2D phongExponentTexture;
layout(binding = 8) uniform sampler2D refractTintTexture;
layout(binding = 9) uniform sampler2D hdrTexture;
layout(binding = 10) uniform sampler2D hdrLightTexture;
layout(binding = 11) uniform sampler2DArray environmentTexture;

const int BASE = 0;
const int BASE2 = 1;
const int DETAIL = 2;
const int BUMP = 3;
const int ENV_MASK = 4;
const int SELF_ILLUM_MASK = 5;
const int PHONG_EXPONENT = 6;
const int REFRACT_TINT = 7;
const int HDR = 8;
const int HDR_LIGHT = 9;

const int LOOK_LIT = 0;
const int LOOK_UNLIT = 1;
const int LOOK_SPRITE = 2;
const int LOOK_MODULATE = 3;
const int LOOK_SKY = 4;
const int LOOK_WINDOW = 5;
const int LOOK_REFRACT = 6;
const int LOOK_WATER = 7;

const float PI = 3.14159265358979;
const vec3 EYE = vec3(0.0, 0.0, 3.0);
const vec3 BUMP_BASIS[3] = vec3[3](vec3(0.81649658, 0.0, 0.57735027), vec3(-0.40824829, 0.70710678, 0.57735027), vec3(-0.40824829, -0.70710678, 0.57735027));

float toLinear(float g) {
	g = clamp(g, 0.0, 1.0);
	return g <= 0.04045 ? g / 12.92 : pow((g + 0.055) / 1.055, 2.4);
}

vec3 toLinear(vec3 c) {
	return vec3(toLinear(c.r), toLinear(c.g), toLinear(c.b));
}

float fromLinear(float l) {
	l = clamp(l, 0.0, 1.0);
	return l <= 0.0031308 ? l * 12.92 : 1.055 * pow(l, 1.0 / 2.4) - 0.055;
}

bool has(int i) {
	return m.valid[i >> 2][i & 3] > 0.5;
}

// Sampler_t::At: scaled, then transformed; wrapping, clamping and point sampling are the sampler's.
vec4 at(int i, vec2 uv) {
	uv *= m.translate[i].zw;
	vec4 t = m.transform[i];
	vec2 c = vec2(t.x * uv.x + t.y * uv.y, t.z * uv.x + t.w * uv.y) + m.translate[i].xy;
	switch (i) {
	case BASE: return textureLod(baseTexture, c, 0.0);
	case BASE2: return textureLod(base2Texture, c, 0.0);
	case DETAIL: return textureLod(detailTexture, c, 0.0);
	case BUMP: return textureLod(bumpTexture, c, 0.0);
	case ENV_MASK: return textureLod(envMaskTexture, c, 0.0);
	case SELF_ILLUM_MASK: return textureLod(selfIllumMaskTexture, c, 0.0);
	case PHONG_EXPONENT: return textureLod(phongExponentTexture, c, 0.0);
	case REFRACT_TINT: return textureLod(refractTintTexture, c, 0.0);
	case HDR: return textureLod(hdrTexture, c, 0.0);
	default: return textureLod(hdrLightTexture, c, 0.0);
	}
}

// StandInSky: a plain sky over a dark floor, in the game's world. Linear light.
vec3 standInSky(vec3 d) {
	float z = clamp(d.z, -1.0, 1.0);
	vec3 horizon = vec3(0.55, 0.55, 0.55);
	if (z >= 0.0) {
		return mix(horizon, vec3(0.32, 0.45, 0.70), sqrt(z));
	}
	return mix(horizon, vec3(0.05, 0.045, 0.04), min(1.0, -z * 4.0));
}

// Environment: what the cube map (or the map's, standing in) shows in a direction of the
// preview's view space. Linear light.
vec3 environment(bool useCube, vec3 v) {
	vec3 d = vec3(-v.z, -v.x, v.y); // ToWorld
	if (!useCube || m.envMasks.w < 0.5) {
		return standInSky(d);
	}
	// sky::Sample: the face the direction hits, and where.
	vec3 a = abs(d);
	int f;
	float major;
	if (a.x >= a.y && a.x >= a.z) {
		major = a.x;
		f = d.x >= 0.0 ? 0 : 2;
	} else if (a.y >= a.z) {
		major = a.y;
		f = d.y >= 0.0 ? 1 : 3;
	} else {
		major = a.z;
		f = d.z >= 0.0 ? 4 : 5;
	}
	if (major <= 0.0 || m.faces[f >> 2][f & 3] < 0.5) {
		return vec3(0.0);
	}
	float s = dot(d, m.faceRight[f].xyz) / major;
	float t = dot(d, m.faceUp[f].xyz) / major;
	vec3 colour = textureLod(environmentTexture, vec3((s + 1.0) * 0.5, (1.0 - t) * 0.5, float(f)), 0.0).rgb;
	return toLinear(colour);
}

// BackdropAt, read the way RenderPreview reads it: at the pixel, clamped to the square.
vec3 backdropAt(vec2 p, float size) {
	int backdrop = int(m.options.z + 0.5);
	if (backdrop == 1) {
		return vec3(0.012, 0.012, 0.014);
	}
	if (backdrop == 2) {
		return vec3(0.62);
	}
	int n = int(size);
	int cell = max(8, n / 16);
	ivec2 i = clamp(ivec2(floor(p)), ivec2(0), ivec2(n - 1));
	return ((i.x / cell) + (i.y / cell)) % 2 != 0 ? vec3(0.30) : vec3(0.13);
}

// SurfaceAt: where a pixel lands on the square or the ball.
bool surfaceAt(vec2 p, float size, out vec3 position, out vec3 normal, out vec3 tangent, out vec3 bitangent, out vec2 uv) {
	float repeat = m.options.y;
	if (m.options.x < 0.5) {
		position = vec3(p.x / size * 2.0 - 1.0, 1.0 - p.y / size * 2.0, 0.0);
		normal = vec3(0.0, 0.0, 1.0);
		tangent = vec3(1.0, 0.0, 0.0);
		bitangent = vec3(0.0, -1.0, 0.0);
		uv = p / size * repeat;
		return true;
	}
	float radius = 0.92;
	float px = (p.x / size * 2.0 - 1.0) / radius;
	float py = (1.0 - p.y / size * 2.0) / radius;
	float rest = 1.0 - px * px - py * py;
	if (rest <= 0.0) {
		return false;
	}
	normal = vec3(px, py, sqrt(rest));
	float longitude = atan(normal.x, normal.z);
	tangent = vec3(cos(longitude), 0.0, -sin(longitude));
	position = normal * radius;
	bitangent = normalize(cross(tangent, normal));
	uv = vec2((0.5 + longitude / (2.0 * PI)) * 2.0 * repeat, acos(clamp(normal.y, -1.0, 1.0)) / PI * repeat);
	return true;
}

// CombineDetail: the detail texture combined with the base before lighting.
void combineDetail(vec4 detail, float baseAlpha, inout vec3 albedo, inout float alpha) {
	vec3 raw = detail.rgb * m.detailTint.rgb;
	vec3 colour = toLinear(detail.rgb) * m.detailTint.rgb;
	float f = m.detailTint.w;
	int mode = int(m.look.y);
	if (mode == 0) {
		albedo *= mix(vec3(1.0), raw * 2.0, f);
	} else if (mode == 1) {
		albedo += colour * f;
	} else if (mode == 2) {
		albedo = mix(albedo, colour, f * detail.a);
	} else if (mode == 3) {
		albedo = mix(albedo, colour, f);
		alpha = alpha + (detail.a - alpha) * f;
	} else if (mode == 4) {
		albedo = mix(albedo, colour, f * (1.0 - baseAlpha));
		alpha = detail.a;
	} else if (mode == 7) {
		float pattern = detail.r + (detail.a - detail.r) * baseAlpha;
		albedo *= mix(vec3(1.0), vec3(2.0) * pattern, f);
	} else if (mode == 8) {
		albedo = mix(albedo, albedo * raw, f);
		alpha = alpha + (alpha * detail.a - alpha) * f;
	} else if (mode == 9) {
		alpha = alpha + (alpha * detail.a - alpha) * f;
	} else if (mode == 11) {
		albedo = albedo * ((raw.r + raw.g + raw.b) * (2.0 / 3.0));
	}
}

// DetailAfterLighting: modes 5 and 6, added after lighting.
vec3 detailAfterLighting(vec4 detail) {
	vec3 colour = toLinear(detail.rgb) * m.detailTint.rgb;
	float f = m.detailTint.w;
	int mode = int(m.look.y);
	if (mode == 5) {
		return colour * f;
	}
	if (mode == 6 && f > 0.0) {
		return f >= 0.5 ? clamp((colour - (1.0 - f)) / f, 0.0, 1.0) : clamp(4.0 * f * (colour - 0.5), 0.0, 1.0);
	}
	return vec3(0.0);
}

// Reflection: what $envmap adds, masked and tinted.
vec3 reflection(vec3 normal, vec3 view, vec3 mask) {
	vec3 reflected = normal * (2.0 * dot(normal, view)) - view;
	vec3 env = environment(true, reflected) * mask * m.envTint.rgb;
	env = mix(env, env * env, m.envTint.w);
	float grey = env.r * 0.299 + env.g * 0.587 + env.b * 0.114;
	return mix(vec3(grey), env, m.envSaturation.rgb);
}

// One pixel's result before it goes over the backdrop (Fragment_t).
struct Fragment {
	vec3 color;
	float alpha;
	bool discarded;
	bool additive;
	bool blend;
	bool modulates;
	vec3 modulate;
	bool refracts;
	vec2 refract;
};

// Shade.
Fragment shade(vec3 position, vec3 surfaceNormal, vec3 tangent, vec3 bitangent, vec2 uv, float across, float size) {
	Fragment o;
	o.color = vec3(0.0);
	o.alpha = 1.0;
	o.discarded = false;
	o.additive = false;
	o.blend = false;
	o.modulates = false;
	o.modulate = vec3(1.0);
	o.refracts = false;
	o.refract = vec2(0.0);
	vec3 light = normalize(m.light.xyz);
	vec3 view = normalize(EYE - position);
	int look = int(m.look.x + 0.5);

	if (look == LOOK_SKY && has(HDR)) {
		vec3 hdr;
		if (m.phongFresnel.w > 0.5) {
			// RGBS: colour times alpha times 8, read without sRGB.
			vec4 raw = at(HDR, uv);
			hdr = raw.rgb * (raw.a * 8.0);
		} else {
			hdr = (m.hdr.y > 0.5 ? at(HDR_LIGHT, uv).rgb : toLinear(at(HDR, uv).rgb)) * m.hdr.x;
		}
		o.color = hdr * m.color.rgb * exp2(m.options.w);
		return o;
	}
	if (look == LOOK_WINDOW) {
		vec3 ray = normalize(vec3(position.x, position.y, -1.2));
		o.color = environment(true, ray) * m.color.rgb;
		return o;
	}

	vec4 base = has(BASE) ? at(BASE, uv) : vec4(1.0);
	vec3 albedo = toLinear(base.rgb);
	float alpha = base.a;
	bool twoTextures = m.sprite.w > 0.5;
	bool blendTextures = m.refractTint.w > 0.5;
	if (has(BASE2) && (twoTextures || blendTextures)) {
		vec4 second = at(BASE2, uv);
		albedo = twoTextures ? albedo * toLinear(second.rgb) : mix(albedo, toLinear(second.rgb), clamp(across * 1.4 - 0.2, 0.0, 1.0));
	}
	vec4 detail = has(DETAIL) ? at(DETAIL, uv) : vec4(0.5, 0.5, 0.5, 1.0);
	if (has(DETAIL)) {
		combineDetail(detail, base.a, albedo, alpha);
	}
	vec3 tint = m.color.rgb;
	if (m.color2.w > 0.5) {
		tint *= m.color2.rgb;
	}
	bool blendTint = m.fog.w > 0.5;
	if (blendTint) {
		vec3 tinted = mix(albedo * tint, tint, m.envSaturation.w);
		albedo = mix(albedo, tinted, base.a);
	} else {
		albedo *= tint;
	}
	if (look == LOOK_SPRITE) {
		albedo = albedo * m.phongTint.w + toLinear(base.rgb) * m.sprite.x;
	}
	// Base alpha that masks something else isn't transparency.
	bool selfIllum = m.selfIllum.w > 0.5;
	float opacity = m.color.a * (m.envMasks.x > 0.5 || selfIllum || blendTint ? 1.0 : alpha);

	if (look == LOOK_MODULATE) {
		vec3 factor = m.sprite.y > 0.5 ? base.rgb * 2.0 : base.rgb;
		o.modulates = true;
		o.modulate = mix(vec3(1.0), factor, opacity);
		return o;
	}

	vec3 normal = surfaceNormal;
	vec4 bump = vec4(0.5, 0.5, 1.0, 1.0);
	vec3 tangentNormal = vec3(0.0, 0.0, 1.0);
	bool ssBump = m.look.w > 0.5;
	if (has(BUMP)) {
		bump = at(BUMP, uv);
		if (!ssBump) {
			tangentNormal = normalize(bump.rgb * 2.0 - 1.0);
			normal = normalize(tangent * tangentNormal.x + bitangent * tangentNormal.y + surfaceNormal * tangentNormal.z);
		}
	}

	bool envMap = m.envMasks.z > 0.5;
	if (look == LOOK_REFRACT || look == LOOK_WATER) {
		float shift = m.sprite.z * size * 0.06;
		o.refracts = true;
		o.refract = tangentNormal.xy * shift;
		vec3 tintColour = m.refractTint.rgb;
		if (has(REFRACT_TINT)) {
			tintColour *= toLinear(at(REFRACT_TINT, uv).rgb);
		}
		o.color = look == LOOK_WATER ? m.fog.rgb : tintColour;
		if (look == LOOK_WATER || envMap) {
			// Water reflects the map (a stand-in here) more at grazing angles.
			float fresnel = 0.1 + 0.9 * pow(max(0.0, 1.0 - max(0.0, dot(normal, view))), 5.0);
			vec3 reflected = normal * (2.0 * dot(normal, view)) - view;
			o.modulates = true;
			o.modulate = environment(envMap, reflected) * fresnel;
		}
		return o;
	}

	vec3 colour = albedo;
	if (look == LOOK_LIT) {
		float diffuse = 0.0;
		if (has(BUMP) && ssBump) {
			// Light from each of the three directions, weighted by how much of it reaches the texel.
			vec3 tangentLight = vec3(dot(light, tangent), dot(light, bitangent), dot(light, surfaceNormal));
			for (int i = 0; i < 3; i++) {
				diffuse += bump[i] * max(0.0, dot(tangentLight, BUMP_BASIS[i]));
			}
		} else {
			float d = dot(normal, light);
			diffuse = m.look.z > 0.5 ? (d * 0.5 + 0.5) * (d * 0.5 + 0.5) : max(0.0, d);
		}
		vec3 ambient=vec3(0.22);
#ifdef MODEL_MESH
		vec3 ax=normal.x>=0.0?vec3(0.20,0.21,0.23):vec3(0.16,0.17,0.20);
		vec3 ay=normal.y>=0.0?vec3(0.30,0.32,0.36):vec3(0.10,0.09,0.08);
		vec3 az=normal.z>=0.0?vec3(0.22,0.23,0.25):vec3(0.14,0.15,0.18);
		ambient=ax*normal.x*normal.x+ay*normal.y*normal.y+az*normal.z*normal.z;
#endif
		colour = albedo * (ambient + vec3(0.95 * diffuse));
	}
	int mode = int(m.look.y);
	if (has(DETAIL) && (mode == 5 || mode == 6)) {
		colour += detailAfterLighting(detail);
	}
	if (selfIllum) {
		float mask = has(SELF_ILLUM_MASK) ? at(SELF_ILLUM_MASK, uv).r : base.a;
		colour = mix(colour, m.selfIllum.rgb * albedo, mask);
	}
	if (m.phong.x > 0.5 && look == LOOK_LIT) {
		float mask = m.phong.w > 0.5 ? base.a : (has(BUMP) ? bump.a : 1.0);
		float exponent = has(PHONG_EXPONENT) ? 1.0 + 149.0 * at(PHONG_EXPONENT, uv).r : m.phong.y;
		float f = pow(max(0.0, 1.0 - max(0.0, dot(normal, view))), 2.0);
		vec3 ranges = m.phongFresnel.xyz;
		float fresnel = f > 0.5 ? ranges.y + (ranges.z - ranges.y) * (f - 0.5) * 2.0 : ranges.x + (ranges.y - ranges.x) * f * 2.0;
		vec3 reflectedLight = normal * (2.0 * dot(normal, light)) - light;
		float specular = pow(max(0.0, dot(reflectedLight, view)), max(1.0, exponent)) * (dot(normal, light) > 0.0 ? 1.0 : 0.0);
		colour += m.phongTint.rgb * (specular * m.phong.z * mask * fresnel);
	}
	if (envMap) {
		vec3 mask = vec3(1.0);
		if (has(ENV_MASK)) {
			mask = at(ENV_MASK, uv).rgb;
		}
		if (m.envMasks.x > 0.5) {
			mask *= 1.0 - base.a;
		}
		if (m.envMasks.y > 0.5 && has(BUMP)) {
			mask *= bump.a;
		}
		colour += reflection(normal, view, mask);
	}

	o.color = colour;
	o.alpha = clamp(opacity, 0.0, 1.0);
	if (m.blending.z > 0.5) {
		o.discarded = alpha < m.blending.w;
		o.alpha = 1.0;
	} else if (m.blending.y > 0.5) {
		o.additive = true;
		if (m.blending.x < 0.5 && look != LOOK_SPRITE) {
			o.alpha = 1.0;
		}
	} else if (m.blending.x > 0.5 || m.color.a < 1.0) {
		o.blend = true;
	}
	return o;
}

