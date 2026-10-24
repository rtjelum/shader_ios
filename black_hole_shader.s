// inspired by
//     https://www.shadertoy.com/view/llSGRG
//     https://www.shadertoy.com/view/XdjXDy
//
// inspired the work of
//     https://www.shadertoy.com/view/tXsGR2
//
// simplex from
//     https://github.com/hughsk/glsl-noise/blob/master/simplex/3d.glsl
//
// blackbody from
//     https://www.shadertoy.com/view/MslSDl
//     http://en.wikipedia.org/wiki/Planckian_locus
//     http://en.wikipedia.org/wiki/CIE_1960_color_space
//     http://en.wikipedia.org/wiki/XYZ_color_space
//     http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html

#define EPSILON 0.1
#define PI 3.14159265358979

struct Camera {
    vec3 position;
    vec3 target;
    vec3 up;
    float focalLength;
};

struct Sphere {
    vec3 position;
    float radius;
    float mass;
};

struct Torus {
    vec3 position;
    float minor;
    float major;
};

// ========================= //

Camera camera = Camera(
    vec3(0.0, 0.25, 0.5),
    vec3(0.0, 0.0, 0.0),
    normalize(vec3(0.5, 1.0, 0.0)),
    1.5
);

const Sphere sphere = Sphere(
    vec3(0.0, 0.0, 0.0),
    0.125,
    2.5 * 0.001 // premultiply G
);

const Torus torus = Torus(
    sphere.position,
    0.8,
    0.99
);

// ========================= //

float sdfSphere(in vec3 p, in Sphere sphere) {
    return length(p - sphere.position) - sphere.radius;
}

float sdfTorus(in vec3 p, in Torus torus) {
    p -= torus.position;
    vec2 q = vec2(length(p.xz) - torus.minor, p.y);
    return length(q) - torus.major;
}

// ========================= //

vec3 perspective(in vec2 clip, in Camera camera) {
    vec3 w = normalize(camera.target - camera.position);
    vec3 u = normalize(cross(w, camera.up));
    vec3 v = normalize(cross(u, w));
    return vec3(normalize(clip.x * u + clip.y * v + (camera.focalLength) * w));
}

// ========================= //

float hash(in vec3 x) { return fract(sin(dot(x, vec3(12.9898, 4.1414, 9.3232))) * 43758.5453); }

const float ONE_OVER_289 = 1.0 / 289.0;
const float ONE_THIRD = 1.0 / 3.0;
const float ONE_SIXTH = 1.0 / 6.0;
const float ONE_SEVENTH = 1.0 / 7.0;
vec3 mod289(vec3 x) { return x - floor(x * ONE_OVER_289) * 289.0; }
vec4 mod289(vec4 x) { return x - floor(x * ONE_OVER_289) * 289.0; }
vec4 permute(vec4 x) { return mod289(((x * 34.0) + 1.0) * x); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float simplex(in vec3 p) {
    const vec2 C = vec2(ONE_SIXTH, ONE_THIRD);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);
    
    vec3 i = floor(p + dot(p, C.yyy));
    vec3 x0 = p - i + dot(i, C.xxx);
    
    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);
    
    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;
    
    i = mod289(i);
    vec4 perm = permute(permute(permute(
        i.z + vec4(0.0, i1.z, i2.z, 1.0)) +
        i.y + vec4(0.0, i1.y, i2.y, 1.0)) +
        i.x + vec4(0.0, i1.x, i2.x, 1.0));
    
    vec3 ns = ONE_SEVENTH * D.wyz - D.xzx;
    vec4 j = perm - 49.0 * floor(perm * ns.z * ns.z);
    
    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);
    
    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);
    
    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);
    
    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));
    
    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;
    
    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);
    
    vec4 norm = taylorInvSqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;
    
    vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m *= m;
    return 42.0 * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

