// Renderer: fires as glowing gas (a ray march through each flame's grid) and embers as
// glowing streaks, both coloured by black-body radiation.
#include <Phoenix/renderer/renderer.h>
#include <Phoenix/renderer/FireVolume.h>
#include <Phoenix/Physics/FireSim.h>
#include "RenderUnits.h"
#include <algorithm>
#include <cmath>

namespace Phoenix{

    Ref<Shader> Renderer::s_FireVolumeShader;
    Ref<Shader> Renderer::s_FireEmberShader;
    Renderer::FireTargets Renderer::s_FireTargets;

    static const int FIRE_TABLE_SIZE = 256;
    static const float FIRE_MAX_KELVIN = 4000.0f;
    // The table's brightness unit: the glow of a black body at this temperature.
    static const float FIRE_REFERENCE_KELVIN = 1900.0f;

    // ---- Black-body colour ----------------------------------------------------------

    // CIE 1931 colour matching functions, multi-lobe Gaussian fit (Wyman, Sloan &
    // Shirley 2013); lambda in nm.
    static double Lobe(double lambda, double mu, double s1, double s2){
        double t = (lambda - mu) / (lambda < mu ? s1 : s2);
        return std::exp(-0.5 * t * t);
    }

    // Linear sRGB radiance of a black body at `kelvin` (Planck's law over 380-780 nm),
    // in arbitrary units.
    static glm::dvec3 BlackBodyRadiance(double kelvin){
        if (kelvin < 300.0) { return glm::dvec3(0.0); }
        const double c2 = 1.4387769e-2;     // h c / k, m K
        glm::dvec3 xyz(0.0);
        for (int l = 380; l <= 780; l += 5){
            double lambda = (double)l, m = lambda * 1e-9;
            double planck = 1.0 / (m * m * m * m * m * (std::exp(c2 / (m * kelvin)) - 1.0));
            double x = 1.056 * Lobe(lambda, 599.8, 37.9, 31.0) + 0.362 * Lobe(lambda, 442.0, 16.0, 26.7) - 0.065 * Lobe(lambda, 501.1, 20.4, 26.2);
            double y = 0.821 * Lobe(lambda, 568.8, 46.9, 40.5) + 0.286 * Lobe(lambda, 530.9, 16.3, 31.1);
            double z = 1.217 * Lobe(lambda, 437.0, 11.8, 36.0) + 0.681 * Lobe(lambda, 459.0, 26.0, 13.8);
            xyz += planck * glm::dvec3(x, y, z);
        }
        glm::dvec3 rgb( 3.2406 * xyz.x - 1.5372 * xyz.y - 0.4986 * xyz.z,
                       -0.9689 * xyz.x + 1.8758 * xyz.y + 0.0415 * xyz.z,
                        0.0557 * xyz.x - 0.2040 * xyz.y + 1.0570 * xyz.z);
        // Not glm::max: it keeps a -0.0, which pow() in a shader can turn into NaN.
        return { rgb.x > 0.0 ? rgb.x : 0.0, rgb.y > 0.0 ? rgb.y : 0.0, rgb.z > 0.0 ? rgb.z : 0.0 };
    }

    glm::vec3 Renderer::BlackBodyColor(float kelvin){
        glm::dvec3 c = BlackBodyRadiance(std::max((double)kelvin, 800.0));
        double peak = std::max({ c.x, c.y, c.z });
        return peak > 0.0 ? glm::vec3(c / peak) : glm::vec3(1.0f, 0.4f, 0.1f);
    }

