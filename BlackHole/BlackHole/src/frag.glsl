#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2 resolution;
    float time;
} pc;

const float BLACK_HOLE_RADIUS = 0.55;
const float DISK_INNER_RADIUS = 0.75;
const float DISK_OUTER_RADIUS = 2.0;

// --------------------------------------------------
// Procedural noise
// --------------------------------------------------

float hash(vec2 p)
{
    return fract(
        sin(dot(p, vec2(127.1, 311.7))) * 43758.5453
    );
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);

    f = f * f * (3.0 - 2.0 * f);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(
        mix(a, b, f.x),
        mix(c, d, f.x),
        f.y
    );
}

float fbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.5;

    for (int i = 0; i < 4; ++i)
    {
        value += amplitude * noise(p);
        p = p * 2.03 + vec2(13.7, 7.1);
        amplitude *= 0.5;
    }

    return value;
}

// --------------------------------------------------
// Background stars
// --------------------------------------------------

float starfield(vec2 uv)
{
    vec2 grid = uv * 100.0;
    vec2 cell = floor(grid);
    vec2 local = fract(grid);

    vec2 starPosition = vec2(
        hash(cell),
        hash(cell + 17.3)
    );

    float starExists = step(0.985, hash(cell + 42.7));

    vec2 delta = local - starPosition;
    float brightness = exp(-dot(delta, delta) * 1800.0);

    return starExists * brightness;
}

// --------------------------------------------------
// Fog-free space background
// --------------------------------------------------

vec3 spaceBackground(vec3 direction)
{
    const float PI = 3.14159265359;

    direction = normalize(direction);

    vec2 skyUV = vec2(
        atan(direction.x, direction.z) / (2.0 * PI) + 0.5,
        asin(clamp(direction.y, -1.0, 1.0)) / PI + 0.5
    );

    vec3 background = vec3(0.0008, 0.0012, 0.0035);

    float stars = starfield(skyUV);
    background += vec3(0.72, 0.80, 1.0) * stars;

    return background;
}

// --------------------------------------------------
// Photon orbit integration
// --------------------------------------------------

vec2 orbitDerivative(vec2 state)
{
    float u = state.x;
    float du_dphi = state.y;

    float d2u_dphi2 =
        1.5 * BLACK_HOLE_RADIUS * u * u - u;

    return vec2(du_dphi, d2u_dphi2);
}

vec2 integrateOrbitRK4(vec2 state, float h)
{
    vec2 k1 = orbitDerivative(state);
    vec2 k2 = orbitDerivative(state + 0.5 * h * k1);
    vec2 k3 = orbitDerivative(state + 0.5 * h * k2);
    vec2 k4 = orbitDerivative(state + h * k3);

    return state + (h / 6.0) *
        (k1 + 2.0 * k2 + 2.0 * k3 + k4);
}

// --------------------------------------------------
// Accretion disk shading
// --------------------------------------------------

vec3 shadeDisk(vec3 diskPosition)
{
    float radius = length(diskPosition.xz);
    float angle = atan(diskPosition.z, diskPosition.x);

    float heat = 1.0 - smoothstep(
        DISK_INNER_RADIUS,
        DISK_OUTER_RADIUS,
        radius
    );

    vec3 darkEmber = vec3(0.12, 0.008, 0.003);
    vec3 deepOrange = vec3(0.48, 0.035, 0.006);
    vec3 orange = vec3(1.00, 0.16, 0.025);
    vec3 gold = vec3(1.00, 0.48, 0.16);
    vec3 hotWhite = vec3(1.00, 0.82, 0.62);

    vec3 color = mix(darkEmber, deepOrange, pow(heat, 0.35));
    color = mix(color, orange, pow(heat, 1.2));
    color = mix(color, gold, pow(heat, 3.0));
    color = mix(color, hotWhite, pow(heat, 9.0));

    float angularSpeed = 1.2 / max(radius, 0.1);

    float swirl =
        angle * 5.0
        - pc.time * angularSpeed
        - radius * 9.0;

    float broad = noise(vec2(swirl * 1.2, radius * 7.0));
    float fineNoise = noise(vec2(swirl * 3.5, radius * 23.0));

    float turbulence = broad * 0.7 + fineNoise * 0.3;
    float filaments = smoothstep(0.28, 0.78, turbulence);

    color *= 0.48 + filaments * 1.15;

    float outerMask = 1.0 - smoothstep(
        DISK_OUTER_RADIUS - 0.10,
        DISK_OUTER_RADIUS,
        radius
    );

    float innerMask = smoothstep(
        DISK_INNER_RADIUS,
        DISK_INNER_RADIUS + 0.10,
        radius
    );

    color *= outerMask * innerMask;
    color *= 0.75 + 0.65 * pow(heat, 2.0);

    return color;
}

