#version 450

// in
layout (location = 0) in vec3 inPos;

// out
layout (location = 0) out vec3 outUVW;

layout (binding = 0) uniform UBO 
{
	mat4 mvp;
} ubo;

void main() 
{
	outUVW = inPos;
	outUVW.xy *= -1.0;
	gl_Position = ubo.mvp * vec4(inPos.xyz, 1.0);
}
