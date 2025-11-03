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

void main() {

	float x = gl_FragCoord.x;
	float y = gl_FragCoord.y;

	x = floor(x / 20.f);
	y = floor(y / 20.f);

	if (mod(x + y, 2) == 0) {
		outFragColor.rgb = vec3(1.f);
	} else {
		outFragColor.rgb = vec3(0.f);
	}
	return;
}