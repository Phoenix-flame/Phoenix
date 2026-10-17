// Bright-pass: keep only fragments brighter than a threshold (the bloom source), and
// downsample 2x into the half-resolution bloom buffer.

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
uniform float u_Threshold;

vec3 Bright(vec2 uv){
    vec3 color = texture(u_Scene, uv).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return (brightness > u_Threshold) ? color : vec3(0.0);
}

void main(){
    // This half-res pixel covers a 2x2 block of scene texels; each tap lands on one
    // texel centre. Thresholding per texel (not the block average) keeps small
    // highlights glowing instead of averaging them away.
    vec2 h = 0.5 / vec2(textureSize(u_Scene, 0));
    vec3 sum = Bright(TexCoords + vec2(-h.x, -h.y)) + Bright(TexCoords + vec2(h.x, -h.y))
             + Bright(TexCoords + vec2(-h.x,  h.y)) + Bright(TexCoords + vec2(h.x,  h.y));
    FragColor = vec4(sum * 0.25, 1.0);
}
