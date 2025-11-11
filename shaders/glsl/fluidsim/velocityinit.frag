#version 450
#extension GL_EXT_debug_printf : enable

// in
layout (location = 0) in vec2 inUV;

// out
layout (location = 0) out vec4 outFragColor;

layout (binding = 0) uniform UBO
{
    vec2 bufferResolution;
} ubo;

const float PI = 3.14159265358979323846;

void main() {
	vec2 xy = (inUV * 2) - 1;
	float x = xy.x;
	float y = xy.y;
	vec2 normalized = xy / length(xy);

	outFragColor.r =  normalized.y;
	outFragColor.g =  -normalized.x;
	return;
}