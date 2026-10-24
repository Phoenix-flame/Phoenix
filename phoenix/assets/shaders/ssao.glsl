// Screen-space ambient occlusion from the camera depth prepass. Each pixel's view-space
// position and normal are rebuilt from depth; samples in the hemisphere around it count
// as occluded where the depth buffer shows geometry in front of them.

#type vertex
#version 300 es
precision highp float;

out vec2 v_UV;

void main(){
    // Vertices (-1,-1), (3,-1), (-1,3): a triangle covering the whole screen.
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    v_UV = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}

#type fragment
#version 300 es
precision highp float;

in vec2 v_UV;
out vec4 FragColor;

uniform highp sampler2D u_Depth;
uniform mat4 u_Projection;
uniform mat4 u_InvProjection;
uniform vec2 u_InvSize;     // 1 / target size
uniform float u_Radius;     // world (= view-space) units
uniform float u_Strength;   // exponent on the result: > 1 darker

const int SAMPLES = 16;

vec3 ViewPos(vec2 uv){
    float d = textureLod(u_Depth, uv, 0.0).r;
    vec4 p = u_InvProjection * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

void main(){
    float depth = textureLod(u_Depth, v_UV, 0.0).r;
    if (depth >= 1.0) { FragColor = vec4(1.0); return; } // sky: nothing to occlude

    vec3 P = ViewPos(v_UV);
    // Normal from neighbouring depths, taking the side with the smaller step so depth
    // edges don't bend it.
    vec3 l = ViewPos(v_UV - vec2(u_InvSize.x, 0.0)), r = ViewPos(v_UV + vec2(u_InvSize.x, 0.0));
    vec3 b = ViewPos(v_UV - vec2(0.0, u_InvSize.y)), t = ViewPos(v_UV + vec2(0.0, u_InvSize.y));
    vec3 dx = abs(r.z - P.z) < abs(P.z - l.z) ? r - P : P - l;
    vec3 dy = abs(t.z - P.z) < abs(P.z - b.z) ? t - P : P - b;
    vec3 N = normalize(cross(dx, dy));
    if (dot(N, -P) < 0.0) { N = -N; }

    // Frame around N, turned per pixel (interleaved gradient noise) so the 16 samples
    // cover different directions in neighbouring pixels; the blur pass averages them.
    float angle = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    vec3 helper = abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(helper, N));
    vec3 B = cross(N, T);
    vec3 Tr = T * cos(angle) + B * sin(angle);
    vec3 Br = cross(N, Tr);

    float occlusion = 0.0;
    for (int i = 0; i < SAMPLES; i++){
        // Spiral over the hemisphere, denser near the centre (closer occluders matter more).
        float fi = float(i) + 0.5;
        float r01 = fi / float(SAMPLES);
        float phi = fi * 2.3999632;
        float cosTheta = sqrt(1.0 - r01);
        float sinTheta = sqrt(r01);
        vec3 dir = Tr * (cos(phi) * sinTheta) + Br * (sin(phi) * sinTheta) + N * cosTheta;
        float scale = mix(0.1, 1.0, r01 * r01);
        vec3 S = P + dir * u_Radius * scale;

        vec4 clip = u_Projection * vec4(S, 1.0);
        vec2 suv = clip.xy / clip.w * 0.5 + 0.5;
        if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) { continue; }
        float sceneZ = ViewPos(suv).z;
        // Ignore occluders far outside the radius (a wall behind a pillar's silhouette).
        float range = smoothstep(0.0, 1.0, u_Radius / max(abs(P.z - sceneZ), 1e-4));
        occlusion += (sceneZ >= S.z + 0.02 * u_Radius ? 1.0 : 0.0) * range;
    }
    float ao = 1.0 - occlusion / float(SAMPLES);
    FragColor = vec4(vec3(pow(clamp(ao, 0.0, 1.0), u_Strength)), 1.0);
}
