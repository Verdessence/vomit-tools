// SPDX-License-Identifier: GPL-3.0-or-later
// Studio meshes share the material preview's shading functions.
#version 440
#extension GL_GOOGLE_include_directive : require
#define MODEL_MESH
#include "material-shading.glsl"
layout(location=0) in vec3 v_position;
layout(location=1) in vec3 v_normal;
layout(location=2) in vec3 v_tangent;
layout(location=3) in vec3 v_bitangent;
layout(location=4) in vec2 v_uv;
void main(){
	Fragment f=shade(v_position,normalize(v_normal),normalize(v_tangent),normalize(v_bitangent),v_uv,0.5,m.square.z);
	if(f.discarded)discard;
	fragColor=vec4(fromLinear(f.color.r),fromLinear(f.color.g),fromLinear(f.color.b),(f.blend||f.additive)?f.alpha:1.0);
}
