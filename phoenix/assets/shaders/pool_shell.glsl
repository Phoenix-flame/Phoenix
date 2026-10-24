// Pool basin: procedural tiles (floor, walls) and a stone coping, lit by the sun and
// the ambient light. Below the waterline the sunlight arrives refracted through the
// moving surface: it is focused into caustics (from the pool's caustics texture) and
// dimmed by the water it crossed. Absorption along the VIEW path is added later, by
// the water surface (seen from above) or the underwater pass (camera in the water).

#type vertex
#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 WorldPos;
out vec3 LocalPos;
out vec3 LocalNormal;
out vec3 WorldNormal;

void main(){
    LocalPos = aPos;
    LocalNormal = aNormal;
    WorldPos = vec3(model * vec4(aPos, 1.0));
    WorldNormal = mat3(model) * aNormal;
    gl_Position = projection * view * vec4(WorldPos, 1.0);
}

#type fragment
#version 300 es
precision highp float;

in vec3 WorldPos;
in vec3 LocalPos;
in vec3 LocalNormal;
in vec3 WorldNormal;
out vec4 FragColor;

uniform vec2 u_PoolSize;        // inner width, length
uniform float u_WaterLevel;     // still surface, local y
uniform vec3 u_TileColor;
uniform vec3 u_GroutColor;
uniform float u_TileSize;
uniform vec3 u_Absorption;      // per metre, per channel
uniform float u_CausticStrength;
uniform vec3 u_LightDirLocal;   // direction sunlight travels, pool frame
uniform vec3 u_LightDirWorld;
uniform vec3 u_SunColor;
uniform vec3 u_Ambient;
uniform highp sampler2D u_HeightTex;
uniform highp sampler2D u_CausticsTex;
uniform bool u_LinearLighting;

vec3 SrgbToLinear(vec3 c) { return u_LinearLighting ? pow(c, vec3(2.2)) : c; }

// The sun's (directional light 0's) shadow, as water.glsl.
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

float Hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

// Tile albedo on a surface: square tiles with soft grout lines and a little variation.
vec3 Tiles(vec2 uv, float size, vec3 tile, vec3 grout){
    vec2 t = uv / size;
    vec2 f = fract(t);
    vec2 edge = min(f, 1.0 - f);
    float lineWidth = 0.045;
    float g = 1.0 - smoothstep(lineWidth * 0.6, lineWidth, min(edge.x, edge.y));
    float variation = 0.94 + 0.08 * Hash(floor(t));
    return mix(tile * variation, grout, g);
}

void main(){
    vec3 N = normalize(LocalNormal);
    bool coping = N.y > 0.5 && LocalPos.y > -0.001;

    // Albedo: stone coping on the rim, tiles inside (planar coordinates per face).
    vec3 albedo;
    if (coping){
        albedo = Tiles(LocalPos.xz + 0.5 * u_TileSize, u_TileSize * 2.0, SrgbToLinear(vec3(0.80, 0.77, 0.72)), SrgbToLinear(vec3(0.62, 0.60, 0.56)));
    }
    else{
        vec2 uv = abs(N.y) > 0.5 ? LocalPos.xz : (abs(N.x) > 0.5 ? LocalPos.zy : LocalPos.xy);
        albedo = Tiles(uv, u_TileSize, u_TileColor, u_GroutColor);
    }

    // Water surface right above this point (heights are relative to the still level).
    vec2 huv = clamp(LocalPos.xz / u_PoolSize + 0.5, 0.0, 1.0);
    float surface = u_WaterLevel + texture(u_HeightTex, huv).r;
    float shadow = SunShadow(WorldPos + normalize(WorldNormal) * 0.02);

    vec3 color;
    if (coping || LocalPos.y > surface){
        float diffuse = max(dot(normalize(WorldNormal), -u_LightDirWorld), 0.0);
        color = albedo * (u_Ambient + u_SunColor * diffuse * (1.0 - shadow));
        // Wet band just above the waterline on the walls.
        if (!coping) { color *= mix(0.78, 1.0, smoothstep(0.0, 0.06, LocalPos.y - surface)); }
    }
    else{
        // Sunlight refracted at a flat surface; the caustics texture holds how the real
        // surface focuses it, indexed by where that light entered the water.
        vec3 R0 = refract(normalize(u_LightDirLocal), vec3(0.0, 1.0, 0.0), 1.0 / 1.333);
        vec3 entry = LocalPos - R0 * ((LocalPos.y - u_WaterLevel) / R0.y);
        vec2 cuv = entry.xz / u_PoolSize + 0.5;
        float caustic = (cuv.x < 0.0 || cuv.x > 1.0 || cuv.y < 0.0 || cuv.y > 1.0) ? 1.0 : texture(u_CausticsTex, cuv).r;
        caustic = mix(1.0, caustic, u_CausticStrength);
        float travelled = max(surface - LocalPos.y, 0.0) / max(-R0.y, 0.05);
        vec3 light = u_SunColor * exp(-u_Absorption * travelled) * caustic * max(dot(N, -R0), 0.0) * (1.0 - shadow);
        // Ambient light under water: the sky seen through the water above, plus sunlight
        // the water scatters sideways (it lights walls and shaded spots too).
        vec3 ambient = (u_Ambient + 0.2 * u_SunColor) * exp(-u_Absorption * max(surface - LocalPos.y, 0.0));
        color = albedo * (ambient + light);
    }
    FragColor = vec4(color, 1.0);
}
