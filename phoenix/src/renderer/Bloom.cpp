#include <Phoenix/renderer/Bloom.h>
#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>
#include <Phoenix/core/assert.h>
#include <algorithm>

namespace Phoenix{

    void Bloom::Init(){
        m_BrightShader    = Shader::Create("assets/shaders/bloom_bright.glsl");
        m_BlurShader      = Shader::Create("assets/shaders/bloom_blur.glsl");
        m_CompositeShader = Shader::Create("assets/shaders/bloom_composite.glsl");

        // Fullscreen quad (position.xy, texcoord.uv), drawn with glDrawArrays.
        float quad[] = {
            -1.0f,  1.0f,  0.0f, 1.0f,
            -1.0f, -1.0f,  0.0f, 0.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
            -1.0f,  1.0f,  0.0f, 1.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 1.0f
        };
        m_Quad = CreateRef<VertexArray>();
        m_Quad->Bind();
        Ref<VertexBuffer> vb = CreateRef<VertexBuffer>(quad, sizeof(quad));
        vb->SetLayout({
            { ShaderDataType::Float2, "a_Position" },
            { ShaderDataType::Float2, "a_TexCoords" }
        });
        m_Quad->AddVertexBuffer(vb);
    }

    void Bloom::Resize(uint32_t width, uint32_t height, uint32_t samples){
        if (width == 0 || height == 0) { return; }
        GLint maxSamples = 1;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        samples = std::max(1u, std::min(samples, (uint32_t)maxSamples));
        if (width == m_Width && height == m_Height && samples == m_Samples && m_HDRFBO) { return; }

        if (m_HDRFBO){
            glDeleteFramebuffers(1, &m_HDRFBO);
            glDeleteTextures(1, &m_HDRColor);
            glDeleteRenderbuffers(1, &m_DepthStencil);
            glDeleteFramebuffers(2, m_PingFBO);
            glDeleteTextures(2, m_PingColor);
        }
        if (m_MSAAFBO){
            glDeleteFramebuffers(1, &m_MSAAFBO);
            glDeleteRenderbuffers(1, &m_MSAAColor);
            glDeleteRenderbuffers(1, &m_MSAADepthStencil);
            m_MSAAFBO = m_MSAAColor = m_MSAADepthStencil = 0;
        }

        m_Width = width;
        m_Height = height;
        m_Samples = samples;

        // HDR scene target: RGBA16F colour + depth/stencil renderbuffer.
        glGenFramebuffers(1, &m_HDRFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, m_HDRFBO);

        glGenTextures(1, &m_HDRColor);
        glBindTexture(GL_TEXTURE_2D, m_HDRColor);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_HDRColor, 0);