float fbm(in vec3 p, in int octaves) {
    const mat3 rot = mat3(
         cos(0.5), 0.0, sin(0.5),
             0.0 , 1.0,     0.0 ,
        -sin(0.5), 0.0, cos(0.5)
    );
    const vec3 scale = vec3(3.5, 4.125, 3.1);
    const vec3 shift = vec3(100.0);
    float value = 0.0;
    float amplitude = 1.0;
    for (int i = 0; i < octaves; i++) {
        value += simplex(p) * amplitude;
        p = rot * (p * scale) + shift;
        amplitude *= 0.5;
    }
    return value;
}
#define STAR_STEP 0.9975
#define STAR_DENSITY 0.025
#define STAR_BRIGHTNESS 0.0025

#define GALAXY_HEIGHT 0.3

/*
#define BLACKBODY_MIN 3000.0
#define BLACKBODY_MAX 15000.0

vec3 blackbody(float t) {
    float _t = t * (BLACKBODY_MAX - BLACKBODY_MIN) + BLACKBODY_MIN;
    float u = (0.860117757 + 1.54118254e-4 * _t + 1.28641212e-7 * _t * _t)
            / (1.0 + 8.42420235e-4* _t + 7.08145163e-7 * _t * _t);
    
    float v = (0.317398726 + 4.22806245e-5 * _t + 4.20481691e-8 * _t * _t)
            / (1.0 - 2.89741816e-5 * _t + 1.61456053e-7 * _t * _t);
    
    float x = 3.0 * u / (2.0 * u - 8.0 * v + 4.0);
    float y = 2.0 * v / (2.0 * u - 8.0 * v + 4.0);
    float z = 1.0 - x - y;
    
    float Y = 1.0;
    float X = (Y / y) * x;
    float Z = (Y / y) * z;
    
    mat3 XYZtosRGB = mat3(
         3.2404542, -1.5371385, -0.4985314,
        -0.9692660,  1.8760108,  0.0415560,
         0.0556434, -0.2040259,  1.0572252
    );
    
    vec3 RGB = vec3(X, Y, Z) * XYZtosRGB;
    return RGB * pow(0.0004 * _t, 4.0);
}
*/

// blackbody color lookup by temperature
// - equidistant sampling
// - generated using the method above
// - minimum temperature 3000K
// - maximum temperature 15000K
// - all values luminance-normalized
#define LUT_SIZE 16.0
const vec3 LUT[] = vec3[](
    vec3(1.000, 0.477, 0.152970),
    vec3(1.000, 0.621, 0.329232),
    vec3(1.000, 0.739, 0.528043),
    vec3(1.000, 0.836, 0.729706),
    vec3(1.000, 0.916, 0.923112),
    vec3(0.907, 0.890, 1.000000),
    vec3(0.789, 0.817, 1.000000),
    vec3(0.706, 0.763, 1.000000),
    vec3(0.645, 0.722, 1.000000),
    vec3(0.599, 0.689, 1.000000),
    vec3(0.563, 0.662, 1.000000),
    vec3(0.533, 0.640, 1.000000),
    vec3(0.510, 0.622, 1.000000),
    vec3(0.490, 0.606, 1.000000),
    vec3(0.473, 0.593, 1.000000),
    vec3(0.459, 0.581, 1.000000)
);

vec3 blackbody(in float t) {
    int i = int(t * LUT_SIZE);
    return LUT[i];
}

