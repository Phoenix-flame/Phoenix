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
    // The 9-tap Gaussian (weights 0.227, 0.195, 0.122, 0.054, 0.016) folded into 5
    // fetches: each pair of neighbouring taps becomes one bilinear sample placed
    // between them by weight, so the hardware filter does half the work.
    const float w0 = 0.2270270270;
    const float w1 = 0.3162162162; // 0.1945946 + 0.1216216
    const float w2 = 0.0702702703; // 0.0540540 + 0.0162162
    const float o1 = 1.3846153846;
    const float o2 = 3.2307692308;

    vec2 texelSize = 1.0 / vec2(textureSize(u_Image, 0));
    vec2 dir = u_Horizontal ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);

    vec3 result = texture(u_Image, TexCoords).rgb * w0;
    result += (texture(u_Image, TexCoords + dir * o1).rgb + texture(u_Image, TexCoords - dir * o1).rgb) * w1;
    result += (texture(u_Image, TexCoords + dir * o2).rgb + texture(u_Image, TexCoords - dir * o2).rgb) * w2;
    FragColor = vec4(result, 1.0);
}
