// Composite: add the blurred bloom on top of the HDR scene and output to LDR, optionally
// with FXAA (fast approximate anti-aliasing) on the final, tonemapped image.

#type vertex
#version 300 es
precision mediump float;
in vec2 aPos;
in vec2 aTexCoords;
out vec2 TexCoords;
void main(){
    TexCoords = aTexCoords;
    gl_Position = vec4(aPos, 0.0, 1.0);
}

#type fragment
#version 300 es
precision highp float; // FXAA offsets are fractions of a texel
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D u_Scene;
uniform sampler2D u_Bloom;
uniform float u_Intensity;
uniform float u_Exposure;
uniform bool u_EncodeSRGB; // linear lighting: the scene is linear, the display expects sRGB
uniform bool u_FXAA;
uniform bool u_Tonemap;    // false: the scene is already display-ready (only FXAA is wanted)
uniform vec2 u_TexelSize;  // 1 / target size

// Narkowicz ACES filmic tonemap: rolls bright HDR values off to white smoothly
// instead of hard-clipping, so glowing areas keep colour/detail.
vec3 ACESFilm(vec3 x){
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// The displayed colour at uv: scene + bloom, exposed, tonemapped, display-encoded.
vec3 Display(vec2 uv){
    // Explicit LOD 0: FXAA calls this inside branches and loops, where implicit
    // derivatives are undefined.
    vec3 scene = textureLod(u_Scene, uv, 0.0).rgb;
    vec3 bloom = textureLod(u_Bloom, uv, 0.0).rgb;
    if (!u_Tonemap) { return clamp(scene, 0.0, 1.0); }
    vec3 color = ACESFilm((scene + bloom * u_Intensity) * u_Exposure);
    if (u_EncodeSRGB){
        // sRGB OETF (piecewise, matches what an sRGB framebuffer would apply).
        vec3 lo = color * 12.92;
        vec3 hi = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
        color = mix(hi, lo, vec3(lessThanEqual(color, vec3(0.0031308))));
    }
    return color;
}

float Luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// FXAA 3.11 "quality" (after Timothy Lottes), on the displayed image so it also smooths
// shading and specular aliasing that MSAA leaves alone. Per pixel: skip low contrast;
// find whether the local edge runs horizontally or vertically; walk along it both ways to
// its ends; then shift the lookup across the edge by how far this pixel sits from the
// nearer end (at most half a pixel), or by a small sub-pixel blend, whichever is larger.
// One bilinear lookup at the shifted uv mixes in just the neighbour across the edge,
// which keeps texture detail far better than a blur along a guessed direction.
float EdgeStep(int i) { return i < 5 ? 1.5 : (i < 10 ? 2.0 : (i < 11 ? 4.0 : 8.0)); }

vec3 Fxaa(vec2 uv){
    vec2 t = u_TexelSize;
    vec3 colorCenter = Display(uv);
    float lumaCenter = Luma(colorCenter);
    float lumaDown  = Luma(Display(uv + vec2( 0.0, -1.0) * t));
    float lumaUp    = Luma(Display(uv + vec2( 0.0,  1.0) * t));
    float lumaLeft  = Luma(Display(uv + vec2(-1.0,  0.0) * t));
    float lumaRight = Luma(Display(uv + vec2( 1.0,  0.0) * t));
    float lumaMin = min(lumaCenter, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
    float lumaMax = max(lumaCenter, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));
    float lumaRange = lumaMax - lumaMin;
    if (lumaRange < max(0.0312, lumaMax * 0.125)) { return colorCenter; }

    float lumaDL = Luma(Display(uv + vec2(-1.0, -1.0) * t));
    float lumaUR = Luma(Display(uv + vec2( 1.0,  1.0) * t));
    float lumaUL = Luma(Display(uv + vec2(-1.0,  1.0) * t));
    float lumaDR = Luma(Display(uv + vec2( 1.0, -1.0) * t));
    float lumaDownUp = lumaDown + lumaUp;
    float lumaLeftRight = lumaLeft + lumaRight;
    float lumaLeftCorners = lumaDL + lumaUL;
    float lumaDownCorners = lumaDL + lumaDR;
    float lumaRightCorners = lumaDR + lumaUR;
    float lumaUpCorners = lumaUR + lumaUL;

    float edgeHorizontal = abs(-2.0 * lumaLeft + lumaLeftCorners) + abs(-2.0 * lumaCenter + lumaDownUp) * 2.0
                         + abs(-2.0 * lumaRight + lumaRightCorners);
    float edgeVertical = abs(-2.0 * lumaUp + lumaUpCorners) + abs(-2.0 * lumaCenter + lumaLeftRight) * 2.0
                       + abs(-2.0 * lumaDown + lumaDownCorners);
    bool horizontal = edgeHorizontal >= edgeVertical;

    // Which side of the edge is the other surface: the neighbour with the bigger step.
    float luma1 = horizontal ? lumaDown : lumaLeft;
    float luma2 = horizontal ? lumaUp : lumaRight;
    float gradient1 = luma1 - lumaCenter;
    float gradient2 = luma2 - lumaCenter;
    bool steepest1 = abs(gradient1) >= abs(gradient2);
    float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));
    float stepLength = horizontal ? t.y : t.x;
    float lumaLocalAverage;
    if (steepest1){
        stepLength = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaCenter);
    }
    else{
        lumaLocalAverage = 0.5 * (luma2 + lumaCenter);
    }

    // Walk along the edge, half a pixel over onto it, until the luma leaves the edge's.
    vec2 edgeUV = uv;
    if (horizontal) { edgeUV.y += stepLength * 0.5; } else { edgeUV.x += stepLength * 0.5; }
    vec2 offset = horizontal ? vec2(t.x, 0.0) : vec2(0.0, t.y);
    vec2 uv1 = edgeUV - offset;
    vec2 uv2 = edgeUV + offset;
    float lumaEnd1 = Luma(Display(uv1)) - lumaLocalAverage;
    float lumaEnd2 = Luma(Display(uv2)) - lumaLocalAverage;
    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    if (!reached1) { uv1 -= offset; }
    if (!reached2) { uv2 += offset; }
    for (int i = 2; i < 12; i++){
        if (reached1 && reached2) { break; }
        if (!reached1) { lumaEnd1 = Luma(Display(uv1)) - lumaLocalAverage; }
        if (!reached2) { lumaEnd2 = Luma(Display(uv2)) - lumaLocalAverage; }
        reached1 = abs(lumaEnd1) >= gradientScaled;
        reached2 = abs(lumaEnd2) >= gradientScaled;
        if (!reached1) { uv1 -= offset * EdgeStep(i); }
        if (!reached2) { uv2 += offset * EdgeStep(i); }
    }

    float distance1 = horizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
    float distance2 = horizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);
    bool direction1 = distance1 < distance2;
    float pixelOffset = -min(distance1, distance2) / (distance1 + distance2) + 0.5;
    // Only shift if the nearer end's luma confirms this side of the edge.
    bool centerSmaller = lumaCenter < lumaLocalAverage;
    bool correctVariation = ((direction1 ? lumaEnd1 : lumaEnd2) < 0.0) != centerSmaller;
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    // Sub-pixel aliasing (a lone bright texel): a small blend, kept low to spare textures.
    float lumaAverage = (1.0 / 12.0) * (2.0 * (lumaDownUp + lumaLeftRight) + lumaLeftCorners + lumaRightCorners);
    float sub = clamp(abs(lumaAverage - lumaCenter) / lumaRange, 0.0, 1.0);
    sub = (-2.0 * sub + 3.0) * sub * sub;
    finalOffset = max(finalOffset, sub * sub * 0.5);

    vec2 finalUV = uv;
    if (horizontal) { finalUV.y += finalOffset * stepLength; } else { finalUV.x += finalOffset * stepLength; }
    return Display(finalUV);
}

void main(){
    FragColor = vec4(u_FXAA ? Fxaa(TexCoords) : Display(TexCoords), 1.0);
}
