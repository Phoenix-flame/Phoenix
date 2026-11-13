// Fire embers: glowing streaks along each ember's motion over a frame (motion blur),
// coloured by their temperature and added to the scene (depth-tested, not written).

#type vertex
#version 300 es
precision highp float;
layout(location = 0) in vec4 a_PositionTemperature;
layout(location = 1) in vec4 a_VelocityRadius;
uniform mat4 u_View;
uniform mat4 u_Projection;
uniform vec2 u_ViewportSize;
out vec2 v_Local;           // x along the streak (-1..1), y across it (-1..1)
out float v_Temperature;
out float v_Coverage;       // dims embers thinner than a pixel instead of shrinking them

void main(){
    vec2 corner = vec2(float(gl_VertexID & 1) * 2.0 - 1.0, float(gl_VertexID >> 1) * 2.0 - 1.0);
    vec3 position = a_PositionTemperature.xyz;
    vec3 velocity = a_VelocityRadius.xyz;
    float radius = a_VelocityRadius.w;

    vec4 head = u_Projection * u_View * vec4(position, 1.0);
    vec4 tail = u_Projection * u_View * vec4(position - velocity * (1.5 / 60.0), 1.0);
    vec2 halfSize = 0.5 * u_ViewportSize;
    vec2 h = head.xy / head.w * halfSize, t = tail.xy / max(tail.w, 1e-4) * halfSize;
    vec2 along = h - t;
    float len = length(along);
    vec2 axis = len > 1e-3 ? along / len : vec2(1.0, 0.0);
    vec2 across = vec2(-axis.y, axis.x);
    float pixels = radius * u_Projection[1][1] * halfSize.y / max(head.w, 1e-4);
    float width = max(pixels, 0.75);
    v_Coverage = min(pixels / width, 1.0);

    vec2 p = (corner.x < 0.0 ? t - axis * width : h + axis * width) + across * width * corner.y;
    v_Local = corner;
    v_Temperature = a_PositionTemperature.w;
    gl_Position = vec4(p / halfSize * head.w, head.z, head.w);
}

#type fragment
#version 300 es
precision highp float;
in vec2 v_Local;
in float v_Temperature;
in float v_Coverage;
out vec4 FragColor;
uniform highp sampler2D u_BlackBody;
uniform float u_KelvinScale;
uniform float u_Brightness;
uniform bool u_LinearLighting;

void main(){
    float fade = (1.0 - v_Local.y * v_Local.y) * (1.0 - 0.6 * max(-v_Local.x, 0.0));
    vec3 color = texture(u_BlackBody, vec2(v_Temperature * u_KelvinScale, 0.5)).rgb * 4.0;
    color *= u_Brightness * fade * v_Coverage;
    color = max(color, vec3(0.0));
    if (!u_LinearLighting) { color = pow(color, vec3(1.0 / 2.2)); }
    FragColor = vec4(color, 1.0);
}
