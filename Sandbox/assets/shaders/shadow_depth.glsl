// Depth-only pass: render the scene from the light's point of view to fill the
// shadow map. Fragment shader is empty (we only need depth).

#type vertex
#version 300 es
precision mediump float;
in vec3 aPos;

uniform mat4 u_LightSpaceMatrix;
uniform mat4 model;

void main(){
    gl_Position = u_LightSpaceMatrix * model * vec4(aPos, 1.0);
}

#type fragment
#version 300 es
precision mediump float;

void main(){
}
