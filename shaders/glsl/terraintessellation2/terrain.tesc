#version 450

// in

// out
layout (vertices = 4) out;

layout(set = 0, binding = 0) uniform UBO
{
	mat4 mvp;
} ubo;

void main(void)
{
}