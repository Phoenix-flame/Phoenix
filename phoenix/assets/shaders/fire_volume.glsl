// Fire: one flame's grid, ray-marched from the camera through its box (drawn as back
// faces) up to the scene's depth. A flame's light comes from soot glowing at the gas's
// temperature, and soot lives where fuel is burning, so the soot is taken from the
// unburnt fuel. Soot absorbs as it emits (Kirchhoff): a thin flame is translucent, a
// thick one (a campfire) glows at its temperature's black-body brightness however deep
// it is. The flame ends where its fuel runs out; the hot gas above it barely glows (a
// trace, for continuity) but bends the view behind it. Burning adds the faint blue of
// the reaction zone; smoke absorbs and is dimly lit. Output is premultiplied: blend
// ONE, ONE_MINUS_SRC_ALPHA.

#type vertex
#version 300 es
precision highp float;
uniform mat4 u_ViewProjection;
uniform vec3 u_Origin;
uniform vec3 u_Size;
out vec3 v_World;

// Unit cube, counter-clockwise from outside.
const int INDICES[36] = int[36](0, 4, 6, 0, 6, 2,  1, 3, 7, 1, 7, 5,  0, 1, 5, 0, 5, 4,
                                2, 6, 7, 2, 7, 3,  0, 2, 3, 0, 3, 1,  4, 5, 7, 4, 7, 6);
void main(){
    int c = INDICES[gl_VertexID];
    vec3 corner = vec3(float(c & 1), float((c >> 1) & 1), float((c >> 2) & 1));
    v_World = u_Origin + corner * u_Size;
    gl_Position = u_ViewProjection * vec4(v_World, 1.0);
}

#type fragment
#version 300 es
precision highp float;
in vec3 v_World;
out vec4 FragColor;

uniform highp sampler3D u_Volume;      // temperature, fuel, smoke, reaction rate
uniform highp sampler2D u_BlackBody;   // glow of 0..4000 K
uniform highp sampler2D u_SceneColor;
uniform highp sampler2D u_SceneDepth;
uniform mat4 u_InvViewProj;
uniform vec3 u_CameraPos;
uniform vec3 u_Origin;
uniform vec3 u_Size;
uniform vec3 u_Dims;
uniform vec2 u_ViewportSize;
uniform vec2 u_ViewportOffset;
uniform float u_Brightness;
uniform float u_KelvinScale;    // table coordinate of temperature 1
uniform vec3 u_SmokeColor;
uniform float u_SmokeDensity;
uniform float u_Haze;
uniform float u_Time;
uniform vec3 u_Ambient;
uniform bool u_LinearLighting;
uniform float u_Seed;

// Black-body brightness (radiance of an opaque flame at the reference temperature),
// soot's extinction per metre at full soot and the soot per unit of fuel, the hot gas's
// own glow (per metre), blue glow per unit of burning, and smoke's extinction per metre
// at density 1.
const float BRIGHTNESS = 8.0;
const float SOOT_EXTINCTION = 30.0;
const float SOOT = 6.0;
const float GAS = 0.3;
const float BLUE = 0.03;
const float SMOKE = 12.0;

float Hash(vec2 p){ return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
float Hash3(vec3 p){ return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453); }
float Noise(vec3 p){
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = mix(mix(Hash3(i), Hash3(i + vec3(1, 0, 0)), f.x), mix(Hash3(i + vec3(0, 1, 0)), Hash3(i + vec3(1, 1, 0)), f.x), f.y);
    float b = mix(mix(Hash3(i + vec3(0, 0, 1)), Hash3(i + vec3(1, 0, 1)), f.x), mix(Hash3(i + vec3(0, 1, 1)), Hash3(i + vec3(1, 1, 1)), f.x), f.y);
    return mix(a, b, f.z);
}

