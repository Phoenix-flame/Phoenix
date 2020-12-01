#include "MainLayer.h"

#include <Phoenix/core/base.h>
#include <Phoenix/event/event.h>

MainLayer::MainLayer(const std::string& name): Layer(name), 
        m_MainCamera(1280.0f / 720.0f, glm::radians(45.0), 0.1, 100.0)
    {  
    m_Origin = CreateRef<Origin>();
    for (int i = -5 ; i <= 5 ; i++){
        m_Boxes.push_back(CreateRef<Box>("Box " + std::to_string(i + 5) ,glm::vec3(2*i, 0, 0)));
    }
}

void MainLayer::OnAttach() {
    PHX_INFO("{0} attached.", this->layer_name);
}
void MainLayer::OnDetach() {
    PHX_INFO("{0} detached.", this->layer_name);
}

void MainLayer::OnUpdate(Phoenix::Timestep ts) {
    m_MainCamera.OnUpdate(ts);
    
    Phoenix::RenderCommand::SetClearColor(glm::vec4(m_BackgroundColor, 1.0));
    Phoenix::RenderCommand::Clear();
    
    glm::mat4 projection = m_MainCamera.GetCamera().GetViewProjectionMatrix();
    
    for (auto b:m_Boxes){
        b->Draw(projection);
    }
    m_Origin->Draw(projection);

}
void MainLayer::OnEvent(Phoenix::Event& e) {
    m_MainCamera.OnEvent(e);
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
    ImGui::ShowMetricsWindow();
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
        {
            ImGui::PushID(0);
            ImGui::AlignTextToFramePadding();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
            ImGui::TreeNodeEx("Enabled", flags);
            ImGui::NextColumn();
            ImGui::SetNextItemWidth(-1);
            ImGui::Checkbox("", obj->GetEnablePtr());
            ImGui::NextColumn();
            ImGui::PopID();
        }
        {
            ImGui::PushID(1);
            ImGui::AlignTextToFramePadding();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
            ImGui::TreeNodeEx("Position", flags);
            ImGui::NextColumn();
            ImGui::SetNextItemWidth(-1);
            float pos[3] = {obj->GetPosition().x, obj->GetPosition().y, obj->GetPosition().z};
            if (ImGui::DragFloat3("", pos)){
                obj->SetPosition(glm::vec3(pos[0], pos[1], pos[2]));
            }
            ImGui::NextColumn();
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}
