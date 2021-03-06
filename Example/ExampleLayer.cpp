#include "ExampleLayer.h"

#include <Phoenix/core/base.h>
#include <Phoenix/event/event.h>
ExampleLayer::ExampleLayer(const std::string& name): Layer(name), 
        main_camera(1280.0f / 720.0f), 
        second_camera(1280.0f / 720.0f, glm::radians(45.0), 0.1, 100.0)
    {  
    this->shader = Shader::Create("/home/alireza/Programming/C++/MyGameEngineProject/Example/assets/shaders/basic.glsl");
    box = CreateRef<Box>("Box");
}

void ExampleLayer::OnAttach() {
    PHX_INFO("{0} attached.", this->layer_name);
    FramebufferSpecification fbSpec;
    fbSpec.Width = 640;
    fbSpec.Height = 480;
    m_Framebuffer = Framebuffer::Create(fbSpec);
}
void ExampleLayer::OnDetach() {
    PHX_INFO("{0} detached.", this->layer_name);
}

void ExampleLayer::OnUpdate(Phoenix::Timestep ts) {
    // Resize
    FramebufferSpecification spec = m_Framebuffer->GetSpecification();
    if ( m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f && // zero sized framebuffer is invalid
        (spec.Width != m_ViewportSize.x || spec.Height != m_ViewportSize.y))
    {
        m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        main_camera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
        second_camera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
    }

    // Update
    if (m_ViewportFocused){
        if (selected_camera == 0) main_camera.OnUpdate(ts);
    }


    m_Framebuffer->Bind();
    Phoenix::RenderCommand::SetClearColor(glm::vec4(_backgroundColor, 1.0));
    Phoenix::RenderCommand::Clear();
    glm::mat4 projection = (selected_camera == 1)?second_camera.GetCamera().GetViewProjectionMatrix():main_camera.GetCamera().GetViewProjectionMatrix();
    box->Draw(projection);


    m_Framebuffer->Unbind();
}

void ExampleLayer::OnEvent(Phoenix::Event& e) {
    if (selected_camera == 0) main_camera.OnEvent(e);
    else if (selected_camera == 1)second_camera.OnEvent(e);
    EventDispatcher dispacher(e);
    dispacher.Dispatch<KeyPressedEvent>([](KeyPressedEvent& e){
        if (e.GetKeyCode() == Phoenix::Key::Escape){
            Phoenix::Application::Get().Close();
            return true;
        }
        return false;
    });
}


void ExampleLayer::OnImGuiRender(){
    ImGui::ShowMetricsWindow();
    ImGui::ShowDemoWindow();
    ImGui::Begin("Settings", nullptr, (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize) & ImGuiWindowFlags_None);
	ImGui::Text("Hello");
    ImGui::ColorEdit3("Background Color", glm::value_ptr(_backgroundColor));
    if (ImGui::Checkbox("VSync", &vsync)){
        Application::Get().GetWindow().SetVSync(vsync);
    }
    
    ImGui::Separator();
    
    ImGui::Text("Camera: ");
    ImGui::RadioButton("Orthographic", &selected_camera, 0); ImGui::SameLine();
    ImGui::RadioButton("Perspective", &selected_camera, 1);

    if (ImGui::SliderFloat("FOV", &fov , 45.0, 100.0)){
        this->second_camera.SetFOV(fov);
    }

	ImGui::End();

    ImGuiOverlay();

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
}


void ExampleLayer::ImGuiOverlay(){
    const float DISTANCE = 10.0f;
    static int corner = 1;
    ImGuiIO& io = ImGui::GetIO();
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (corner != -1){
        window_flags |= ImGuiWindowFlags_NoMove;
        ImVec2 window_pos = ImVec2((corner & 1) ? io.DisplaySize.x - DISTANCE : DISTANCE, (corner & 2) ? io.DisplaySize.y - DISTANCE : DISTANCE);
        ImVec2 window_pos_pivot = ImVec2((corner & 1) ? 1.0f : 0.0f, (corner & 2) ? 1.0f : 0.0f);
        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
    }
    ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
    if (ImGui::Begin("Example: Simple overlay", &overlayEnabled, window_flags)){
        ImGui::Text("Simple overlay\n" "in the corner of the screen.\n" "(right-click to change position)");
        ImGui::Separator();
        if (ImGui::IsMousePosValid())
            ImGui::Text("Mouse Position: (%.1f,%.1f)", io.MousePos.x, io.MousePos.y);
        else
            ImGui::Text("Mouse Position: <invalid>");
        if (ImGui::BeginPopupContextWindow())
        {
            if (ImGui::MenuItem("Custom",       NULL, corner == -1)) corner = -1;
            if (ImGui::MenuItem("Top-left",     NULL, corner == 0)) corner = 0;
            if (ImGui::MenuItem("Top-right",    NULL, corner == 1)) corner = 1;
            if (ImGui::MenuItem("Bottom-left",  NULL, corner == 2)) corner = 2;
            if (ImGui::MenuItem("Bottom-right", NULL, corner == 3)) corner = 3;
            if (overlayEnabled && ImGui::MenuItem("Close")) overlayEnabled = false;
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}