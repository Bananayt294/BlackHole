
#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2 resolution;
    float time;
} pc;

const float DISK_BRIGHTNESS = 2.5;
const float BLACK_HOLE_RADIUS = 0.55;
const float DISK_INNER_RADIUS = 0.75;
const float DISK_OUTER_RADIUS = 2.0;

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

vec3 spaceBackground(vec3 direction)
{
    const float PI = 3.14159265359;

    direction = normalize(direction);

    vec2 skyUV = vec2(
        atan(direction.x, direction.z) / (2.0 * PI) + 0.5,
        asin(clamp(direction.y, -1.0, 1.0)) / PI + 0.5
    );

    vec3 background = vec3(0.0);

    float stars = starfield(skyUV);
    background += vec3(0.45, 0.52, 0.70) * stars * 0.65;

    return background;
}

vec3 toDiskSpace(vec3 p)
{
    float tilt = radians(15.0);

    float c = cos(tilt);
    float s = sin(tilt);

    return vec3(
        p.x,
        c * p.y + s * p.z,
       -s * p.y + c * p.z
    );
}

float sampleDiskDensity(vec3 p)
{
    p = toDiskSpace(p);

    float radius = length(p.xz);

    if (radius <= DISK_INNER_RADIUS ||
        radius >= DISK_OUTER_RADIUS)
    {
        return 0.0;
    }

    float thickness = mix(
        0.04,
        0.085,
        smoothstep(
            DISK_INNER_RADIUS,
            DISK_OUTER_RADIUS,
            radius
        )
    );

    float normalizedHeight = abs(p.y) / thickness;

    if (normalizedHeight >= 1.0)
        return 0.0;

    float verticalDensity = exp(
        -2.5 * normalizedHeight * normalizedHeight
    );

    float innerFade = smoothstep(
        DISK_INNER_RADIUS,
        DISK_INNER_RADIUS + 0.12,
        radius
    );

    float outerFade = 1.0 - smoothstep(
        DISK_OUTER_RADIUS - 0.18,
        DISK_OUTER_RADIUS,
        radius
    );

    float rotation =
        pc.time * (0.12 + 0.4 / max(radius, 0.2));

    float c = cos(rotation);
    float s = sin(rotation);

    vec2 q = mat2(c, -s, s, c) * p.xz;

    float broadNoise = noise(
        q * 8.0 + vec2(1.7, 4.2)
    );

    float fineNoise = noise(
        q * 23.0 + vec2(17.0, 5.0)
    );

    float turbulence =
        broadNoise * 0.65 +
        fineNoise * 0.35;

    float plasmaDensity = mix(
        0.18,
        1.25,
        smoothstep(0.24, 0.76, turbulence)
    );

    return verticalDensity *
           innerFade *
           outerFade *
           plasmaDensity;
}

float samplePlasmaHaze(vec3 p)
{
    float radius = length(p.xz);

    const float HAZE_INNER_RADIUS = 0.64;
    const float HAZE_OUTER_RADIUS = 2.75;

    if (radius <= HAZE_INNER_RADIUS ||
        radius >= HAZE_OUTER_RADIUS)
    {
        return 0.0;
    }

    float innerFade = smoothstep(
        HAZE_INNER_RADIUS,
        DISK_INNER_RADIUS + 0.18,
        radius
    );

    float outerFade = 1.0 - smoothstep(
        DISK_OUTER_RADIUS + 0.10,
        HAZE_OUTER_RADIUS,
        radius
    );

    float thickness = mix(
        0.12,
        0.40,
        smoothstep(
            DISK_INNER_RADIUS,
            HAZE_OUTER_RADIUS,
            radius
        )
    );

    float heightRatio = abs(p.y) / thickness;

    float verticalFalloff = exp(
        -1.8 * heightRatio * heightRatio
    );

    vec2 q = p.xz * 2.8;

    q += vec2(
        pc.time * 0.035,
        -pc.time * 0.022
    );

    float broadNoise = noise(q);

    float fineNoise = noise(
        q * 2.7 +
        vec2(p.y * 3.0, -p.y * 2.0) +
        vec2(-pc.time * 0.05, pc.time * 0.03)
    );

    float clouds =
        broadNoise * 0.65 +
        fineNoise * 0.35;

    float wisps = smoothstep(
        0.30,
        0.72,
        clouds
    );

    float density = mix(
        0.10,
        0.85,
        wisps
    );

    return innerFade *
           outerFade *
           verticalFalloff *
           density;
}

