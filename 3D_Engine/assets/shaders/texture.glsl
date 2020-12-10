// Basic Texture Shader

#type vertex
#version 130
in vec3 aPos;
in vec2 aTexCoord;
out vec2 TexCoord;

uniform mat4 model;
uniform mat4 projection;

void main()
{
    gl_Position = projection * model * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
}

#type fragment
#version 130

out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D ourTexture;
uniform vec4 color;

void main()
{
    FragColor = texture(ourTexture, TexCoord);
}


