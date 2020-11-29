#pragma once

namespace Phoenix{
    class VertexBuffer{
    public:
        VertexBuffer(float* data, int size);
        ~VertexBuffer();
        
        void Bind();
        void Unbind();
    private:
        unsigned int VBO;
    };


    class IndexBuffer{
    public:
        IndexBuffer(float* data, int size);
        ~IndexBuffer();
        
        void Bind();
        void Unbind();
    private:
        unsigned int EBO;
    };
}