#include <Phoenix/renderer/VertexArray.h>
#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>
namespace Phoenix{
    VertexArray::VertexArray(){
        glGenVertexArrays(1, &VAO);
    }
    VertexArray::~VertexArray(){
        glDeleteVertexArrays(1, &VAO);
    }

    void VertexArray::Bind(){
        glBindVertexArray(VAO);
    }
    void VertexArray::Unbind(){
        glBindVertexArray(0);
    }

    void VertexArray::AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer){
        
    }
    void VertexArray::SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer){
        Bind();
        indexBuffer->Bind();
        _indicies = indexBuffer;
    }
}