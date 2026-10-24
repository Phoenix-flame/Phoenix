#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/VertexArray.h>
#include <cstdint>

namespace Phoenix{

    // HDR scene target + bloom post-process. Render the scene into the HDR target
    // (BindHDRTarget), then Composite() extracts bright areas, blurs them at half
    // resolution, and writes tonemapped scene+bloom into the given (LDR) framebuffer.
    class Bloom{
    public:
        void Init();
        // samples > 1 renders the scene with MSAA (clamped to the GPU's limit); it is
        // resolved to the single-sample HDR texture at the start of Composite().
        void Resize(uint32_t width, uint32_t height, uint32_t samples = 1);

        // Bind the HDR framebuffer as the render target (with depth + stencil).
        void BindHDRTarget();

        // Run the bloom passes and composite into targetFBO (0 = default framebuffer).
        // intensity <= 0 skips the bloom passes (tonemap only). encodeSRGB applies the
        // sRGB transfer curve after tonemapping, for linear lighting. fxaa anti-aliases
        // the final image (FXAA); tonemap false copies the scene through unchanged (for
        // FXAA alone, without bloom or linear lighting).
        void Composite(uint32_t targetFBO, float intensity, float threshold, float exposure,
                       bool encodeSRGB = false, bool fxaa = false, bool tonemap = true);
    private:
        void DrawFullscreenQuad();

        uint32_t m_Width = 0, m_Height = 0;
        uint32_t m_BloomWidth = 0, m_BloomHeight = 0; // half resolution

        uint32_t m_Samples = 1;
        uint32_t m_HDRFBO = 0, m_HDRColor = 0, m_DepthStencil = 0;
        // MSAA scene target (only when m_Samples > 1), resolved into m_HDRColor.
        uint32_t m_MSAAFBO = 0, m_MSAAColor = 0, m_MSAADepthStencil = 0;
        uint32_t m_PingFBO[2] = { 0, 0 };
        uint32_t m_PingColor[2] = { 0, 0 };

        Ref<Shader> m_BrightShader;
        Ref<Shader> m_BlurShader;
        Ref<Shader> m_CompositeShader;
        Ref<VertexArray> m_Quad;
    };
}
