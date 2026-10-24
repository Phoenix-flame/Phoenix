// Background sky: one fullscreen triangle at the far plane. Each pixel turns its view
// direction into a colour from the gradient (with a sun disc) or an equirectangular
// panorama. Drawn first, without depth, so the scene simply covers it.

#type vertex
#version 300 es
precision highp float;

out vec2 v_NDC;

void main(){
    // Vertices (-1,-1), (3,-1), (-1,3): a triangle covering the whole screen.
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    v_NDC = pos;
    gl_Position = vec4(pos, 1.0, 1.0);
}

#type fragment
#version 300 es
precision highp float;

in vec2 v_NDC;
out vec4 FragColor;

// inverse(projection * view without translation): clip space -> world direction.
uniform mat4 u_InvViewProj;

uniform int u_SkyMode;           // 1 gradient, 2 panorama
uniform vec3 u_SkyZenith;        // colours arrive linear when linear lighting is on
uniform vec3 u_SkyHorizon;
uniform vec3 u_SkyGround;
uniform float u_SkyIntensity;
uniform sampler2D u_SkyPanorama;
uniform bool u_SkyHasPanorama;
uniform float u_SkyRotation;     // radians

uniform bool u_SunDisc;
uniform vec3 u_SunDir;           // towards the sun
uniform vec3 u_SunColor;
uniform float u_SunCos;          // cos(angular radius)

const float PI = 3.14159265;

vec3 Gradient(vec3 dir){
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = mix(u_SkyHorizon, u_SkyZenith, smoothstep(0.5, 1.0, t));
    return mix(u_SkyGround, sky, smoothstep(0.45, 0.55, t)) * u_SkyIntensity;
}

vec3 Panorama(vec3 dir){
    float u = atan(dir.x, -dir.z) / (2.0 * PI) + 0.5 + u_SkyRotation / (2.0 * PI);
    float v = asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5;
    // Level 0: the wrap seam at u = 0/1 would otherwise pick the smallest mip.
    return textureLod(u_SkyPanorama, vec2(u, v), 0.0).rgb * u_SkyIntensity;
}

void main(){
    vec4 world = u_InvViewProj * vec4(v_NDC, 1.0, 1.0);
    vec3 dir = normalize(world.xyz / world.w);

    vec3 color;
    if (u_SkyMode == 2 && u_SkyHasPanorama){
        color = Panorama(dir);
    }
    else{
        color = Gradient(dir);
        if (u_SunDisc){
            // Bright core with a soft glow around it (values above 1 bloom).
            float c = dot(dir, u_SunDir);
            float core = smoothstep(u_SunCos - 0.00015, u_SunCos + 0.00015, c);
            float glow = pow(max(c, 0.0), 600.0) * 0.6 + pow(max(c, 0.0), 40.0) * 0.12;
            color += u_SunColor * (core * 4.0 + glow) * step(0.0, dir.y + 0.02);
        }
    }
    FragColor = vec4(color, 1.0);
}
