#version 450

// in
layout (location = 0) in vec2 uv;

// out
layout (location = 0) out vec4 fragColor;

layout (binding = 0) uniform UBO  {
	vec2 dstResolution;
} ubo;

layout (binding = 1) uniform sampler2D prevUpSampledTex;
layout (binding = 2) uniform sampler2D downSampledTex;

void main() {
  vec2 inputTexelSize = 1.0 / ubo.dstResolution * 0.5;
  vec4 o = inputTexelSize.xyxy * vec4(-1.0, -1.0, 1.0, 1.0); // Offset
  fragColor =
      0.25 * (texture(prevUpSampledTex, uv + o.xy) + texture(prevUpSampledTex, uv + o.zy) +
              texture(prevUpSampledTex, uv + o.xw) + texture(prevUpSampledTex, uv + o.zw));

  fragColor += texture(downSampledTex, uv);
  fragColor.a = 1.0;
}