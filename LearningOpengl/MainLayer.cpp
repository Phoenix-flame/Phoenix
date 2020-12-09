#include "MainLayer.h"

#include <Phoenix/core/base.h>
#include <Phoenix/event/event.h>
#include <Phoenix/imGui/imgui_internal.h>
MainLayer::MainLayer(const std::string& name): Layer(name), 
        m_MainCamera(1280.0f / 720.0f, glm::radians(45.0), 0.1, 100.0)
    { }

void MainLayer::OnAttach() {
    PHX_INFO("{0} attached.", this->layer_name);

    Application::Get().GetWindow().SetVSync(true);

    FramebufferSpecification fbSpec;
    fbSpec.Width = 640;
    fbSpec.Height = 480;
    m_Framebuffer = Framebuffer::Create(fbSpec);

    m_Scene = CreateRef<Phoenix::Scene>();

    for (int i = 0 ; i < 5 ; i++){
        m_Scene->CreateEntity("Entity" + std::to_string(i));
    }

    m_SceneEditor = CreateRef<SceneEditor>(m_Scene);

    m_Origin = CreateRef<Origin>();
    for (int i = -4 ; i <= 4 ; i++){
        m_Boxes.push_back(CreateRef<Box>("Box " + std::to_string(i + 4) ,glm::vec3(2*i, 0, 0)));
    }
    m_Boxes.push_back(CreateRef<TexturedBox>("Textured Box", glm::vec3(0, 0, 2)));
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
    
    m_Scene->OnUpdate(m_MainCamera.GetCamera(), ts);
    // for (auto b:m_Boxes){
    //     b->Draw(projection);
    // }
    m_Origin->Draw(projection);

    m_Framebuffer->Unbind();
}


void MainLayer::OnEvent(Phoenix::Event& e) {
    if (m_ViewportFocused){
        m_MainCamera.OnEvent(e);
    }
    
    EventDispatcher dispacher(e);
    dispacher.Dispatch<KeyPressedEvent>([](KeyPressedEvent& e){
        if (e.GetKeyCode() == Phoenix::Key::Escape){
            Phoenix::Application::Get().Close();
            return true;
        }
        return false;
    });
}


void MainLayer::OnImGuiRender(){
    // ImGui::ShowMetricsWindow();
    ImGui::Begin("Settings", nullptr, (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize) & ImGuiWindowFlags_None);
	ImGui::Text("Hello");
    ImGui::ColorEdit3("Background Color", glm::value_ptr(m_BackgroundColor));
    if (ImGui::Checkbox("VSync", &vsync)){
        Application::Get().GetWindow().SetVSync(vsync);
    }
	ImGui::End();



    ImGui::Begin("Objects", nullptr, (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize) & ImGuiWindowFlags_None);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
    ImGui::Columns(2);
    ImGui::Separator();
    int id = 0;
    for (auto& obj:m_Boxes){
        ShowObject(static_cast<std::string>(*obj).c_str(), id++, obj);
    }  
    ImGui::Columns(1);
    ImGui::Separator();
    ImGui::PopStyleVar();
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

void MainLayer::ShowObject(const char* prefix, int uid, Ref<Object> obj){
    static int test[3] = {1, 2, 3};
    ImGui::PushID(uid);
    ImGui::AlignTextToFramePadding();
    bool node_open = ImGui::TreeNode(prefix);
    ImGui::NextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::NextColumn();
    if (node_open){
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
        {
            ImGui::PushID(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TreeNodeEx("Enabled", flags);
            ImGui::NextColumn();
            ImGui::SetNextItemWidth(-1);
            ImGui::Checkbox("", obj->GetEnablePtr());
            ImGui::NextColumn();
            ImGui::PopID();
        }
        {
            ImGui::PushID(2);
            ImGui::AlignTextToFramePadding();
            ImGui::TreeNodeEx("Scale", flags);
            ImGui::NextColumn();
            ImGui::SetNextItemWidth(-1);
            float scale[3] = {obj->GetScale().x, obj->GetScale().y, obj->GetScale().z};
            if (ImGui::DragFloat3("", scale, 0.01, 0.01, 10)){
                obj->SetScale(glm::vec3(scale[0], scale[1], scale[2]));
            }
            if (ImGui::Button("Reset")){
                obj->SetScale(glm::vec3(1.0, 1.0, 1.0));
            }
            ImGui::NextColumn();
            ImGui::PopID();
        }
        {
            ImGui::PushID(3);
            ImGui::AlignTextToFramePadding();
            ImGui::TreeNodeEx("Rotation", flags);
            ImGui::NextColumn();
            ImGui::SetNextItemWidth(-1);
            float rotation[3] = {obj->GetRotation().x, obj->GetRotation().y, obj->GetRotation().z};
            if (ImGui::DragFloat3("", rotation, 0.5, -180, 180)){
                obj->SetRotationX(rotation[0]);
                obj->SetRotationY(rotation[1]);
                obj->SetRotationZ(rotation[2]);
            }
            if (ImGui::Button("Reset")){
                obj->ResetRotation();
            }
            ImGui::NextColumn();
            ImGui::PopID();
        }
        {
            DrawVec3Control("Translation", obj->GetPosition());
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}



void MainLayer::DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue, float columnWidth )
{
    ImGuiIO& io = ImGui::GetIO();
    auto boldFont = io.Fonts->Fonts[0];

    ImGui::PushID(label.c_str());

    ImGui::Columns(2);
    ImGui::SetColumnWidth(0, columnWidth);
    ImGui::Text(label.c_str());
    ImGui::NextColumn();
    ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
    // ImGui::Push();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

    float lineHeight = ImGui::GetIO().FontDefault->FontSize + ImGui::GetStyle().FramePadding.y * 2.0;
    ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f });
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
    ImGui::PushFont(boldFont);
    if (ImGui::Button("X", buttonSize))
        values.x = resetValue;
    ImGui::PopFont();
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
    ImGui::PopItemWidth();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.8f, 0.3f, 1.0f });
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
    ImGui::PushFont(boldFont);
    if (ImGui::Button("Y", buttonSize))
        values.y = resetValue;
    ImGui::PopFont();
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
    ImGui::PopItemWidth();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.35f, 0.9f, 1.0f });
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
    ImGui::PushFont(boldFont);
    if (ImGui::Button("Z", buttonSize))
        values.z = resetValue;
    ImGui::PopFont();
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
    ImGui::PopItemWidth();

    ImGui::PopStyleVar();

    ImGui::Columns(1);

    ImGui::PopID();
}