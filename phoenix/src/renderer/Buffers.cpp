#include <Phoenix/renderer/Buffers.h>
#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>

namespace Phoenix{
    VertexBuffer::VertexBuffer(float* data, int size){
        glGenBuffers(1, &VBO);
        Bind();
        glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
    }
    VertexBuffer::~VertexBuffer(){
        glDeleteBuffers(GL_ARRAY_BUFFER, &VBO);
    }
    
    void VertexBuffer::Bind(){
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
    }
    void VertexBuffer::Unbind(){
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    IndexBuffer::IndexBuffer(float* data, int size){
        glGenBuffers(1, &EBO);
        Bind();
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
    }
    IndexBuffer::~IndexBuffer(){
        glDeleteBuffers(GL_ARRAY_BUFFER, &EBO);
    }
    
    void IndexBuffer::Bind(){
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    }

    void IndexBuffer::Unbind(){
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
}