// Advanced water surface: Gerstner (trochoidal) waves with horizontal choppiness
// and analytic normals, plus a dynamic ripple heightfield (CPU wave-equation sim,
// splashes/wakes from physics bodies) sampled in the vertex shader. Fresnel
// environment reflection, sun specular, and crest/ripple foam in the fragment.
//
// NOTE: the Gerstner wave set (directions/frequencies/amplitudes/speeds) MUST match
// Scene.cpp's WaterWaveHeight() — the CPU mirror used for buoyancy and splashes.

#type vertex
#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float u_Time;
uniform float u_Amplitude;
uniform float u_WaveScale;
uniform float u_Speed;
uniform float u_Choppiness;   // 0 = pure sine, 1 = sharp trochoidal crests
uniform float u_WaterSize;    // world width of the grid (ripple slope scale)
uniform bool u_HasRipples;
uniform highp sampler2D u_RippleTex;

out vec3 FragPos;
out vec3 Normal;
out float Crest;   // 0..1 how close to a wave crest (drives foam)
out float Ripple;  // ripple activity at this vertex (drives foam)

// The 4 Gerstner waves (direction, freq mult, amp mult, speed mult).
const vec2  WDIR[4] = vec2[4](vec2(0.9438798, 0.3303579), vec2(-0.4103913, 0.9119215),
                              vec2(0.7071068, -0.7071068), vec2(-0.9048187, -0.4258407));
const float WFRQ[4] = float[4](1.0, 1.9, 3.1, 5.3);
const float WAMP[4] = float[4](1.0, 0.5, 0.25, 0.12);
const float WSPD[4] = float[4](1.0, 1.25, 1.7, 2.3);

void main(){
    vec3 world = vec3(model * vec4(aPos, 1.0));
    vec2 p = world.xz;

    // Gerstner displacement + analytic derivatives.
    vec3 disp = vec3(0.0);
    vec3 dNorm = vec3(0.0, 1.0, 0.0); // accumulated normal terms
    float ampSum = 0.0;
    for (int i = 0; i < 4; i++){
        float w = u_WaveScale * WFRQ[i];
        float a = u_Amplitude * WAMP[i];
        float ph = dot(p, WDIR[i]) * w + u_Time * u_Speed * WSPD[i];
        float s = sin(ph), c = cos(ph);
        // Per-wave steepness, normalized so 4 waves can't fold over each other.
        float q = (a > 0.0) ? u_Choppiness / (w * a * 4.0) : 0.0;

        disp.xz += WDIR[i] * (q * a * c);
        disp.y  += a * s;
        dNorm.x -= WDIR[i].x * w * a * c;
        dNorm.z -= WDIR[i].y * w * a * c;
        dNorm.y -= q * w * a * s;
        ampSum  += a;
    }
    world += disp;
    Crest = (ampSum > 0.0) ? clamp(disp.y / ampSum, 0.0, 1.0) : 0.0;

    // Dynamic ripples: height added straight up; slope from finite differences.
    Ripple = 0.0;
    if (u_HasRipples){
        vec2 texel = 1.0 / vec2(textureSize(u_RippleTex, 0));
        float h  = texture(u_RippleTex, aTexCoord).r;
        float hl = texture(u_RippleTex, aTexCoord - vec2(texel.x, 0.0)).r;
        float hr = texture(u_RippleTex, aTexCoord + vec2(texel.x, 0.0)).r;
        float hd = texture(u_RippleTex, aTexCoord - vec2(0.0, texel.y)).r;
        float hu = texture(u_RippleTex, aTexCoord + vec2(0.0, texel.y)).r;
        world.y += h;
        float cell = u_WaterSize * texel.x; // world size of one ripple cell
        dNorm.x -= (hr - hl) / (2.0 * cell);
        dNorm.z -= (hu - hd) / (2.0 * cell);
        Ripple = clamp(abs(h) * 6.0 + (abs(hr - hl) + abs(hu - hd)) * 12.0, 0.0, 1.0);
    }

    FragPos = world;
    Normal = normalize(dNorm);
    gl_Position = projection * view * vec4(world, 1.0);
}

#type fragment
#version 300 es
precision highp float;

in vec3 FragPos;
in vec3 Normal;
in float Crest;
in float Ripple;
out vec4 FragColor;

uniform vec3 u_CameraPos;
uniform vec3 u_Color;
uniform float u_Alpha;
uniform vec3 u_LightDir; // direction the sun travels
uniform float u_Foam;    // foam intensity (0 disables)

// Same procedural sky/ground as the reflective materials.
vec3 SampleEnvironment(vec3 dir){
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.55, 0.60, 0.70);
    vec3 zenith  = vec3(0.15, 0.35, 0.75);
    vec3 ground  = vec3(0.10, 0.10, 0.12);
    vec3 sky = mix(horizon, zenith, smoothstep(0.5, 1.0, t));
    return mix(ground, sky, smoothstep(0.45, 0.55, t));
}

void main(){
    vec3 N = normalize(Normal);
    vec3 V = normalize(u_CameraPos - FragPos);

    // Fresnel: more reflective at grazing angles.
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    fresnel = mix(0.06, 1.0, fresnel);

    vec3 envColor = SampleEnvironment(reflect(-V, N));

    // Troughs read deeper/darker, crests brighter (cheap subsurface cue).
    vec3 deep = u_Color * 0.55;
    vec3 waterColor = mix(deep, u_Color * 1.15, Crest);

    // Sun specular (sparkle).
    vec3 L = normalize(-u_LightDir);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 200.0);

    vec3 color = mix(waterColor, envColor, fresnel) + vec3(spec);

    // Foam on sharp crests and where ripples are active.
    float foam = u_Foam * clamp(smoothstep(0.62, 0.95, Crest) + Ripple, 0.0, 1.0);
    color = mix(color, vec3(0.92, 0.96, 1.0), foam);

    // grazing/reflective angles and foam read more opaque
    float alpha = clamp(u_Alpha + fresnel * 0.4 + spec + foam * 0.5, 0.0, 1.0);
    FragColor = vec4(color, alpha);
}
