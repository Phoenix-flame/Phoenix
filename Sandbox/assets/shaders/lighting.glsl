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

    gl_Position = projection * view * model * vec4(localPos, 1.0);
    Normal = mat3(transpose(inverse(model))) * localNormal;
    FragPos = vec3(model * vec4(localPos, 1.0));
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
#define NR_DIR_LIGHTS 4
#define NR_SHADOWS 4
uniform PointLight pointLights[NR_POINT_LIGHTS];

// Multiple directional lights, each optionally casting its own shadow. Directional
// light i is shadowed by u_ShadowMaps[i] / u_LightSpaceMatrices[i] when i < u_NumShadows.
uniform DirLight dirLights[NR_DIR_LIGHTS];
uniform int u_NumDirLights;

in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoords;
out vec4 FragColor;

// Directional shadow maps (comparison samplers -> hardware PCF). GLES requires an
// explicit precision for shadow samplers. Indexed only by the loop counter below
// (a constant-index-expression, which GLSL ES 3.00 requires for sampler arrays).
uniform highp sampler2DShadow u_ShadowMaps[NR_SHADOWS];
uniform mat4 u_LightSpaceMatrices[NR_SHADOWS];
uniform int u_NumShadows;
uniform bool u_ShadowsEnabled;

uniform vec3 cameraPos;

uniform Material material;

// Global ambient light, applied to every fragment regardless of light sources.
uniform vec3 u_Ambient;

// Self-illumination (added on top of lighting); drives the bloom/glow effect.
uniform vec3 u_Emissive;

// Environment reflection amount (0 = none, 1 = mirror).
uniform float u_Reflectivity;

// Optional diffuse texture. When u_HasDiffuseMap is false, material.diffuse is used.
uniform sampler2D u_DiffuseMap;
uniform bool u_HasDiffuseMap;

// Procedural environment: a sky gradient above the horizon, dim ground below.
vec3 SampleEnvironment(vec3 dir)
{
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.55, 0.60, 0.70);
    vec3 zenith  = vec3(0.15, 0.35, 0.75);
    vec3 ground  = vec3(0.10, 0.10, 0.12);
    vec3 sky = mix(horizon, zenith, smoothstep(0.5, 1.0, t));
    return mix(ground, sky, smoothstep(0.45, 0.55, t));
}

vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir, vec3 diffuseColor, float shadow)
{
    vec3 lightDir = normalize(-light.direction);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3 ambient  = light.ambient  * diffuseColor;
    vec3 diffuse  = light.diffuse  * diff * diffuseColor;
    vec3 specular = light.specular * spec * material.specular;
    // ambient still applies in shadow; diffuse+specular are occluded
    return ambient + (1.0 - shadow) * (diffuse + specular);
}

vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 diffuseColor)
{
    vec3 lightDir = normalize(light.position - fragPos);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    float distance    = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance +
  			     light.quadratic * (distance * distance));
    vec3 ambient  = light.ambient  * diffuseColor;
    vec3 diffuse  = light.diffuse  * diff * diffuseColor;
    vec3 specular = light.specular * spec * material.specular;
    ambient  *= attenuation;
    diffuse  *= attenuation;
    specular *= attenuation;
    return (ambient + diffuse + specular);
}

void main()
{
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(cameraPos - FragPos);
    vec3 diffuseColor = u_HasDiffuseMap ? texture(u_DiffuseMap, TexCoords).rgb : material.diffuse;

    // global ambient
    vec3 result = u_Ambient * diffuseColor;

    // Directional lights, each with its own shadow map. The shadow sampling is INLINE
    // here so u_ShadowMaps[] is indexed by the loop counter (required by GLSL ES 3.00).
    for (int i = 0; i < NR_DIR_LIGHTS; i++){
        if (i >= u_NumDirLights) { break; }

        float shadow = 0.0;
        if (u_ShadowsEnabled && i < u_NumShadows){
            vec3 lightDir = normalize(-dirLights[i].direction);
            vec4 fls = u_LightSpaceMatrices[i] * vec4(FragPos, 1.0);
            vec3 proj = fls.xyz / fls.w;
            proj = proj * 0.5 + 0.5;
            if (proj.z <= 1.0){
                float bias = max(0.0025 * (1.0 - dot(norm, lightDir)), 0.0008);
                float refDepth = proj.z - bias;
                vec2 texel = (1.0 / vec2(textureSize(u_ShadowMaps[i], 0))) * 1.4;
                float s = 0.0;
                for (int x = -1; x <= 1; x++){
                    for (int y = -1; y <= 1; y++){
                        s += 1.0 - texture(u_ShadowMaps[i], vec3(proj.xy + vec2(float(x), float(y)) * texel, refDepth));
                    }
                }
                shadow = s / 9.0;
            }
        }

        result += CalcDirLight(dirLights[i], norm, viewDir, diffuseColor, shadow);
    }

    // Point lights (unshadowed).
    for (int i = 0; i < NR_POINT_LIGHTS; i++)
        result += CalcPointLight(pointLights[i], norm, FragPos, viewDir, diffuseColor);

    // environment reflection (mirror a procedural sky/ground)
    if (u_Reflectivity > 0.0){
        vec3 reflectDir = reflect(-viewDir, norm);
        result = mix(result, SampleEnvironment(reflectDir), u_Reflectivity);
    }

    // emissive / glow
    result += u_Emissive;

    FragColor = vec4(result, 1.0);
}
