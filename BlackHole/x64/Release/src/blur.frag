#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sourceImage;

layout(push_constant) uniform BlurPushConstants
{
    vec2 direction;
    vec2 texelSize;
} pc;

const float weights[5] = float[](
    0.227027,
    0.1945946,
    0.1216216,
    0.054054,
    0.016216
);

void main()
{
    vec3 result = texture(sourceImage, inUV).rgb * weights[0];

    for (int i = 1; i <= 4; ++i)
    {
        vec2 offset = pc.direction * pc.texelSize * float(i);

        vec3 positiveSample = texture(sourceImage, inUV + offset).rgb;

        vec3 negativeSample = texture(sourceImage, inUV - offset).rgb;

        result += (positiveSample + negativeSample) * weights[i];
    }

    outColor = vec4(result, 1.0);
}