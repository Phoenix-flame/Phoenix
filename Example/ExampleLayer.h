#include <Phoenix/core/layer.h>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/event/keyEvent.h>
#include <Phoenix/core/application.h>
#include <Phoenix/imGui/imgui.h>



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
private:
    Ref<Shader> shader;
};