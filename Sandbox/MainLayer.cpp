#include "MainLayer.h"

#include <Phoenix/core/base.h>
#include <Phoenix/event/event.h>
#include <Phoenix/imGui/imgui_internal.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/core/Profiler.h>

#include <Phoenix/core/application.h>
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/SceneSerializer.h>

#include <Phoenix/renderer/Framebuffer.h>
#include <Phoenix/core/log.h>
#include <vendor/ImGuizmo/ImGuizmo.h>
#include <Phoenix/core/Input.h>
#include <Phoenix/Math/Math.h>
MainLayer::MainLayer(const std::string& name): Phoenix::Layer(name), 
        m_MainCamera()
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

    BuildShowcaseScene();

    m_ShaderLibrary = CreateScope<ShaderLibrary>();
    m_ShaderLibrary->Add(Shader::Create("assets/shaders/basic.glsl"));
    m_ShaderLibrary->Add(Shader::Create("assets/shaders/lighting.glsl"));
    m_ShaderLibrary->Add(Shader::Create("assets/shaders/texture.glsl"));
    

    m_SceneEditor = CreateRef<SceneEditor>(m_Scene);

    m_Bloom.Init();

    m_LastSnapshot = SceneSerializer(m_Scene).SerializeToString();
}

void MainLayer::BuildShowcaseScene() {
    // Low ambient so shadows read clearly and the neon glow pops.
    m_Scene->AmbientColor() = glm::vec3(0.04f);

    {
        auto cam = m_Scene->CreateEntity("Camera");
        cam.AddComponent<CameraComponent>();
        cam.GetComponent<TransformComponent>().Translation = { 0.0f, 4.0f, 14.0f };
    }

    // Directional "sun" aimed down at an angle to cast clear shadows.
    {
        auto sun = m_Scene->CreateDirLightEntity("Sun");
        auto& t = sun.GetComponent<TransformComponent>();
        t.Translation = { 7.0f, 9.0f, 5.0f };
        t.Rotation = { glm::radians(-55.0f), glm::radians(35.0f), 0.0f };
        auto& dl = sun.GetComponent<DirLightComponent>();
        dl.ambient  = glm::vec3(0.04f);
        dl.diffuse  = glm::vec3(0.9f);
        dl.specular = glm::vec3(1.0f);
    }

    // Matte floor: receives shadows; also a static physics collider.
    {
        auto floor = m_Scene->CreateEntity("Floor");
        auto& c = floor.AddComponent<CubeComponent>();
        c.material.ambient = glm::vec3(0.5f);
        c.material.diffuse = glm::vec3(0.5f);
        c.material.specular = glm::vec3(0.1f);
        c.material.shininess = 8.0f;
        c.material.reflectivity = 0.25f; // subtle reflective sheen
        auto& t = floor.GetComponent<TransformComponent>();
        t.Translation = { 0.0f, -1.0f, 0.0f };
        t.Scale = { 24.0f, 0.5f, 24.0f };
        floor.AddComponent<RigidBodyComponent>().type = RigidBodyComponent::Type::Static;
        floor.AddComponent<BoxColliderComponent>();
    }

    // Shadow-casting pillars.
    const glm::vec3 pillarPos[] = { {-5.0f, 1.5f, -3.0f}, {5.0f, 2.0f, -4.0f}, {-3.0f, 1.0f, 4.0f} };
    const float pillarHeight[] = { 3.0f, 4.0f, 2.0f };
    for (int i = 0; i < 3; i++){
        auto e = m_Scene->CreateEntity("Pillar " + std::to_string(i + 1));
        auto& c = e.AddComponent<CubeComponent>();
        c.material.ambient = glm::vec3(0.35f, 0.35f, 0.4f);
        c.material.diffuse = glm::vec3(0.35f, 0.35f, 0.4f);
        c.material.specular = glm::vec3(0.4f);
        c.material.shininess = 32.0f;
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = pillarPos[i];
        t.Scale = { 1.0f, pillarHeight[i], 1.0f };
    }

    // Neon glowing cubes: bloom + emit coloured light onto the scene.
    struct Neon { const char* name; glm::vec3 pos; glm::vec3 color; };
    const Neon neons[] = {
        { "Neon Magenta", { -4.0f, 2.5f,  0.0f }, { 1.0f, 0.0f, 1.0f } },
        { "Neon Cyan",    {  4.0f, 1.2f,  1.0f }, { 0.0f, 1.0f, 1.0f } },
        { "Neon Green",   {  0.0f, 3.5f, -3.0f }, { 0.2f, 1.0f, 0.2f } },
    };
    for (const auto& n : neons){
        auto e = m_Scene->CreateEntity(n.name);
        auto& c = e.AddComponent<CubeComponent>();
        c.material.ambient = n.color * 0.1f;
        c.material.diffuse = n.color;
        c.material.emissive = n.color;
        c.material.emissiveStrength = 4.0f;
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = n.pos;
        t.Scale = glm::vec3(0.6f);
    }

    // Glossy cube: strong specular highlight (closest thing to reflection we have).
    {
        auto e = m_Scene->CreateEntity("Mirror Cube");
        auto& c = e.AddComponent<CubeComponent>();
        c.material.ambient = glm::vec3(0.05f);
        c.material.diffuse = glm::vec3(0.15f, 0.15f, 0.2f);
        c.material.specular = glm::vec3(1.0f);
        c.material.shininess = 128.0f;
        c.material.reflectivity = 0.8f; // mirror-like environment reflection
        e.GetComponent<TransformComponent>().Translation = { 2.5f, 0.5f, 4.0f };
    }

    // Textured backpack: lit, casts a shadow, and drops with a convex mesh collider
    // (its actual geometry) onto the floor when you press Run.
    {
        auto bp = m_Scene->CreateEntity("Backpack");
        auto& mesh = bp.AddComponent<MeshComponent>();
        mesh.model = CreateRef<Model>("backpack/backpack.obj");
        bp.GetComponent<TransformComponent>().Translation = { 0.0f, 5.0f, 0.0f };
        bp.AddComponent<RigidBodyComponent>(); // dynamic
        bp.AddComponent<MeshColliderComponent>(); // convex hull of the mesh
    }

    // A Lua-scripted cube that orbits and colour-cycles while playing.
    {
        auto e = m_Scene->CreateEntity("Scripted Cube");
        auto& c = e.AddComponent<CubeComponent>();
        c.material.diffuse = glm::vec3(0.8f, 0.3f, 0.2f);
        e.GetComponent<TransformComponent>().Translation = { -2.0f, 1.0f, 3.0f };
        auto& script = e.AddComponent<LuaScriptComponent>();
        script.source =
            "local t = 0.0\n"
            "function OnUpdate(dt)\n"
            "    t = t + dt\n"
            "    SetTranslation(-2.0 + math.sin(t) * 2.0, 1.0 + 0.5 * math.sin(t * 2.0), 3.0)\n"
            "    SetColor(0.5 + 0.5 * math.sin(t), 0.5 + 0.5 * math.sin(t + 2.0), 0.5 + 0.5 * math.sin(t + 4.0))\n"
            "end\n";
    }

    // A glowing cube that drops when you press Run (physics + bloom).
    {
        auto box = m_Scene->CreateEntity("Falling Glow Cube");
        auto& c = box.AddComponent<CubeComponent>();
        c.material.diffuse = glm::vec3(1.0f, 0.6f, 0.1f);
        c.material.emissive = glm::vec3(1.0f, 0.6f, 0.1f);
        c.material.emissiveStrength = 3.0f;
        box.GetComponent<TransformComponent>().Translation = { 1.0f, 7.0f, 0.0f };
        box.AddComponent<RigidBodyComponent>();
        box.AddComponent<BoxColliderComponent>();
    }
}

