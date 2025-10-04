#version 450

layout (binding = 0) uniform UBO 
{
    // Tonemapping
    float exposure;
    float gamma;
} ubo;

layout (binding = 1) uniform sampler2D samplerColor;

// in 
layout (location = 0) in vec2 inUV;

// out
layout (location = 0) out vec4 outFragColor;

void main() {
	//outFragColor = vec4(0, 0, 1.f, 1.0);
	//return;

	// Tonemapping
	vec3 fragColor = texture(samplerColor, inUV).rgb;
	fragColor = vec3(1.0) - exp(-fragColor * ubo.exposure);
	// gamma correct
	fragColor = pow(fragColor, vec3(1.0 / ubo.gamma));

	outFragColor = vec4(fragColor, 1.0);
}