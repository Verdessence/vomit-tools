// SPDX-License-Identifier: GPL-3.0-or-later
// Tinted texture, premultiplied: blended over what's behind (One, OneMinusSrcAlpha) or added (One,
// One). addSelf adds the texture on top of blending it, as SpriteCard's $addself does. With
// premultiply at 0 ($addoverblend) the texture's colour isn't weighted by its own alpha, only by
// the particle's, and still darkens what's behind by the alpha.
#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec2 v_addSelf; // $addself, and 1 to premultiply by the texture's alpha

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D tex;

void main() {
	vec4 texel = texture(tex, v_uv);
	float alpha = texel.a * v_color.a;
	float weight = mix(v_color.a, alpha, v_addSelf.y);
	fragColor = vec4(texel.rgb * v_color.rgb * weight * (1.0 + v_addSelf.x), alpha);
}
