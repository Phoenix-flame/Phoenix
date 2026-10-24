// Renderer: particle liquids as smooth surfaces (screen-space fluid rendering, Green
// 2010): sphere depths, thickness, a bilateral blur, then a shaded composite.
#include <Phoenix/renderer/renderer.h>
#include "RenderUnits.h"
#include <algorithm>

namespace Phoenix{

    Ref<Shader> Renderer::s_FluidParticleShader;
    Ref<Shader> Renderer::s_FluidSmoothShader;
    Ref<Shader> Renderer::s_FluidCompositeShader;
    Renderer::FluidTargets Renderer::s_FluidTargets;

    static uint32_t FloatTarget(GLenum format, int w, int h, uint32_t& fbo){
        uint32_t tex;
        glCreateTextures(GL_TEXTURE_2D, 1, &tex);
        glTextureStorage2D(tex, 1, format, w, h);
        glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glCreateFramebuffers(1, &fbo);
        glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, tex, 0);
        return tex;
    }

    void Renderer::RenderFluid(const std::vector<glm::vec4>& positions, const FluidParams& params){
        if (positions.empty() || !s_SceneCopy.fbo) { return; }
        FluidTargets& t = s_FluidTargets;
        const int w = s_SceneCopy.width, h = s_SceneCopy.height;
        if (t.width != w || t.height != h){
            if (t.depthFBO){
                glDeleteFramebuffers(1, &t.depthFBO); glDeleteFramebuffers(1, &t.blurFBO); glDeleteFramebuffers(1, &t.thickFBO);
                glDeleteTextures(1, &t.depthTex); glDeleteTextures(1, &t.blurTex); glDeleteTextures(1, &t.thickTex);
                glDeleteRenderbuffers(1, &t.depthBuffer);
            }
            t.depthTex = FloatTarget(GL_R32F, w, h, t.depthFBO);
            glCreateRenderbuffers(1, &t.depthBuffer);
            glNamedRenderbufferStorage(t.depthBuffer, GL_DEPTH_COMPONENT24, w, h);
            glNamedFramebufferRenderbuffer(t.depthFBO, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depthBuffer);
            t.blurTex = FloatTarget(GL_R32F, w, h, t.blurFBO);
            t.thickTex = FloatTarget(GL_R16F, w, h, t.thickFBO);
            t.width = w;
            t.height = h;
        }
        if (!t.vao){
            glCreateVertexArrays(1, &t.vao);
            glCreateBuffers(1, &t.vbo);
            glVertexArrayVertexBuffer(t.vao, 0, t.vbo, 0, sizeof(glm::vec4));
            glEnableVertexArrayAttrib(t.vao, 0);
            glVertexArrayAttribFormat(t.vao, 0, 4, GL_FLOAT, GL_FALSE, 0);
            glVertexArrayAttribBinding(t.vao, 0, 0);
        }
        if (positions.size() > t.capacity){
            t.capacity = std::max(positions.size(), t.capacity * 2);
            glNamedBufferData(t.vbo, t.capacity * sizeof(glm::vec4), nullptr, GL_DYNAMIC_DRAW);
        }
        glNamedBufferSubData(t.vbo, 0, positions.size() * sizeof(glm::vec4), positions.data());

        GLint previousFBO = 0, viewport[4];
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFBO);
        glGetIntegerv(GL_VIEWPORT, viewport);
        const glm::mat4& view = s_SceneData->ViewMatrix;
        const glm::mat4& projection = s_SceneData->ProjectionMatrix;
        glEnable(GL_PROGRAM_POINT_SIZE);
        glViewport(0, 0, w, h);
        glDisable(GL_BLEND);

        // Passes 1-2: sphere depths (nearest wins), then summed thickness.
        const Ref<Shader>& particles = s_FluidParticleShader;
        particles->Bind();
        particles->SetMat4("view", view);
        particles->SetMat4("projection", projection);
        particles->SetFloat("u_Radius", params.radius);
        particles->SetFloat("u_ViewportHeight", (float)h);
        particles->SetInt("u_SceneDepth", (int)SCENE_DEPTH_UNIT);
        glBindTextureUnit(SCENE_DEPTH_UNIT, s_SceneCopy.depth);
        glBindVertexArray(t.vao);

        glBindFramebuffer(GL_FRAMEBUFFER, t.depthFBO);
        const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        glClearBufferfv(GL_COLOR, 0, zero);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        particles->SetInt("u_Pass", 0);
        glDrawArrays(GL_POINTS, 0, (GLsizei)positions.size());

        glBindFramebuffer(GL_FRAMEBUFFER, t.thickFBO);
        glClearBufferfv(GL_COLOR, 0, zero);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        particles->SetInt("u_Pass", 1);
        glDrawArrays(GL_POINTS, 0, (GLsizei)positions.size());
        glDisable(GL_BLEND);
        particles->Unbind();

        // Pass 3: bilateral blur, x then y, three times.
        if (!s_SceneCopy.emptyVAO) { glCreateVertexArrays(1, &s_SceneCopy.emptyVAO); }
        glBindVertexArray(s_SceneCopy.emptyVAO);
        const Ref<Shader>& smooth = s_FluidSmoothShader;
        smooth->Bind();
        smooth->SetInt("u_Depth", (int)SCENE_COLOR_UNIT);
        smooth->SetFloat("u_WorldRadius", params.radius * 4.5f);
        smooth->SetFloat("u_PixelsPerUnit", projection[1][1] * 0.5f * (float)h);
        smooth->SetFloat("u_DepthFalloff", params.radius * 4.0f);
        for (int pass = 0; pass < 3; pass++){
            glBindFramebuffer(GL_FRAMEBUFFER, t.blurFBO);
            glBindTextureUnit(SCENE_COLOR_UNIT, t.depthTex);
            smooth->SetFloat2("u_Direction", { 1.0f / w, 0.0f });
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindFramebuffer(GL_FRAMEBUFFER, t.depthFBO);
            glBindTextureUnit(SCENE_COLOR_UNIT, t.blurTex);
            smooth->SetFloat2("u_Direction", { 0.0f, 1.0f / h });
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        smooth->Unbind();

        // Pass 4: shade into the scene's target, with depth.
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFBO);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        const Ref<Shader>& composite = s_FluidCompositeShader;
        composite->Bind();
        composite->SetInt("u_Depth", (int)CAUSTICS_UNIT);
        glBindTextureUnit(CAUSTICS_UNIT, t.depthTex);
        composite->SetInt("u_Thickness", (int)HEIGHT_UNIT);
        glBindTextureUnit(HEIGHT_UNIT, t.thickTex);
        composite->SetInt("u_SceneColor", (int)SCENE_COLOR_UNIT);
        glBindTextureUnit(SCENE_COLOR_UNIT, s_SceneCopy.color);
        composite->SetInt("u_SceneDepth", (int)SCENE_DEPTH_UNIT);
        glBindTextureUnit(SCENE_DEPTH_UNIT, s_SceneCopy.depth);
        composite->SetMat4("projection", projection);
        composite->SetMat4("u_InvProjection", glm::inverse(projection));
        composite->SetMat4("u_InvView", glm::inverse(view));
        composite->SetFloat3("u_Color", ToLinear(params.color));
        composite->SetFloat("u_Clarity", params.clarity);
        composite->SetFloat3("u_LightDirWorld", glm::normalize(params.lightDir));
        composite->SetFloat3("u_SunColor", ToLinear(params.sunColor));
        composite->SetFloat3("u_Ambient", ToLinear(params.ambient));
        composite->SetInt("u_LinearLighting", s_LinearLighting ? 1 : 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        composite->Unbind();
        glBindVertexArray(0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}
