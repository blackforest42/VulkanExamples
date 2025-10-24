#version 450

// in
layout (location = 0) in vec2 inUV;

// out
layout (location = 0) out vec4 outFragColor;


void main() {
	vec3 red = vec3(1.0, 0., 0.);
	vec3 blue = vec3(0., 0., 1.);
	outFragColor = vec4(blue, 1.0);
}
