#include <Phoenix/renderer/renderer.h>
#include <Phoenix/renderer/renderer_command.h>
#include "RenderUnits.h"
#include <Phoenix/core/base.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace Phoenix{
    Scope<Renderer::SceneData> Renderer::s_SceneData = CreateScope<Renderer::SceneData>();
    Scope<RenderLightCube> Renderer::s_RenderLightCube = CreateScope<RenderLightCube>();
    Scope<RenderCameraGizmo> Renderer::s_CameraGizmo = CreateScope<RenderCameraGizmo>();
    Scope<RenderDirLightGizmo> Renderer::s_DirLightGizmo = CreateScope<RenderDirLightGizmo>();
    Ref<Shader> Renderer::s_OutlineShader;
    Ref<Shader> Renderer::s_WaterShader;

    Renderer::ShadowAtlas Renderer::s_DirAtlas;
    Renderer::ShadowAtlas Renderer::s_PointAtlas;
    uint32_t Renderer::s_RawDepthSampler = 0;
    Ref<Shader> Renderer::s_ShadowShader;
    glm::mat4 Renderer::s_LightSpaceMatrix[Renderer::MAX_DIR_LIGHTS] = { glm::mat4(1.0f) };
    glm::vec4 Renderer::s_DirShadowRect[Renderer::MAX_DIR_LIGHTS] = {};
    glm::vec4 Renderer::s_DirShadowParams[Renderer::MAX_DIR_LIGHTS] = {};
    glm::vec4 Renderer::s_DirShadowInfo[Renderer::MAX_DIR_LIGHTS] = {};
    glm::mat4 Renderer::s_PointShadowMatrix[Renderer::MAX_POINT_LIGHTS * 6] = { glm::mat4(1.0f) };
    glm::vec4 Renderer::s_PointShadowRect[Renderer::MAX_POINT_LIGHTS * 6] = {};
    glm::vec4 Renderer::s_PointShadowParams[Renderer::MAX_POINT_LIGHTS] = {};
    glm::vec4 Renderer::s_PointShadowInfo[Renderer::MAX_POINT_LIGHTS] = {};
    int Renderer::s_DirShadowSlots = 0;
    int Renderer::s_PointShadowSlots = 0;
    int Renderer::s_DirSlotOf[Renderer::MAX_DIR_LIGHTS] = {};
    int Renderer::s_PointSlotOf[Renderer::MAX_POINT_LIGHTS] = {};
    bool Renderer::s_ShadowsEnabled = true;
    int Renderer::s_ShadowSamples = 16;
    int Renderer::s_PrevFBO = 0;
    int Renderer::s_PrevViewport[4] = { 0, 0, 0, 0 };
    bool Renderer::s_LinearLighting = false;
    Ref<Shader> Renderer::s_SkyShader;
    uint32_t Renderer::s_SkyVAO = 0;
    SkySettings Renderer::s_Sky;
    Ref<Texture2D> Renderer::s_SkyPanorama;
    bool Renderer::s_HasSun = false;
    glm::vec3 Renderer::s_SunDir = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 Renderer::s_SunColor = glm::vec3(1.0f);

    glm::vec3 Renderer::ToLinear(const glm::vec3& c){
        if (!s_LinearLighting) { return c; }
        // sRGB EOTF; the power segment also extends sensibly to HDR values above 1.
        auto channel = [](float v){
            v = std::max(v, 0.0f);
            return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
        };
        return { channel(c.r), channel(c.g), channel(c.b) };
    }

    // Shadow atlases. Directional: 4096x2048, two 2048 tiles when at most two lights cast
    // shadows, else eight 1024 tiles. Point: 3072x2048, one light's six faces as a 3x2
    // block of 1024 tiles, or up to four lights as 3x2 blocks of 512 tiles.
    static const uint32_t DIR_ATLAS_W = 4096, DIR_ATLAS_H = 2048;
    static const uint32_t POINT_ATLAS_W = 3072, POINT_ATLAS_H = 2048;
    // Point faces span a bit more than 90 degrees (tan(half fov) = 1.1) so the soft
    // filter kernel near a face edge still reads valid depths. Must exceed the shader's
    // maximum filter radius (MAX_PENUMBRA in lighting.glsl).
    static const float POINT_TAN_HALF_FOV = 1.1f;
    static const float POINT_NEAR = 0.05f;

    void Renderer::ShadowAtlas::Init(uint32_t w, uint32_t h){
        width = w; height = h;
        // LINEAR + compare mode = hardware PCF (each tap is a bilinear 2x2 comparison).
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

	void Renderer::Init(){
		RenderCommand::Init();
        s_RenderLightCube->Init();
        s_CameraGizmo->Init();
        s_DirLightGizmo->Init();
        s_OutlineShader = Shader::Create("assets/shaders/outline.glsl");
        s_WaterShader = Shader::Create("assets/shaders/water.glsl");
        s_PoolShellShader = Shader::Create("assets/shaders/pool_shell.glsl");
        s_PoolWaterShader = Shader::Create("assets/shaders/pool_water.glsl");
        s_PoolCausticsShader = Shader::Create("assets/shaders/pool_caustics.glsl");
        s_PoolUnderwaterShader = Shader::Create("assets/shaders/pool_underwater.glsl");
        s_FluidParticleShader = Shader::Create("assets/shaders/fluid_particles.glsl");
        s_FluidSmoothShader = Shader::Create("assets/shaders/fluid_smooth.glsl");
        s_FluidCompositeShader = Shader::Create("assets/shaders/fluid_composite.glsl");
        s_ShadowShader = Shader::Create("assets/shaders/shadow_depth.glsl");
        s_SkyShader = Shader::Create("assets/shaders/sky.glsl");
        glGenVertexArrays(1, &s_SkyVAO);

        s_DirAtlas.Init(DIR_ATLAS_W, DIR_ATLAS_H);
        s_PointAtlas.Init(POINT_ATLAS_W, POINT_ATLAS_H);
        s_DirAtlas.tile = GetDirShadowResolution(MAX_DIR_LIGHTS);
        s_PointAtlas.tile = 512;

        // Reads the atlases' raw depths (no comparison) for the soft-shadow blocker
        // search. Bound only to its own units, overriding the textures' compare mode.
        glGenSamplers(1, &s_RawDepthSampler);
        glSamplerParameteri(s_RawDepthSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glSamplerParameteri(s_RawDepthSampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glSamplerParameteri(s_RawDepthSampler, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glSamplerParameteri(s_RawDepthSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(s_RawDepthSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        BeginShadows(0, 0);
	}

    uint32_t Renderer::GetDirShadowResolution(int numDirShadows){
        return numDirShadows <= 2 ? 2048 : 1024;
    }

    glm::mat4 Renderer::GetPointShadowMatrix(const glm::vec3& position, int face, float range){
        static const glm::vec3 dirs[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
        static const glm::vec3 ups[6]  = { {0,1,0}, {0,1,0}, {0,0,1}, {0,0,-1}, {0,1,0}, {0,1,0} };
        face = std::max(0, std::min(5, face));
        glm::mat4 proj = glm::perspective(2.0f * std::atan(POINT_TAN_HALF_FOV), 1.0f, POINT_NEAR, std::max(range, POINT_NEAR * 2.0f));
        return proj * glm::lookAt(position, position + dirs[face], ups[face]);
    }

    void Renderer::SetShadowSamples(int samples){
        s_ShadowSamples = std::max(4, std::min(64, samples));
    }

    void Renderer::BeginShadows(int numDirShadows, int numPointShadows){
        for (int i = 0; i < MAX_DIR_LIGHTS; i++){ s_DirShadowRect[i] = glm::vec4(0.0f); s_DirSlotOf[i] = -1; }
        for (int i = 0; i < MAX_POINT_LIGHTS * 6; i++){ s_PointShadowRect[i] = glm::vec4(0.0f); }
        for (int i = 0; i < MAX_POINT_LIGHTS; i++){ s_PointShadowParams[i] = glm::vec4(0.0f); s_PointSlotOf[i] = -1; }
        s_DirShadowSlots = s_PointShadowSlots = 0;
        s_DirAtlas.tile = GetDirShadowResolution(numDirShadows);
        s_PointAtlas.tile = numPointShadows <= 1 ? 1024 : 512;

        // Clear what this frame will use: unrendered texels read as "lit".
        if (numDirShadows <= 0 && numPointShadows <= 0) { return; }
        GLint prevFBO, prevViewport[4];
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        glDepthMask(GL_TRUE);
        for (ShadowAtlas* atlas : { &s_DirAtlas, &s_PointAtlas }){
            if ((atlas == &s_DirAtlas ? numDirShadows : numPointShadows) <= 0) { continue; }
            glBindFramebuffer(GL_FRAMEBUFFER, atlas->fbo);
            glViewport(0, 0, atlas->width, atlas->height);
            glClear(GL_DEPTH_BUFFER_BIT);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, (uint32_t)prevFBO);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    }

    // Bind the atlas FBO and restrict rasterization to one tile (a zero viewport when the
    // layout has no room left, so stray casters draw nothing). Returns the tile's
    // (offset, size) in atlas UV, or zero size when there was no room.
    static glm::vec4 BeginTile(uint32_t fbo, uint32_t atlasW, uint32_t atlasH, uint32_t tile, int col, int row,
            const Ref<Shader>& shader, int* prevFBO, int* prevViewport){
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, prevFBO);
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        shader->Bind();
        if ((uint32_t)(col + 1) * tile > atlasW || (uint32_t)(row + 1) * tile > atlasH){
            glViewport(0, 0, 0, 0);
            return glm::vec4(0.0f);
        }
        glViewport(col * tile, row * tile, tile, tile);
        // Slope-scaled offset on the casters: steep surfaces need more bias than flat ones.
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 2.0f);
        return glm::vec4((float)(col * tile) / atlasW, (float)(row * tile) / atlasH,
                         (float)tile / atlasW, (float)tile / atlasH);
    }

    void Renderer::BeginShadowPass(int lightIndex, const glm::mat4& lightSpaceMatrix, const ShadowSettings& settings){
        if (lightIndex < 0 || lightIndex >= MAX_DIR_LIGHTS) { return; }
        int& slot = s_DirSlotOf[lightIndex];
        if (slot < 0) { slot = s_DirShadowSlots++; }
        const uint32_t tile = s_DirAtlas.tile;
        const int cols = (int)(s_DirAtlas.width / tile);
        glm::vec4 rect = BeginTile(s_DirAtlas.fbo, s_DirAtlas.width, s_DirAtlas.height, tile, slot % cols, slot / cols,
                                   s_ShadowShader, &s_PrevFBO, s_PrevViewport);
        s_ShadowShader->SetMat4("u_LightSpaceMatrix", lightSpaceMatrix);

        // The light space is an orthographic projection of a rigid view, so the lengths of
        // its x and z rows give the frustum's world width and depth range.
        float width = 2.0f / std::max(glm::length(glm::vec3(lightSpaceMatrix[0][0], lightSpaceMatrix[1][0], lightSpaceMatrix[2][0])), 1e-6f);
        float depth = 2.0f / std::max(glm::length(glm::vec3(lightSpaceMatrix[0][2], lightSpaceMatrix[1][2], lightSpaceMatrix[2][2])), 1e-6f);
        float texelWorld = width / (float)tile;
        float tanAngle = std::tan(glm::radians(std::max(settings.softness, 0.0f)));

        s_LightSpaceMatrix[lightIndex] = lightSpaceMatrix;
        s_DirShadowRect[lightIndex] = rect;
        // x: penumbra width in tile UV per unit of (raw) blocker-receiver depth
        // y: strength, z: depth bias (raw depth), w: normal offset (world)
        s_DirShadowParams[lightIndex] = glm::vec4(tanAngle * depth / width, glm::clamp(settings.strength, 0.0f, 1.0f),
                                                  settings.bias * texelWorld / depth, settings.normalBias * texelWorld);
        // x: one texel in tile UV, y: raw depth per tile UV on a 45-degree slope
        s_DirShadowInfo[lightIndex] = glm::vec4(1.0f / (float)tile, width / depth, 0.0f, 0.0f);
    }

    void Renderer::BeginPointShadowPass(int lightIndex, int face, const glm::vec3& position, float range, const ShadowSettings& settings){
        if (lightIndex < 0 || lightIndex >= MAX_POINT_LIGHTS || face < 0 || face >= 6) { return; }
        int& slot = s_PointSlotOf[lightIndex];
        if (slot < 0) { slot = s_PointShadowSlots++; }
        const uint32_t tile = s_PointAtlas.tile;
        // Each light owns a 3x2 block of face tiles; blocks fill the atlas left to right.
        const int blocksPerRow = (int)(s_PointAtlas.width / (tile * 3));
        int col = (slot % blocksPerRow) * 3 + face % 3;
        int row = (slot / blocksPerRow) * 2 + face / 3;
        glm::vec4 rect = BeginTile(s_PointAtlas.fbo, s_PointAtlas.width, s_PointAtlas.height, tile, col, row,
                                   s_ShadowShader, &s_PrevFBO, s_PrevViewport);
        range = std::max(range, POINT_NEAR * 2.0f);
        glm::mat4 matrix = GetPointShadowMatrix(position, face, range);
        s_ShadowShader->SetMat4("u_LightSpaceMatrix", matrix);

        s_PointShadowMatrix[lightIndex * 6 + face] = matrix;
        s_PointShadowRect[lightIndex * 6 + face] = rect;
        // x: light radius (world), y: strength, z: depth bias (texels), w: normal offset (texels)
        s_PointShadowParams[lightIndex] = glm::vec4(std::max(settings.softness, 0.0f), glm::clamp(settings.strength, 0.0f, 1.0f),
                                                    settings.bias, settings.normalBias);
        // x: near, y: far, z: one texel in tile UV, w: tan(half fov)
        s_PointShadowInfo[lightIndex] = glm::vec4(POINT_NEAR, range, 1.0f / (float)tile, POINT_TAN_HALF_FOV);
    }

    static void BindShadowAlpha(const Ref<Shader>& shader, const Ref<Texture2D>& alphaMap, float alphaCutoff){
        if (alphaMap && alphaCutoff > 0.0f){
            alphaMap->Bind(0);
            shader->SetInt("u_AlphaMap", 0);
            shader->SetFloat("u_AlphaCutoff", alphaCutoff);
        }
        else{
            shader->SetFloat("u_AlphaCutoff", 0.0f);
        }
    }

    void Renderer::SubmitShadow(const Ref<VertexArray>& vertexArray, const glm::mat4& transform,
            const Ref<Texture2D>& alphaMap, float alphaCutoff){
        s_ShadowShader->SetMat4("model", transform);
        s_ShadowShader->SetInt("u_Animated", 0);
        BindShadowAlpha(s_ShadowShader, alphaMap, alphaCutoff);
        vertexArray->Bind();
        RenderCommand::DrawIndexed(vertexArray);
    }

    void Renderer::SubmitShadowAnimated(const Ref<VertexArray>& vertexArray, const glm::mat4& transform,
            const std::vector<glm::mat4>& boneMatrices, const Ref<Texture2D>& alphaMap, float alphaCutoff){
        s_ShadowShader->SetMat4("model", transform);
        s_ShadowShader->SetInt("u_Animated", 1);
        BindShadowAlpha(s_ShadowShader, alphaMap, alphaCutoff);
        if (!boneMatrices.empty())
            s_ShadowShader->SetMat4Array("u_BoneMatrices", boneMatrices.data(), (uint32_t)boneMatrices.size());
        vertexArray->Bind();
        RenderCommand::DrawIndexed(vertexArray);
        s_ShadowShader->SetInt("u_Animated", 0);
    }

    void Renderer::SubmitShadowCube(const glm::mat4& transform){
        SubmitShadow(s_RenderLightCube->m_Vertex_array, transform);
    }

    void Renderer::EndShadowPass(){
        glDisable(GL_POLYGON_OFFSET_FILL);
        s_ShadowShader->Unbind();
        glBindFramebuffer(GL_FRAMEBUFFER, (uint32_t)s_PrevFBO);
        glViewport(s_PrevViewport[0], s_PrevViewport[1], s_PrevViewport[2], s_PrevViewport[3]);
    }

	void Renderer::Shutdown(){
	}

	void Renderer::OnWindowResize(uint32_t width, uint32_t height){
		RenderCommand::SetViewport(0, 0, width, height);
	}

    void Renderer::BeginScene(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos){
        s_SceneData->ProjectionMatrix = projection;
        s_SceneData->ViewMatrix = view;
        s_SceneData->CameraPos = cameraPos;

        auto& shader = s_RenderLightCube->m_Shader;
        shader->Bind();
        shader->SetMat4("view", view);
        shader->SetMat4("projection", projection);
        shader->SetFloat3("cameraPos", cameraPos);

        // Shadow atlases + this frame's per-light shadow tiles. The matrices/params are
        // plain uniform arrays (dynamic indexing of non-samplers IS allowed). With shadows
        // globally off every tile size is zero, i.e. nothing is shadowed.
        static const glm::vec4 noRects[MAX_POINT_LIGHTS * 6] = {};
        shader->SetInt("u_ShadowMap", (int)DIR_SHADOW_UNIT);
        shader->SetInt("u_ShadowDepth", (int)DIR_DEPTH_UNIT);
        shader->SetInt("u_PointShadowMap", (int)POINT_SHADOW_UNIT);
        shader->SetInt("u_PointShadowDepth", (int)POINT_DEPTH_UNIT);
        glBindTextureUnit(DIR_SHADOW_UNIT, s_DirAtlas.texture);
        glBindTextureUnit(DIR_DEPTH_UNIT, s_DirAtlas.texture);
        glBindTextureUnit(POINT_SHADOW_UNIT, s_PointAtlas.texture);
        glBindTextureUnit(POINT_DEPTH_UNIT, s_PointAtlas.texture);
        glBindSampler(DIR_DEPTH_UNIT, s_RawDepthSampler);
        glBindSampler(POINT_DEPTH_UNIT, s_RawDepthSampler);
        shader->SetMat4Array("u_LightSpaceMatrices", s_LightSpaceMatrix, MAX_DIR_LIGHTS);
        shader->SetFloat4Array("u_DirShadowRect", s_ShadowsEnabled ? s_DirShadowRect : noRects, MAX_DIR_LIGHTS);
        shader->SetFloat4Array("u_DirShadowParams", s_DirShadowParams, MAX_DIR_LIGHTS);
        shader->SetFloat4Array("u_DirShadowInfo", s_DirShadowInfo, MAX_DIR_LIGHTS);
        BindSkyUniforms(shader);
        shader->SetMat4Array("u_PointShadowMatrices", s_PointShadowMatrix, MAX_POINT_LIGHTS * 6);
        shader->SetFloat4Array("u_PointShadowRect", s_ShadowsEnabled ? s_PointShadowRect : noRects, MAX_POINT_LIGHTS * 6);
        shader->SetFloat4Array("u_PointShadowParams", s_PointShadowParams, MAX_POINT_LIGHTS);
        shader->SetFloat4Array("u_PointShadowInfo", s_PointShadowInfo, MAX_POINT_LIGHTS);
        shader->SetInt("u_ShadowSamples", s_ShadowSamples);
    }

    // Uniform names for the light arrays, built once rather than every frame.
    static const int NUM_POINT_LIGHTS = 4; // NR_POINT_LIGHTS in lighting.glsl

    struct DirLightUniformNames { std::string ambient, diffuse, specular, direction; };
    struct PointLightUniformNames { std::string constant, linear, quadratic, ambient, diffuse, specular, position; };

    static const DirLightUniformNames& DirLightNames(int i){
        static const auto names = []{
            std::array<DirLightUniformNames, Renderer::MAX_DIR_LIGHTS> n;
            for (int j = 0; j < Renderer::MAX_DIR_LIGHTS; j++){
                std::string base = "dirLights[" + std::to_string(j) + "].";
                n[j] = { base + "ambient", base + "diffuse", base + "specular", base + "direction" };
            }
            return n;
        }();
        return names[i];
    }

    static const PointLightUniformNames& PointLightNames(int i){
        static const auto names = []{
            std::array<PointLightUniformNames, NUM_POINT_LIGHTS> n;
            for (int j = 0; j < NUM_POINT_LIGHTS; j++){
                std::string base = "pointLights[" + std::to_string(j) + "].";
                n[j] = { base + "constant", base + "linear", base + "quadratic", base + "ambient",
                         base + "diffuse", base + "specular", base + "position" };
            }
            return n;
        }();
        return names[i];
    }

    void Renderer::SetLights(
            const glm::vec3& ambient,
            const DirLightComponent dirLights[],
            const glm::vec3 dirLightDirs[],
            int numDirLights,
            PointLightComponent pointLight[],
            const glm::vec3 pointLightPos[],
            int numOfPointLight ){

        auto& shader = s_RenderLightCube->m_Shader;

        shader->SetFloat3("u_Ambient", ToLinear(ambient));

        if (numDirLights > MAX_DIR_LIGHTS) { numDirLights = MAX_DIR_LIGHTS; }
        shader->SetInt("u_NumDirLights", numDirLights);
        for (int i = 0; i < MAX_DIR_LIGHTS; i++){
            const DirLightUniformNames& u = DirLightNames(i);
            if (i < numDirLights){
                shader->SetFloat3(u.ambient, ToLinear(dirLights[i].ambient));
                shader->SetFloat3(u.diffuse, ToLinear(dirLights[i].diffuse));
                shader->SetFloat3(u.specular, ToLinear(dirLights[i].specular));
                shader->SetFloat3(u.direction, dirLightDirs[i]);
            }
            else{
                shader->SetFloat3(u.ambient, glm::vec3(0.0f));
                shader->SetFloat3(u.diffuse, glm::vec3(0.0f));
                shader->SetFloat3(u.specular, glm::vec3(0.0f));
                shader->SetFloat3(u.direction, glm::vec3(0.0f, -1.0f, 0.0f));
            }
        }

        shader->SetInt("u_NumPointLights", std::min(std::max(numOfPointLight, 0), NUM_POINT_LIGHTS));
        for (int i = 0 ; i < NUM_POINT_LIGHTS ; i ++)
        {
            const PointLightUniformNames& u = PointLightNames(i);
            if (i < numOfPointLight)
            {
                shader->SetFloat(u.constant, pointLight[i].constant);
                shader->SetFloat(u.linear, pointLight[i].linear);
                shader->SetFloat(u.quadratic, pointLight[i].quadratic);
                shader->SetFloat3(u.ambient, ToLinear(pointLight[i].ambient));
                shader->SetFloat3(u.diffuse, ToLinear(pointLight[i].diffuse));
                shader->SetFloat3(u.specular, ToLinear(pointLight[i].specular));
                shader->SetFloat3(u.position, pointLightPos[i]);
            }
            else{
                shader->SetFloat(u.constant, 1.0f);
                shader->SetFloat(u.linear, 0.03f);
                shader->SetFloat(u.quadratic, 0.099f);
                shader->SetFloat3(u.ambient, glm::vec3(0.0, 0.0, 0.0));
                shader->SetFloat3(u.diffuse, glm::vec3(0.0, 0.0, 0.0));
                shader->SetFloat3(u.specular, glm::vec3(0.0, 0.0, 0.0));
                shader->SetFloat3(u.position, glm::vec3(0.0, 0.0, 0.0));
            }
        }
    }

    static const uint32_t NORMAL_MAP_UNIT = 2;

    // Upload material params + bind its textures. The material's own diffuse / normal paths
    // (set via the Textures panel) override the mesh-embedded `diffuseMap` / `normalMap`;
    // paths are resolved through Texture2D's path cache.
    static void BindMaterial(const Ref<Shader>& shader, const Material& material, const Ref<Texture2D>& diffuseMap,
            const Ref<Texture2D>& normalMap, float alphaCutoff){
        shader->SetFloat3("material.ambient", Renderer::ToLinear(material.ambient));
        shader->SetFloat3("material.diffuse", Renderer::ToLinear(material.diffuse));
        shader->SetFloat3("material.specular", Renderer::ToLinear(material.specular));
        shader->SetFloat("material.shininess", material.shininess);
        shader->SetFloat3("u_Emissive", Renderer::ToLinear(material.emissive) * material.emissiveStrength);
        shader->SetFloat("u_Reflectivity", material.reflectivity);
        shader->SetInt("u_ReceiveShadows", material.receiveShadows ? 1 : 0);

        Ref<Texture2D> diffuse = !material.diffusePath.empty() ? Texture2D::Create(material.diffusePath) : diffuseMap;
        if (diffuse){
            diffuse->Bind(0, Renderer::IsLinearLighting()); // colour data: sRGB-decoded when linear
            shader->SetInt("u_DiffuseMap", 0);
            shader->SetInt("u_HasDiffuseMap", 1);
        }
        else{
            shader->SetInt("u_HasDiffuseMap", 0);
        }
        // The cutoff tests the diffuse map's alpha, so it needs one.
        shader->SetFloat("u_AlphaCutoff", diffuse ? alphaCutoff : 0.0f);

        Ref<Texture2D> normal = !material.normalPath.empty() ? Texture2D::Create(material.normalPath) : normalMap;
        if (normal){
            normal->Bind(NORMAL_MAP_UNIT);
            shader->SetInt("u_NormalMap", (int)NORMAL_MAP_UNIT);
            shader->SetInt("u_HasNormalMap", 1);
        }
        else{
            shader->SetInt("u_HasNormalMap", 0);
        }
    }

    void Renderer::Submit(const Ref<VertexArray>& vertexArray, const Material& material, const glm::mat4& transform,
            const Ref<Texture2D>& diffuseMap, const Ref<Texture2D>& normalMap, float alphaCutoff){
        auto& shader = s_RenderLightCube->m_Shader;
        BindMaterial(shader, material, diffuseMap, normalMap, alphaCutoff);
        shader->SetMat4("model", transform);
        shader->UploadUniformMat3("u_NormalMatrix", glm::transpose(glm::inverse(glm::mat3(transform))));
        shader->SetInt("u_Animated", 0);

        vertexArray->Bind();
        RenderCommand::DrawIndexed(vertexArray);
    }

    void Renderer::SubmitAnimated(const Ref<VertexArray>& vertexArray, const Material& material, const glm::mat4& transform,
            const std::vector<glm::mat4>& boneMatrices, const Ref<Texture2D>& diffuseMap,
            const Ref<Texture2D>& normalMap, float alphaCutoff){
        auto& shader = s_RenderLightCube->m_Shader;
        BindMaterial(shader, material, diffuseMap, normalMap, alphaCutoff);
        shader->SetMat4("model", transform);
        shader->UploadUniformMat3("u_NormalMatrix", glm::transpose(glm::inverse(glm::mat3(transform))));
        shader->SetInt("u_Animated", 1);
        if (!boneMatrices.empty())
            shader->SetMat4Array("u_BoneMatrices", boneMatrices.data(), (uint32_t)boneMatrices.size());

        vertexArray->Bind();
        RenderCommand::DrawIndexed(vertexArray);

        shader->SetInt("u_Animated", 0); // reset so following draws aren't skinned
    }

    void Renderer::SubmitCube(const Material& material, const glm::mat4& transform){
        Submit(s_RenderLightCube->m_Vertex_array, material, transform);
    }

    void Renderer::SetWireframe(bool enabled){
        glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL);
    }

    void Renderer::DrawOutline(const std::vector<Ref<VertexArray>>& vertexArrays, const glm::mat4& transform, const glm::vec3& color,
            const std::vector<glm::mat4>* boneMatrices){
        if (vertexArrays.empty()) { return; }

        s_OutlineShader->Bind();
        s_OutlineShader->SetMat4("view", s_SceneData->ViewMatrix);
        s_OutlineShader->SetMat4("projection", s_SceneData->ProjectionMatrix);
        s_OutlineShader->SetFloat3("u_Color", ToLinear(color));

        // Skin the silhouette for animated meshes so it tracks the posed geometry.
        if (boneMatrices && !boneMatrices->empty()){
            s_OutlineShader->SetInt("u_Animated", 1);
            s_OutlineShader->SetMat4Array("u_BoneMatrices", boneMatrices->data(), (uint32_t)boneMatrices->size());
        }
        else{
            s_OutlineShader->SetInt("u_Animated", 0);
        }

        glEnable(GL_STENCIL_TEST);
        glClear(GL_STENCIL_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST); // keep the outline visible even when occluded

        // Pass 1: mark the silhouette (stencil = 1), writing neither colour nor depth.
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glStencilMask(0xFF);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        s_OutlineShader->SetMat4("model", transform);
        for (const auto& va : vertexArrays){ va->Bind(); RenderCommand::DrawIndexed(va); }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        // Pass 2: draw the enlarged object only OUTSIDE the silhouette -> a thin border.
        glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
        glStencilMask(0x00);
        s_OutlineShader->SetMat4("model", transform * glm::scale(glm::mat4(1.0f), glm::vec3(1.03f)));
        for (const auto& va : vertexArrays){ va->Bind(); RenderCommand::DrawIndexed(va); }

        // Restore state.
        glStencilMask(0xFF);
        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_DEPTH_TEST);
        s_OutlineShader->Unbind();
    }

    void Renderer::DrawOutlineCube(const glm::mat4& transform, const glm::vec3& color){
        DrawOutline({ s_RenderLightCube->m_Vertex_array }, transform, color);
    }

    void Renderer::DrawCameraGizmo(const glm::mat4& transform, float verticalFov, float aspect, const glm::vec3& color){
        // Shape the unit frustum (base half-size 1 at z=-1) to the camera's FOV/aspect.
        const float length = 1.5f;
        float halfHeight = length * std::tan(verticalFov * 0.5f);
        float halfWidth = halfHeight * aspect;
        glm::mat4 model = transform * glm::scale(glm::mat4(1.0f), glm::vec3(halfWidth, halfHeight, length));

        s_OutlineShader->Bind();
        s_OutlineShader->SetMat4("view", s_SceneData->ViewMatrix);
        s_OutlineShader->SetMat4("projection", s_SceneData->ProjectionMatrix);
        s_OutlineShader->SetMat4("model", model);
        s_OutlineShader->SetFloat3("u_Color", ToLinear(color));
        s_OutlineShader->SetInt("u_Animated", 0); // DrawOutline may have left skinning on

        s_CameraGizmo->m_Vertex_array->Bind();
        glLineWidth(2.0f);
        glDrawElements(GL_LINES, 16, GL_UNSIGNED_INT, 0);

        s_OutlineShader->Unbind();
    }

    void Renderer::DrawDirLightGizmo(const glm::mat4& transform, const glm::vec3& color){
        // Use rotation+translation only (drop scale) so the arrow keeps a fixed size.
        glm::vec3 pos = glm::vec3(transform[3]);
        glm::mat3 rotation = glm::mat3(transform);
        rotation[0] = glm::normalize(rotation[0]);
        rotation[1] = glm::normalize(rotation[1]);
        rotation[2] = glm::normalize(rotation[2]);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos) * glm::mat4(rotation);

        s_OutlineShader->Bind();
        s_OutlineShader->SetMat4("view", s_SceneData->ViewMatrix);
        s_OutlineShader->SetMat4("projection", s_SceneData->ProjectionMatrix);
        s_OutlineShader->SetMat4("model", model);
        s_OutlineShader->SetFloat3("u_Color", ToLinear(color));
        s_OutlineShader->SetInt("u_Animated", 0); // DrawOutline may have left skinning on

        s_DirLightGizmo->m_Vertex_array->Bind();
        glLineWidth(2.0f);
        glDrawElements(GL_LINES, 10, GL_UNSIGNED_INT, 0);

        s_OutlineShader->Unbind();
    }

    void Renderer::DrawLines(const std::vector<glm::vec3>& points, const glm::vec3& color, bool onTop){
        if (points.size() < 2) { return; }
        // One streamed buffer shared by every call (debug geometry changes each frame).
        static uint32_t vao = 0, vbo = 0;
        if (!vao){
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
        }
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, points.size() * sizeof(glm::vec3), points.data(), GL_STREAM_DRAW);

        s_OutlineShader->Bind();
        s_OutlineShader->SetMat4("view", s_SceneData->ViewMatrix);
        s_OutlineShader->SetMat4("projection", s_SceneData->ProjectionMatrix);
        s_OutlineShader->SetMat4("model", glm::mat4(1.0f));
        s_OutlineShader->SetFloat3("u_Color", ToLinear(color));
        s_OutlineShader->SetInt("u_Animated", 0); // DrawOutline may have left skinning on

        GLboolean depthWasOn = glIsEnabled(GL_DEPTH_TEST);
        if (onTop) { glDisable(GL_DEPTH_TEST); }
        glLineWidth(1.5f);
        glDrawArrays(GL_LINES, 0, (GLsizei)(points.size() & ~(size_t)1));
        if (onTop && depthWasOn) { glEnable(GL_DEPTH_TEST); }

        glBindVertexArray(0);
        s_OutlineShader->Unbind();
    }

    void Renderer::SubmitWater(const Ref<VertexArray>& vertexArray, const glm::mat4& transform,
            const WaterParams& params){
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE); // transparent: test against depth but don't write it

        s_WaterShader->Bind();
        s_WaterShader->SetMat4("view", s_SceneData->ViewMatrix);
        s_WaterShader->SetMat4("projection", s_SceneData->ProjectionMatrix);
        s_WaterShader->SetMat4("model", transform);
        s_WaterShader->SetFloat3("u_CameraPos", s_SceneData->CameraPos);
        s_WaterShader->SetFloat3("u_Color", ToLinear(params.color));
        s_WaterShader->SetInt("u_LinearLighting", s_LinearLighting ? 1 : 0);
        s_WaterShader->SetFloat("u_Alpha", params.alpha);
        s_WaterShader->SetFloat3("u_LightDir", params.lightDir);
        s_WaterShader->SetFloat("u_Time", params.time);
        s_WaterShader->SetFloat("u_Amplitude", params.amplitude);
        s_WaterShader->SetFloat("u_WaveScale", params.waveScale);
        s_WaterShader->SetFloat("u_Speed", params.speed);
        s_WaterShader->SetFloat("u_Choppiness", params.choppiness);
        s_WaterShader->SetFloat("u_Foam", params.foam);
        s_WaterShader->SetFloat("u_WaterSize", params.size);

        BindSkyUniforms(s_WaterShader);

        // The sun's (directional light 0's) shadow darkens the water and hides its glint.
        s_WaterShader->SetInt("u_ShadowMap", (int)DIR_SHADOW_UNIT);
        glBindTextureUnit(DIR_SHADOW_UNIT, s_DirAtlas.texture);
        s_WaterShader->SetMat4("u_LightSpaceMatrix", s_LightSpaceMatrix[0]);
        s_WaterShader->SetFloat4("u_ShadowRect", s_ShadowsEnabled ? s_DirShadowRect[0] : glm::vec4(0.0f));
        s_WaterShader->SetFloat4("u_ShadowParams", s_DirShadowParams[0]);
        s_WaterShader->SetFloat4("u_ShadowInfo", s_DirShadowInfo[0]);

        // Dynamic ripple heightfield on unit 3 (0 diffuse, 1 shadow atlas, 2 normal map).
        s_WaterShader->SetInt("u_HasRipples", params.rippleTexture ? 1 : 0);
        if (params.rippleTexture){
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, params.rippleTexture);
            s_WaterShader->SetInt("u_RippleTex", 3);
            glActiveTexture(GL_TEXTURE0);
        }

        vertexArray->Bind();
        RenderCommand::DrawIndexed(vertexArray);

        s_WaterShader->Unbind();
        glDepthMask(GL_TRUE);
    }

    // Panorama on its own unit: 0 diffuse, 1 shadow atlas, 2 normal map, 3 ripples, 4-6 atlases.
    static const uint32_t SKY_PANORAMA_UNIT = 7;

    void Renderer::SetSky(const SkySettings& sky, bool hasSun, const glm::vec3& sunDir, const glm::vec3& sunColor){
        s_Sky = sky;
        // Texture2D caches by path (failures too), so this is a lookup after the first frame.
        s_SkyPanorama = (sky.mode == SkySettings::Mode::Panorama && !sky.panorama.empty())
            ? Texture2D::Create(sky.panorama) : nullptr;
        s_HasSun = hasSun;
        s_SunDir = glm::length(sunDir) > 1e-6f ? glm::normalize(sunDir) : glm::vec3(0.0f, 1.0f, 0.0f);
        s_SunColor = sunColor;
    }

    // Sky colours / panorama for SampleEnvironment (reflections) and the sky pass.
    void Renderer::BindSkyUniforms(const Ref<Shader>& shader){
        shader->SetFloat3("u_SkyZenith", ToLinear(s_Sky.zenith));
        shader->SetFloat3("u_SkyHorizon", ToLinear(s_Sky.horizon));
        shader->SetFloat3("u_SkyGround", ToLinear(s_Sky.ground));
        shader->SetFloat("u_SkyIntensity", std::max(s_Sky.intensity, 0.0f));
        shader->SetFloat("u_SkyRotation", glm::radians(s_Sky.rotation));
        shader->SetInt("u_SkyHasPanorama", s_SkyPanorama ? 1 : 0);
        // Always on its own unit, even unused, so it never shares one with another sampler type.
        shader->SetInt("u_SkyPanorama", (int)SKY_PANORAMA_UNIT);
        if (s_SkyPanorama) { s_SkyPanorama->Bind(SKY_PANORAMA_UNIT, s_LinearLighting); }
    }

    void Renderer::DrawSky(){
        if (s_Sky.mode == SkySettings::Mode::Color || !s_SkyShader) { return; }
        s_SkyShader->Bind();
        // Directions only: drop the view's translation so the sky stays at infinity.
        glm::mat4 rotationOnly = glm::mat4(glm::mat3(s_SceneData->ViewMatrix));
        s_SkyShader->SetMat4("u_InvViewProj", glm::inverse(s_SceneData->ProjectionMatrix * rotationOnly));
        s_SkyShader->SetInt("u_SkyMode", (int)s_Sky.mode);
        BindSkyUniforms(s_SkyShader);
        s_SkyShader->SetInt("u_SunDisc", (s_Sky.sunDisc && s_HasSun) ? 1 : 0);
        s_SkyShader->SetFloat3("u_SunDir", s_SunDir);
        s_SkyShader->SetFloat3("u_SunColor", ToLinear(s_SunColor));
        s_SkyShader->SetFloat("u_SunCos", std::cos(glm::radians(glm::clamp(s_Sky.sunSize, 0.05f, 20.0f))));

        GLboolean depthWasOn = glIsEnabled(GL_DEPTH_TEST);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glBindVertexArray(s_SkyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        if (depthWasOn) { glEnable(GL_DEPTH_TEST); }

        // Geometry submitted next expects the lighting shader bound (BeginScene state).
        s_RenderLightCube->m_Shader->Bind();
    }

	void Renderer::EndScene(){
        s_RenderLightCube->m_Shader->Unbind();
        // Other passes (bloom, ImGui) use these units with the textures' own state.
        glBindSampler(DIR_DEPTH_UNIT, 0);
        glBindSampler(POINT_DEPTH_UNIT, 0);
	}

}
