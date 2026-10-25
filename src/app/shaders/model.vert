// SPDX-License-Identifier: GPL-3.0-or-later
// Three weighted bones per studio vertex, followed by the orbit camera projection.
#version 440
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in vec3 weights;
layout(location=4) in vec3 bones;
layout(location=5) in vec3 tangent;
layout(location=6) in vec3 bitangent;
layout(location=0) out vec3 v_position;
layout(location=1) out vec3 v_normal;
layout(location=2) out vec3 v_tangent;
layout(location=3) out vec3 v_bitangent;
layout(location=4) out vec2 v_uv;
layout(std140,binding=12) uniform Mesh {
	vec4 bones[768]; // three affine rows per bone, within older OpenGL uniform limits
	vec4 eye;
	vec4 right;
	vec4 up;
	vec4 forward;
	vec4 projection; // focal length in pixels, width, height, y up
	vec4 depth; // zero-to-one depth, surface coordinate scale
} mesh;
vec3 view(vec3 v){return vec3(dot(v,mesh.right.xyz),dot(v,mesh.up.xyz),-dot(v,mesh.forward.xyz));}
vec3 skin(vec3 value,float w){
	float total=weights.x+weights.y+weights.z;
	if(total<=0.0)return value;
	vec3 result=vec3(0.0);
	for(int i=0;i<3;i++){
		int first=int(bones[i])*3;
		vec4 v=vec4(value,w);
		result+=vec3(dot(mesh.bones[first],v),dot(mesh.bones[first+1],v),dot(mesh.bones[first+2],v))*weights[i];
	}
	return result/total;
}
void main(){
	vec3 world=skin(position,1.0);
	vec3 p=view(world-mesh.eye.xyz);
	float w=-p.z;
	float d=100000.0/99999.0*(1.0-1.0/max(w,0.0001));
	float z=mesh.depth.x>0.5?d:d*2.0-1.0;
	gl_Position=vec4(p.x*mesh.projection.x*2.0/mesh.projection.y,p.y*mesh.projection.x*2.0/mesh.projection.z*mesh.projection.w,z*w,w);
	v_position=p*mesh.depth.y+vec3(0,0,3);
	v_normal=view(skin(normal,0.0));
	v_tangent=view(skin(tangent,0.0));
	v_bitangent=view(skin(bitangent,0.0));
	v_uv=uv;
}
