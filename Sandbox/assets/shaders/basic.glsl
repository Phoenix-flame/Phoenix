// Basic Triangle Shader

#type vertex
#version 300 es
precision mediump float;

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 2) in mat4 amodel;
out vec3 color;

uniform mat4 view;
uniform mat4 projection;
void main()
{
    color = aColor;
    gl_Position = projection * view * (amodel) * (vec4(aPos, 1.0));
}

#type fragment
#version 300 es
precision mediump float;

in vec3 color;
out vec4 FragColor;
  
uniform vec4 my_color;

void main()
{
    FragColor = vec4(color, 1.0f) + my_color;
}



