#include <Phoenix/core/layer.h>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/event/keyEvent.h>
#include <Phoenix/core/application.h>
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/renderer/CameraController.h>
#include <Phoenix/renderer/Buffers.h>
#include <Phoenix/renderer/VertexArray.h>
#include <Phoenix/renderer/renderer_command.h>

class Triangle{
public:
    Triangle(float* vertices){
        _vertex_array = Phoenix::CreateRef<Phoenix::VertexArray>();
        Phoenix::Ref<Phoenix::VertexBuffer> vertexBuffer = Phoenix::CreateRef<Phoenix::VertexBuffer>(vertices, sizeof(vertices));
        Phoenix::BufferLayout layout = {
            { Phoenix::ShaderDataType::Float3, "a_Position" },
            { Phoenix::ShaderDataType::Float3, "a_Color" }
        };
        vertexBuffer->SetLayout(layout);
        _vertex_array->AddVertexBuffer(vertexBuffer);
        uint32_t indices[3] = { 0, 1, 2 };
        Phoenix::Ref<Phoenix::IndexBuffer> indexBuffer = Phoenix::CreateRef<Phoenix::IndexBuffer>(indices, sizeof(indices) / sizeof(uint32_t));
        _vertex_array->SetIndexBuffer(indexBuffer);
    }
    void Draw(){
        Phoenix::RenderCommand::DrawIndexed(_vertex_array, 3);
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
    Ref<Triangle> t;
    glm::vec3 _backgroundColor = { 0.28, 0.65, 0.87 };
    OrthographicCameraController main_camera;
    PerspectiveCameraController second_camera;
    int selected_camera = 0;
    bool overlayEnabled = true;
private:
    void ImGuiOverlay();
};