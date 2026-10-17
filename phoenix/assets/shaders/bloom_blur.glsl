// Separable Gaussian blur, run alternately horizontal/vertical (ping-pong).

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

uniform sampler2D u_Image;
uniform bool u_Horizontal;

void main(){
    float weight[5];
    weight[0] = 0.227027;
    weight[1] = 0.1945946;
    weight[2] = 0.1216216;
    weight[3] = 0.054054;
    weight[4] = 0.016216;

    vec2 texelSize = 1.0 / vec2(textureSize(u_Image, 0));
    vec3 result = texture(u_Image, TexCoords).rgb * weight[0];

    if (u_Horizontal){
        for (int i = 1; i < 5; ++i){
            result += texture(u_Image, TexCoords + vec2(texelSize.x * float(i), 0.0)).rgb * weight[i];
            result += texture(u_Image, TexCoords - vec2(texelSize.x * float(i), 0.0)).rgb * weight[i];
        }
    } else {
        for (int i = 1; i < 5; ++i){
            result += texture(u_Image, TexCoords + vec2(0.0, texelSize.y * float(i))).rgb * weight[i];
            result += texture(u_Image, TexCoords - vec2(0.0, texelSize.y * float(i))).rgb * weight[i];
        }
    }
    FragColor = vec4(result, 1.0);
}
