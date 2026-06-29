// Depth-only pass: render the scene from the light's point of view to fill the
// shadow map. Fragment shader is empty (we only need depth).

#type vertex
#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 3) in vec4 aBoneIDs;
layout(location = 4) in vec4 aWeights;

uniform mat4 u_LightSpaceMatrix;
uniform mat4 model;

// Skin animated casters so their shadow matches the posed mesh.
const int MAX_BONES = 100;
uniform mat4 u_BoneMatrices[MAX_BONES];
uniform bool u_Animated;

void main(){
    vec3 localPos = aPos;
    if (u_Animated){
        mat4 skin = mat4(0.0);
        float total = 0.0;
        for (int i = 0; i < 4; i++){
            if (aBoneIDs[i] < 0.0) { continue; }
            skin += u_BoneMatrices[int(aBoneIDs[i])] * aWeights[i];
            total += aWeights[i];
        }
        if (total > 0.0) { localPos = vec3(skin * vec4(aPos, 1.0)); }
    }
    gl_Position = u_LightSpaceMatrix * model * vec4(localPos, 1.0);
}

#type fragment
#version 300 es
precision mediump float;

void main(){
}
