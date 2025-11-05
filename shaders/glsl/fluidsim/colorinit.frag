#version 450
#extension GL_EXT_debug_printf : enable

// in
layout (location = 0) in vec2 inUV;

// out
layout (location = 0) out vec4 outFragColor;

layout (binding = 0) uniform UBO
{
    vec2 bufferResolution;
    int whichTexture;
} ubo;

const float PI = 3.14159265358979323846;

void main() {

	float x = inUV.x;
	float y = inUV.y;
	x *= ubo.bufferResolution.x / ubo.bufferResolution.y;
	switch (ubo.whichTexture) {
		case 0: {
			// init color map to checkerboard
			x = floor(x / .2f);
			y = floor(y / .2f);

			if (mod(x + y, 2) < 1) {
				outFragColor.rgb = vec3(1.f);
			} else {
				outFragColor.rgb = vec3(0.f);
			}
			break;
		}
		case 1: {
			// init velocity map
			outFragColor.r = sin(2.5*PI*inUV.y);
			outFragColor.g = sin(2.5*PI*inUV.x);
			break;
		}
	}

	return;
}