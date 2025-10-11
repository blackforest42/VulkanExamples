#version 450


// out
layout (location = 0) out vec4 outFragColor;

layout (binding = 0) uniform UBO  {
vec2 viewportResolution;
} ubo;

layout (binding = 1) uniform sampler2D srcTexture;

const float brightPassThreshold = 1.0;
const vec3 luminanceVector = vec3(0.2125, 0.7154, 0.0721);

void main() {
  vec2 texCoord = gl_FragCoord.xy / ubo.viewportResolution.xy;

  vec4 c = texture(srcTexture, texCoord);

  float luminance = dot(luminanceVector, c.xyz);
  luminance = max(0.0, luminance - brightPassThreshold);
  c.xyz *= sign(luminance);
  c.a = 1.0;

  outFragColor = c;
}