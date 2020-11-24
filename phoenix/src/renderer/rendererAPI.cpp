#include <Phoenix/renderer/renderer_api.h>
#include <Phoenix/core/assert.h>
#include <Phoenix/opengl/opengl_rendererAPI.h>
namespace Phoenix{
    RendererAPI::API RendererAPI::s_API = RendererAPI::API::OpenGL;
    Scope<RendererAPI> RendererAPI::Create(){
        switch(RendererAPI::s_API){
            case RendererAPI::API::None:{
                PHX_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
            }
            case RendererAPI::API::OpenGL:{
                return CreateScope<OpenGLRendererAPI>();
            }
        }
        PHX_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
    }
}