    // Glow from 0 to FIRE_MAX_KELVIN: the black body's colour (chromaticity) at the
    // brightness of its total radiation (Stefan-Boltzmann, T^4, 1 at the reference
    // temperature), fading out below the Draper point (~800 K) where nothing glows
    // visibly. The visible band alone grows far faster with temperature (about T^12
    // here), which no exposure can show: cameras and eyes see a flame's core
    // saturated and its edges still glowing, as this does.
    static uint32_t CreateBlackBodyTable(){
        std::vector<float> texels((size_t)FIRE_TABLE_SIZE * 3);
        for (int i = 0; i < FIRE_TABLE_SIZE; i++){
            float kelvin = (i + 0.5f) / FIRE_TABLE_SIZE * FIRE_MAX_KELVIN;
            float r = kelvin / FIRE_REFERENCE_KELVIN;
            float visible = glm::smoothstep(650.0f, 1050.0f, kelvin);
            glm::vec3 c = Renderer::BlackBodyColor(kelvin) * (r * r * r * r * visible);
            texels[(size_t)i * 3] = c.x;
            texels[(size_t)i * 3 + 1] = c.y;
            texels[(size_t)i * 3 + 2] = c.z;
        }
        uint32_t tex;
        glCreateTextures(GL_TEXTURE_2D, 1, &tex);
        glTextureStorage2D(tex, 1, GL_RGB16F, FIRE_TABLE_SIZE, 1);
        glTextureSubImage2D(tex, 0, 0, 0, FIRE_TABLE_SIZE, 1, GL_RGB, GL_FLOAT, texels.data());
        glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return tex;
    }

    // ---- FireVolume -----------------------------------------------------------------

    FireVolume::~FireVolume(){
        if (m_Texture) { glDeleteTextures(1, &m_Texture); }
    }

