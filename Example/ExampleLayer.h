#include <Phoenix/core/layer.h>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/event/keyEvent.h>
#include <Phoenix/core/application.h>
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/renderer/CameraController.h>


class Triangle{
public:
    Triangle(float* points, float* colors){
        this->points = points;
        this->colors = colors;
    }
    void Init(int num = 9){
        glGenVertexArrays(1, &this->VAO);
        glBindVertexArray(this->VAO);
        glGenBuffers(1, &this->VBO_Points);
        glGenBuffers(1, &this->VBO_Colors);
        glBindBuffer(GL_ARRAY_BUFFER, this->VBO_Points);
        glBufferData(GL_ARRAY_BUFFER, num * sizeof(float), this->points, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindBuffer(GL_ARRAY_BUFFER, this->VBO_Colors);
        glBufferData(GL_ARRAY_BUFFER, num * sizeof(float), this->colors, GL_STATIC_DRAW);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
    void Draw(){
        glBindVertexArray(this->VAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    void Draw(int num){
        glBindVertexArray(this->VAO);
        glDrawArrays(GL_TRIANGLES, 0, num);
    }
private:
    float* points;
    float* colors;
    unsigned int VAO;
    unsigned int VBO_Points;
    unsigned int VBO_Colors;
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