void MainLayer::CommitHistory() {
    // Don't record during simulation (transforms change every frame) or while an
    // edit is in progress (a gizmo drag / held slider) — wait until it settles.
    if (m_Scene->IsRunning() || ImGuizmo::IsUsing() || ImGui::IsAnyItemActive())
        return;

    std::string current = SceneSerializer(m_Scene).SerializeToString();
    if (current == m_LastSnapshot)
        return;

    m_UndoStack.push_back(m_LastSnapshot);
    if (m_UndoStack.size() > 100)
        m_UndoStack.erase(m_UndoStack.begin());
    m_RedoStack.clear();
    m_LastSnapshot = std::move(current);
}

void MainLayer::RestoreScene(const std::string& snapshot) {
    // Reconcile the existing scene in place (keeps loaded models + selection)
    // instead of rebuilding it from scratch.
    SceneSerializer(m_Scene).ApplyFromString(snapshot);
    // Re-serialize the live scene as the baseline so CommitHistory doesn't record
    // a spurious entry (entity ids can differ from the snapshot after create/delete).
    m_LastSnapshot = SceneSerializer(m_Scene).SerializeToString();
}

void MainLayer::Undo() {
    if (m_Scene->IsRunning() || m_UndoStack.empty())
        return;
    m_RedoStack.push_back(m_LastSnapshot);
    std::string snapshot = m_UndoStack.back();
    m_UndoStack.pop_back();
    RestoreScene(snapshot);
}

