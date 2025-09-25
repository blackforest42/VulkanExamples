#version 330 core

in vec2 uv;

out vec4 fragColor;

uniform float gamma = 2.2;
uniform float tonemappingEnabled;
uniform sampler2D texture0;
uniform vec2 resolution;

void main() {
  vec3 hdrColor = texture(texture0, uv).rgb;

  if (tonemappingEnabled > 0.5) {
    float exposure = 1.f;
    vec3 mapped = vec3(1.0) - exp(-hdrColor * exposure);

    // Gamma correction
    fragColor.rgb = pow(mapped, vec3(1.0 / gamma));
  }
}