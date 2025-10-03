#version 450

layout (binding = 0) uniform UBO 
{
    // Tonemapping
    float exposure;
    float gamma;
} ubo;

layout (binding = 1) uniform sampler2D samplerColor;


layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

void main() 
{
	vec3 fragColor = texture(samplerColor, inUV).rgb;
	// Tonemapping
	fragColor = vec3(1.0) - exp(-fragColor * ubo.exposure);
	// gamma correct
	fragColor = pow(fragColor, vec3(1.0 / ubo.gamma));

	outFragColor = vec4(fragColor, 1.0);
}