void mainCubemap(out vec4 fragColor, in vec2 fragCoord, in vec3 rayOri, in vec3 rayDir) {
    float noise0 = hash(rayDir);
    float noise1 = hash(rayDir.xyz + rayDir.xzy - rayDir.zyx);
    float noise2 = simplex(rayDir);
    float noise3 = fbm(-rayDir * 2.0, 3);
    float noise4 = fbm(rayDir * 3.0, 2);
    
    // vary star density
    float center = STAR_STEP - (noise2 * 0.5 + 0.5) * STAR_BRIGHTNESS;
    float starmask = smoothstep(center - STAR_DENSITY, center + STAR_DENSITY, noise0);
    
    // increased probability of cooler stars
    float temp = noise1 * noise1 * noise1;
    
    vec3 bb = blackbody(temp);
    float bf = exp(-(temp * temp));
    bb += -(bf * bf) + 1.0; // make hotter stars brighter 
    
    // simulate darker (more distant) patches
    starmask *= abs(1.0 - noise2) * 0.5 + 0.5;
   
    vec3 star = bb * starmask;
    
    float galaxynoise = noise3 * 0.45 + 0.55;
    float galaxymask = max(0.0, GALAXY_HEIGHT * 2.0 - abs(rayDir.y + noise2 * 0.1));
    galaxymask *= galaxymask;
    galaxymask = smoothstep(0.0, 0.6, galaxymask);
    
    float dustmask = galaxymask * (noise4 * 0.4 + 0.6) * 0.6;
    
    vec3 galaxy = mix(
        vec3(0.45, 0.52, 0.96),
        vec3(0.62, 0.45, 0.66),
        abs(1.0 - galaxynoise)
    ) * galaxymask * galaxynoise * 2.0;
    
    vec3 dust = mix(
        vec3(0.69, 0.65, 0.62),
        vec3(0.73, 0.59, 0.47),
        noise4
    ) * dustmask * 2.0;

    fragColor = vec4(star + galaxy + dust, 1.0);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    
    if (iMouse.z > 0.0) {
        vec2 md = (iMouse.xy - iResolution.xy * 0.5) / iResolution.y;
        float azimuth = md.x * PI * 2.0;
        float elevation = md.y * PI * 0.5;
        const float radius = 2.0;
        float ce = cos(elevation);
        vec3 rot = vec3(
            cos(azimuth) * ce,
            sin(elevation),
            sin(azimuth) * ce
        );
        camera.position = sphere.position + radius * rot;
    }
    else {
        camera.position.x = cos(iTime * 0.1) * 2.0;
        camera.position.y = sin(iTime * 0.2) * 0.5 + 0.25;
        camera.position.z = sin(iTime * 0.1) * 2.0;
    }
    
    vec2 clip = (2.0 * fragCoord - iResolution.xy) / iResolution.y;
    
    vec3 rd = perspective(clip, camera);
    vec3 color = vec3(0.0); 
    vec3 glow = vec3(0.0);
    vec3 disc = vec3(0.0);
    vec3 jets = vec3(0.0);
    
    bool captured = false;
    float noncaptured = 1.0;
    
    float s2 = sphere.radius * sphere.radius;
    
    int i = 0;
    vec3 p = camera.position;
    for (float t = 0.0; t < 1.0; t += 0.005) {
        p += rd * 0.02 * noncaptured;
        
        vec3 to = sphere.position - p;
        float r2 = dot(to, to);
        rd += normalize(to) * (sphere.mass / r2);
        
        float s = sdfSphere(p, sphere);
        noncaptured = smoothstep(0.0, 0.666, s);
        
        glow += vec3(1.0, 0.9, 0.85) * (1.0 / r2) * 0.0030 * noncaptured;
        
        float discRadius = length(to.xz);
        float discAngle = atan(to.x, to.z);
        vec2 discSample = vec2(
            discRadius, 
            discAngle * 
            (0.01 + (discRadius - sphere.radius) * 0.002) + 
            2.0 * PI + -iTime * 0.005
        ) * vec2(10.0, 20.0);
        
        float discMask = max(
            0.0,
            texture(
                iChannel1, 
                discSample * vec2(0.1, 0.5)
            ).r + 0.05
        ) * (4.0 / (0.001 + s * 50.0));
        
        vec3 discColor = mix(
            vec3(1.0, 0.8, 0.6),
            vec3(0.5, 0.46, 0.4),
            s * s
        ) * discMask;
        
        disc += max(
            vec3(0.0), 
            discColor * 
            smoothstep(
                0.0, 1.0,
                -sdfTorus(p * vec3(1.0, 25.0, 1.0), torus)
            ) * noncaptured
        );
        
        jets += vec3(0.5,0.7,0.99) * (1.0 / discRadius) * noncaptured * 0.0001;
        
        if (s < EPSILON) {
            captured = true;
            color = vec3(0.0);
            break;
        }
    }
    
    if (!captured) color = texture(iChannel0, rd).rgb;
    color += glow * 0.55;
    color += disc * 2.0;
    color += jets * 5.0;
    
    // artistic contrast
    color = pow(color, vec3(1.7));

    fragColor = vec4(color, 1.0);
}
