// Basic Lighting Shader

#type vertex
#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;
layout(location = 3) in vec4 aBoneIDs; // -1 marks an unused influence slot
layout(location = 4) in vec4 aWeights;

out vec3 Normal;
out vec3 FragPos;
out vec2 TexCoords;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
// transpose(inverse(mat3(model))), computed once per draw on the CPU rather than
// per vertex here (a 3x3 inverse for every vertex of every mesh).
uniform mat3 u_NormalMatrix;

// Skeletal animation: when u_Animated, blend the bone matrices by the vertex weights.
const int MAX_BONES = 100;
uniform mat4 u_BoneMatrices[MAX_BONES];
uniform bool u_Animated;

void main()
{
    vec3 localPos = aPos;
    vec3 localNormal = aNormal;

    if (u_Animated){
        mat4 skin = mat4(0.0);
        float total = 0.0;
        for (int i = 0; i < 4; i++){
            if (aBoneIDs[i] < 0.0) { continue; }
            skin += u_BoneMatrices[int(aBoneIDs[i])] * aWeights[i];
            total += aWeights[i];
        }
        if (total > 0.0){
            localPos = vec3(skin * vec4(aPos, 1.0));
            localNormal = mat3(skin) * aNormal;
        }
    }

    vec4 worldPos = model * vec4(localPos, 1.0);
    gl_Position = projection * view * worldPos;
    Normal = u_NormalMatrix * localNormal;
    FragPos = vec3(worldPos);
    TexCoords = aTexCoords;
}

#type fragment
#version 300 es
precision highp float;

struct Material {
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    float shininess;
};

struct DirLight {
    vec3 direction;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

struct PointLight {
    vec3 position;

