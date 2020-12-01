#include <Phoenix/core/layer.h>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/event/keyEvent.h>
#include <Phoenix/core/application.h>
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/renderer/CameraController.h>
#include <Phoenix/renderer/Buffers.h>
#include <Phoenix/renderer/VertexArray.h>
#include <Phoenix/renderer/renderer_command.h>

class Box{
public:
    Box(){
        _vertex_array = Phoenix::CreateRef<Phoenix::VertexArray>();
        _vertex_array->Bind();
        Phoenix::Ref<Phoenix::VertexBuffer> vertexBuffer = Phoenix::CreateRef<Phoenix::VertexBuffer>(this->vertices, sizeof(this->vertices));
        Phoenix::BufferLayout layout = {
            { Phoenix::ShaderDataType::Float3, "a_Position" },
            { Phoenix::ShaderDataType::Float3, "a_Color" }
        };
        vertexBuffer->SetLayout(layout);
        _vertex_array->AddVertexBuffer(vertexBuffer);
        Phoenix::Ref<Phoenix::IndexBuffer> indexBuffer = Phoenix::CreateRef<Phoenix::IndexBuffer>(indices, sizeof(indices) / sizeof(unsigned int));
        _vertex_array->SetIndexBuffer(indexBuffer);
    }
    void Bind(){
        
    }
    void Draw(){
        _vertex_array->Bind();
        Phoenix::RenderCommand::DrawIndexed(_vertex_array);
    }
private:
    float vertices[218] = {
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,

        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
         0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,

        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,

         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
         0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,

        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,

        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f
    };
    unsigned int indices[36] = {
        0, 1, 2,   // first Box
        3, 4, 5,
        6, 7, 8,
        9, 10, 11,
        12, 13, 14, 
        15, 16, 17, 
        18, 19, 20,
        21, 22, 23,
        24, 25, 26, 
        27, 28, 29,
        30, 31, 32,
        33, 34, 35
    };  
    Phoenix::Ref<Phoenix::VertexArray> _vertex_array;
};

class Base{
public: 
    Base(){
        std::vector<float> vertices;
        std::vector<unsigned int> indices;
        for(int j=0; j<=50; ++j) {
            for(int i=0; i<=50; ++i) {
                float x = (float)i/(float)50;
                float y = 0;
                float z = (float)j/(float)50;
                vertices.push_back(x);vertices.push_back(y);vertices.push_back(z);
            }
        }
        for(int j=0; j<50; ++j) {
            for(int i=0; i<50; ++i) {
                int row1 =  j    * (50+1);
                int row2 = (j+1) * (50+1);
                indices.push_back(row1+i);indices.push_back(row1+i+1);indices.push_back(row1+i+1);indices.push_back(row2+i+1);
                indices.push_back(row2+i+1);indices.push_back(row2+i);indices.push_back(row2+i);indices.push_back(row1+i);
            }
        }
        _vertex_array = Phoenix::CreateRef<Phoenix::VertexArray>();
        _vertex_array->Bind();
        unsigned int* indices_ = indices.data();
        float* vertices_ = vertices.data();
        Phoenix::Ref<Phoenix::VertexBuffer> vertexBuffer = Phoenix::CreateRef<Phoenix::VertexBuffer>(vertices_, sizeof(vertices_));
        Phoenix::BufferLayout layout = {
            { Phoenix::ShaderDataType::Float3, "a_Position" }
        };
        vertexBuffer->SetLayout(layout);
        _vertex_array->AddVertexBuffer(vertexBuffer);
        Phoenix::Ref<Phoenix::IndexBuffer> indexBuffer = Phoenix::CreateRef<Phoenix::IndexBuffer>(indices_, sizeof(indices_) / sizeof(unsigned int));
        _vertex_array->SetIndexBuffer(indexBuffer);
    }
    void Draw(){
        _vertex_array->Bind();
        Phoenix::RenderCommand::DrawIndexed(_vertex_array);
    }

private:
    Phoenix::Ref<Phoenix::VertexArray> _vertex_array;
};

using namespace Phoenix;
class ExampleLayer: public Layer{
public:
    ExampleLayer(const std::string& name = "Layer");
    ~ExampleLayer() = default;

    virtual void OnAttach() override;
    virtual void OnDetach() override;

    virtual void OnUpdate(Timestep ts) override;
    virtual void OnEvent(Event& e) override;
    virtual void OnImGuiRender() override;

private:
    bool test = true;
    bool vsync = false;
private:
    unsigned int fbo;
    Ref<Shader> shader;
    Ref<Box> t;
    Ref<Base> base;
    glm::vec3 _backgroundColor = { 0.28, 0.65, 0.87 };
    OrthographicCameraController main_camera;
    PerspectiveCameraController second_camera;
    int selected_camera = 0;
    bool overlayEnabled = true;
    float fov = 45.0;
private:
    void ImGuiOverlay();
};