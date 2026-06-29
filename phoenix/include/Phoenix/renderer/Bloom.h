#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/VertexArray.h>
#include <cstdint>

namespace Phoenix{

    // HDR scene target + bloom post-process. Render the scene into the HDR target
    // (BindHDRTarget), then Composite() extracts bright areas, blurs them, and
    // writes scene+bloom into the given (LDR) target framebuffer.
    class Bloom{
    public:
        void Init();
        void Resize(uint32_t width, uint32_t height);

        // Bind the HDR framebuffer as the render target (with depth + stencil).
        void BindHDRTarget();

        // Run the bloom passes and composite into targetFBO (0 = default framebuffer).
        void Composite(uint32_t targetFBO, float intensity, float threshold, float exposure);
    private:
        void DrawFullscreenQuad();

        uint32_t m_Width = 0, m_Height = 0;

        uint32_t m_HDRFBO = 0, m_HDRColor = 0, m_DepthStencil = 0;
        uint32_t m_PingFBO[2] = { 0, 0 };
        uint32_t m_PingColor[2] = { 0, 0 };

        Ref<Shader> m_BrightShader;
        Ref<Shader> m_BlurShader;
        Ref<Shader> m_CompositeShader;
        Ref<VertexArray> m_Quad;
    };
}