        glGenRenderbuffers(1, &m_DepthStencil);
        glBindRenderbuffer(GL_RENDERBUFFER, m_DepthStencil);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_DepthStencil);

        PHX_CORE_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "HDR framebuffer incomplete!");

        // MSAA scene target: the scene renders here; Composite() resolves it into
        // m_HDRColor (the resolved texture the bloom passes read).
        if (m_Samples > 1){
            glGenFramebuffers(1, &m_MSAAFBO);
            glBindFramebuffer(GL_FRAMEBUFFER, m_MSAAFBO);
            glGenRenderbuffers(1, &m_MSAAColor);
            glBindRenderbuffer(GL_RENDERBUFFER, m_MSAAColor);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_Samples, GL_RGBA16F, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_MSAAColor);
            glGenRenderbuffers(1, &m_MSAADepthStencil);
            glBindRenderbuffer(GL_RENDERBUFFER, m_MSAADepthStencil);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_Samples, GL_DEPTH24_STENCIL8, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_MSAADepthStencil);
            PHX_CORE_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "MSAA HDR framebuffer incomplete!");
        }

        // Ping-pong buffers for the separable blur, at half resolution: the blur is low
        // frequency, so a quarter of the pixels looks the same and costs a quarter.
        m_BloomWidth  = std::max(1u, width / 2);
        m_BloomHeight = std::max(1u, height / 2);
        glGenFramebuffers(2, m_PingFBO);
        glGenTextures(2, m_PingColor);
        for (int i = 0; i < 2; i++){
            glBindFramebuffer(GL_FRAMEBUFFER, m_PingFBO[i]);
            glBindTexture(GL_TEXTURE_2D, m_PingColor[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_BloomWidth, m_BloomHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_PingColor[i], 0);
            PHX_CORE_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Bloom framebuffer incomplete!");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void Bloom::BindHDRTarget(){
        glBindFramebuffer(GL_FRAMEBUFFER, m_MSAAFBO ? m_MSAAFBO : m_HDRFBO);
        glViewport(0, 0, m_Width, m_Height);
    }

    void Bloom::DrawFullscreenQuad(){
        m_Quad->Bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    void Bloom::Composite(uint32_t targetFBO, float intensity, float threshold, float exposure, bool encodeSRGB, bool fxaa,
                          bool tonemap){
        if (!m_HDRFBO) { return; }

        if (m_MSAAFBO){
            // Resolve the multisampled scene into the HDR texture.
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_MSAAFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_HDRFBO);
            glBlitFramebuffer(0, 0, m_Width, m_Height, 0, 0, m_Width, m_Height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }

        glDisable(GL_DEPTH_TEST);

        uint32_t bloomTex = 0;
        if (intensity > 0.0f){
            // Bright pass + 2x downsample: HDR colour -> ping[0] (half resolution).
            glBindFramebuffer(GL_FRAMEBUFFER, m_PingFBO[0]);
            glViewport(0, 0, m_BloomWidth, m_BloomHeight);
            m_BrightShader->Bind();
            m_BrightShader->SetInt("u_Scene", 0);
            m_BrightShader->SetFloat("u_Threshold", threshold);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_HDRColor);
            DrawFullscreenQuad();

            // Separable Gaussian blur, ping-ponging between the two buffers. Each half-res
            // pass spans twice the screen distance of a full-res one, so one horizontal +
            // one vertical pass gives about the radius the old 10 full-res passes did.
            const int passes = 2;
            bool horizontal = true;
            m_BlurShader->Bind();
            m_BlurShader->SetInt("u_Image", 0);
            for (int i = 0; i < passes; i++){
                // Horizontal reads ping[0] -> ping[1]; vertical reads ping[1] -> ping[0].
                glBindFramebuffer(GL_FRAMEBUFFER, m_PingFBO[horizontal ? 1 : 0]);
                m_BlurShader->SetInt("u_Horizontal", horizontal ? 1 : 0);
                glBindTexture(GL_TEXTURE_2D, m_PingColor[horizontal ? 0 : 1]);
                DrawFullscreenQuad();
                horizontal = !horizontal;
            }
            // The last pass wrote ping[horizontal ? 0 : 1] (horizontal already flipped).
            bloomTex = m_PingColor[horizontal ? 0 : 1];
        }

        // Composite scene + bloom into the target framebuffer.
        glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
        glViewport(0, 0, m_Width, m_Height);
        m_CompositeShader->Bind();
        m_CompositeShader->SetInt("u_Scene", 0);
        m_CompositeShader->SetInt("u_Bloom", 1);
        m_CompositeShader->SetFloat("u_Intensity", bloomTex ? intensity : 0.0f);
        m_CompositeShader->SetFloat("u_Exposure", exposure);
        m_CompositeShader->SetInt("u_EncodeSRGB", encodeSRGB ? 1 : 0);
        m_CompositeShader->SetInt("u_FXAA", fxaa ? 1 : 0);
        m_CompositeShader->SetInt("u_Tonemap", tonemap ? 1 : 0);
        m_CompositeShader->SetFloat2("u_TexelSize", glm::vec2(1.0f / (float)m_Width, 1.0f / (float)m_Height));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_HDRColor);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, bloomTex ? bloomTex : m_HDRColor); // unused at intensity 0
        DrawFullscreenQuad();

        // Restore default state.
        glActiveTexture(GL_TEXTURE0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glEnable(GL_DEPTH_TEST);
    }
}
