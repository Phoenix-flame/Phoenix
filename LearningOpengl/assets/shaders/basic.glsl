// Basic Triangle Shader

#type vertex
#version 130

in vec3 aPos;
in vec3 aColor;

out vec3 color;
uniform mat4 projection;
void main()
{
    color = aColor;
    gl_Position = projection * vec4(aPos, 1.0);
}

#type fragment
#version 130

in vec3 color;
out vec4 FragColor;
  
uniform vec4 my_color;

void main()
{
    FragColor = vec4(color, 1.0f) + my_color;
}



