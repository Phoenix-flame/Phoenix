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
    private:
        std::vector<Ref<VertexBuffer>> _verticies;
        Ref<IndexBuffer> _indicies;
        unsigned int VAO;
    };
}