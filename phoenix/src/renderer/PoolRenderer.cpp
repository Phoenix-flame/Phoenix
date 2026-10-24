// Renderer: pools (PoolComponent). See renderer.h for the per-frame order.
#include <Phoenix/renderer/renderer.h>
#include <Phoenix/renderer/renderer_command.h>
#include "RenderUnits.h"
#include <algorithm>
#include <cmath>

namespace Phoenix{

    Ref<Shader> Renderer::s_PoolShellShader;
    Ref<Shader> Renderer::s_PoolWaterShader;
    Ref<Shader> Renderer::s_PoolCausticsShader;
    Ref<Shader> Renderer::s_PoolUnderwaterShader;
    Renderer::SceneCopy Renderer::s_SceneCopy;

    // Light lost per metre, per channel: pure water's absorption (red fades within a
    // few metres, blue barely; roughly 650/550/450 nm), plus grey extinction from
    // particles that sets how far one can see (`clarity` metres before ~85% is gone).
    static glm::vec3 Absorption(float clarity){
        const glm::vec3 water(0.35f, 0.055f, 0.025f);
        return water + glm::vec3(2.0f / std::max(clarity, 0.1f));
    }

    // The sunlight direction in the pool's frame (it only turns about Y).
    static glm::vec3 ToPoolFrame(const glm::mat4& model, const glm::vec3& worldDir){
        return glm::transpose(glm::mat3(model)) * worldDir;
    }

    void Renderer::BindSunShadow(const Ref<Shader>& shader){
        shader->SetInt("u_ShadowMap", (int)DIR_SHADOW_UNIT);
        glBindTextureUnit(DIR_SHADOW_UNIT, s_DirAtlas.texture);
        shader->SetMat4("u_LightSpaceMatrix", s_LightSpaceMatrix[0]);
        shader->SetFloat4("u_ShadowRect", s_ShadowsEnabled ? s_DirShadowRect[0] : glm::vec4(0.0f));
        shader->SetFloat4("u_ShadowParams", s_DirShadowParams[0]);
        shader->SetFloat4("u_ShadowInfo", s_DirShadowInfo[0]);
    }

