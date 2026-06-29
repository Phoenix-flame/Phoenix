// Animated transparent water surface: sum-of-sines wave displacement with analytic
// normals, Fresnel environment reflection, and a sun specular highlight.

#type vertex
#version 300 es
precision mediump float;
in vec3 aPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float u_Time;
uniform float u_Amplitude;
uniform float u_WaveScale;
uniform float u_Speed;

out vec3 FragPos;
out vec3 Normal;

// Sum of directional sine waves; also returns the xz slope (derivatives) for normals.
float waveHeight(vec2 p, out vec2 slope){
    slope = vec2(0.0);
    float h = 0.0;

    vec2 d1 = normalize(vec2(1.0, 0.35));
    float f1 = u_WaveScale;
    float ph1 = dot(p, d1) * f1 + u_Time * u_Speed;
    h += sin(ph1) * u_Amplitude;
    slope += d1 * (f1 * cos(ph1) * u_Amplitude);

    vec2 d2 = normalize(vec2(-0.45, 1.0));
    float f2 = u_WaveScale * 1.9;
    float ph2 = dot(p, d2) * f2 + u_Time * u_Speed * 1.25;
    h += sin(ph2) * u_Amplitude * 0.5;
    slope += d2 * (f2 * cos(ph2) * u_Amplitude * 0.5);

    vec2 d3 = normalize(vec2(0.7, -0.7));
    float f3 = u_WaveScale * 3.1;
    float ph3 = dot(p, d3) * f3 + u_Time * u_Speed * 1.7;
    h += sin(ph3) * u_Amplitude * 0.25;
    slope += d3 * (f3 * cos(ph3) * u_Amplitude * 0.25);

    return h;
}

void main(){
    vec3 world = vec3(model * vec4(aPos, 1.0));
    vec2 slope;
    world.y += waveHeight(world.xz, slope);

    FragPos = world;
    Normal = normalize(vec3(-slope.x, 1.0, -slope.y));
    gl_Position = projection * view * vec4(world, 1.0);
}

#type fragment
#version 300 es
precision mediump float;

in vec3 FragPos;
in vec3 Normal;
out vec4 FragColor;

uniform vec3 u_CameraPos;
uniform vec3 u_Color;
uniform float u_Alpha;
uniform vec3 u_LightDir; // direction the sun travels

// Same procedural sky/ground as the reflective materials.
vec3 SampleEnvironment(vec3 dir){
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.55, 0.60, 0.70);
    vec3 zenith  = vec3(0.15, 0.35, 0.75);
    vec3 ground  = vec3(0.10, 0.10, 0.12);
    vec3 sky = mix(horizon, zenith, smoothstep(0.5, 1.0, t));
    return mix(ground, sky, smoothstep(0.45, 0.55, t));
}

void main(){
    vec3 N = normalize(Normal);
    vec3 V = normalize(u_CameraPos - FragPos);

    // Fresnel: more reflective at grazing angles.
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    fresnel = mix(0.06, 1.0, fresnel);

    vec3 envColor = SampleEnvironment(reflect(-V, N));

    // Sun specular (sparkle).
    vec3 L = normalize(-u_LightDir);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 200.0);

    vec3 color = mix(u_Color, envColor, fresnel) + vec3(spec);
    // grazing/reflective angles read more opaque
    float alpha = clamp(u_Alpha + fresnel * 0.4 + spec, 0.0, 1.0);
    FragColor = vec4(color, alpha);
}