void MainLayer::Redo() {
    if (m_Scene->IsRunning() || m_RedoStack.empty())
        return;
    m_UndoStack.push_back(m_LastSnapshot);
    std::string snapshot = m_RedoStack.back();
    m_RedoStack.pop_back();
    RestoreScene(snapshot);
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
    if (m_ViewportHovered){
        PHX_PROFILE("MainCamera Update");
        m_MainCamera.OnUpdate(ts);
    }

    if (m_BloomEnabled){
        // Render the scene to the HDR target, then bloom + composite into m_Framebuffer.
        m_Bloom.Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        m_Bloom.BindHDRTarget();
        Phoenix::RenderCommand::SetClearColor(glm::vec4(m_BackgroundColor, 1.0));
        Phoenix::RenderCommand::Clear();
        {
            PHX_PROFILE("Scene Update");
            m_Scene->OnUpdate(m_MainCamera, ts, m_SceneEditor->GetSelectedEntity());
        }
        m_Bloom.Composite(m_Framebuffer->GetRendererID(), m_BloomIntensity, m_BloomThreshold, m_Exposure);
    }
    else{
        m_Framebuffer->Bind();
        Phoenix::RenderCommand::SetClearColor(glm::vec4(m_BackgroundColor, 1.0));
        Phoenix::RenderCommand::Clear();
        {
            PHX_PROFILE("Scene Update");
            m_Scene->OnUpdate(m_MainCamera, ts, m_SceneEditor->GetSelectedEntity());
        }
        m_Framebuffer->Unbind();
    }
}


void MainLayer::OnEvent(Phoenix::Event& e) {
    if (m_ViewportHovered){
        m_MainCamera.OnEvent(e);
    }
    
    EventDispatcher dispacher(e);
    dispacher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(MainLayer::OnResize));
    dispacher.Dispatch<KeyPressedEvent>(BIND_EVENT_FN(MainLayer::OnKeyPressed));
    dispacher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& e){
        if (e.GetKeyCode() == Phoenix::Key::Escape){
            Phoenix::Application::Get().Close();
            return true;
        }
        // if (e.GetKeyCode() == Phoenix::Key::S){
        //     for (unsigned int i = 0 ; i < 100; i++) {
        //         std::string name = "cube" + std::to_string(i);
        //         auto cube = m_Scene->CreateEntity(name);
        //         cube.AddComponent<CubeComponent>();
        //         auto& t = cube.GetComponent<TransformComponent>();
        //         t.Translation.x = rand()%100;
        //         t.Translation.z = rand()%100;
        //     }
        // }
        return false;
    });
}


