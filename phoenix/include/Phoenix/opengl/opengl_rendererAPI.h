#pragma once
#include <Phoenix/renderer/renderer_api.h>
#include <Phoenix/renderer/VertexArray.h>
namespace Phoenix{

    class OpenGLRendererAPI : public RendererAPI{
	public:
		virtual void Init() override;
		virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;

        virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) override;
		virtual void SetClearColor(const glm::vec4& color) override;
		virtual void Clear() override;
	};
}