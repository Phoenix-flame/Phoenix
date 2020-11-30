#pragma once
#include <Phoenix/renderer/Buffers.h>
#include <Phoenix/core/base.h>
#include <vector>
namespace Phoenix{
    class VertexArray{
    public:
        VertexArray();
        ~VertexArray();

        void Bind();
        void Unbind();

        void AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer);
        void SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer);
        const std::vector<Ref<VertexBuffer>>& GetVertexBuffers() const { return _verticies; }
		const Ref<IndexBuffer>& GetIndexBuffer() const { return _indicies; }
    private:
        std::vector<Ref<VertexBuffer>> _verticies;
        Ref<IndexBuffer> _indicies;
        uint32_t _vertexBufferIndex = 0;
        unsigned int VAO;
    };
}