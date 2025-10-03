#version 450

layout (binding = 1) uniform sampler2D samplerColor;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

void main() 
{

	// Tonemapping
	//vec3 fragColor = vec3(1.0) - exp(-result * ubo.exposure);
	// gamma correct
	//fragColor = pow(fragColor, vec3(1.0 / ubo.gamma));
	vec3 fragColor = texture(samplerColor, inUV).rgb;

	outFragColor = vec4(fragColor, 1.0);
}