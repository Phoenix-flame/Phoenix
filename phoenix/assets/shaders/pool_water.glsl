// Pool water surface, from the simulated height texture. Opaque: it composites the
// scene behind it itself, from a copy of the colour and depth rendered so far.
//  - From above: the refracted ray is traced to the pool behind (screen-space, with a
//    depth check so things in front of the water never bleed in), dimmed and tinted by
//    the water it crosses (Beer-Lambert per channel plus in-scattered water colour),
//    then mixed by Fresnel with the reflected sky and the sun's glint.
//  - From below (camera in the water): Snell's window. Inside it the world above is
//    seen refracted; outside it the surface mirrors the water (total internal
//    reflection). Either way the view is hazed by the water between eye and surface.

#type vertex
#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform highp sampler2D u_HeightTex;
uniform vec2 u_Spacing;

out vec3 WorldPos;
out vec3 WorldNormal;
out float Slope;

void main(){
    vec2 texel = 1.0 / vec2(textureSize(u_HeightTex, 0));
    float h  = texture(u_HeightTex, aTexCoord).r;
    float hl = texture(u_HeightTex, aTexCoord - vec2(texel.x, 0.0)).r;
    float hr = texture(u_HeightTex, aTexCoord + vec2(texel.x, 0.0)).r;
    float hd = texture(u_HeightTex, aTexCoord - vec2(0.0, texel.y)).r;
    float hu = texture(u_HeightTex, aTexCoord + vec2(0.0, texel.y)).r;
    vec3 n = normalize(vec3(-(hr - hl) / (2.0 * u_Spacing.x), 1.0, -(hu - hd) / (2.0 * u_Spacing.y)));
    Slope = 1.0 - n.y;

    WorldPos = vec3(model * vec4(aPos.x, aPos.y + h, aPos.z, 1.0));
    WorldNormal = normalize(mat3(model) * n);
    gl_Position = projection * view * vec4(WorldPos, 1.0);
}

#type fragment
#version 300 es
precision highp float;

in vec3 WorldPos;
in vec3 WorldNormal;
in float Slope;
out vec4 FragColor;

uniform mat4 view;
uniform mat4 projection;
uniform mat4 u_InvViewProj;
uniform vec3 u_CameraPos;
uniform bool u_CameraUnderwater;
uniform highp sampler2D u_SceneColor;
uniform highp sampler2D u_SceneDepth;
uniform vec3 u_Absorption;      // per metre, per channel
uniform vec3 u_WaterColor;      // in-scattered light colour (linear)
uniform vec3 u_LightDirWorld;   // direction sunlight travels
uniform vec3 u_SunColor;
uniform vec3 u_Ambient;
uniform vec3 u_FloorColor;      // the lit pool floor (what total internal reflection mirrors)
uniform bool u_LinearLighting;

vec3 SrgbToLinear(vec3 c) { return u_LinearLighting ? pow(c, vec3(2.2)) : c; }

uniform highp sampler2DShadow u_ShadowMap;
uniform mat4 u_LightSpaceMatrix;
uniform vec4 u_ShadowRect;
uniform vec4 u_ShadowParams;
uniform vec4 u_ShadowInfo;

float SunShadow(vec3 p){
    if (u_ShadowRect.z <= 0.0) { return 0.0; }
    vec4 ls = u_LightSpaceMatrix * vec4(p, 1.0);
    vec3 proj = ls.xyz / ls.w * 0.5 + 0.5;
    if (proj.z >= 1.0 || proj.x <= 0.0 || proj.x >= 1.0 || proj.y <= 0.0 || proj.y >= 1.0) { return 0.0; }
    float texel = u_ShadowInfo.x;
    float lit = 0.0;
    for (int x = -1; x <= 1; x++){
        for (int y = -1; y <= 1; y++){
            vec2 uv = clamp(proj.xy + vec2(float(x), float(y)) * 1.5 * texel, vec2(0.5 * texel), vec2(1.0 - 0.5 * texel));
            lit += texture(u_ShadowMap, vec3(u_ShadowRect.xy + uv * u_ShadowRect.zw, proj.z - u_ShadowParams.z));
        }
    }
    vec2 edge = abs(proj.xy - 0.5);
    float fade = 1.0 - smoothstep(0.45, 0.5, max(edge.x, edge.y));
    return (1.0 - lit / 9.0) * fade * u_ShadowParams.y;
}

