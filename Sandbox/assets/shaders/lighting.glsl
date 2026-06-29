// Basic Lighting Shader

#type vertex
#version 300 es
precision mediump float;
in vec3 aPos;
in vec3 aNormal;
in vec2 aTexCoords;

out vec3 Normal;
out vec3 FragPos;
out vec2 TexCoords;
out vec4 FragPosLightSpace;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 u_LightSpaceMatrix;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    Normal = mat3(transpose(inverse(model))) * aNormal;
    FragPos = vec3(model * vec4(aPos, 1.0));
    TexCoords = aTexCoords;
    FragPosLightSpace = u_LightSpaceMatrix * vec4(FragPos, 1.0);
}

#type fragment
#version 300 es
precision mediump float;

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
uniform PointLight pointLights[NR_POINT_LIGHTS];




in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoords;
in vec4 FragPosLightSpace;
out vec4 FragColor;

// Directional shadow map.
uniform sampler2D u_ShadowMap;
uniform bool u_ShadowsEnabled;
uniform vec3 boxColor;
uniform vec3 lightColor;
uniform vec3 lightPos;
uniform vec3 cameraPos;

uniform Material material;
uniform DirLight dirLight;

// Global ambient light, applied to every fragment regardless of light sources.
uniform vec3 u_Ambient;

// Self-illumination (added on top of lighting); drives the bloom/glow effect.
uniform vec3 u_Emissive;

// Optional diffuse texture. When u_HasDiffuseMap is false, material.diffuse is used.
uniform sampler2D u_DiffuseMap;
uniform bool u_HasDiffuseMap;

float ShadowCalculation(vec3 normal, vec3 lightDir)
{
    if (!u_ShadowsEnabled) { return 0.0; }
    // perspective divide + map to [0,1]
    vec3 projCoords = FragPosLightSpace.xyz / FragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0) { return 0.0; } // beyond the shadow frustum -> lit

    float currentDepth = projCoords.z;
    // slope-scaled bias to avoid shadow acne
    float bias = max(0.0025 * (1.0 - dot(normal, lightDir)), 0.0008);

    // 3x3 PCF for soft edges
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(u_ShadowMap, 0));
    for (int x = -1; x <= 1; ++x){
        for (int y = -1; y <= 1; ++y){
            float pcfDepth = texture(u_ShadowMap, projCoords.xy + vec2(float(x), float(y)) * texelSize).r;
            shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir, vec3 diffuseColor)
{
    vec3 lightDir = normalize(-light.direction);
    // diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    // specular shading
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    // combine results
    vec3 ambient  = light.ambient  * diffuseColor;
    vec3 diffuse  = light.diffuse  * diff * diffuseColor;
    vec3 specular = light.specular * spec * material.specular;
    // directional light is shadowed; ambient still applies
    float shadow = ShadowCalculation(normal, lightDir);
    return (ambient + (1.0 - shadow) * (diffuse + specular));
}

vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 diffuseColor)
{
    vec3 lightDir = normalize(light.position - fragPos);
    // diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    // specular shading
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    // attenuation
    float distance    = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance +
  			     light.quadratic * (distance * distance));
    // combine results
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
    // properties
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(cameraPos - FragPos);
    vec3 diffuseColor = u_HasDiffuseMap ? texture(u_DiffuseMap, TexCoords).rgb : material.diffuse;
    // global ambient
    vec3 result = u_Ambient * diffuseColor;
    // phase 1: Directional lighting
    result += CalcDirLight(dirLight, norm, viewDir, diffuseColor);
    // phase 2: Point lights
    for(int i = 0; i < NR_POINT_LIGHTS; i++)
        result += CalcPointLight(pointLights[i], norm, FragPos, viewDir, diffuseColor);

    // emissive / glow
    result += u_Emissive;

    FragColor = vec4(result, 1.0);
}
