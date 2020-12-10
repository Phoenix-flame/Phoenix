#include "MainLayer.h"

#include <Phoenix/core/base.h>
#include <Phoenix/event/event.h>
#include <Phoenix/imGui/imgui_internal.h>
#include <Phoenix/Scene/Component.h>
MainLayer::MainLayer(const std::string& name): Layer(name), 
        m_MainCamera(1280.0f / 720.0f)
    { }

void MainLayer::OnAttach() {
    PHX_INFO("{0} attached.", this->layer_name);

    Application::Get().GetWindow().SetVSync(true);

    FramebufferSpecification fbSpec;
    fbSpec.Width = 640;
    fbSpec.Height = 480;
    m_Framebuffer = Framebuffer::Create(fbSpec);

    m_Scene = CreateRef<Phoenix::Scene>();

    m_Scene->OnResize(m_Framebuffer->GetSpecification().Width, m_Framebuffer->GetSpecification().Height);

    m_CameraEntity = m_Scene->CreateEntity("Camera");
    m_CameraEntity.AddComponent<CameraComponent>();

    m_CubeEntity = m_Scene->CreateEntity("Cube");
    m_CubeEntity.AddComponent<CubeComponent>();

    m_SceneEditor = CreateRef<SceneEditor>(m_Scene);
}

void MainLayer::OnDetach() {
    PHX_INFO("{0} detached.", this->layer_name);
}

void MainLayer::OnUpdate(Phoenix::Timestep ts) {
    Phoenix::RenderCommand::SetClearColor(glm::vec4(0.0, 0.0, 0.0, 1.0));
    Phoenix::RenderCommand::Clear();

    FramebufferSpecification spec = m_Framebuffer->GetSpecification();
    if ( m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
        (spec.Width != m_ViewportSize.x || spec.Height != m_ViewportSize.y)){
        m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        m_Scene->OnResize(m_ViewportSize.x, m_ViewportSize.y);
        m_MainCamera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
    }

    // Update
    if (m_ViewportFocused){
        m_MainCamera.OnUpdate(ts);
    }

    m_Framebuffer->Bind();

    Phoenix::RenderCommand::SetClearColor(glm::vec4(m_BackgroundColor, 1.0));
    Phoenix::RenderCommand::Clear();
    
    glm::mat4 projection = m_MainCamera.GetCamera().GetViewProjectionMatrix();
    m_Scene->OnUpdate(projection, ts);
    
    m_Framebuffer->Unbind();
}


void MainLayer::OnEvent(Phoenix::Event& e) {
    if (m_ViewportFocused){
        m_MainCamera.OnEvent(e);
    }
    
    EventDispatcher dispacher(e);
    dispacher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(MainLayer::OnResize));
    dispacher.Dispatch<KeyPressedEvent>([](KeyPressedEvent& e){
        if (e.GetKeyCode() == Phoenix::Key::Escape){
            Phoenix::Application::Get().Close();
            return true;
        }
        return false;
    });
}


void MainLayer::OnImGuiRender(){
    ImGui::Begin("Settings", nullptr, (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize) & ImGuiWindowFlags_None);
	ImGui::Text("Metrics");
    ImGuiContext& g = *GImGui;
    ImGuiIO& io = g.IO;
    ImGuiMetricsConfig* cfg = &g.DebugMetricsConfig;

    // Basic info
    ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
    ImGui::Text("%d vertices, %d indices (%d triangles)", io.MetricsRenderVertices, io.MetricsRenderIndices, io.MetricsRenderIndices / 3);
    ImGui::Separator();

    ImGui::ColorEdit3("Background Color", glm::value_ptr(m_BackgroundColor));
    if (ImGui::Checkbox("VSync", &vsync)){
        Application::Get().GetWindow().SetVSync(vsync);
    }
	ImGui::End();


    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
    ImGui::Begin("Viewport");

    m_ViewportFocused = ImGui::IsWindowFocused();
    m_ViewportHovered = ImGui::IsWindowHovered();
    
    Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused || !m_ViewportHovered);

    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

    uint64_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
    ImGui::Image(reinterpret_cast<void*>(textureID), ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
    ImGui::End();
    ImGui::PopStyleVar();

    m_SceneEditor->OnImGuiRender();

    

}
