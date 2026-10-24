// Pool caustics: the water surface grid is drawn where its refracted sunlight lands on
// the floor, and each fragment adds (area the light covered at the surface) / (area it
// covers on the floor): focused light gets brighter, spread light dimmer, and flat
// water gives exactly 1. The target is indexed by where light would have entered a
// FLAT surface to reach the same floor point, so shading a point anywhere under water
// (pool_shell.glsl) looks its caustic up by tracing the flat-refracted sun ray back
// up to the surface. Additive blending into a float texture.

#type vertex
#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;        // surface vertex at the still level (pool frame)
layout(location = 2) in vec2 aTexCoord;   // its height texel

uniform highp sampler2D u_HeightTex;
uniform vec2 u_PoolSize;
uniform vec2 u_Spacing;                   // world distance between grid vertices (x, z)
uniform float u_WaterLevel;
uniform float u_Floor;
uniform vec3 u_LightDirLocal;             // direction sunlight travels

out vec2 OldPos;
out vec2 NewPos;

void main(){
    vec2 texel = 1.0 / vec2(textureSize(u_HeightTex, 0));
    float h  = texture(u_HeightTex, aTexCoord).r;
    float hl = texture(u_HeightTex, aTexCoord - vec2(texel.x, 0.0)).r;
    float hr = texture(u_HeightTex, aTexCoord + vec2(texel.x, 0.0)).r;
    float hd = texture(u_HeightTex, aTexCoord - vec2(0.0, texel.y)).r;
    float hu = texture(u_HeightTex, aTexCoord + vec2(0.0, texel.y)).r;
    vec3 N = normalize(vec3(-(hr - hl) / (2.0 * u_Spacing.x), 1.0, -(hu - hd) / (2.0 * u_Spacing.y)));

    vec3 L = normalize(u_LightDirLocal);
    vec3 P = vec3(aPos.x, u_WaterLevel + h, aPos.z);
    vec3 R = refract(L, N, 1.0 / 1.333);
    vec3 hit = P + R * ((u_Floor - P.y) / min(R.y, -1e-3));
    // Back along the flat-water refracted ray to the still surface.
    vec3 R0 = refract(L, vec3(0.0, 1.0, 0.0), 1.0 / 1.333);
    vec3 entry = hit - R0 * ((hit.y - u_WaterLevel) / R0.y);

    OldPos = aPos.xz;
    NewPos = entry.xz;
    gl_Position = vec4(entry.xz / (0.5 * u_PoolSize), 0.0, 1.0);
}

#type fragment
#version 300 es
precision highp float;

in vec2 OldPos;
in vec2 NewPos;
out vec4 FragColor;

void main(){
    float oldArea = length(dFdx(OldPos)) * length(dFdy(OldPos));
    float newArea = length(dFdx(NewPos)) * length(dFdy(NewPos));
    // Focusing is capped at 4x: sharper peaks only blow out to white.
    float intensity = oldArea / max(newArea, oldArea * 0.25);
    FragColor = vec4(intensity, 0.0, 0.0, 1.0);
}
