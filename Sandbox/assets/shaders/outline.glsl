// Flat-color shader used to draw a selection outline (scaled shell, front-face culled).

#type vertex
#version 300 es
precision mediump float;
in vec3 aPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}

#type fragment
#version 300 es
precision mediump float;

out vec4 FragColor;
uniform vec3 u_Color;

void main()
{
    FragColor = vec4(u_Color, 1.0);
}