vec3 shadeDisk(vec3 diskPosition, vec3 cameraPosition)
{
    diskPosition = toDiskSpace(diskPosition);

    float radius = length(diskPosition.xz);
    float angle = atan(diskPosition.z, diskPosition.x);

    float heat = 1.0 - smoothstep(
        DISK_INNER_RADIUS,
        DISK_OUTER_RADIUS,
        radius
    );

    vec3 outerAmber = vec3(0.24, 0.095, 0.027);
    vec3 deepGold   = vec3(0.52, 0.27, 0.085);
    vec3 warmGold   = vec3(0.88, 0.54, 0.25);
    vec3 paleGold   = vec3(1.00, 0.77, 0.49);
    vec3 creamWhite = vec3(1.00, 0.91, 0.76);
    vec3 hotWhite   = vec3(1.00, 0.985, 0.94);

    vec3 color = mix(
        outerAmber,
        deepGold,
        smoothstep(0.0, 0.35, heat)
    );

    color = mix(
        color,
        warmGold,
        smoothstep(0.18, 0.52, heat)
    );

    color = mix(
        color,
        paleGold,
        smoothstep(0.38, 0.70, heat)
    );

    color = mix(
        color,
        creamWhite,
        smoothstep(0.58, 0.86, heat)
    );

    color = mix(
        color,
        hotWhite,
        pow(heat, 6.0)
    );

    float angularSpeed =
        1.2 / pow(max(radius, 0.1), 1.5);

    float swirl =
        angle * 7.0
        - radius * 13.0
        - pc.time * angularSpeed;

    float broad = noise(
        vec2(swirl * 1.1, radius * 8.0)
    );

    float fine = noise(
        vec2(swirl * 2.8, radius * 24.0)
    );

    float streaks = 0.5 + 0.5 * sin(
        swirl + (broad - 0.5) * 4.0
    );

    float detail =
        broad * 0.40 +
        fine * 0.25 +
        streaks * 0.35;

    float filaments = smoothstep(
        0.32,
        0.72,
        detail
    );

    color *= mix(0.42, 1.45, filaments);

    vec3 diskTangent = normalize(
        vec3(-diskPosition.z, 0.0, diskPosition.x)
    );

    vec3 toCamera = normalize(
        cameraPosition - diskPosition
    );

    float directional = dot(diskTangent, toCamera);

    color *= 1.0 + 0.06 * directional;

    float innerMask = smoothstep(
        DISK_INNER_RADIUS,
        DISK_INNER_RADIUS + 0.08,
        radius
    );

    float outerMask = 1.0 - smoothstep(
        DISK_OUTER_RADIUS - 0.12,
        DISK_OUTER_RADIUS,
        radius
    );

    color *= innerMask * outerMask;

    color *= 0.90 + 0.75 * pow(heat, 2.0);

    return color;
}

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