    float constant;
    float linear;
    float quadratic;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};
#define NR_POINT_LIGHTS 4
#define NR_DIR_LIGHTS 8
uniform PointLight pointLights[NR_POINT_LIGHTS];
uniform int u_NumPointLights;

uniform DirLight dirLights[NR_DIR_LIGHTS];
uniform int u_NumDirLights;

in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoords;
out vec4 FragColor;

// Shadows. Each light's shadow map is a tile of a depth ATLAS (directional: one tile per
// light; point: six cube-face tiles per light), located by per-light rects (xy offset,
// zw size; zero size = unshadowed). Plain sampler2DShadow + rect math avoids sampler
// arrays, which this driver won't dynamically index. Each atlas is bound twice: with
// hardware depth comparison (PCF) and as raw depth (soft-shadow blocker search). GLES
// requires explicit precision on shadow samplers.
uniform highp sampler2DShadow u_ShadowMap;
uniform highp sampler2D u_ShadowDepth;
uniform mat4 u_LightSpaceMatrices[NR_DIR_LIGHTS];
uniform vec4 u_DirShadowRect[NR_DIR_LIGHTS];
uniform vec4 u_DirShadowParams[NR_DIR_LIGHTS]; // x penumbra (tile uv per raw depth), y strength, z depth bias (raw), w normal offset (world)
uniform vec4 u_DirShadowInfo[NR_DIR_LIGHTS];   // x texel (tile uv), y raw depth per tile uv at 45 degrees

uniform highp sampler2DShadow u_PointShadowMap;
uniform highp sampler2D u_PointShadowDepth;
uniform mat4 u_PointShadowMatrices[NR_POINT_LIGHTS * 6]; // face order +X -X +Y -Y +Z -Z
uniform vec4 u_PointShadowRect[NR_POINT_LIGHTS * 6];
uniform vec4 u_PointShadowParams[NR_POINT_LIGHTS]; // x light radius, y strength, z depth bias (texels), w normal offset (texels)
uniform vec4 u_PointShadowInfo[NR_POINT_LIGHTS];   // x near, y far, z texel (tile uv), w tan(half fov)

uniform int u_ShadowSamples;   // taps per lookup (quality)
uniform bool u_ReceiveShadows; // per material

// Widest penumbra, in tile UV. Point faces carry a guard band wider than this
// (POINT_TAN_HALF_FOV in renderer.cpp) so filtering never needs a neighbouring face.
#define MAX_PENUMBRA 0.04

uniform vec3 cameraPos;

uniform Material material;

// Linear lighting (Renderer::SetLinearLighting): colour uniforms and diffuse maps arrive
// already linear; the constants authored in this file are sRGB and get converted here.
uniform bool u_LinearLighting;
vec3 SrgbToLinear(vec3 c) { return u_LinearLighting ? pow(c, vec3(2.2)) : c; }

// Global ambient light, applied to every fragment regardless of light sources.
uniform vec3 u_Ambient;

// Self-illumination (added on top of lighting); drives the bloom/glow effect.
uniform vec3 u_Emissive;

// Environment reflection amount (0 = none, 1 = mirror).
uniform float u_Reflectivity;

// Optional diffuse texture. When u_HasDiffuseMap is false, material.diffuse is used.
uniform sampler2D u_DiffuseMap;
uniform bool u_HasDiffuseMap;

// Cut-out materials (foliage, chains): fragments whose diffuse alpha is below this are
// discarded. 0 = opaque.
uniform float u_AlphaCutoff;

// Optional tangent-space normal map ("depth"/surface detail). The tangent frame is
// derived from screen-space derivatives, so no per-vertex tangents are needed.
uniform sampler2D u_NormalMap;
uniform bool u_HasNormalMap;

// pow(0, 0) is undefined in GLSL (NaN on some drivers, which bloom then smears into
// black blocks), and the editor allows shininess 0, so keep the exponent >= 1.
float Shininess() { return max(material.shininess, 1.0); }

vec3 PerturbNormal(vec3 N, vec2 uv)
{
    vec3 mapN = texture(u_NormalMap, uv).xyz * 2.0 - 1.0;
    vec3 dp1 = dFdx(FragPos);
    vec3 dp2 = dFdy(FragPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    // No UV variation (e.g. a mesh without texcoords): the tangent frame is undefined
    // and inversesqrt(0) would turn the normal into NaN, so keep the surface normal.
    float lenSq = max(dot(T, T), dot(B, B));
    if (lenSq < 1e-12) { return N; }
    float invmax = inversesqrt(lenSq);
    mat3 TBN = mat3(T * invmax, B * invmax, N);
    return normalize(TBN * mapN);
}

// Procedural environment: a sky gradient above the horizon, dim ground below.
vec3 SampleEnvironment(vec3 dir)
{
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.55, 0.60, 0.70);
    vec3 zenith  = vec3(0.15, 0.35, 0.75);
    vec3 ground  = vec3(0.10, 0.10, 0.12);
    vec3 sky = mix(horizon, zenith, smoothstep(0.5, 1.0, t));
    return SrgbToLinear(mix(ground, sky, smoothstep(0.45, 0.55, t)));
}

// ---- Soft shadows (PCSS) ------------------------------------------------------------
// 1. Blocker search: average depth of the occluders between the receiver and the light.
// 2. Penumbra estimate: wider the further the receiver is behind its occluders, and the
//    bigger the light (contact hardening).
// 3. Filter: that many hardware-PCF taps over a disk of that size.
// Taps lie on a Vogel (golden-angle) disk rotated per pixel, so a few taps give a smooth
// penumbra instead of banding; QuadAverage then blends each 2x2 pixel quad, whose four
// rotations differ, so the per-pixel grain doesn't show.

float ShadowNoise()
{
    // interleaved gradient noise, as an angle
    return 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
}

// Box-average a value over the pixel's 2x2 quad using the screen-space derivatives
// (the quad neighbour is v +/- dFdx(v)). Must be called in uniform control flow.
float QuadAverage(float v)
{
    vec2 parity = mod(floor(gl_FragCoord.xy), 2.0) - 0.5; // -0.5 even, +0.5 odd
    v -= dFdx(v) * parity.x;
    v -= dFdy(v) * parity.y;
    return v;
}

vec2 VogelDisk(int i, int count, float phi)
{
    float r = sqrt((float(i) + 0.5) / float(count));
    float theta = float(i) * 2.3999632 + phi;
    return r * vec2(cos(theta), sin(theta));
}

// Tile UV -> atlas UV, clamped half a texel inside the tile so taps never read a neighbour.
vec2 AtlasUV(vec4 rect, vec2 uv, float texel)
{
    return rect.xy + clamp(uv, vec2(0.5 * texel), vec2(1.0 - 0.5 * texel)) * rect.zw;
}

// Average raw depth of the occluders within `radius` (x) and how many were found (y).
// `slope` (raw depth per tile uv) keeps a tilted receiver from occluding itself.
vec2 FindBlockers(highp sampler2D depthMap, vec4 rect, vec2 uv, float ref, float radius, float slope, float texel, int count, float phi)
{
    // The centre tap needs no slope allowance. Without it, near contact (receiver just
    // behind its occluder) the slope allowance can reject every disk tap, and pixels
    // inside the shadow would come out fully lit.
    float sum = 0.0;
    float found = 0.0;
    float center = textureLod(depthMap, AtlasUV(rect, uv, texel), 0.0).r;
    if (center < ref){
        sum = center;
        found = 1.0;
    }
    for (int i = 0; i < count; i++){
        vec2 offset = VogelDisk(i, count, phi) * radius;
        float depth = textureLod(depthMap, AtlasUV(rect, uv + offset, texel), 0.0).r;
        if (depth < ref - length(offset) * slope){
            sum += depth;
            found += 1.0;
        }
    }
    return vec2(found > 0.0 ? sum / found : 0.0, found);
}

// Fraction of the disk around uv that is in shadow (0 lit .. 1 shadowed).
float FilterShadow(highp sampler2DShadow shadowMap, vec4 rect, vec2 uv, float ref, float radius, float slope, float texel, int count, float phi)
{
    float lit = 0.0;
    for (int i = 0; i < count; i++){
        vec2 offset = VogelDisk(i, count, phi) * radius;
        lit += textureLod(shadowMap, vec3(AtlasUV(rect, uv + offset, texel), ref - length(offset) * slope), 0.0);
    }
    return 1.0 - lit / float(count);
}

// tan(angle between the surface and the light), capped: scales the slope bias.
float SlopeTan(float nDotL)
{
    return min(sqrt(max(1.0 - nDotL * nDotL, 0.0)) / max(nDotL, 0.05), 4.0);
}

float DirShadow(int i, vec3 geomNormal, vec3 lightDir, float phi)
{
    vec4 rect = u_DirShadowRect[i];
    if (rect.z <= 0.0) { return 0.0; }
    vec4 params = u_DirShadowParams[i];
    vec4 info = u_DirShadowInfo[i];
    float texel = info.x;

    // Normal offset: sample from slightly off the surface, more so at grazing angles.
    float nDotL = dot(geomNormal, lightDir);
    vec3 pos = FragPos + geomNormal * params.w * (1.0 - 0.5 * clamp(nDotL, 0.0, 1.0));
    vec4 ls = u_LightSpaceMatrices[i] * vec4(pos, 1.0);
    vec3 proj = ls.xyz / ls.w * 0.5 + 0.5;
    if (proj.z >= 1.0 || proj.x <= 0.0 || proj.x >= 1.0 || proj.y <= 0.0 || proj.y >= 1.0) { return 0.0; }

    float ref = proj.z - params.z;
    float slope = info.y * SlopeTan(abs(nDotL));
    int count = u_ShadowSamples;
    float radius = texel; // hard shadow: a hardware-PCF disk about one texel wide

    if (params.x > 0.0){
        // Blockers can sit anywhere between the light and the receiver.
        float search = clamp(proj.z * params.x, 2.0 * texel, MAX_PENUMBRA);
        vec2 blockers = FindBlockers(u_ShadowDepth, rect, proj.xy, ref, search, slope, texel, count, phi);
        if (blockers.y == 0.0) { return 0.0; }
        radius = clamp((ref - blockers.x) * params.x, texel, MAX_PENUMBRA);
    }
    float shadow = FilterShadow(u_ShadowMap, rect, proj.xy, ref, radius, slope, texel, count, phi);

    // Fade out toward the border of the shadowed region instead of cutting off.
    vec2 edge = abs(proj.xy - 0.5);
    shadow *= 1.0 - smoothstep(0.45, 0.5, max(edge.x, edge.y));
    return shadow * params.y;
}

float PointShadow(int j, vec3 geomNormal, vec3 lightPos, float phi)
{
    vec4 params = u_PointShadowParams[j];
    if (params.y <= 0.0) { return 0.0; }
    vec4 info = u_PointShadowInfo[j];
    float near = info.x, far = info.y, texel = info.z, tanHalf = info.w;

    // Cube face = the major axis of the light -> fragment vector.
    vec3 v = FragPos - lightPos;
    vec3 a = abs(v);
    int face;
    float dist;
    if (a.x >= a.y && a.x >= a.z) { face = v.x > 0.0 ? 0 : 1; dist = a.x; }
    else if (a.y >= a.z)          { face = v.y > 0.0 ? 2 : 3; dist = a.y; }
    else                          { face = v.z > 0.0 ? 4 : 5; dist = a.z; }
    if (dist >= far) { return 0.0; }
    vec4 rect = u_PointShadowRect[j * 6 + face];
    if (rect.z <= 0.0) { return 0.0; }

    // Texels grow with distance from the light; the biases are given in texels.
    float texelWorld = 2.0 * dist * tanHalf * texel;
    vec3 toLight = normalize(-v);
    float nDotL = dot(geomNormal, toLight);
    vec3 pos = FragPos + geomNormal * params.w * texelWorld * (1.0 - 0.5 * clamp(nDotL, 0.0, 1.0));
    vec4 ls = u_PointShadowMatrices[j * 6 + face] * vec4(pos, 1.0);
    vec3 proj = ls.xyz / ls.w * 0.5 + 0.5;
    if (proj.z >= 1.0) { return 0.0; }

    float zR = ls.w;                                     // linear depth along the face axis
    float rawPerWorld = near * far / ((far - near) * zR * zR); // d(raw depth)/d(depth) here
    float uvPerWorld = 1.0 / (2.0 * zR * tanHalf);
    float ref = proj.z - params.z * texelWorld * rawPerWorld;
    float slope = SlopeTan(abs(nDotL)) * rawPerWorld / uvPerWorld;
    int count = u_ShadowSamples;
    float radius = texel;

    if (params.x > 0.0){
        float search = clamp(2.0 * params.x * uvPerWorld, 2.0 * texel, MAX_PENUMBRA);
        vec2 blockers = FindBlockers(u_PointShadowDepth, rect, proj.xy, ref, search, slope, texel, count, phi);
        if (blockers.y == 0.0) { return 0.0; }
        float zB = near * far / (far - blockers.x * (far - near));
        float penumbra = params.x * (zR - zB) / zB; // world units at the receiver
        radius = clamp(penumbra * uvPerWorld, texel, MAX_PENUMBRA);
    }
    return FilterShadow(u_PointShadowMap, rect, proj.xy, ref, radius, slope, texel, count, phi) * params.y;
}

vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir, vec3 diffuseColor, float shadow)
{
    vec3 lightDir = normalize(-light.direction);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), Shininess());
    vec3 ambient  = light.ambient  * diffuseColor;
    vec3 diffuse  = light.diffuse  * diff * diffuseColor;
    vec3 specular = light.specular * spec * material.specular;
    // ambient still applies in shadow; diffuse+specular are occluded
    return ambient + (1.0 - shadow) * (diffuse + specular);
}

vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 diffuseColor, float shadow)
{
    vec3 lightDir = normalize(light.position - fragPos);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), Shininess());
    float distance    = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance +
  			     light.quadratic * (distance * distance));
    vec3 ambient  = light.ambient  * diffuseColor;
    vec3 diffuse  = light.diffuse  * diff * diffuseColor;
    vec3 specular = light.specular * spec * material.specular;
    ambient  *= attenuation;
    diffuse  *= attenuation;
    specular *= attenuation;
    return ambient + (1.0 - shadow) * (diffuse + specular);
}

void main()
{
    vec3 norm = normalize(Normal);
    if (u_HasNormalMap) { norm = PerturbNormal(norm, TexCoords); }
    vec3 viewDir = normalize(cameraPos - FragPos);
    vec4 diffuseSample = u_HasDiffuseMap ? texture(u_DiffuseMap, TexCoords) : vec4(material.diffuse, 1.0);
    vec3 diffuseColor = diffuseSample.rgb;

    // global ambient
    vec3 result = u_Ambient * diffuseColor;

    // Shadows use the geometric normal: a normal map's bumps shouldn't move the
    // shadow-map lookup.
    vec3 geomNormal = normalize(Normal);
    float phi = ShadowNoise();

    // Directional lights. A surface facing away from a light gets no diffuse/specular
    // from it anyway, so skip its shadow lookup.
    for (int i = 0; i < NR_DIR_LIGHTS; i++){
        if (i >= u_NumDirLights) { break; }
        vec3 lightDir = normalize(-dirLights[i].direction);
        float shadow = 0.0;
        if (u_ReceiveShadows && dot(norm, lightDir) > 0.0)
            shadow = DirShadow(i, geomNormal, lightDir, phi);
        shadow = clamp(QuadAverage(shadow), 0.0, 1.0);
        result += CalcDirLight(dirLights[i], norm, viewDir, diffuseColor, shadow);
    }

    // Point lights, each optionally with an omnidirectional shadow.
    for (int i = 0; i < NR_POINT_LIGHTS; i++){
        if (i >= u_NumPointLights) { break; }
        float shadow = 0.0;
        if (u_ReceiveShadows && dot(norm, pointLights[i].position - FragPos) > 0.0)
            shadow = PointShadow(i, geomNormal, pointLights[i].position, phi);
        shadow = clamp(QuadAverage(shadow), 0.0, 1.0);
        result += CalcPointLight(pointLights[i], norm, FragPos, viewDir, diffuseColor, shadow);
    }

    // environment reflection (mirror a procedural sky/ground)
    if (u_Reflectivity > 0.0){
        vec3 reflectDir = reflect(-viewDir, norm);
        result = mix(result, SampleEnvironment(reflectDir), u_Reflectivity);
    }

    // emissive / glow
    result += u_Emissive;

    // Alpha test last: discarding earlier would leave QuadAverage's derivatives
    // undefined for the rest of the pixel quad.
    if (diffuseSample.a < u_AlphaCutoff) { discard; }

    FragColor = vec4(result, 1.0);
}
