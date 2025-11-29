#version 450

// in
 
// out

layout (set = 0, binding = 0) uniform UBO 
{
	mat4 mvp;
} ubo; 

layout(quads, equal_spacing, cw) in;

void main()
{
}