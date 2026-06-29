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

void main(){
    vec3 scene = texture(u_Scene, TexCoords).rgb;
    vec3 bloom = texture(u_Bloom, TexCoords).rgb;
    FragColor = vec4(scene + bloom * u_Intensity, 1.0);
}