void main(){
    vec2 uv = (gl_FragCoord.xy - u_ViewportOffset) / u_ViewportSize;
    vec3 rd = normalize(v_World - u_CameraPos);

    // The part of the ray inside the box, cut short by the scene.
    vec3 inv = 1.0 / rd;
    vec3 ta = (u_Origin - u_CameraPos) * inv, tb = (u_Origin + u_Size - u_CameraPos) * inv;
    vec3 lo = min(ta, tb), hi = max(ta, tb);
    float t0 = max(max(max(lo.x, lo.y), lo.z), 0.0);
    float t1 = min(min(hi.x, hi.y), hi.z);
    float depth = texture(u_SceneDepth, uv).r;
    if (depth < 1.0){
        vec4 p = u_InvViewProj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
        t1 = min(t1, dot(p.xyz / p.w - u_CameraPos, rd));
    }
    if (t1 <= t0) { discard; }

    // About two samples per cell, jittered per pixel against banding.
    vec3 cell = u_Size / u_Dims;
    float spacing = min(min(cell.x, cell.y), cell.z) * 0.5;
    int steps = int(clamp(ceil((t1 - t0) / spacing), 1.0, 160.0));
    float dt = (t1 - t0) / float(steps);
    float jitter = Hash(gl_FragCoord.xy + u_Seed);

    vec3 light = vec3(0.0);
    float transmittance = 1.0, heat = 0.0;
    vec3 heatPos = vec3(0.0);
    for (int i = 0; i < 160; i++){
        if (i >= steps) { break; }
        vec3 pos = u_CameraPos + rd * (t0 + (float(i) + jitter) * dt);
        vec4 s = texture(u_Volume, (pos - u_Origin) / u_Size);
        float T = max(s.r, 0.0);
        vec3 body = texture(u_BlackBody, vec2(T * u_KelvinScale, 0.5)).rgb * BRIGHTNESS;
        float sigmaSoot = (1.0 - exp(-max(s.g, 0.0) * SOOT)) * SOOT_EXTINCTION;
        float sigmaSmoke = max(s.b, 0.0) * u_SmokeDensity * SMOKE;
        float a = exp(-(sigmaSoot + sigmaSmoke) * dt);
        // Over this step: soot emits (and absorbs) towards its black-body brightness,
        // smoke scatters the ambient light (and a little of the flame's), and burning
        // and hot gas add their faint glows.
        float sigma = sigmaSoot + sigmaSmoke;
        vec3 inside = sigma > 1e-6 ? (body * u_Brightness * sigmaSoot + u_SmokeColor * (u_Ambient + body * 0.02) * sigmaSmoke) / sigma
                                   : vec3(0.0);
        vec3 thin = (vec3(0.12, 0.3, 1.0) * max(s.a, 0.0) * BLUE + body * GAS) * (dt * u_Brightness);
        light += transmittance * (inside * (1.0 - a) + thin);
        transmittance *= a;
        heat += T * dt;
        heatPos += pos * T * dt;
        if (transmittance < 0.01) { break; }
    }

    // Heat haze: the hot air bends the view behind it, wavering as it rises.
    float haze = 0.0;
    vec3 behind = vec3(0.0);
    if (u_Haze > 0.0 && heat > 1e-5){
        vec3 centre = heatPos / heat;
        vec3 q = centre * 30.0 + vec3(0.0, -u_Time * 4.0, 0.0);
        vec2 wobble = vec2(Noise(q), Noise(q + vec3(19.1, 7.3, 3.7))) - 0.5;
        float distance = max(length(centre - u_CameraPos), 0.3);
        vec2 offset = wobble * u_Haze * min(heat, 0.1) * 0.25 / distance;
        haze = clamp(heat * 30.0, 0.0, 1.0) * clamp(u_Haze, 0.0, 1.0);
        behind = texture(u_SceneColor, clamp(uv + offset, vec2(0.0), vec2(1.0))).rgb;
    }

    light = max(light, vec3(0.0));
    if (!u_LinearLighting) { light = pow(light, vec3(1.0 / 2.2)); }
    FragColor = vec4(light + transmittance * haze * behind, 1.0 - transmittance * (1.0 - haze));
}
