// Basic Lighting Shader

#type vertex
#version 300 es
precision mediump float;
in vec3 aPos;
in vec3 aNormal;

out vec3 Normal;
out vec3 FragPos;
uniform mat4 model;
uniform mat4 projection;

void main()
{
    gl_Position = projection * model * vec4(aPos, 1.0);
    Normal = mat3(transpose(inverse(model))) * aNormal;  
    FragPos = vec3(model * vec4(aPos, 1.0));
}

#type fragment
#version 300 es
precision mediump float;
in vec3 Normal;  
in vec3 FragPos;  
out vec4 FragColor;
uniform vec3 color;
uniform vec3 lightColor;
uniform vec3 lightPos;
uniform vec3 viewPos;
void main()
{
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * lightColor;

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);  
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    float specularStrength = 0.5;
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);  
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3 specular = specularStrength * spec * lightColor;  

    vec3 result = (ambient + diffuse + specular) * color;
    FragColor = vec4(result, 1.0);
}


