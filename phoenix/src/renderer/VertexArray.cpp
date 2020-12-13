#include <Phoenix/renderer/VertexArray.h>
#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>
#include <Phoenix/core/assert.h>
namespace Phoenix{
    static GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type){
		switch (type){
			case ShaderDataType::Float:    return GL_FLOAT;
			case ShaderDataType::Float2:   return GL_FLOAT;
			case ShaderDataType::Float3:   return GL_FLOAT;
			case ShaderDataType::Float4:   return GL_FLOAT;
			case ShaderDataType::Mat3:     return GL_FLOAT;
			case ShaderDataType::Mat4:     return GL_FLOAT;
			case ShaderDataType::Int:      return GL_INT;
			case ShaderDataType::Int2:     return GL_INT;
			case ShaderDataType::Int3:     return GL_INT;
			case ShaderDataType::Int4:     return GL_INT;
			case ShaderDataType::Bool:     return GL_BOOL;
		}
		return 0;
	}

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
        PHX_CORE_ASSERT(vertexBuffer->GetLayout().GetElements().size(), "Vertex Buffer has no layout!");

		glBindVertexArray(VAO);
		vertexBuffer->Bind();

		const auto& layout = vertexBuffer->GetLayout();
		for (const auto& element : layout){
			switch (element.Type){
				case ShaderDataType::Float:
				case ShaderDataType::Float2:
				case ShaderDataType::Float3:
				case ShaderDataType::Float4:
				case ShaderDataType::Int:
				case ShaderDataType::Int2:
				case ShaderDataType::Int3:
				case ShaderDataType::Int4:
				case ShaderDataType::Bool:{
					glEnableVertexAttribArray(_vertexBufferIndex);
					glVertexAttribPointer(_vertexBufferIndex,
						element.GetComponentCount(),
						ShaderDataTypeToOpenGLBaseType(element.Type),
						element.Normalized ? GL_TRUE : GL_FALSE,
						layout.GetStride(),
						(const void*)element.Offset);
					_vertexBufferIndex++;
					break;
				}
				case ShaderDataType::Mat3:
				case ShaderDataType::Mat4:{
					uint8_t count = element.GetComponentCount();
					for (uint8_t i = 0; i < count; i++){
                        PHX_CORE_TRACE("{0}", _vertexBufferIndex);
						glEnableVertexAttribArray(_vertexBufferIndex);
						glVertexAttribPointer(_vertexBufferIndex,
							count,
							ShaderDataTypeToOpenGLBaseType(element.Type),
							element.Normalized ? GL_TRUE : GL_FALSE,
							layout.GetStride(),
							(const void*)(element.Offset + sizeof(float) * count * i));
                        glEnableVertexAttribArray(_vertexBufferIndex);
						glVertexAttribDivisor(_vertexBufferIndex, 1);
						_vertexBufferIndex++;
					}
					break;
				}
				default:
					PHX_CORE_ASSERT(false, "Unknown ShaderDataType!");
			}
		}

		_verticies.push_back(vertexBuffer);
    }
    
    void VertexArray::SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer){
        Bind();
        indexBuffer->Bind();
        _indicies = indexBuffer;
    }
}