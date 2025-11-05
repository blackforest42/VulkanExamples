#version 450
#extension GL_EXT_debug_printf : enable

// in
layout (location = 0) in vec2 inUV;

// out
layout (location = 0) out vec4 outFragColor;

layout (binding = 0) uniform UBO
{
    vec2 bufferResolution;
	float scale;
} ubo;

layout (binding = 1) uniform sampler2D field1;


void main() {
	float x = gl_FragCoord.x - 0.5;
	float y = gl_FragCoord.y - 0.5;
	float width = ubo.bufferResolution.x;
	float height = ubo.bufferResolution.y;

	outFragColor = texture(field1, inUV);

	// skip corners
	if (x == 0 && y == 0) {
		return;
	}
	if (x == 0 && y == height - 1) {
		return;
	}
	if (x == width - 1 && y == 0) {
		return;
	}
	if (x == width - 1 && y == height - 1) {
		return;
	}

	vec2 dudv = 1 / ubo.bufferResolution;
	float du = dudv.x;
	float dv = dudv.y;

	// boundaries
	if (x == 0) {
		outFragColor = ubo.scale * texture(field1, inUV + vec2(du, 0));
		return;
	}
	else if (x == width - 1) {
		outFragColor = ubo.scale * texture(field1, inUV - vec2(du, 0));
		return;
	}
	else if (y == 0) {
		outFragColor = ubo.scale * texture(field1, inUV + vec2(0, dv));
		return;
	}
	else if (y == height - 1) {
		outFragColor = ubo.scale * texture(field1, inUV - vec2(0, dv));
		return;
	}
	return; 
}
