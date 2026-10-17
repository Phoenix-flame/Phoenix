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
uniform bool u_EncodeSRGB; // linear lighting: the scene is linear, the display expects sRGB

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
    vec3 color = ACESFilm((scene + bloom * u_Intensity) * u_Exposure);
    if (u_EncodeSRGB){
        // sRGB OETF (piecewise, matches what an sRGB framebuffer would apply).
        vec3 lo = color * 12.92;
        vec3 hi = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
        color = mix(hi, lo, vec3(lessThanEqual(color, vec3(0.0031308))));
    }
    FragColor = vec4(color, 1.0);
}