vec3 traceBlackHole(vec3 origin, vec3 initialDirection)
{
    const float inverseHorizon = 1.0 / BLACK_HOLE_RADIUS;

    const float angularStep = 0.015;
    const int maxSteps = 500;

    float initialRadius = length(origin);

    vec3 direction = normalize(initialDirection);
    vec3 radialBasis = normalize(origin);

    float radialVelocity = dot(direction, radialBasis);

    vec3 tangent =
        direction - radialVelocity * radialBasis;

    float tangentMagnitude = length(tangent);

    if (tangentMagnitude < 0.00001)
    {
        if (radialVelocity < 0.0)
            return vec3(0.0);

        return spaceBackground(direction);
    }

    vec3 tangentBasis = tangent / tangentMagnitude;

    float u = 1.0 / initialRadius;
    float v = -u * radialVelocity / tangentMagnitude;

    vec2 state = vec2(u, v);

    float phi = 0.0;

    vec3 previousPosition = origin;

    vec3 accumulatedDiskLight = vec3(0.0);
    float accumulatedDiskOpacity = 0.0;

    vec3 accumulatedHazeLight = vec3(0.0);
    float accumulatedHazeOpacity = 0.0;

    bool swallowed = false;

    for (int i = 0; i < maxSteps; ++i)
    {
        state = integrateOrbitRK4(state, angularStep);

        phi += angularStep;

        u = state.x;
        v = state.y;

        if (u >= inverseHorizon)
        {
            swallowed = true;
            break;
        }

        if (u <= 0.0)
            break;

        float radius = 1.0 / u;

        vec3 currentRadial =
            radialBasis * cos(phi) +
            tangentBasis * sin(phi);

        vec3 currentPosition = radius * currentRadial;

        vec3 samplePosition =
            0.5 * (previousPosition + currentPosition);

        float segmentLength =
            length(currentPosition - previousPosition);

        float hazeDensity = samplePlasmaHaze(samplePosition);

        if (hazeDensity > 0.001)
        {
            vec3 hazeColor = vec3(0.52, 0.39, 0.23);

            float hazeSampleOpacity = 1.0 - exp(
                -hazeDensity * segmentLength * 0.28
            );

            float remainingHaze =
                1.0 - accumulatedHazeOpacity;

            accumulatedHazeLight +=
                remainingHaze *
                hazeColor *
                hazeSampleOpacity *
                0.65;

            accumulatedHazeOpacity +=
                remainingHaze * hazeSampleOpacity;
        }

        float density = sampleDiskDensity(samplePosition);

        if (density > 0.001)
        {
            vec3 emission = shadeDisk(samplePosition, origin);

            float sampleOpacity = 1.0 - exp(
                -density * segmentLength * 4.0
            );

            float remainingOpacity =
                1.0 - accumulatedDiskOpacity;

            accumulatedDiskLight +=
                remainingOpacity *
                emission *
                sampleOpacity *
                2.0;

            accumulatedDiskOpacity +=
                remainingOpacity * sampleOpacity;
        }

        previousPosition = currentPosition;

        if (radius > 12.0 && v < 0.0)
            break;
    }

    if (swallowed)
    {
        return accumulatedDiskLight * DISK_BRIGHTNESS
             + accumulatedHazeLight;
    }

    vec3 finalRadial =
        radialBasis * cos(phi) +
        tangentBasis * sin(phi);

    vec3 finalTangent =
        -radialBasis * sin(phi) +
        tangentBasis * cos(phi);

    vec3 finalDirection;

    if (u <= 0.0)
    {
        finalDirection = normalize(finalRadial);
    }
    else
    {
        float dr_dphi = -v / (u * u);

        finalDirection = normalize(
            dr_dphi * finalRadial +
            (1.0 / u) * finalTangent
        );
    }

    vec3 background = spaceBackground(finalDirection);

    vec3 attenuatedBackground =
        background * (1.0 - accumulatedHazeOpacity);

    return attenuatedBackground
         + accumulatedDiskLight * DISK_BRIGHTNESS
         + accumulatedHazeLight;
}

void main()
{
    vec2 uv =
        (gl_FragCoord.xy - 0.5 * pc.resolution)
        / pc.resolution.y;

    vec3 cameraPosition = vec3(0.0, 0.7, 3.7);
    vec3 target = vec3(0.0, 0.0, 0.0);

    vec3 forward = normalize(target - cameraPosition);

    vec3 right = normalize(
        cross(forward, vec3(0.0, 1.0, 0.0))
    );

    vec3 up = normalize(cross(right, forward));

    float fovScale = tan(radians(70.0) * 0.5);

    vec3 rayDirection = normalize(
        forward
        + right * uv.x * fovScale
        - up * uv.y * fovScale
    );

    vec3 color = traceBlackHole(
        cameraPosition,
        rayDirection
    );

    float cameraRadius = length(cameraPosition);

    vec3 cameraRadial = normalize(cameraPosition);

    float radialComponent = dot(
        rayDirection,
        cameraRadial
    );

    float transverseLength = length(
        rayDirection - cameraRadial * radialComponent
    );

    float impactParameter =
        cameraRadius * transverseLength /
        sqrt(max(
            0.0001,
            1.0 - BLACK_HOLE_RADIUS / cameraRadius
        ));

    float criticalImpactParameter =
        1.5 * sqrt(3.0) * BLACK_HOLE_RADIUS;

    float ringDistance = abs(
        impactParameter - criticalImpactParameter
    );

    float photonRing = exp(
        -pow(ringDistance / 0.025, 2.0)
    );

    color += vec3(1.0, 0.78, 0.52) * photonRing * 0.65;

    color = max(color, vec3(0.0));
    color = color / (color + vec3(0.65));

    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
