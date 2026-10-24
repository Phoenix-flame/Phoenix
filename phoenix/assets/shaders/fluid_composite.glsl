// Particle liquid, pass 4: shades the smoothed surface into the scene. Normals come from
// the smoothed depth (the smaller one-sided difference, so edges don't smear); the
// scene copy is refracted through it and absorbed by the liquid's thickness; then the
// sky's reflection and the sun's glint are mixed in by Fresnel. Writes depth, so later
// geometry (outlines, gizmos) is occluded by the liquid as by any surface.

#type vertex
#version 300 es
precision highp float;
out vec2 UV;
void main(){
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    UV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}

#type fragment
#version 300 es
precision highp float;
in vec2 UV;
out vec4 FragColor;

uniform highp sampler2D u_Depth;       // smoothed eye distance (0 = no liquid)
uniform highp sampler2D u_Thickness;
uniform highp sampler2D u_SceneColor;
uniform highp sampler2D u_SceneDepth;
uniform mat4 projection;
uniform mat4 u_InvProjection;
uniform mat4 u_InvView;
uniform vec3 u_Color;           // tint of thick liquid (linear)
uniform float u_Clarity;        // metres before most light is absorbed
uniform vec3 u_LightDirWorld;   // direction sunlight travels
uniform vec3 u_SunColor;
uniform vec3 u_Ambient;
uniform bool u_LinearLighting;

vec3 SrgbToLinear(vec3 c) { return u_LinearLighting ? pow(c, vec3(2.2)) : c; }

// The procedural sky, with a lighter ground than the materials': liquid edges face
// sideways and down a lot, and a near-black ground would rim every drop in black.
vec3 SampleEnvironment(vec3 dir){
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.55, 0.60, 0.70);
    vec3 zenith  = vec3(0.15, 0.35, 0.75);
    vec3 ground  = vec3(0.42, 0.42, 0.44);
    vec3 sky = mix(horizon, zenith, smoothstep(0.5, 1.0, t));
    return SrgbToLinear(mix(ground, sky, smoothstep(0.45, 0.55, t)));
}

// View-space position of the liquid at uv.
vec3 ViewPosition(vec2 uv, float eye){
    vec4 p = u_InvProjection * vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    vec3 ray = p.xyz / p.w;
    return ray * (eye / -ray.z);
}

void main(){
    float eye = texture(u_Depth, UV).r;
    if (eye <= 0.0) { discard; }
    vec2 texel = 1.0 / vec2(textureSize(u_Depth, 0));
    vec3 P = ViewPosition(UV, eye);

    // Normal from the closer of the two one-sided differences on each axis.
    vec3 ddx = vec3(0.0), ddy = vec3(0.0);
    float r = texture(u_Depth, UV + vec2(texel.x, 0.0)).r, l = texture(u_Depth, UV - vec2(texel.x, 0.0)).r;
    vec3 dr = r > 0.0 ? ViewPosition(UV + vec2(texel.x, 0.0), r) - P : vec3(0.0);
    vec3 dl = l > 0.0 ? P - ViewPosition(UV - vec2(texel.x, 0.0), l) : vec3(0.0);
    ddx = (r <= 0.0 || (l > 0.0 && abs(dl.z) < abs(dr.z))) ? dl : dr;
    float u = texture(u_Depth, UV + vec2(0.0, texel.y)).r, d = texture(u_Depth, UV - vec2(0.0, texel.y)).r;
    vec3 du = u > 0.0 ? ViewPosition(UV + vec2(0.0, texel.y), u) - P : vec3(0.0);
    vec3 dd = d > 0.0 ? P - ViewPosition(UV - vec2(0.0, texel.y), d) : vec3(0.0);
    ddy = (u <= 0.0 || (d > 0.0 && abs(dd.z) < abs(du.z))) ? dd : du;
    vec3 Nv = normalize(cross(ddx, ddy));
    if (dot(Nv, vec3(0.0, 0.0, 1.0)) < 0.0) { Nv = -Nv; }   // facing the camera
    if (length(ddx) < 1e-6 || length(ddy) < 1e-6) { Nv = vec3(0.0, 0.0, 1.0); }

    vec3 N = normalize(mat3(u_InvView) * Nv);
    vec3 Pw = vec3(u_InvView * vec4(P, 1.0));
    vec3 camera = vec3(u_InvView * vec4(0.0, 0.0, 0.0, 1.0));
    vec3 V = normalize(camera - Pw);
    vec3 L = normalize(-u_LightDirWorld);

    // Refraction: offset into the scene copy by the normal, more through thick liquid.
    float thickness = texture(u_Thickness, UV).r;
    vec2 offset = Nv.xy * clamp(thickness, 0.0, 0.6) * 0.08;
    vec2 behindUV = clamp(UV - offset, vec2(0.0), vec2(1.0));
    // Nothing drawn behind (the background): the sky the refracted ray would see.
    vec3 behind = texture(u_SceneDepth, behindUV).r < 1.0 ? texture(u_SceneColor, behindUV).rgb
                : SampleEnvironment(refract(-V, N, 1.0 / 1.333));
    vec3 sigma = (vec3(1.0) - clamp(u_Color, 0.0, 1.0)) * (4.0 / max(u_Clarity, 0.05)) + vec3(0.02);
    vec3 T = exp(-sigma * thickness);
    vec3 lit = u_Ambient + u_SunColor * (0.35 + 0.65 * max(dot(N, L), 0.0));
    vec3 refracted = behind * T + u_Color * lit * (1.0 - T) * 0.5;

    vec3 R = reflect(-V, N);
    float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    vec3 reflected = SampleEnvironment(R);
    float spec = pow(max(dot(R, L), 0.0), 400.0) * 3.0 + pow(max(dot(R, L), 0.0), 40.0) * 0.08;
    vec3 color = mix(refracted, reflected, fresnel) + u_SunColor * spec;

    vec4 clip = projection * vec4(P, 1.0);
    gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
    FragColor = vec4(color, 1.0);
}
