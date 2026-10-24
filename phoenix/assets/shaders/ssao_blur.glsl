// Depth-aware 4x4 blur of the raw SSAO: averages away the per-pixel sample rotation,
// skipping neighbours at a different depth so occlusion doesn't bleed across edges.

#type vertex
#version 300 es
precision highp float;

out vec2 v_UV;

void main(){
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    v_UV = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}

#type fragment
#version 300 es
precision highp float;

in vec2 v_UV;
out vec4 FragColor;

uniform sampler2D u_AO;
uniform highp sampler2D u_Depth;
uniform mat4 u_InvProjection;
uniform vec2 u_InvSize;

float ViewZ(vec2 uv){
    float d = textureLod(u_Depth, uv, 0.0).r;
    vec4 p = u_InvProjection * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.z / p.w;
}

void main(){
    float centerZ = ViewZ(v_UV);
    float sum = 0.0, weight = 0.0;
    for (int y = -2; y < 2; y++){
        for (int x = -2; x < 2; x++){
            vec2 uv = v_UV + (vec2(float(x), float(y)) + 0.5) * u_InvSize;
            // Relative depth difference: tolerant far away, strict up close.
            float w = 1.0 - smoothstep(0.0, 0.05, abs(ViewZ(uv) - centerZ) / max(abs(centerZ), 1e-3));
            sum += textureLod(u_AO, uv, 0.0).r * w;
            weight += w;
        }
    }
    float ao = weight > 0.0 ? sum / weight : textureLod(u_AO, v_UV, 0.0).r;
    FragColor = vec4(vec3(ao), 1.0);
}
