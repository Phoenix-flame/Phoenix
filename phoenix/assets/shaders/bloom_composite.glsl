// Composite: add the blurred bloom on top of the HDR scene and output to LDR.

#type vertex
#version 300 es
precision mediump float;
in vec2 aPos;
in vec2 aTexCoords;
out vec2 TexCoords;
void main(){
    TexCoords = aTexCoords;
    gl_Position = vec4(aPos, 0.0, 1.0);
}

#type fragment
#version 300 es
precision mediump float;
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D u_Scene;
uniform sampler2D u_Bloom;
uniform float u_Intensity;
uniform float u_Exposure;

// Narkowicz ACES filmic tonemap: rolls bright HDR values off to white smoothly
// instead of hard-clipping, so glowing areas keep colour/detail.
vec3 ACESFilm(vec3 x){
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main(){
    vec3 scene = texture(u_Scene, TexCoords).rgb;
    vec3 bloom = texture(u_Bloom, TexCoords).rgb;
    vec3 color = (scene + bloom * u_Intensity) * u_Exposure;
    FragColor = vec4(ACESFilm(color), 1.0);
}
