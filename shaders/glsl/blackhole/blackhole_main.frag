#version 450

// out
layout (location = 0) out vec4 outFragColor;

layout (binding = 1) uniform samplerCube samplerCubeMap;

void main() {
	outFragColor = texture(samplerCubeMap, vec3(1.0, 1.0, 1.0));
}
