#version 450
#extension GL_EXT_debug_printf : enable

// in
layout (location = 0) in vec2 inUV;

// out
layout (location = 0) out vec4 outFragColor;

layout (binding = 0) uniform UBO
{
	float timestep;
} ubo;

layout (binding = 1) uniform sampler2D field1; // velocity field
layout (binding = 2) uniform sampler2D field2; // color field

void main() {
	// follow the velocity field "back in time"
	vec2 pos = inUV - ubo.timestep * texture(field1, inUV).xy;
	// interpolate and write to the output fragment
	outFragColor = texture(field2, pos);

}