// Same procedural sky/ground as the reflective materials.
vec3 SampleEnvironment(vec3 dir){
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.55, 0.60, 0.70);
    vec3 zenith  = vec3(0.15, 0.35, 0.75);
    vec3 ground  = vec3(0.10, 0.10, 0.12);
    vec3 sky = mix(horizon, zenith, smoothstep(0.5, 1.0, t));
    return SrgbToLinear(mix(ground, sky, smoothstep(0.45, 0.55, t)));
}

vec2 ScreenSize() { return vec2(textureSize(u_SceneColor, 0)); }

// World position of what the scene copy shows at uv (sky / nothing = far away).
vec3 ScenePosition(vec2 uv){
    float d = texture(u_SceneDepth, uv).r;
    vec4 p = u_InvViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

vec2 Project(vec3 p){
    vec4 c = projection * view * vec4(p, 1.0);
    return c.xy / c.w * 0.5 + 0.5;
}

bool OnScreen(vec2 uv) { return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))); }

// Light gathered and lost over `path` metres of water.
vec3 ThroughWater(vec3 color, float path, float shadow){
    vec3 T = exp(-u_Absorption * path);
    vec3 inscatter = u_WaterColor * (u_Ambient + 0.35 * u_SunColor * (1.0 - shadow));
    return color * T + inscatter * (1.0 - T);
}

float Fresnel(float cosTheta) { return 0.02 + 0.98 * pow(1.0 - clamp(cosTheta, 0.0, 1.0), 5.0); }

void main(){
    vec3 N = normalize(WorldNormal);
    vec3 V = normalize(u_CameraPos - WorldPos);
    vec3 L = normalize(-u_LightDirWorld);   // towards the sun
    vec2 screenUV = gl_FragCoord.xy / ScreenSize();
    float shadow = SunShadow(WorldPos);
    float eyeToSurface = length(u_CameraPos - WorldPos);

    vec3 color;
    if (!u_CameraUnderwater){
        // Refraction: march the refracted ray as far as the undistorted view sees,
        // re-project, and keep the result only if it lies behind the surface.
        vec3 R = refract(-V, N, 1.0 / 1.333);
        float behind = max(length(ScenePosition(screenUV) - WorldPos), 0.05);
        vec2 uv = Project(WorldPos + R * behind);
        if (!OnScreen(uv) || length(ScenePosition(uv) - u_CameraPos) < eyeToSurface) { uv = screenUV; }
        vec3 bottom = ScenePosition(uv);
        float path = min(length(bottom - WorldPos), 60.0);
        vec3 refracted = ThroughWater(texture(u_SceneColor, uv).rgb, path, shadow);

        vec3 reflected = SampleEnvironment(reflect(-V, N));
        float spec = pow(max(dot(reflect(-V, N), L), 0.0), 900.0) * 6.0 + pow(max(dot(reflect(-V, N), L), 0.0), 90.0) * 0.25;
        color = mix(refracted, reflected, Fresnel(dot(N, V))) + u_SunColor * spec * (1.0 - shadow);
    }
    else{
        // Looking up from below: the surface normal faces the camera flipped.
        vec3 Nd = -N;
        vec3 R = refract(-V, Nd, 1.333);
        vec3 inside = ThroughWater(u_FloorColor, 3.0, shadow);   // the pool mirrored back down
        vec3 above;
        float f;
        if (dot(R, R) < 1e-6){
            above = inside;   // total internal reflection
            f = 1.0;
        }
        else{
            // The world above, or the sky where nothing was drawn.
            vec2 uv = Project(WorldPos + R * 6.0);
            vec3 seen = OnScreen(uv) && texture(u_SceneDepth, uv).r < 1.0 && length(ScenePosition(uv) - u_CameraPos) > eyeToSurface
                ? texture(u_SceneColor, uv).rgb : SampleEnvironment(R);
            above = seen + u_SunColor * pow(max(dot(R, L), 0.0), 600.0) * 8.0 * (1.0 - shadow);
            f = Fresnel(dot(R, N));
        }
        color = ThroughWater(mix(above, inside, f), eyeToSurface, shadow);
    }

    // A little white on steep splash crests only.
    color = mix(color, SrgbToLinear(vec3(0.92, 0.96, 1.0)) * (u_Ambient + u_SunColor), clamp((Slope - 0.25) * 3.0, 0.0, 0.35));
    FragColor = vec4(color, 1.0);
}
