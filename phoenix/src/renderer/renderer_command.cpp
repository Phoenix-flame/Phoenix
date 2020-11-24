#include <Phoenix/renderer/renderer_command.h>


namespace Phoenix{
    Scope<RendererAPI> RenderCommand::s_RendererAPI = RendererAPI::Create();
}