// Particle liquid, pass 3: bilateral blur of the sphere depths (one axis per run), so
// the spheres melt into one smooth surface while separate layers (a big jump in depth)
// stay apart. Empty pixels (0) stay empty and don't contribute.

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

uniform highp sampler2D u_Depth;
uniform vec2 u_Direction;       // one texel along x or y
uniform float u_WorldRadius;    // blur reach in world units (a few particle radii)
uniform float u_PixelsPerUnit;  // projection scale: pixels per world unit at distance 1
uniform float u_DepthFalloff;   // depth difference (world) that stops the blur

void main(){
    float d = texture(u_Depth, UV).r;
    if (d <= 0.0) { FragColor = vec4(0.0); return; }
    // Wider in pixels up close, narrower far away; capped for cost.
    float radius = clamp(u_WorldRadius * u_PixelsPerUnit / d, 1.0, 24.0);
    float sigma = radius * 0.5;
    float sum = 0.0, weight = 0.0;
    for (int i = -24; i <= 24; i++){
        float x = float(i);
        if (abs(x) > radius) { continue; }
        float s = texture(u_Depth, UV + u_Direction * x).r;
        if (s <= 0.0) { continue; }
        float w = exp(-x * x / (2.0 * sigma * sigma));
        float dz = (s - d) / u_DepthFalloff;
        w *= exp(-dz * dz);
        sum += s * w;
        weight += w;
    }
    FragColor = vec4(weight > 0.0 ? sum / weight : d, 0.0, 0.0, 1.0);
}
