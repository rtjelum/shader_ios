precision highp float;


float noise(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float smoothNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(noise(i + vec2(0.0, 0.0)), noise(i + vec2(1.0, 0.0)), u.x),
               mix(noise(i + vec2(0.0, 1.0)), noise(i + vec2(1.0, 1.0)), u.x), u.y);
}

float distanceField(vec2 p) {
    return length(p) - 0.5 + 0.3 * sin(iTime * 0.5);
}

vec2 rotate(vec2 p, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return vec2(p.x * c - p.y * s, p.x * s + p.y * c);
}

vec3 fractal(vec2 p) {
    vec2 uv = rotate(p - (iMouse.xy / iResolution.xy * 2.0 - 1.0), iTime * 0.1);
    float t = iTime * 0.5;
    vec3 col = vec3(0.0);
    float scale = 1.0;
    for (int i = 0; i < 5; i++) {
        uv = abs(uv) / dot(uv, uv) - 0.6;
        uv = uv * 1.5 + vec2(sin(t), cos(t)) * 0.2;
        float d = length(uv) * scale;
        float n = smoothNoise(uv * 2.0);
        vec3 color = mix(vec3(0.8, 0.2, 0.1), vec3(1.0, 0.7, 0.2), n);
        col += n * exp(-d * 0.5) * color;
        scale *= 0.5;
    }
    float df = distanceField(uv);
    col *= 1.0 / (1.0 + df * df * 2.0);
    float sparkle = smoothNoise(uv * 10.0 + iTime * 2.0);
    if (sparkle > 0.95) col += vec3(1.0, 0.9, 0.7) * (sparkle - 0.95) * 20.0;
    return col * 0.5;
}

vec3 particleBurst(vec2 uv, vec2 mouse) {
    vec2 dir = uv - mouse;
    float dist = length(dir);
    float intensity = dist < 0.5 ? 0.5 * exp(-dist * 10.0) * sin(iTime * 5.0 + dist * 20.0) : 0.0;
    return vec3(1.0, 0.8, 0.5) * intensity;
}

vec3 nebula(vec2 uv) {
    float n = smoothNoise(uv * 0.1 + iTime * 0.05);
    return vec3(0.1, 0.2, 0.3) * n * 0.3;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord.xy / iResolution.xy;
    uv = uv * 2.0 - 1.0;
    uv.x *= iResolution.x / iResolution.y;
    vec2 mouse = iMouse.xy / iResolution.xy * 2.0 - 1.0;

    vec3 col = nebula(uv);
    col += fractal(uv);
    col += particleBurst(uv, mouse);

    vec3 glowColor = mix(mix(vec3(0.8, 0.2, 0.1), vec3(1.0, 0.7, 0.2), sin(iTime * 2.0)),
                       mix(vec3(0.5, 0.1, 0.8), vec3(0.2, 0.7, 1.0), cos(iTime * 1.5)),
                       0.5 + 0.5 * sin(iTime * 0.5));
    col += 0.3 * glowColor * sin(length(uv) * 5.0 - iTime * 2.0);

    fragColor = vec4(col, 1.0);
}
