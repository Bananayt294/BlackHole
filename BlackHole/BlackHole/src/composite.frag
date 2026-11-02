
#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneTexture;
layout(set = 0, binding = 1) uniform sampler2D bloomTexture;

const float EXPOSURE = 1.0;
const float BLOOM_STRENGTH = 0.12;

vec3 ACESFilm(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    return clamp(
        (color * (a * color + b)) /
        (color * (c * color + d) + e),
        0.0,
        1.0
    );
}

void main()
{
    vec3 sceneColor = texture(sceneTexture, inUV).rgb;
    vec3 bloomColor = texture(bloomTexture, inUV).rgb;

    vec3 hdrColor = sceneColor + bloomColor * BLOOM_STRENGTH;

    hdrColor *= EXPOSURE;

    vec3 finalColor = ACESFilm(hdrColor);

    outColor = vec4(finalColor, 1.0);
}