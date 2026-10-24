// Full-screen haze while the camera is in a pool's water: every pixel of the scene copy
// is dimmed and tinted by the water between the eye and what it shows (up to where the
// view ray leaves through the surface; the surface itself is drawn afterwards).

#type vertex
#version 300 es
precision highp float;
out vec2 UV;
void main(){
    // One triangle covering the screen.
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    UV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}

#type fragment
#version 300 es
precision highp float;
in vec2 UV;
out vec4 FragColor;

uniform highp sampler2D u_SceneColor;
uniform highp sampler2D u_SceneDepth;
uniform mat4 u_InvViewProj;
uniform vec3 u_CameraPos;
uniform float u_SurfaceY;       // still water level (world)
uniform vec3 u_Absorption;
uniform vec3 u_WaterColor;
uniform vec3 u_SunColor;
uniform vec3 u_Ambient;

void main(){
    float d = texture(u_SceneDepth, UV).r;
    vec4 p = u_InvViewProj * vec4(UV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec3 target = p.xyz / p.w;
    vec3 ray = target - u_CameraPos;
    float dist = length(ray);
    vec3 dir = ray / max(dist, 1e-5);
    // Leaves the water where the ray crosses the surface going up.
    float path = dist;
    if (dir.y > 1e-4) { path = min(path, (u_SurfaceY - u_CameraPos.y) / dir.y); }
    if (d >= 1.0) { path = dir.y > 1e-4 ? (u_SurfaceY - u_CameraPos.y) / dir.y : 60.0; }
    path = clamp(path, 0.0, 60.0);

    vec3 T = exp(-u_Absorption * path);
    vec3 inscatter = u_WaterColor * (u_Ambient + 0.35 * u_SunColor);
    FragColor = vec4(texture(u_SceneColor, UV).rgb * T + inscatter * (1.0 - T), 1.0);
}