    void Renderer::RenderPoolCaustics(const Ref<VertexArray>& surface, const PoolParams& params){
        if (!params.causticsFramebuffer || !params.heightTexture) { return; }
        GLint previousFBO = 0, viewport[4];
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFBO);
        glGetIntegerv(GL_VIEWPORT, viewport);
        GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);

        glBindFramebuffer(GL_FRAMEBUFFER, params.causticsFramebuffer);
        glViewport(0, 0, params.causticsSize.x, params.causticsSize.y);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);

        s_PoolCausticsShader->Bind();
        s_PoolCausticsShader->SetInt("u_HeightTex", (int)HEIGHT_UNIT);
        glBindTextureUnit(HEIGHT_UNIT, params.heightTexture);
        s_PoolCausticsShader->SetFloat2("u_PoolSize", params.size);
        s_PoolCausticsShader->SetFloat2("u_Spacing", params.spacing);
        s_PoolCausticsShader->SetFloat("u_WaterLevel", params.waterLevel);
        s_PoolCausticsShader->SetFloat("u_Floor", -params.depth);
        s_PoolCausticsShader->SetFloat3("u_LightDirLocal", ToPoolFrame(params.model, params.lightDir));
        surface->Bind();
        RenderCommand::DrawIndexed(surface);
        s_PoolCausticsShader->Unbind();

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (depthTest) { glEnable(GL_DEPTH_TEST); }
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFBO);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    }

    void Renderer::SubmitPoolShell(const Ref<VertexArray>& shell, const PoolParams& params){
        const Ref<Shader>& shader = s_PoolShellShader;
        shader->Bind();
        shader->SetMat4("view", s_SceneData->ViewMatrix);
        shader->SetMat4("projection", s_SceneData->ProjectionMatrix);
        shader->SetMat4("model", params.model);
        shader->SetFloat2("u_PoolSize", params.size);
        shader->SetFloat("u_WaterLevel", params.waterLevel);
        shader->SetFloat3("u_TileColor", ToLinear(params.tileColor));
        shader->SetFloat3("u_GroutColor", ToLinear(params.groutColor));
        shader->SetFloat("u_TileSize", std::max(params.tileSize, 0.02f));
        shader->SetFloat3("u_Absorption", Absorption(params.clarity));
        shader->SetFloat("u_CausticStrength", params.causticStrength);
        shader->SetFloat3("u_LightDirLocal", ToPoolFrame(params.model, params.lightDir));
        shader->SetFloat3("u_LightDirWorld", glm::normalize(params.lightDir));
        shader->SetFloat3("u_SunColor", ToLinear(params.sunColor));
        shader->SetFloat3("u_Ambient", ToLinear(params.ambient));
        shader->SetInt("u_LinearLighting", s_LinearLighting ? 1 : 0);
        shader->SetInt("u_HeightTex", (int)HEIGHT_UNIT);
        glBindTextureUnit(HEIGHT_UNIT, params.heightTexture);
        shader->SetInt("u_CausticsTex", (int)CAUSTICS_UNIT);
        glBindTextureUnit(CAUSTICS_UNIT, params.causticsTexture);
        BindSunShadow(shader);
        shell->Bind();
        RenderCommand::DrawIndexed(shell);
        shader->Unbind();
    }

    void Renderer::CaptureScene(){
        GLint fbo = 0, viewport[4];
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_VIEWPORT, viewport);
        const int w = std::max(1, viewport[2]), h = std::max(1, viewport[3]);

        // Match the target's colour format (an MSAA target resolves on the copy).
        GLint format = GL_RGBA8;
        if (fbo != 0){
            GLint type = GL_NONE, name = 0;
            glGetNamedFramebufferAttachmentParameteriv(fbo, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
            glGetNamedFramebufferAttachmentParameteriv(fbo, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
            if (type == GL_TEXTURE) { glGetTextureLevelParameteriv(name, 0, GL_TEXTURE_INTERNAL_FORMAT, &format); }
            else if (type == GL_RENDERBUFFER) { glGetNamedRenderbufferParameteriv(name, GL_RENDERBUFFER_INTERNAL_FORMAT, &format); }
        }

        SceneCopy& copy = s_SceneCopy;
        if (copy.width != w || copy.height != h || copy.colorFormat != format){
            if (copy.fbo) { glDeleteFramebuffers(1, &copy.fbo); glDeleteTextures(1, &copy.color); glDeleteTextures(1, &copy.depth); }
            glCreateTextures(GL_TEXTURE_2D, 1, &copy.color);
            glTextureStorage2D(copy.color, 1, (GLenum)format, w, h);
            glCreateTextures(GL_TEXTURE_2D, 1, &copy.depth);
            glTextureStorage2D(copy.depth, 1, GL_DEPTH24_STENCIL8, w, h);
            for (uint32_t t : { copy.color, copy.depth }){
                glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, t == copy.color ? GL_LINEAR : GL_NEAREST);
                glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, t == copy.color ? GL_LINEAR : GL_NEAREST);
                glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTextureParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glCreateFramebuffers(1, &copy.fbo);
            glNamedFramebufferTexture(copy.fbo, GL_COLOR_ATTACHMENT0, copy.color, 0);
            glNamedFramebufferTexture(copy.fbo, GL_DEPTH_STENCIL_ATTACHMENT, copy.depth, 0);
            copy.width = w;
            copy.height = h;
            copy.colorFormat = format;
        }
        glBlitNamedFramebuffer((GLuint)fbo, copy.fbo, viewport[0], viewport[1], viewport[0] + w, viewport[1] + h,
            0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBlitNamedFramebuffer((GLuint)fbo, copy.fbo, viewport[0], viewport[1], viewport[0] + w, viewport[1] + h,
            0, 0, w, h, GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);
    }

    void Renderer::DrawPoolUnderwater(const PoolParams& params, float surfaceY){
        if (!s_SceneCopy.fbo) { return; }
        if (!s_SceneCopy.emptyVAO) { glCreateVertexArrays(1, &s_SceneCopy.emptyVAO); }
        const Ref<Shader>& shader = s_PoolUnderwaterShader;
        shader->Bind();
        shader->SetInt("u_SceneColor", (int)SCENE_COLOR_UNIT);
        glBindTextureUnit(SCENE_COLOR_UNIT, s_SceneCopy.color);
        shader->SetInt("u_SceneDepth", (int)SCENE_DEPTH_UNIT);
        glBindTextureUnit(SCENE_DEPTH_UNIT, s_SceneCopy.depth);
        shader->SetMat4("u_InvViewProj", glm::inverse(s_SceneData->ProjectionMatrix * s_SceneData->ViewMatrix));
        shader->SetFloat3("u_CameraPos", s_SceneData->CameraPos);
        shader->SetFloat("u_SurfaceY", surfaceY);
        shader->SetFloat3("u_Absorption", Absorption(params.clarity));
        shader->SetFloat3("u_WaterColor", ToLinear(params.waterColor));
        shader->SetFloat3("u_SunColor", ToLinear(params.sunColor));
        shader->SetFloat3("u_Ambient", ToLinear(params.ambient));
        GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glBindVertexArray(s_SceneCopy.emptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        if (depthTest) { glEnable(GL_DEPTH_TEST); }
        shader->Unbind();
    }

    void Renderer::SubmitPoolWater(const Ref<VertexArray>& surface, const PoolParams& params, bool cameraUnderwater){
        if (!s_SceneCopy.fbo) { return; }
        const Ref<Shader>& shader = s_PoolWaterShader;
        shader->Bind();
        shader->SetMat4("view", s_SceneData->ViewMatrix);
        shader->SetMat4("projection", s_SceneData->ProjectionMatrix);
        shader->SetMat4("model", params.model);
        shader->SetMat4("u_InvViewProj", glm::inverse(s_SceneData->ProjectionMatrix * s_SceneData->ViewMatrix));
        shader->SetFloat3("u_CameraPos", s_SceneData->CameraPos);
        shader->SetInt("u_CameraUnderwater", cameraUnderwater ? 1 : 0);
        shader->SetFloat2("u_Spacing", params.spacing);
        shader->SetInt("u_HeightTex", (int)HEIGHT_UNIT);
        glBindTextureUnit(HEIGHT_UNIT, params.heightTexture);
        shader->SetInt("u_SceneColor", (int)SCENE_COLOR_UNIT);
        glBindTextureUnit(SCENE_COLOR_UNIT, s_SceneCopy.color);
        shader->SetInt("u_SceneDepth", (int)SCENE_DEPTH_UNIT);
        glBindTextureUnit(SCENE_DEPTH_UNIT, s_SceneCopy.depth);
        shader->SetFloat3("u_Absorption", Absorption(params.clarity));
        shader->SetFloat3("u_WaterColor", ToLinear(params.waterColor));
        shader->SetFloat3("u_LightDirWorld", glm::normalize(params.lightDir));
        shader->SetFloat3("u_SunColor", ToLinear(params.sunColor));
        shader->SetFloat3("u_Ambient", ToLinear(params.ambient));
        shader->SetFloat3("u_FloorColor", ToLinear(params.tileColor) * (ToLinear(params.ambient) + 0.6f * ToLinear(params.sunColor)));
        shader->SetInt("u_LinearLighting", s_LinearLighting ? 1 : 0);
        BindSunShadow(shader);
        surface->Bind();
        RenderCommand::DrawIndexed(surface);
        shader->Unbind();
    }
}
