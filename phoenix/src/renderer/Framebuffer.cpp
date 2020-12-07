#include <Phoenix/renderer/Framebuffer.h>
#include <Phoenix/renderer/renderer.h>
#include <Phoenix/core/assert.h>
#include <Phoenix/opengl/opengl_framebuffer.h>

namespace Phoenix{
    Ref<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec){
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None:    PHX_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
			case RendererAPI::API::OpenGL:  return CreateRef<OpenGLFramebuffer>(spec);
		}

		PHX_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}