    void FireVolume::Upload(const FireSim& sim){
        const glm::ivec3 dims = sim.Dims();
        if (dims.x <= 0 || sim.Scalars().empty()) { return; }
        if (dims != m_Dims){
            if (m_Texture) { glDeleteTextures(1, &m_Texture); }
            glCreateTextures(GL_TEXTURE_3D, 1, &m_Texture);
            glTextureStorage3D(m_Texture, 1, GL_RGBA16F, dims.x, dims.y, dims.z);
            glTextureParameteri(m_Texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(m_Texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            // Zero outside, so the gas fades out at the box's faces.
            const float border[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            glTextureParameterfv(m_Texture, GL_TEXTURE_BORDER_COLOR, border);
            for (GLenum wrap : { GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R })
                glTextureParameteri(m_Texture, wrap, GL_CLAMP_TO_BORDER);
            m_Dims = dims;
        }
        glTextureSubImage3D(m_Texture, 0, 0, 0, 0, dims.x, dims.y, dims.z, GL_RGBA, GL_FLOAT, sim.Scalars().data());
    }

    // ---- Drawing ------------------------------------------------------------------

    void Renderer::RenderFire(const FireParams& params){
        if (!params.volume || !s_SceneCopy.fbo) { return; }
        FireTargets& t = s_FireTargets;
        if (!t.blackBody) { t.blackBody = CreateBlackBodyTable(); }
        if (!s_SceneCopy.emptyVAO) { glCreateVertexArrays(1, &s_SceneCopy.emptyVAO); }

        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        const glm::mat4& view = s_SceneData->ViewMatrix;
        const glm::mat4& projection = s_SceneData->ProjectionMatrix;
        const Ref<Shader>& shader = s_FireVolumeShader;
        shader->Bind();
        shader->SetMat4("u_ViewProjection", projection * view);
        shader->SetMat4("u_InvViewProj", glm::inverse(projection * view));
        shader->SetFloat3("u_CameraPos", s_SceneData->CameraPos);
        shader->SetFloat3("u_Origin", params.origin);
        shader->SetFloat3("u_Size", params.size);
        shader->SetFloat3("u_Dims", glm::vec3(params.dims));
        shader->SetFloat2("u_ViewportSize", { (float)viewport[2], (float)viewport[3] });
        shader->SetFloat2("u_ViewportOffset", { (float)viewport[0], (float)viewport[1] });
        shader->SetFloat("u_Brightness", params.brightness);
        shader->SetFloat("u_KelvinScale", params.flameTemperature / FIRE_MAX_KELVIN);
        shader->SetFloat3("u_SmokeColor", ToLinear(params.smokeColor));
        shader->SetFloat("u_SmokeDensity", params.smokeDensity);
        shader->SetFloat("u_Haze", params.heatHaze);
        shader->SetFloat("u_Time", params.time);
        shader->SetFloat3("u_Ambient", ToLinear(params.ambient));
        shader->SetInt("u_LinearLighting", s_LinearLighting ? 1 : 0);
        shader->SetFloat("u_Seed", (float)(t.frame++ % 64u) * 0.618f);
        shader->SetInt("u_Volume", (int)HEIGHT_UNIT);
        glBindTextureUnit(HEIGHT_UNIT, params.volume);
        shader->SetInt("u_BlackBody", (int)CAUSTICS_UNIT);
        glBindTextureUnit(CAUSTICS_UNIT, t.blackBody);
        shader->SetInt("u_SceneColor", (int)SCENE_COLOR_UNIT);
        glBindTextureUnit(SCENE_COLOR_UNIT, s_SceneCopy.color);
        shader->SetInt("u_SceneDepth", (int)SCENE_DEPTH_UNIT);
        glBindTextureUnit(SCENE_DEPTH_UNIT, s_SceneCopy.depth);

        // The box's back faces: one fragment per covered pixel, also from inside it. The
        // march stops at the scene's depth itself, so no depth test.
        GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE);
        GLint cullFace;
        glGetIntegerv(GL_CULL_FACE_MODE, &cullFace);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(s_SceneCopy.emptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        shader->Unbind();

        glCullFace((GLenum)cullFace);
        if (!cull) { glDisable(GL_CULL_FACE); }
        if (depthTest) { glEnable(GL_DEPTH_TEST); }
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    void Renderer::RenderEmbers(const std::vector<EmberPoint>& embers, float flameTemperature, float brightness){
        if (embers.empty()) { return; }
        FireTargets& t = s_FireTargets;
        if (!t.blackBody) { t.blackBody = CreateBlackBodyTable(); }
        if (!t.vao){
            glCreateVertexArrays(1, &t.vao);
            glCreateBuffers(1, &t.vbo);
            glVertexArrayVertexBuffer(t.vao, 0, t.vbo, 0, sizeof(EmberPoint));
            glVertexArrayBindingDivisor(t.vao, 0, 1);
            for (GLuint a : { 0u, 1u }){
                glEnableVertexArrayAttrib(t.vao, a);
                glVertexArrayAttribFormat(t.vao, a, 4, GL_FLOAT, GL_FALSE, a * 4 * sizeof(float));
                glVertexArrayAttribBinding(t.vao, a, 0);
            }
        }
        if (embers.size() > t.capacity){
            t.capacity = std::max(embers.size(), t.capacity * 2);
            glNamedBufferData(t.vbo, t.capacity * sizeof(EmberPoint), nullptr, GL_DYNAMIC_DRAW);
        }
        glNamedBufferSubData(t.vbo, 0, embers.size() * sizeof(EmberPoint), embers.data());

        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        const Ref<Shader>& shader = s_FireEmberShader;
        shader->Bind();
        shader->SetMat4("u_View", s_SceneData->ViewMatrix);
        shader->SetMat4("u_Projection", s_SceneData->ProjectionMatrix);
        shader->SetFloat2("u_ViewportSize", { (float)viewport[2], (float)viewport[3] });
        shader->SetFloat("u_KelvinScale", flameTemperature / FIRE_MAX_KELVIN);
        shader->SetFloat("u_Brightness", brightness);
        shader->SetInt("u_LinearLighting", s_LinearLighting ? 1 : 0);
        shader->SetInt("u_BlackBody", (int)CAUSTICS_UNIT);
        glBindTextureUnit(CAUSTICS_UNIT, t.blackBody);

        GLboolean cull = glIsEnabled(GL_CULL_FACE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glBindVertexArray(t.vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)embers.size());
        glBindVertexArray(0);
        shader->Unbind();
        if (cull) { glEnable(GL_CULL_FACE); }
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}