// --------------------------------------------------
// Black hole ray tracing
// --------------------------------------------------

vec3 traceBlackHole(vec3 origin, vec3 initialDirection)
{
    const float horizonRadius = BLACK_HOLE_RADIUS;
    const float diskInnerRadius = DISK_INNER_RADIUS;
    const float diskOuterRadius = DISK_OUTER_RADIUS;

    const float stepSize = 0.025;
    const int maxSteps = 600;

    vec3 position = origin;
    vec3 direction = normalize(initialDirection);

    vec3 accumulatedDiskLight = vec3(0.0);
    vec3 previousPosition = position;

    bool swallowed = false;

    for (int i = 0; i < maxSteps; ++i)
    {
        float radius = length(position);

        // The ray has crossed the event horizon.
        if (radius <= horizonRadius)
        {
            swallowed = true;
            break;
        }

        // The ray has escaped the strong gravitational field.
        if (radius > 12.0 &&
            dot(position, direction) > 0.0)
        {
            break;
        }

        // --------------------------------------------------
        // Approximate 3D gravitational ray bending
        // --------------------------------------------------

        vec3 radial = position / radius;

        // Only bend the ray toward the black hole in the
        // direction perpendicular to its current travel.
        vec3 perpendicular = radial -
            direction * dot(radial, direction);

        vec3 bend = -1.5 * horizonRadius *
            perpendicular / (radius * radius);

        // Midpoint estimate improves stability over a
        // simple Euler integration step.
        vec3 midpointDirection = normalize(
            direction + bend * (0.5 * stepSize)
        );

        vec3 midpointPosition =
            position + midpointDirection * (0.5 * stepSize);

        float midpointRadius = max(
            length(midpointPosition),
            horizonRadius
        );

        vec3 midpointRadial =
            midpointPosition / midpointRadius;

        vec3 midpointPerpendicular =
            midpointRadial -
            midpointDirection *
            dot(midpointRadial, midpointDirection);

        vec3 midpointBend =
            -1.5 * horizonRadius *
            midpointPerpendicular /
            (midpointRadius * midpointRadius);

        // Advance the ray.
        vec3 newDirection = normalize(
            direction + midpointBend * stepSize
        );

        vec3 newPosition =
            position + midpointDirection * stepSize;

        // --------------------------------------------------
        // Accretion disk intersection
        // --------------------------------------------------

        bool crossedDiskPlane =
            (previousPosition.y > 0.0 &&
             newPosition.y <= 0.0) ||
            (previousPosition.y < 0.0 &&
             newPosition.y >= 0.0);

        if (crossedDiskPlane)
        {
            float denominator =
                previousPosition.y - newPosition.y;

            if (abs(denominator) > 0.000001)
            {
                float t =
                    previousPosition.y / denominator;

                vec3 hitPosition = mix(
                    previousPosition,
                    newPosition,
                    t
                );

                float hitRadius = length(hitPosition.xz);

                if (hitRadius >= diskInnerRadius &&
                    hitRadius <= diskOuterRadius)
                {
                    // Accumulate emitted light from the disk.
                    vec3 diskEmission = shadeDisk(hitPosition);

                    accumulatedDiskLight +=
                        diskEmission * 0.65;
                }
            }
        }

        previousPosition = newPosition;
        position = newPosition;
        direction = newDirection;
    }

    // A captured ray cannot see the background beyond
    // the event horizon, but it retains any disk light
    // encountered before being captured.
    if (swallowed)
    {
        return accumulatedDiskLight;
    }

    // Escaping rays reveal the background after bending.
    vec3 background = spaceBackground(direction);

    return background + accumulatedDiskLight;
}
// --------------------------------------------------
// Main
// --------------------------------------------------

void main()
{
    vec2 uv =
        (gl_FragCoord.xy - 0.5 * pc.resolution)
        / pc.resolution.y;

    vec3 cameraPosition = vec3(0.0, 1.8, 6.0);
    vec3 target = vec3(0.0, 0.0, 0.0);

    vec3 forward = normalize(target - cameraPosition);

    vec3 right = normalize(
        cross(forward, vec3(0.0, 1.0, 0.0))
    );

    vec3 up = normalize(cross(right, forward));

    float fovScale = tan(radians(35.0) * 0.5);

    vec3 rayDirection = normalize(
        forward
        + right * uv.x * fovScale
        - up * uv.y * fovScale
    );

    vec3 color = traceBlackHole(
        cameraPosition,
        rayDirection
    );

    // Tone mapping to keep bright disk colors controlled.
    color = max(color, vec3(0.0));
    color = color / (color + vec3(0.65));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}