void MainLayer::OnImGuiRender(){
    ImGuizmo::BeginFrame();
    // Note: Switch this to true to enable dockspace
    static bool dockspaceOpen = true;
    static bool opt_fullscreen_persistant = true;
    bool opt_fullscreen = opt_fullscreen_persistant;
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

    // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
    // because it would be confusing to have two docking targets within each others.
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    if (opt_fullscreen)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        // PHX_TRACE("Position: [{0}, {1}] , Size: [{2}, {3}]", viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    }

    // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background and handle the pass-thru hole, so we ask Begin() to not render a background.
    if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
        window_flags |= ImGuiWindowFlags_NoBackground;

    // Important: note that we proceed even if Begin() returns false (aka window is collapsed).
    // This is because we want to keep our DockSpace() active. If a DockSpace() is inactive, 
    // all active windows docked into it will lose their parent and become undocked.
    // We cannot preserve the docking relationship between an active window and an inactive docking, otherwise 
    // any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace Demo", &dockspaceOpen, window_flags);
    ImGui::PopStyleVar();

    if (opt_fullscreen)
        ImGui::PopStyleVar(2);

    // DockSpace
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    float minWinSizeX = style.WindowMinSize.x;
    style.WindowMinSize.x = 370.0f;
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    }

    style.WindowMinSize.x = minWinSizeX;

    // Main Menu
    {
        bool import_shader = false, save = false;
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Load Showcase Scene")) {
                    m_Scene = CreateRef<Scene>();
                    m_Scene->OnResize(m_ViewportSize.x, m_ViewportSize.y);
                    BuildShowcaseScene();
                    m_SceneEditor = CreateRef<SceneEditor>(m_Scene);
                    m_UndoStack.clear();
                    m_RedoStack.clear();
                    m_LastSnapshot = SceneSerializer(m_Scene).SerializeToString();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Import Scene")) {
                    std::string path = "scene.phx";
                    auto scene = CreateRef<Scene>();
                    if (SceneSerializer(scene).Deserialize(path)) {
                        m_Scene = scene;
                        m_Scene->OnResize(m_ViewportSize.x, m_ViewportSize.y);
                        m_SceneEditor = CreateRef<SceneEditor>(m_Scene);
                    }
                }
                if (ImGui::MenuItem("Export Scene")) {
                    std::string path = "scene.phx";
                    SceneSerializer(m_Scene).Serialize(path);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Import Shader")) { import_shader = true; }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo", "CTRL+Z", false, !m_UndoStack.empty())) { Undo(); }
                if (ImGui::MenuItem("Redo", "CTRL+SHIFT+Z", false, !m_RedoStack.empty())) { Redo(); }
                ImGui::Separator();
                if (ImGui::MenuItem("Cut", "CTRL+X")) {}
                if (ImGui::MenuItem("Copy", "CTRL+C")) {}
                if (ImGui::MenuItem("Paste", "CTRL+V")) {}
                ImGui::EndMenu();
            }

            // Run / Stop the physics simulation (right-aligned in the menu bar).
            {
                const char* label = m_Scene->IsRunning() ? "Stop" : "Run";
                float buttonWidth = 60.0f;
                ImGui::SameLine(ImGui::GetWindowWidth() - buttonWidth - 10.0f);
                if (ImGui::Button(label, ImVec2(buttonWidth, 0.0f))){
                    if (m_Scene->IsRunning()) { m_Scene->OnRuntimeStop(); }
                    else                      { m_Scene->OnRuntimeStart(); }
                }
            }
            ImGui::EndMainMenuBar();
        }

        if(import_shader)
            ImGui::OpenPopup("Import Shader");

        if(file_dialog.showFileDialog("Import Shader", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".glsl"))
        {
            m_ShaderLibrary->Add(Shader::Create(file_dialog.selected_path));
        }
    }

    // Settings
    {
        ImGui::Begin("Settings", nullptr, (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize) & ImGuiWindowFlags_None);
        ImGui::Text("Metrics");
        ImGuiIO& io = ImGui::GetIO();

        // Basic info
        ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        ImGui::Text("%d vertices, %d indices (%d triangles)", io.MetricsRenderVertices, io.MetricsRenderIndices, io.MetricsRenderIndices / 3);
        ImGui::Separator();

        ImGui::ColorEdit3("Background Color", glm::value_ptr(m_BackgroundColor));
        ImGui::ColorEdit3("Ambient Light", glm::value_ptr(m_Scene->AmbientColor()));

        ImGui::Separator();
        ImGui::Checkbox("Bloom", &m_BloomEnabled);
        if (m_BloomEnabled){
            ImGui::DragFloat("Exposure", &m_Exposure, 0.05f, 0.1f, 8.0f);
            ImGui::DragFloat("Bloom Intensity", &m_BloomIntensity, 0.05f, 0.0f, 5.0f);
            ImGui::DragFloat("Bloom Threshold", &m_BloomThreshold, 0.05f, 0.0f, 5.0f);
        }
        if (ImGui::Checkbox("VSync", &vsync)){
            Application::Get().GetWindow().SetVSync(vsync);
        }
        ImGui::End();
    }

    // Profiler
    {
        ImGui::Begin("Profiler", nullptr, (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize) & ImGuiWindowFlags_None);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20);
        ImGui::Columns(2);

        ImGui::Text("Scope");
        ImGui::NextColumn();
        ImGui::Text("Elapsed Time (µs)");
        ImGui::NextColumn();
        ImGui::Separator();

        int id = 0;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
        for (auto p:Application::s_TimeContainer){
            ImGui::PushID(id++);
            ImGui::AlignTextToFramePadding();
            ImGui::Text((p.first).c_str(), nullptr);
            ImGui::NextColumn();
            ImGui::SetNextItemWidth(-1);
            ImGui::Text((std::to_string(p.second) + " µs").c_str(), nullptr);
            ImGui::NextColumn();
            ImGui::PopID();
        }
        ImGui::Columns(1);
        ImGui::Separator();
        ImGui::PopStyleVar();
        ImGui::PopStyleVar();
        ImGui::End();
    }

    // Shader Library
    {
        ImGui::Begin("Shader Library", nullptr, (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize) & ImGuiWindowFlags_None);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
        ImGui::Columns(2);
        ImGui::Text("Name");
        ImGui::NextColumn();
        ImGui::Text("Number of usage");
        ImGui::NextColumn();
        ImGui::Separator();

        ShaderLibrary::ShaderMap::iterator iter;
        int id = 0;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
        for(iter = m_ShaderLibrary->GetBegin(); iter != m_ShaderLibrary->GetEnd() ; iter ++)
        {
            ImGui::PushID(id++);
            ImGui::AlignTextToFramePadding();
            ImGui::Text((iter->first).c_str(), nullptr);
            ImGui::NextColumn();
            ImGui::Text("0");
            ImGui::NextColumn();
            ImGui::PopID();
        }
        ImGui::Columns(1);
        ImGui::Separator();
        ImGui::PopStyleVar();
        ImGui::End();
    }



    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
    ImGui::Begin("Viewport");

    m_ViewportFocused = ImGui::IsWindowFocused();
    m_ViewportHovered = ImGui::IsWindowHovered();
    
    Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused || !m_ViewportHovered);

    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

    // Top-left of the rendered image in screen space (used for the gizmo rect and picking).
    ImVec2 imageMin = ImGui::GetCursorScreenPos();
    uint64_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
    ImGui::Image(reinterpret_cast<void*>(textureID), ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

    // Transform gizmo for the selected entity.
    Entity selectedEntity = m_SceneEditor->GetSelectedEntity();
    if (selectedEntity && m_GizmoType != -1 && selectedEntity.HasComponent<TransformComponent>())
    {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(imageMin.x, imageMin.y, m_ViewportSize.x, m_ViewportSize.y);

        const glm::mat4& cameraProjection = m_MainCamera.GetProjection();
        glm::mat4 cameraView = m_MainCamera.GetView();

        auto& tc = selectedEntity.GetComponent<TransformComponent>();
        glm::mat4 transform = tc.GetTransform();

        ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
            (ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::LOCAL, glm::value_ptr(transform));

        if (ImGuizmo::IsUsing())
        {
            glm::vec3 translation, rotation, scale;
            if (Math::DecomposeTransform(transform, translation, rotation, scale))
            {
                glm::vec3 deltaRotation = rotation - tc.Rotation;
                tc.Translation = translation;
                tc.Rotation += deltaRotation;
                tc.Scale = scale;
            }
        }
    }

    // Click-to-select: cast a ray from the cursor and pick the nearest entity.
    // Skipped while interacting with the gizmo or orbiting the camera.
    if (m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver())
    {
        ImVec2 mouse = ImGui::GetMousePos();
        float mx = mouse.x - imageMin.x;
        float my = mouse.y - imageMin.y;
        if (mx >= 0.0f && my >= 0.0f && mx < m_ViewportSize.x && my < m_ViewportSize.y)
        {
            float ndcX = (mx / m_ViewportSize.x) * 2.0f - 1.0f;
            float ndcY = 1.0f - (my / m_ViewportSize.y) * 2.0f;

            glm::mat4 invViewProj = glm::inverse(m_MainCamera.GetProjection() * m_MainCamera.GetView());
            glm::vec4 nearPoint = invViewProj * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
            glm::vec4 farPoint  = invViewProj * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
            nearPoint /= nearPoint.w;
            farPoint  /= farPoint.w;

            glm::vec3 rayOrigin = glm::vec3(nearPoint);
            glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint) - glm::vec3(nearPoint));

            m_SceneEditor->SetSelectedEntity(m_Scene->PickEntity(rayOrigin, rayDir));
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();

    m_SceneEditor->OnImGuiRender();

    ImGui::End();

    // Record an undo snapshot once the current edit has settled.
    CommitHistory();
}



bool MainLayer::OnKeyPressed(KeyPressedEvent& e){
    // Shortcuts
    if (e.GetRepeatCount() > 0)
        return false;

    bool control = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
    bool shift = Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);

    // Undo / redo: Ctrl+Z and Ctrl+Shift+Z.
    if (control && e.GetKeyCode() == Key::Z){
        if (shift) { Redo(); }
        else       { Undo(); }
        return true;
    }

    // Gizmo operation shortcuts (only when not mid-manipulation).
    if (!ImGuizmo::IsUsing()){
        switch (e.GetKeyCode()){
            case Key::Q: m_GizmoType = -1; break; // none
            case Key::W: m_GizmoType = 0;  break; // translate
            case Key::E: m_GizmoType = 1;  break; // rotate
            case Key::R: m_GizmoType = 2;  break; // scale
        }
    }

    return false;
}