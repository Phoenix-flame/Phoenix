// Flat-color shader used to draw a selection outline (scaled shell, front-face culled).

#type vertex
#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 3) in vec4 aBoneIDs;
layout(location = 4) in vec4 aWeights;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// Skin the outline so a selected animated mesh's silhouette matches its pose.
const int MAX_BONES = 100;
uniform mat4 u_BoneMatrices[MAX_BONES];
uniform bool u_Animated;

void main()
{
    vec3 p = aPos;
    if (u_Animated){
        mat4 skin = mat4(0.0);
        float total = 0.0;
        for (int i = 0; i < 4; i++){
            if (aBoneIDs[i] < 0.0) { continue; }
            skin += u_BoneMatrices[int(aBoneIDs[i])] * aWeights[i];
            total += aWeights[i];
        }
        if (total > 0.0) { p = vec3(skin * vec4(aPos, 1.0)); }
    }
    gl_Position = projection * view * model * vec4(p, 1.0);
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
