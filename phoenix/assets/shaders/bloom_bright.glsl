// Bright-pass: keep only fragments brighter than a threshold (the bloom source).

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

void main(){
    vec3 color = texture(u_Scene, TexCoords).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    FragColor = (brightness > u_Threshold) ? vec4(color, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
}
