#pragma once
#include <Phoenix/core/layer.h>
#include <Phoenix/Scene/Component.h>
#include <glm/glm.hpp>

using namespace Phoenix;

// The smallest useful engine program: a lit, spinning cube drawn straight through the
// Renderer API into the window, with a small ImGui panel. Start here to see the render
// loop without the Scene/ECS or editor; Sandbox shows the full engine.
class ExampleLayer: public Layer{
public:
    ExampleLayer(const std::string& name = "Layer");

    virtual void OnUpdate(Timestep ts) override;
    virtual void OnEvent(Event& e) override;
    virtual void OnImGuiRender() override;

private:
    Material m_Material;
    DirLightComponent m_Light;
    glm::vec3 m_LightDirection = { -0.4f, -1.0f, -0.6f };
    glm::vec3 m_Ambient = glm::vec3(0.15f);
    glm::vec3 m_BackgroundColor = { 0.10f, 0.12f, 0.16f };
    float m_Angle = 0.0f;           // degrees
    float m_SpinSpeed = 45.0f;      // degrees per second
    bool  m_VSync = true;
};
