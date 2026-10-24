// Particle liquid, passes 1 and 2 (screen-space fluid rendering, Green 2010): each
// particle is a camera-facing point sprite shaped as a sphere.
//  u_Pass 0: the sphere's eye-space distance (nearest wins via the depth test).
//  u_Pass 1: the sphere's thickness along the view, added up (blending) for the
//            liquid's absorption.
// Fragments behind the opaque scene (its depth copy) are dropped.

#type vertex
#version 300 es
precision highp float;
layout(location = 0) in vec4 aPos;

uniform mat4 view;
uniform mat4 projection;
uniform float u_Radius;
uniform float u_ViewportHeight;

out vec3 ViewCenter;

void main(){
    vec4 v = view * vec4(aPos.xyz, 1.0);
    ViewCenter = v.xyz;
    gl_Position = projection * v;
    // Diameter in pixels at this distance.
    gl_PointSize = max(1.0, 2.0 * u_Radius * projection[1][1] * 0.5 * u_ViewportHeight / max(-v.z, 1e-3));
}

#type fragment
#version 300 es
precision highp float;

in vec3 ViewCenter;
out vec4 FragColor;

uniform mat4 projection;
uniform float u_Radius;
uniform int u_Pass;
uniform highp sampler2D u_SceneDepth;

// Eye distance of an NDC depth (perspective).
float EyeDistance(float ndcZ){ return projection[3][2] / (ndcZ + projection[2][2]); }

void main(){
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(c, c);
    if (r2 > 1.0) { discard; }
    float nz = sqrt(1.0 - r2);
    // Front of the sphere at this pixel (view space looks down -Z; point coords run down).
    vec3 p = ViewCenter + vec3(c.x, -c.y, nz) * u_Radius;
    float eye = -p.z;
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(u_SceneDepth, 0));
    float scene = EyeDistance(texture(u_SceneDepth, uv).r * 2.0 - 1.0);
    if (eye > scene) { discard; }

    if (u_Pass == 0){
        float ndcZ = (projection[2][2] * p.z + projection[3][2]) / -p.z;
        gl_FragDepth = ndcZ * 0.5 + 0.5;
        FragColor = vec4(eye, 0.0, 0.0, 1.0);
    }
    else{
        gl_FragDepth = gl_FragCoord.z;
        FragColor = vec4(2.0 * nz * u_Radius, 0.0, 0.0, 1.0);
    }
}
