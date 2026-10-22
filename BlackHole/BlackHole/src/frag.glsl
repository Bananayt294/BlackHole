
#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2 resolution;
} pc;

void main()
{
    vec2 uv = (gl_FragCoord.xy - 0.5 * pc.resolution)
              / pc.resolution.y;

    float d = length(uv);

    // Create a ring between the inner and outer radius.
    float disk = smoothstep(0.30, 0.27, d)
               * smoothstep(0.12, 0.15, d);

    // Color gradient: dark red to hot orange.
    vec3 diskColor = mix(
        vec3(0.8, 0.05, 0.01),
        vec3(1.0, 0.65, 0.08),
        smoothstep(0.12, 0.30, d)
    );

    vec3 color = vec3(0.0);

    // Only show the gradient in the disk.
    if (d >= 0.12 && d <= 0.30)
        color = diskColor;

    outColor = vec4(color, 1.0);
}
