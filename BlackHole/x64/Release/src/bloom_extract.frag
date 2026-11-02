
#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sourceImage;

const float THRESHOLD = 1.0;
const float SOFT_KNEE = 0.5;

void main()
{
    vec3 color = texture(sourceImage, inUV).rgb;

    float brightness = max(
        max(color.r, color.g),
        color.b
    );

    float soft = clamp(
        brightness - THRESHOLD + SOFT_KNEE,
        0.0,
        2.0 * SOFT_KNEE
    );

    soft = soft * soft / (4.0 * SOFT_KNEE + 0.00001);

    float contribution = max(
        brightness - THRESHOLD,
        soft
    );

    contribution /= max(brightness, 0.00001);

    vec3 bloomColor = color * contribution;

    outColor = vec4(bloomColor, 1.0);
}
