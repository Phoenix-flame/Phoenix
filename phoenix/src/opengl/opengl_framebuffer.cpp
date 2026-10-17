#include <Phoenix/opengl/opengl_framebuffer.h>
#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>
#include <Phoenix/core/assert.h>
#include <algorithm>
namespace Phoenix{
    static const uint32_t s_MaxFramebufferSize = 8192;

	OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferSpecification& spec)
		: m_Specification(spec){
		Invalidate();
	}

	OpenGLFramebuffer::~OpenGLFramebuffer(){
		Release();
	}

	void OpenGLFramebuffer::Release(){
		if (m_RendererID){
			glDeleteFramebuffers(1, &m_RendererID);
			glDeleteTextures(1, &m_ColorAttachment);
			glDeleteTextures(1, &m_DepthAttachment);
			m_RendererID = m_ColorAttachment = m_DepthAttachment = 0;
		}
		if (m_MultisampleFBO){
			glDeleteFramebuffers(1, &m_MultisampleFBO);
			glDeleteRenderbuffers(1, &m_MultisampleColor);
			glDeleteRenderbuffers(1, &m_MultisampleDepth);
			m_MultisampleFBO = m_MultisampleColor = m_MultisampleDepth = 0;
		}
	}

	void OpenGLFramebuffer::Invalidate(){
		Release();

		glCreateFramebuffers(1, &m_RendererID);
		glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
		
		glCreateTextures(GL_TEXTURE_2D, 1, &m_ColorAttachment);
		glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Specification.Width, m_Specification.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachment, 0);

		glCreateTextures(GL_TEXTURE_2D, 1, &m_DepthAttachment);
		glBindTexture(GL_TEXTURE_2D, m_DepthAttachment);
		glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH24_STENCIL8, m_Specification.Width, m_Specification.Height);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_DepthAttachment, 0);

		PHX_CORE_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Framebuffer is incomplete!");

		if (m_Specification.Samples > 1){
			glCreateFramebuffers(1, &m_MultisampleFBO);
			glBindFramebuffer(GL_FRAMEBUFFER, m_MultisampleFBO);
			glGenRenderbuffers(1, &m_MultisampleColor);
			glBindRenderbuffer(GL_RENDERBUFFER, m_MultisampleColor);
			glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_Specification.Samples, GL_RGBA8,
				m_Specification.Width, m_Specification.Height);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_MultisampleColor);
			glGenRenderbuffers(1, &m_MultisampleDepth);
			glBindRenderbuffer(GL_RENDERBUFFER, m_MultisampleDepth);
			glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_Specification.Samples, GL_DEPTH24_STENCIL8,
				m_Specification.Width, m_Specification.Height);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_MultisampleDepth);
			PHX_CORE_ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Multisample framebuffer is incomplete!");
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void OpenGLFramebuffer::Bind(){
		glBindFramebuffer(GL_FRAMEBUFFER, m_MultisampleFBO ? m_MultisampleFBO : m_RendererID);
		glViewport(0, 0, m_Specification.Width, m_Specification.Height);
	}

	void OpenGLFramebuffer::Unbind(){
		if (m_MultisampleFBO){
			// Resolve: average each pixel's samples into the displayed texture.
			glBindFramebuffer(GL_READ_FRAMEBUFFER, m_MultisampleFBO);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_RendererID);
			glBlitFramebuffer(0, 0, m_Specification.Width, m_Specification.Height,
				0, 0, m_Specification.Width, m_Specification.Height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void OpenGLFramebuffer::Resize(uint32_t width, uint32_t height){
		if (width == 0 || height == 0 || width > s_MaxFramebufferSize || height > s_MaxFramebufferSize){
			PHX_CORE_WARN("Attempted to rezize framebuffer to {0}, {1}", width, height);
			return;
		}
		m_Specification.Width = width;
		m_Specification.Height = height;
		
		Invalidate();
	}

	uint32_t Framebuffer::GetMaxSamples(){
		GLint maxSamples = 1;
		glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
		return (uint32_t)std::max(1, maxSamples);
	}

	void OpenGLFramebuffer::SetSamples(uint32_t samples){
		samples = std::max(1u, std::min(samples, GetMaxSamples()));
		if (samples == m_Specification.Samples) { return; }
		m_Specification.Samples = samples;
		Invalidate();
	}
}
