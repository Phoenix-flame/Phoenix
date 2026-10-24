#include "SceneEditor.h"
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/imGui/imgui_internal.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/imGui/ImGuiLayer.h>
#include <Phoenix/Physics/Physics.h>
#include <glm/gtc/type_ptr.hpp>
#include <random>
#include "EditorUI.h"
#include "AIPrefabs.h"
#include <Phoenix/AI/Environment.h>
#include <cstring>
#include <cctype>
#include <type_traits>
#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <cmath>

namespace Phoenix{

    void SceneEditor::CreateEntityMenu(){
        if (ImGui::MenuItem("Empty Entity"))
            m_SelectedEntity = m_ActiveScene->CreateEntity("Empty Entity");

        if (ImGui::BeginMenu("AI")){
            Scene& scene = *m_ActiveScene;
            if (ImGui::MenuItem("Agent (walker)"))
                m_SelectedEntity = AIPrefabs::CreateAgent(scene, { 0.0f, 0.45f, 0.0f });
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Upright box with Move control, a 7-ray sensor and the Python policy.");
            if (ImGui::MenuItem("Agent (rolling ball)"))
                m_SelectedEntity = AIPrefabs::CreateRollerAgent(scene, { 0.0f, 0.5f, 0.0f });
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sphere pushed by continuous forces.");
            ImGui::Separator();
            if (ImGui::MenuItem("Goal Zone"))
                m_SelectedEntity = AIPrefabs::CreateGoal(scene, { 0.0f, 0.06f, -4.0f });
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("+1 reward and end of episode on entry (tag \"Goal\").");
            if (ImGui::MenuItem("Hazard Zone"))
                m_SelectedEntity = AIPrefabs::CreateHazard(scene, { 3.0f, 0.03f, 0.0f });
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("-1 reward and end of episode on entry (tag \"Hazard\").");
            if (ImGui::MenuItem("Wall"))
                m_SelectedEntity = AIPrefabs::CreateWall(scene, { 0.0f, 0.6f, 4.0f }, { 6.0f, 1.2f, 0.4f });
            if (ImGui::MenuItem("Training Arena"))
                m_SelectedEntity = AIPrefabs::CreateArena(scene, { 0.0f, 0.0f, 0.0f }, 16.0f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("16 x 16 m floor enclosed by four walls.");
            ImGui::EndMenu();
        }

        ImGui::Separator();
        CreatePhysicsObjectMenu();
        if (ImGui::BeginMenu("Shape")){
            struct ShapeDef { const char* label; const char* name; PrimitiveComponent::Type type; };
            const ShapeDef shapes[] = {
                { "Cube",              "Cube",     PrimitiveComponent::Type::Cube     },
                { "Sphere",            "Sphere",   PrimitiveComponent::Type::Sphere   },
                { "Pillar (Cylinder)", "Pillar",   PrimitiveComponent::Type::Cylinder },
                { "Cone",              "Cone",     PrimitiveComponent::Type::Cone     },
                { "Plane",             "Plane",    PrimitiveComponent::Type::Plane    },
                { "Capsule",           "Capsule",  PrimitiveComponent::Type::Capsule  },
            };
            for (const auto& shape : shapes){
                if (ImGui::MenuItem(shape.label)){
                    m_SelectedEntity = m_ActiveScene->CreateEntity(shape.name);
                    m_SelectedEntity.AddComponent<PrimitiveComponent>(shape.type);
                }
            }
            ImGui::EndMenu();
        }

        bool pointLightsFull = m_ActiveScene->GetNumberOfPointLights() >= Scene::MAX_NUM_POINT_LIGHTS;
        if (ImGui::MenuItem("Point Light", nullptr, false, !pointLightsFull)){
            try
            {
                m_SelectedEntity = m_ActiveScene->CreatePointLightEntity("Point Light");
            }
            catch(const std::exception& e)
            {
                PHX_ERROR(e.what());
            }
        }
        if (pointLightsFull && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("The renderer supports up to %d point lights.", Scene::MAX_NUM_POINT_LIGHTS);
        if (ImGui::MenuItem("Directional Light"))
            m_SelectedEntity = m_ActiveScene->CreateDirLightEntity("Directional Light");

        ImGui::Separator();
        if (ImGui::MenuItem("Terrain")){
            m_SelectedEntity = m_ActiveScene->CreateEntity("Terrain");
            auto& terrain = m_SelectedEntity.AddComponent<TerrainComponent>();
            terrain.material.ambient  = glm::vec3(0.25f, 0.35f, 0.20f);
            terrain.material.diffuse  = glm::vec3(0.35f, 0.50f, 0.28f);
            terrain.material.specular = glm::vec3(0.1f);
            terrain.material.shininess = 8.0f;
        }
        if (ImGui::MenuItem("Water")){
            m_SelectedEntity = m_ActiveScene->CreateEntity("Water");
            m_SelectedEntity.AddComponent<WaterComponent>();
        }
        if (ImGui::MenuItem("Pool")){
            m_SelectedEntity = m_ActiveScene->CreateEntity("Pool");
            m_SelectedEntity.AddComponent<PoolComponent>();
        }
        if (ImGui::MenuItem("Particle Fluid")){
            m_SelectedEntity = m_ActiveScene->CreateEntity("Fluid");
            m_SelectedEntity.AddComponent<FluidComponent>();
        }
    }

    void SceneEditor::ScenePanel(){
        ImGui::Begin("Hierarchy");

        // Toolbar: filter + create.
        {
            float buttonSize = ImGui::GetFrameHeight();
            ImGui::SetNextItemWidth(-(buttonSize + ImGui::GetStyle().ItemSpacing.x));
            ImGui::InputTextWithHint("##Search", "Search entities", m_SearchBuf, sizeof(m_SearchBuf));
            ImGui::SameLine();
            if (ImGui::Button("##CreateEntity", ImVec2(buttonSize, buttonSize)))
                ImGui::OpenPopup("CreateEntity");
            ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            EditorUI::DrawIcon(ImGui::GetWindowDrawList(), EditorUI::Icon::Plus,
                ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f), 12.0f, EditorUI::TextColor());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Create entity");
            if (ImGui::BeginPopup("CreateEntity")){
                CreateEntityMenu();
                ImGui::EndPopup();
            }
        }
        ImGui::Spacing();

        // EnTT 3.16 removed registry::each(); iterate the entity storage instead.
        // Snapshot ids first (EntityNode may DestroyEntity, invalidating live
        // iteration) AND filter to valid entities: the entity storage retains
        // tombstones for in-place-deleted entities, which would otherwise reach
        // EntityNode as stale handles and crash on GetComponent.
        auto& registry = m_ActiveScene->m_Registry;
        std::vector<entt::entity> entities;
        for (auto entityID : registry.storage<entt::entity>())
            if (registry.valid(entityID))
                entities.push_back(entityID);
        m_EntityCount = (int)entities.size();

        // Case-insensitive substring filter on the tag.
        std::string filter = m_SearchBuf;
        std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        auto matches = [&](Entity entity){
            if (filter.empty()) { return true; }
            std::string tag = entity.GetComponent<TagComponent>().Tag;
            std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c){ return (char)std::tolower(c); });
            return tag.find(filter) != std::string::npos;
        };

        ImGui::BeginChild("##EntityList", ImVec2(0, 0), false);
        int shown = 0;
        for (auto entityID : entities)
		{
			Entity entity{ entityID , m_ActiveScene.get() };
            if (!matches(entity)) { continue; }
			EntityNode(entity);
            shown++;
		}
        if (shown == 0){
            if (entities.empty()) { EditorUI::EmptyState("The scene is empty", "Right-click or press + to create an entity."); }
            else                  { EditorUI::EmptyState("No matching entities"); }
        }

        // Click on blank space deselects.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
		{
			m_SelectedEntity = {};
		}

		// Right-click on blank space
		if (ImGui::BeginPopupContextWindow(NULL, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)){
            ImGui::TextDisabled("Create");
            ImGui::Separator();
            CreateEntityMenu();
			ImGui::EndPopup();
		}
        ImGui::EndChild();

        ImGui::End();
    }


    void SceneEditor::EntityPanel(){
        ImGui::Begin("Properties");
		if (m_SelectedEntity){
			DrawComponents(m_SelectedEntity);
		}
        else{
            EditorUI::EmptyState("No entity selected", "Pick one in the Hierarchy or click it in the viewport.");
        }

		ImGui::End();
    }

    void SceneEditor::ScriptEditorPanel(){
        m_ScriptPanel.OnImGuiRender(m_SelectedEntity, &m_ShowScriptEditor);
    }

    void SceneEditor::TimelinePanel(){
        if (!m_ShowTimeline) { return; }
        ImGui::Begin("Timeline", &m_ShowTimeline);

        Entity e = m_SelectedEntity;
        if (!e || !e.HasComponent<AnimationComponent>() || !e.HasComponent<MeshComponent>()){
            EditorUI::EmptyState("Nothing to animate", "Select an entity with an Animation component.");
            ImGui::End();
            return;
        }
        auto& comp = e.GetComponent<AnimationComponent>();
        auto& mc = e.GetComponent<MeshComponent>();
        if (!mc.model || !mc.model->IsReady() || !mc.model->HasAnimations()){
            ImGui::TextDisabled("Model still loading or has no animations.");
            ImGui::End();
            return;
        }

        int clipCount = (int)mc.model->GetAnimationCount();
        if (comp.clip >= clipCount) { comp.clip = clipCount - 1; }

        // Clip selector.
        std::string preview = mc.model->GetAnimationName((size_t)comp.clip);
        if (preview.empty()) { preview = "clip " + std::to_string(comp.clip); }
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("Clip", preview.c_str())){
            for (int i = 0; i < clipCount; i++){
                std::string n = mc.model->GetAnimationName((size_t)i);
                if (n.empty()) { n = "clip " + std::to_string(i); }
                bool sel = (comp.clip == i);
                if (ImGui::Selectable(n.c_str(), sel)) { comp.clip = i; }
                if (sel) { ImGui::SetItemDefaultFocus(); }
            }
            ImGui::EndCombo();
        }

        // Transport.
        if (ImGui::Button(comp.playing ? "Pause" : "Play ")) { comp.playing = !comp.playing; }
        ImGui::SameLine();
        if (ImGui::Button("Stop")){ comp.playing = false; comp.pendingSeek = 0.0f; }
        ImGui::SameLine();
        const char* loopModes[] = { "Loop", "Once", "PingPong" };
        ImGui::SetNextItemWidth(110); ImGui::Combo("##loop", &comp.loopMode, loopModes, IM_ARRAYSIZE(loopModes));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120); ImGui::DragFloat("Speed", &comp.speed, 0.05f, 0.0f, 5.0f);

        // Scrubber (dragging pauses + seeks).
        float dur = comp.animator ? comp.animator->GetDurationSeconds() : 0.0f;
        float cur = comp.animator ? comp.animator->GetCurrentSeconds() : 0.0f;
        float t = cur;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##scrub", &t, 0.0f, dur > 0.0f ? dur : 1.0f, "")){
            comp.playing = false;
            comp.pendingSeek = t;
        }
        ImGui::Text("%.2f / %.2f s   (clip %d/%d)", cur, dur, comp.clip + 1, clipCount);

        // Ruler with playhead + read-only event markers for the current clip.
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        float h = 22.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
        if (dur > 0.0f){
            // Second ticks.
            for (float s = 1.0f; s < dur; s += 1.0f){
                float tx = p0.x + w * (s / dur);
                dl->AddLine(ImVec2(tx, p0.y + h * 0.6f), ImVec2(tx, p0.y + h), EditorUI::MutedColor(), 1.0f);
            }
            float px = p0.x + w * (cur / dur);
            dl->AddLine(ImVec2(px, p0.y), ImVec2(px, p0.y + h), EditorUI::Accent(), 2.0f);
            dl->AddTriangleFilled(ImVec2(px - 5.0f, p0.y), ImVec2(px + 5.0f, p0.y), ImVec2(px, p0.y + 6.0f), EditorUI::Accent());
            for (const auto& ev : comp.events){
                if (ev.clip != comp.clip) { continue; }
                float frac = ev.time / dur; frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
                float ex = p0.x + w * frac;
                dl->AddLine(ImVec2(ex, p0.y), ImVec2(ex, p0.y + h), IM_COL32(80, 180, 255, 255), 1.5f);
            }
        }
        ImGui::Dummy(ImVec2(w, h));

        ImGui::End();
    }

    // The editable Material of an entity (cube / primitive / mesh / terrain), if any.
    static Material* EntityMaterial(Entity e){
        if (!e) { return nullptr; }
        if (e.HasComponent<CubeComponent>())      { return &e.GetComponent<CubeComponent>().material; }
        if (e.HasComponent<PrimitiveComponent>()) { return &e.GetComponent<PrimitiveComponent>().material; }
        if (e.HasComponent<MeshComponent>())      { return &e.GetComponent<MeshComponent>().material; }
        if (e.HasComponent<TerrainComponent>())   { return &e.GetComponent<TerrainComponent>().material; }
        return nullptr;
    }

    void SceneEditor::TexturePanel(){
        if (!m_ShowTextures) { return; }
        ImGui::Begin("Textures", &m_ShowTextures);

        // Load a texture file into the palette (path relative to the executable, e.g.
        // "assets/textures/brick.png"). Texture2D caches by path.
        ImGui::SetNextItemWidth(-90.0f);
        ImGui::InputText("##texpath", m_TextureLoadBuf, sizeof(m_TextureLoadBuf));
        ImGui::SameLine();
        if (ImGui::Button("Load", ImVec2(80, 0))){
            std::string p = m_TextureLoadBuf;
            if (!p.empty() && std::find(m_LoadedTextures.begin(), m_LoadedTextures.end(), p) == m_LoadedTextures.end()){
                if (Texture2D::Create(p)) // loads + caches; logs and returns null on failure
                    m_LoadedTextures.push_back(p);
            }
        }

        // Selected object's current maps (auto-add their paths to the palette so they show).
        Material* mat = EntityMaterial(m_SelectedEntity);
        ImGui::Separator();
        if (mat){
            for (const std::string& pth : { mat->diffusePath, mat->normalPath })
                if (!pth.empty() && std::find(m_LoadedTextures.begin(), m_LoadedTextures.end(), pth) == m_LoadedTextures.end())
                    m_LoadedTextures.push_back(pth);

            ImGui::Text("Selected: %s", m_SelectedEntity.GetComponent<TagComponent>().Tag.c_str());
            ImGui::Text("Diffuse: %s", mat->diffusePath.empty() ? "(none)" : mat->diffusePath.c_str());
            ImGui::SameLine(); if (ImGui::SmallButton("Clear##diff")) { mat->diffusePath.clear(); }
            ImGui::Text("Normal:  %s", mat->normalPath.empty() ? "(none)" : mat->normalPath.c_str());
            ImGui::SameLine(); if (ImGui::SmallButton("Clear##norm")) { mat->normalPath.clear(); }
        }
        else{
            ImGui::TextDisabled("Select an object with a material to assign textures.");
        }

        ImGui::Separator();
        ImGui::TextDisabled("Palette  (Diffuse / Normal assigns to the selected object)");

        for (size_t i = 0; i < m_LoadedTextures.size(); i++){
            ImGui::PushID((int)i);
            const std::string& path = m_LoadedTextures[i];
            Ref<Texture2D> tex = Texture2D::Create(path); // cached
            // Thumbnail (flip V so it shows upright).
            if (tex){
                ImGui::Image((ImTextureID)(intptr_t)tex->GetRendererID(), ImVec2(56, 56), ImVec2(0, 1), ImVec2(1, 0));
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            ImGui::TextWrapped("%s", path.c_str());
            if (ImGui::SmallButton("Diffuse") && mat) { mat->diffusePath = path; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Normal") && mat)  { mat->normalPath = path; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")){ m_LoadedTextures.erase(m_LoadedTextures.begin() + i); ImGui::EndGroup(); ImGui::PopID(); break; }
            ImGui::EndGroup();
            ImGui::Separator();
            ImGui::PopID();
        }

        ImGui::End();
    }

    // Small glyph for the hierarchy row, picked from what the entity mostly is.
    static void DrawEntityIcon(ImDrawList* dl, Entity entity, ImVec2 c){
        const float r = 5.5f;
        if (entity.HasComponent<AgentComponent>()){
            // Robot face: accent head with two eyes and an antenna.
            dl->AddRectFilled(ImVec2(c.x - r, c.y - r * 0.8f), ImVec2(c.x + r, c.y + r * 0.9f), EditorUI::Accent(), 2.5f);
            dl->AddCircleFilled(ImVec2(c.x - r * 0.4f, c.y), 1.4f, IM_COL32(20, 20, 24, 255));
            dl->AddCircleFilled(ImVec2(c.x + r * 0.4f, c.y), 1.4f, IM_COL32(20, 20, 24, 255));
            dl->AddLine(ImVec2(c.x, c.y - r * 0.8f), ImVec2(c.x, c.y - r * 1.25f), EditorUI::Accent(), 1.2f);
        }
        else if (entity.HasComponent<RewardZoneComponent>()){
            bool positive = entity.GetComponent<RewardZoneComponent>().reward >= 0.0f;
            ImU32 col = positive ? EditorUI::Success() : EditorUI::Danger();
            ImU32 fill = positive ? EditorUI::Success(0.3f) : EditorUI::Danger(0.3f);
            dl->AddRectFilled(ImVec2(c.x - r * 0.85f, c.y - r * 0.85f), ImVec2(c.x + r * 0.85f, c.y + r * 0.85f), fill, 2.0f);
            dl->AddRect(ImVec2(c.x - r * 0.85f, c.y - r * 0.85f), ImVec2(c.x + r * 0.85f, c.y + r * 0.85f), col, 2.0f, 0, 1.3f);
        }
        else if (entity.HasComponent<CameraComponent>()){
            ImU32 col = IM_COL32(110, 170, 255, 255);
            dl->AddRectFilled(ImVec2(c.x - r, c.y - r * 0.6f), ImVec2(c.x + r * 0.35f, c.y + r * 0.6f), col, 1.5f);
            dl->AddTriangleFilled(ImVec2(c.x + r * 0.35f, c.y), ImVec2(c.x + r, c.y - r * 0.6f), ImVec2(c.x + r, c.y + r * 0.6f), col);
        }
        else if (entity.HasComponent<DirLightComponent>() || entity.HasComponent<PointLightComponent>()){
            ImU32 col = IM_COL32(255, 205, 80, 255);
            dl->AddCircleFilled(c, r * 0.5f, col);
            for (int i = 0; i < 8; i++){
                float a = IM_PI * 0.25f * i;
                dl->AddLine(ImVec2(c.x + cosf(a) * r * 0.75f, c.y + sinf(a) * r * 0.75f),
                            ImVec2(c.x + cosf(a) * r * 1.1f, c.y + sinf(a) * r * 1.1f), col, 1.2f);
            }
        }
        else if (entity.HasComponent<TerrainComponent>()){
            ImU32 col = IM_COL32(120, 200, 110, 255);
            dl->AddTriangleFilled(ImVec2(c.x - r, c.y + r * 0.7f), ImVec2(c.x - r * 0.15f, c.y - r * 0.8f), ImVec2(c.x + r * 0.7f, c.y + r * 0.7f), col);
            dl->AddTriangleFilled(ImVec2(c.x, c.y + r * 0.7f), ImVec2(c.x + r * 0.55f, c.y - r * 0.2f), ImVec2(c.x + r * 1.1f, c.y + r * 0.7f), col);
        }
        else if (entity.HasComponent<WaterComponent>() || entity.HasComponent<PoolComponent>() || entity.HasComponent<FluidComponent>()){
            ImU32 col = IM_COL32(90, 200, 230, 255);
            for (int row = 0; row < 2; row++){
                float y = c.y - r * 0.3f + row * r * 0.75f;
                dl->PathClear();
                for (int i = 0; i <= 8; i++){
                    float x = c.x - r + i * (2.0f * r / 8.0f);
                    dl->PathLineTo(ImVec2(x, y + sinf(i * IM_PI * 0.5f) * r * 0.22f));
                }
                dl->PathStroke(col, 0, 1.4f);
            }
        }
        else if (entity.HasComponent<MeshComponent>()){
            EditorUI::DrawIcon(dl, EditorUI::Icon::Local, c, r * 2.2f, IM_COL32(190, 140, 255, 255));
        }
        else if (entity.HasComponent<CubeComponent>() || entity.HasComponent<PrimitiveComponent>()){
            // Swatch in the object's own colour.
            const Material& m = entity.HasComponent<CubeComponent>() ? entity.GetComponent<CubeComponent>().material
                                                                     : entity.GetComponent<PrimitiveComponent>().material;
            glm::vec3 col = glm::clamp(glm::max(m.diffuse, m.emissive * std::min(1.0f, m.emissiveStrength)), 0.0f, 1.0f);
            dl->AddRectFilled(ImVec2(c.x - r * 0.8f, c.y - r * 0.8f), ImVec2(c.x + r * 0.8f, c.y + r * 0.8f),
                ImGui::GetColorU32(ImVec4(col.r, col.g, col.b, 1.0f)), 2.0f);
            dl->AddRect(ImVec2(c.x - r * 0.8f, c.y - r * 0.8f), ImVec2(c.x + r * 0.8f, c.y + r * 0.8f), IM_COL32(255, 255, 255, 60), 2.0f);
        }
        else{
            dl->AddCircle(c, r * 0.7f, EditorUI::MutedColor(), 12, 1.3f);
        }
    }

    void SceneEditor::EntityNode(Entity entity){
        auto& tag = entity.GetComponent<TagComponent>().Tag;
        const bool selected = (m_SelectedEntity == entity);

        ImGui::PushID((int)(uint32_t)entity);
        const float rowHeight = ImGui::GetTextLineHeight() + 8.0f;
        ImVec2 rowMin = ImGui::GetCursorScreenPos();

        // Selection uses the accent (the theme's Header colours are neutral).
        ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(EditorUI::Accent(0.24f)));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::ColorConvertU32ToFloat4(selected ? EditorUI::Accent(0.30f) : IM_COL32(255, 255, 255, 14)));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::ColorConvertU32ToFloat4(EditorUI::Accent(0.36f)));
        if (ImGui::Selectable("##entity", selected, ImGuiSelectableFlags_SpanAvailWidth, ImVec2(0.0f, rowHeight)))
            m_SelectedEntity = entity;
        ImGui::PopStyleColor(3);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 rowMax = ImGui::GetItemRectMax();
        if (selected)
            dl->AddRectFilled(ImVec2(rowMin.x, rowMin.y), ImVec2(rowMin.x + 2.0f, rowMax.y), EditorUI::Accent());
        float midY = rowMin.y + rowHeight * 0.5f;
        DrawEntityIcon(dl, entity, ImVec2(rowMin.x + 13.0f, midY));
        dl->AddText(ImVec2(rowMin.x + 26.0f, midY - ImGui::GetTextLineHeight() * 0.5f), EditorUI::TextColor(), tag.c_str());

        // Right-aligned hints for behaviour that isn't visible in the viewport.
        {
            const char* badges[3] = {};
            int count = 0;
            if (entity.HasComponent<AgentComponent>()){
                static const char* policies[] = { "Keyboard", "Random", "Python" };
                badges[count++] = policies[std::clamp((int)entity.GetComponent<AgentComponent>().policy, 0, 2)];
            }
            else if (entity.HasComponent<RewardZoneComponent>()){
                badges[count++] = entity.GetComponent<RewardZoneComponent>().reward >= 0.0f ? "+Reward" : "-Reward";
            }
            if (entity.HasComponent<LuaScriptComponent>()) { badges[count++] = "Lua"; }
            if (count < 2 && entity.HasComponent<RigidBodyComponent>()) { badges[count++] = "Physics"; }
            ImFont* small = ImGuiLayer::GetFont(ImGuiLayer::FontType::Small);
            float x = rowMax.x - 6.0f;
            for (int i = 0; i < count; i++){
                ImVec2 ts = small->CalcTextSizeA(small->FontSize, FLT_MAX, 0.0f, badges[i]);
                x -= ts.x + 10.0f;
                ImVec2 bMin(x, midY - ts.y * 0.5f - 1.0f), bMax(x + ts.x + 10.0f, midY + ts.y * 0.5f + 1.0f);
                dl->AddRectFilled(bMin, bMax, IM_COL32(255, 255, 255, 16), 3.0f);
                dl->AddText(small, small->FontSize, ImVec2(x + 5.0f, bMin.y + 1.0f), EditorUI::MutedColor(), badges[i]);
                x -= 4.0f;
            }
        }

		bool entityDeleted = false;
		if (ImGui::BeginPopupContextItem()){
            m_SelectedEntity = entity;
			if (ImGui::MenuItem("Delete Entity", "Del"))
				entityDeleted = true;
			ImGui::EndPopup();	
		}
        ImGui::PopID();

		if (selected && !entityDeleted)
		{
			// Ignore Delete while typing (e.g. in the Tag field), or it deletes the entity.
			if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput)
			{
				entityDeleted = true;
			}
		}

		if (entityDeleted){
			m_ActiveScene->DestroyEntity(entity);
			if (m_SelectedEntity == entity)
				m_SelectedEntity = {};
		}
    }


    template<typename T, typename UIFunction>
	static void DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction){
		const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
		if (entity.HasComponent<T>()){
			auto& component = entity.GetComponent<T>();
			ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

			// Scope every ID below (tree node, settings button, popup) to this component type.
			// TreeNodeEx only pushes its ID while open, so without this the settings buttons and
			// "ComponentSettings" popups of collapsed components collided, and "Remove"
			// could remove a different component.
			ImGui::PushID((void*)typeid(T).hash_code());

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 6, 5 });
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
			float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
			ImGui::PushFont(ImGuiLayer::GetFont(ImGuiLayer::FontType::Bold));
			bool open = ImGui::TreeNodeEx("##component", treeNodeFlags, "%s", name.c_str());
			ImGui::PopFont();
			ImGui::PopStyleVar(2);

			ImGui::SameLine(contentRegionAvailable.x - lineHeight + ImGui::GetStyle().WindowPadding.x * 0.5f);
			if (EditorUI::IconButton("##settings", EditorUI::Icon::More, ImVec2{ lineHeight, lineHeight }, false, "Component settings")){
				ImGui::OpenPopup("ComponentSettings");
			}

			bool removeComponent = false;
			if (ImGui::BeginPopup("ComponentSettings")){
				// Every entity is assumed to have a Transform (rendering, gizmos, sculpting).
				// While playing, the runtime's bodies and scripts were built from these
				// components (and Stop restores the scene anyway), so don't allow removal.
				bool removable = !std::is_same_v<T, TransformComponent> && !entity.GetScene()->IsRunning();
				if (ImGui::MenuItem("Remove component", nullptr, false, removable))
					removeComponent = true;

				ImGui::EndPopup();
			}

			if (open){
				// Widgets fill the space left of a label column, so labels never clip.
				ImGui::Indent(4.0f);
				ImGui::PushItemWidth(-std::max(110.0f, ImGui::GetContentRegionAvail().x * 0.38f));
				uiFunction(component);
				ImGui::PopItemWidth();
				ImGui::Unindent(4.0f);
				ImGui::Spacing();
				ImGui::TreePop();
			}
			ImGui::PopID();

			if (removeComponent){
                entity.RemoveComponent<T>();
            }
				
		}
	}


    // Cast / receive shadow toggles shared by every component that draws with a Material.
    static void DrawShadowFlags(Material& material){
        ImGui::Checkbox("Cast Shadows", &material.castShadows);
        ImGui::SameLine();
        ImGui::Checkbox("Receive Shadows", &material.receiveShadows);
    }

    // A light's shadow look, under a collapsible (unindented) "Shadow" node; returns false
    // when collapsed. Directional softness is the sun's angular radius; a point light's is
    // the bulb's radius in world units.
    static bool DrawShadowSettings(ShadowSettings& shadow, bool directional){
        if (!ImGui::TreeNodeEx("Shadow", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_NoTreePushOnOpen)) { return false; }
        if (directional){
            ImGui::DragFloat("Softness", &shadow.softness, 0.05f, 0.0f, 10.0f, "%.2f deg");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Angular radius of the light in degrees (the real sun is ~0.27).\n"
                                  "Penumbrae widen with distance from the caster; 0 = hard.");
        }
        else{
            ImGui::DragFloat("Softness", &shadow.softness, 0.005f, 0.0f, 2.0f, "%.3f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Radius of the light source in world units.\n"
                                  "Penumbrae widen with distance from the caster; 0 = hard.");
        }
        ImGui::SliderFloat("Strength", &shadow.strength, 0.0f, 1.0f);
        ImGui::DragFloat("Bias", &shadow.bias, 0.05f, 0.0f, 10.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Depth bias in shadow texels. Raise it if surfaces show\n"
                              "striped self-shadowing (acne); lower it if shadows detach\n"
                              "from their casters.");
        ImGui::DragFloat("Normal Bias", &shadow.normalBias, 0.05f, 0.0f, 10.0f, "%.2f");
        return true;
    }

    static void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f
            ,std::string label1="X", std::string label2="Y", std::string label3="Z"){
        ImGuiIO& io = ImGui::GetIO();
        auto boldFont = io.Fonts->Fonts[0];

        ImGui::PushID(label.c_str());

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::Text(label.c_str(), nullptr);
        ImGui::NextColumn();
        ImGui::PushMultiItemsWidths(3, ImGui::GetContentRegionAvail().x);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 4, 4 });

        float lineHeight = ImGui::GetIO().FontDefault->FontSize + ImGui::GetStyle().FramePadding.y * 2.0;
        ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.78f, 0.25f, 0.27f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.88f, 0.35f, 0.36f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.78f, 0.25f, 0.27f, 1.0f });
        ImGui::PushFont(boldFont);
        if (ImGui::Button(label1.c_str(), buttonSize))
            values.x = resetValue;
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat((std::string("##") + label1).c_str(), &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.33f, 0.62f, 0.30f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.42f, 0.72f, 0.38f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.33f, 0.62f, 0.30f, 1.0f });
        ImGui::PushFont(boldFont);
        if (ImGui::Button(label2.c_str(), buttonSize))
            values.y = resetValue;
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat((std::string("##") + label2).c_str(), &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.25f, 0.45f, 0.82f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.35f, 0.55f, 0.90f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.25f, 0.45f, 0.82f, 1.0f });
        ImGui::PushFont(boldFont);
        if (ImGui::Button(label3.c_str(), buttonSize))
            values.z = resetValue;
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat((std::string("##") + label3).c_str(), &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();

        ImGui::PopStyleVar();

        ImGui::Columns(1);

        ImGui::PopID();
    }



    // "Add Component" entry: disabled when the entity already has one.
    template<typename T>
    static void AddComponentItem(Entity entity, const char* label){
        if (ImGui::MenuItem(label, nullptr, false, !entity.HasComponent<T>()))
            entity.AddComponent<T>();
    }

    static const char* s_JointTypeNames[] = { "Point (ball-socket)", "Distance / Spring", "Hinge", "Fixed (weld)",
                                              "Slider", "Cone", "Swing-Twist (ragdoll)" };

    // Euler angles stored in radians, edited in degrees.
    static void DragAngles(const char* label, glm::vec3& radians){
        glm::vec3 degrees = glm::degrees(radians);
        if (ImGui::DragFloat3(label, glm::value_ptr(degrees), 0.5f, -360.0f, 360.0f, "%.1f deg"))
            radians = glm::radians(degrees);
    }

    // Local-space bounds of what the entity draws (model, primitive or cube), for the
    // collider "Fit to Shape" buttons. Primitives/cubes are built in the unit cube;
    // the plane is flat, so give it a thin slab.
    static bool GetEntityLocalBounds(Entity entity, glm::vec3& lo, glm::vec3& hi){
        if (entity.HasComponent<MeshComponent>()){
            auto& mesh = entity.GetComponent<MeshComponent>();
            return mesh.model && mesh.model->GetLocalBounds(lo, hi);
        }
        if (entity.HasComponent<PrimitiveComponent>() || entity.HasComponent<CubeComponent>()){
            lo = glm::vec3(-0.5f); hi = glm::vec3(0.5f);
            if (entity.HasComponent<PrimitiveComponent>()
                && entity.GetComponent<PrimitiveComponent>().type == PrimitiveComponent::Type::Plane){
                lo.y = -0.01f; hi.y = 0.01f;
            }
            return true;
        }
        return false;
    }

    void SceneEditor::DrawComponents(Entity entity){
		{
			const float addWidth = ImGui::CalcTextSize("Add Component").x + 30.0f;
			if (entity.HasComponent<TagComponent>()){
				auto& tag = entity.GetComponent<TagComponent>().Tag;
				char buffer[256];
				memset(buffer, 0, sizeof(buffer));
				std::strncpy(buffer, tag.c_str(), sizeof(buffer) - 1);
				ImGui::SetNextItemWidth(-(addWidth + ImGui::GetStyle().ItemSpacing.x));
				if (ImGui::InputTextWithHint("##Tag", "Name", buffer, sizeof(buffer))){
					tag = std::string(buffer);
				}
				ImGui::SameLine();
			}
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(EditorUI::Accent(0.80f)));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(EditorUI::Accent(0.95f)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(EditorUI::Accent(0.70f)));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.08f, 0.09f, 1.0f));
			if (ImGui::Button("Add Component", ImVec2(addWidth, 0.0f)))
				ImGui::OpenPopup("AddComponent");
			ImGui::PopStyleColor(4);
			ImGui::Spacing();
		}

		if (ImGui::BeginPopup("AddComponent")){
            ImGui::TextDisabled("Rendering");
            AddComponentItem<CubeComponent>(entity, "Cube");
            AddComponentItem<PrimitiveComponent>(entity, "Shape");
            AddComponentItem<WireframeComponent>(entity, "Wireframe");
            AddComponentItem<AnimationComponent>(entity, "Animation");
            AddComponentItem<BoneAttachmentComponent>(entity, "Bone Attachment");
            ImGui::Separator();
            ImGui::TextDisabled("Lighting & Camera");
            // The shader only lights with the first MAX_NUM_POINT_LIGHTS; more are ignored.
            bool pointLightsFull = m_ActiveScene->GetNumberOfPointLights() >= Scene::MAX_NUM_POINT_LIGHTS;
            if (ImGui::MenuItem("Point Light", nullptr, false, !pointLightsFull && !entity.HasComponent<PointLightComponent>()))
                entity.AddComponent<PointLightComponent>();
            if (pointLightsFull && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("The renderer supports up to %d point lights.", Scene::MAX_NUM_POINT_LIGHTS);
            AddComponentItem<CameraComponent>(entity, "Camera");
            AddComponentItem<CameraFollowComponent>(entity, "Camera Follow");
            ImGui::Separator();
            ImGui::TextDisabled("Physics");
            AddComponentItem<RigidBodyComponent>(entity, "Rigid Body");
            AddComponentItem<BoxColliderComponent>(entity, "Box Collider");
            AddComponentItem<SphereColliderComponent>(entity, "Sphere Collider");
            AddComponentItem<CapsuleColliderComponent>(entity, "Capsule Collider");
            AddComponentItem<CylinderColliderComponent>(entity, "Cylinder Collider");
            AddComponentItem<MeshColliderComponent>(entity, "Mesh Collider");
            if (ImGui::MenuItem("Joint", nullptr, false, !entity.HasComponent<JointComponent>()))
                entity.AddComponent<JointComponent>().Add().pivot = entity.GetComponent<TransformComponent>().Translation;
            if (ImGui::MenuItem("Compound Collider", nullptr, false, !entity.HasComponent<CompoundColliderComponent>()))
                entity.AddComponent<CompoundColliderComponent>().Add(CompoundColliderComponent::Shape::Type::Box);
            AddComponentItem<CharacterControllerComponent>(entity, "Character Controller");
            AddComponentItem<ForceFieldComponent>(entity, "Force Field");
            AddComponentItem<VehicleComponent>(entity, "Vehicle");
            AddComponentItem<RagdollComponent>(entity, "Ragdoll");
            ImGui::Separator();
            ImGui::TextDisabled("AI / RL Environment");
            AddComponentItem<AgentComponent>(entity, "Agent");
            AddComponentItem<RaySensorComponent>(entity, "Ray Sensor");
            AddComponentItem<RewardZoneComponent>(entity, "Reward Zone");
            AddComponentItem<RandomizerComponent>(entity, "Spawn Randomizer");
            ImGui::Separator();
            ImGui::TextDisabled("Scripting");
            AddComponentItem<LuaScriptComponent>(entity, "Lua Script");

			ImGui::EndPopup();
		}

		DrawComponent<TransformComponent>("Transform", entity, [](auto& component){
			DrawVec3Control("Translation", component.Translation);
			glm::vec3 rotation = glm::degrees(component.Rotation);
			DrawVec3Control("Rotation", rotation);
			component.Rotation = glm::radians(rotation);
			DrawVec3Control("Scale", component.Scale, 1.0f);
		});
        DrawComponent<CubeComponent>("Cube", entity, [](CubeComponent& component){
            float ambient[] = {component.material.ambient.x, component.material.ambient.y, component.material.ambient.z};
            if (ImGui::ColorEdit3("Ambient", ambient)){
                component.material.ambient.x = ambient[0];
                component.material.ambient.y = ambient[1];
                component.material.ambient.z = ambient[2];
            }
            float diffuse[] = {component.material.diffuse.x, component.material.diffuse.y, component.material.diffuse.z};
            if (ImGui::ColorEdit3("Diffuse", diffuse)){
                component.material.diffuse.x = diffuse[0];
                component.material.diffuse.y = diffuse[1];
                component.material.diffuse.z = diffuse[2];
            }
            float specular[] = {component.material.specular.x, component.material.specular.y, component.material.specular.z};
            if (ImGui::ColorEdit3("Specular", specular)){
                component.material.specular.x = specular[0];
                component.material.specular.y = specular[1];
                component.material.specular.z = specular[2];
            }
            ImGui::DragFloat("Shininess", &component.material.shininess, 1.0f, 0.0f, 128.0f);
            ImGui::ColorEdit3("Emissive", glm::value_ptr(component.material.emissive));
            ImGui::DragFloat("Glow Strength", &component.material.emissiveStrength, 0.05f, 0.0f, 10.0f);
            ImGui::SliderFloat("Reflectivity", &component.material.reflectivity, 0.0f, 1.0f);
            DrawShadowFlags(component.material);
		});

        DrawComponent<PrimitiveComponent>("Shape", entity, [](PrimitiveComponent& component){
            const char* kinds[] = { "Cube", "Sphere", "Cylinder", "Cone", "Plane", "Capsule" };
            int current = (int)component.type;
            if (ImGui::Combo("Shape", &current, kinds, IM_ARRAYSIZE(kinds)))
                component.type = (PrimitiveComponent::Type)current;
            ImGui::ColorEdit3("Ambient", glm::value_ptr(component.material.ambient));
            ImGui::ColorEdit3("Diffuse", glm::value_ptr(component.material.diffuse));
            ImGui::ColorEdit3("Specular", glm::value_ptr(component.material.specular));
            ImGui::DragFloat("Shininess", &component.material.shininess, 1.0f, 0.0f, 128.0f);
            ImGui::ColorEdit3("Emissive", glm::value_ptr(component.material.emissive));
            ImGui::DragFloat("Glow Strength", &component.material.emissiveStrength, 0.05f, 0.0f, 10.0f);
            ImGui::SliderFloat("Reflectivity", &component.material.reflectivity, 0.0f, 1.0f);
            DrawShadowFlags(component.material);
		});

        DrawComponent<AnimationComponent>("Animation", entity, [entity](AnimationComponent& component) mutable {
            Ref<Model> model;
            if (entity.HasComponent<MeshComponent>()){
                auto& mc = entity.GetComponent<MeshComponent>();
                if (mc.model && mc.model->IsReady()) { model = mc.model; }
            }
            int clipCount = model ? (int)model->GetAnimationCount() : 0;

            // Clip selector by name when the model is loaded.
            if (clipCount > 0){
                if (component.clip >= clipCount) { component.clip = clipCount - 1; }
                std::string preview = model->GetAnimationName((size_t)component.clip);
                if (preview.empty()) { preview = "clip " + std::to_string(component.clip); }
                if (ImGui::BeginCombo("Clip", preview.c_str())){
                    for (int i = 0; i < clipCount; i++){
                        std::string n = model->GetAnimationName((size_t)i);
                        if (n.empty()) { n = "clip " + std::to_string(i); }
                        bool sel = (component.clip == i);
                        if (ImGui::Selectable(n.c_str(), sel)) { component.clip = i; }
                        if (sel) { ImGui::SetItemDefaultFocus(); }
                    }
                    ImGui::EndCombo();
                }
            }
            else{
                ImGui::TextDisabled("Add a rigged Mesh; animations load with it.");
                ImGui::DragInt("Clip", &component.clip, 0.1f, 0, 32);
            }

            ImGui::Checkbox("Playing", &component.playing);
            ImGui::DragFloat("Speed", &component.speed, 0.05f, 0.0f, 5.0f);
            const char* loopModes[] = { "Loop", "Once", "PingPong" };
            ImGui::Combo("Loop Mode", &component.loopMode, loopModes, IM_ARRAYSIZE(loopModes));
            ImGui::DragFloat("Crossfade (s)", &component.crossfade, 0.01f, 0.0f, 2.0f);
            ImGui::Checkbox("In Place", &component.inPlace);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Strip the clips' forward travel (root motion) so scripts move the\n"
                                  "character. Applied when the clips load; reload the scene after changing.");

            // Extra clip files merged onto this skeleton (e.g. more Mixamo clips).
            ImGui::Separator(); ImGui::TextDisabled("Extra Clip Files");
            for (size_t i = 0; i < component.extraClips.size(); i++){
                ImGui::PushID((int)i);
                char buf[256];
                std::strncpy(buf, component.extraClips[i].c_str(), sizeof(buf)); buf[sizeof(buf) - 1] = 0;
                if (ImGui::InputText("##path", buf, sizeof(buf))){ component.extraClips[i] = buf; component.extraClipsLoaded = false; }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")){ component.extraClips.erase(component.extraClips.begin() + i); component.extraClipsLoaded = false; ImGui::PopID(); break; }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("Add Clip File")){ component.extraClips.push_back("assets/models/"); component.extraClipsLoaded = false; }

            // Animation events (fire OnAnimationEvent(name) in the entity's Lua script).
            ImGui::Separator(); ImGui::TextDisabled("Events");
            for (size_t i = 0; i < component.events.size(); i++){
                ImGui::PushID(1000 + (int)i);
                auto& ev = component.events[i];
                ImGui::SetNextItemWidth(50); ImGui::DragInt("clip", &ev.clip, 0.1f, 0, 64); ImGui::SameLine();
                ImGui::SetNextItemWidth(70); ImGui::DragFloat("t", &ev.time, 0.01f, 0.0f, 9999.0f); ImGui::SameLine();
                char nbuf[64]; std::strncpy(nbuf, ev.name.c_str(), sizeof(nbuf)); nbuf[sizeof(nbuf) - 1] = 0;
                ImGui::SetNextItemWidth(120);
                if (ImGui::InputText("##name", nbuf, sizeof(nbuf))) { ev.name = nbuf; }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")){ component.events.erase(component.events.begin() + i); ImGui::PopID(); break; }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("Add Event")){ AnimEvent e; e.clip = component.clip; e.time = 0.0f; e.name = "event"; component.events.push_back(e); }
		});

        DrawComponent<MeshComponent>("Mesh", entity, [](MeshComponent& component){
            ImGui::ColorEdit3("Emissive", glm::value_ptr(component.material.emissive));
            ImGui::DragFloat("Glow Strength", &component.material.emissiveStrength, 0.05f, 0.0f, 10.0f);
            ImGui::SliderFloat("Reflectivity", &component.material.reflectivity, 0.0f, 1.0f);
            float diffuse[] = {component.material.diffuse.x, component.material.diffuse.y, component.material.diffuse.z};
            if (ImGui::ColorEdit3("Tint (no texture)", diffuse)){
                component.material.diffuse.x = diffuse[0];
                component.material.diffuse.y = diffuse[1];
                component.material.diffuse.z = diffuse[2];
            }
            ImGui::DragFloat("Shininess", &component.material.shininess, 1.0f, 0.0f, 128.0f);
            DrawShadowFlags(component.material);
        });

        DrawComponent<DirLightComponent>("Directional Light", entity, [](auto& component){
			ImGui::Checkbox("Active", &component.isActive);
			ImGui::Checkbox("Casts Shadow", &component.castsShadow);
			if (component.castsShadow && DrawShadowSettings(component.shadow, true)){
				ImGui::DragFloat("Distance", &component.shadowDistance, 0.5f, 1.0f, 500.0f, "%.1f");
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("How far from the camera shadows reach (the whole scene\n"
					                  "is covered when it is smaller). Shorter = sharper.");
			}

			float ambient[] = {component.ambient.x, component.ambient.y, component.ambient.z};
            if (ImGui::ColorEdit3("Ambient", ambient)){
                component.ambient.x = ambient[0];
                component.ambient.y = ambient[1];
                component.ambient.z = ambient[2];
            }
            float diffuse[] = {component.diffuse.x, component.diffuse.y, component.diffuse.z};
            if (ImGui::ColorEdit3("Diffuse", diffuse)){
                component.diffuse.x = diffuse[0];
                component.diffuse.y = diffuse[1];
                component.diffuse.z = diffuse[2];
            }
            float specular[] = {component.specular.x, component.specular.y, component.specular.z};
            if (ImGui::ColorEdit3("Specular", specular)){
                component.specular.x = specular[0];
                component.specular.y = specular[1];
                component.specular.z = specular[2];
            }
		});

		DrawComponent<PointLightComponent>("Point Light", entity, [](auto& component){
			ImGui::Checkbox("Active", &component.isActive);
			ImGui::Checkbox("Casts Shadow", &component.castsShadow);
			if (component.castsShadow)
				DrawShadowSettings(component.shadow, false);
			float ambient[] = {component.ambient.x, component.ambient.y, component.ambient.z};
            if (ImGui::ColorEdit3("Ambient", ambient)){
                component.ambient.x = ambient[0];
                component.ambient.y = ambient[1];
                component.ambient.z = ambient[2];
            }
            float diffuse[] = {component.diffuse.x, component.diffuse.y, component.diffuse.z};
            if (ImGui::ColorEdit3("Diffuse", diffuse)){
                component.diffuse.x = diffuse[0];
                component.diffuse.y = diffuse[1];
                component.diffuse.z = diffuse[2];
            }
            float specular[] = {component.specular.x, component.specular.y, component.specular.z};
            if (ImGui::ColorEdit3("Specular", specular)){
                component.specular.x = specular[0];
                component.specular.y = specular[1];
                component.specular.z = specular[2];
            }
			float constant = component.constant, linear = component.linear, quadratic = component.quadratic;
			if (ImGui::DragFloat("Constnat", &(constant)))
				component.SetConstant(constant);
			if (ImGui::DragFloat("Linear", &(linear)))
				component.SetLinear(linear);
			if (ImGui::DragFloat("Quadratic", &(quadratic)))
				component.SetQuadratic(quadratic);
		});

		DrawComponent<CameraComponent>("Camera", entity, [](auto& component){
			auto& camera = component.camera;

			ImGui::Checkbox("Primary", &component.primary);

			const char* projectionTypeStrings[] = { "Perspective", "Orthographic" };
			const char* currentProjectionTypeString = projectionTypeStrings[(int)camera.GetProjectionType()];
			if (ImGui::BeginCombo("Projection", currentProjectionTypeString)){
				for (int i = 0; i < 2; i++){
					bool isSelected = currentProjectionTypeString == projectionTypeStrings[i];
					if (ImGui::Selectable(projectionTypeStrings[i], isSelected)){
						currentProjectionTypeString = projectionTypeStrings[i];
						camera.SetProjectionType((SceneCamera::ProjectionType)i);
					}
					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}

				ImGui::EndCombo();
			}

			if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective){
				float perspectiveVerticalFov = glm::degrees(camera.GetPerspectiveVerticalFOV());
				if (ImGui::DragFloat("Vertical FOV", &perspectiveVerticalFov))
					camera.SetPerspectiveVerticalFOV(glm::radians(perspectiveVerticalFov));

				float perspectiveNear = camera.GetPerspectiveNearClip();
				if (ImGui::DragFloat("Near", &perspectiveNear))
					camera.SetPerspectiveNearClip(perspectiveNear);

				float perspectiveFar = camera.GetPerspectiveFarClip();
				if (ImGui::DragFloat("Far", &perspectiveFar))
					camera.SetPerspectiveFarClip(perspectiveFar);
			}

			if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic){
				float orthoSize = camera.GetOrthographicSize();
				if (ImGui::DragFloat("Size", &orthoSize))
					camera.SetOrthographicSize(orthoSize);

				float orthoNear = camera.GetOrthographicNearClip();
				if (ImGui::DragFloat("Near", &orthoNear))
					camera.SetOrthographicNearClip(orthoNear);

				float orthoFar = camera.GetOrthographicFarClip();
				if (ImGui::DragFloat("Far", &orthoFar))
					camera.SetOrthographicFarClip(orthoFar);
                // float aspect = camera.GetOrthographicAcpectRatio();
				// if (ImGui::DragFloat("Aspect Ratio", &aspect, 0.1))
				// 	camera.SetOrthographicAcpectRatio(aspect);

			}
		});

		DrawComponent<CameraFollowComponent>("Camera Follow", entity, [](CameraFollowComponent& component){
			char buf[128];
			std::strncpy(buf, component.target.c_str(), sizeof(buf)); buf[sizeof(buf) - 1] = 0;
			if (ImGui::InputText("Target (Tag)", buf, sizeof(buf))) { component.target = buf; }
			ImGui::DragFloat("Distance", &component.distance, 0.1f, 0.5f, 50.0f);
			ImGui::DragFloat("Height", &component.height, 0.1f, -10.0f, 30.0f);
			ImGui::DragFloat("Look Height", &component.lookHeight, 0.1f, -10.0f, 30.0f);
			ImGui::Checkbox("Follow Yaw", &component.followYaw);
			ImGui::Checkbox("Model Faces +Z", &component.modelForwardZ);
			ImGui::DragFloat("Yaw Smoothing", &component.yawSmoothing, 0.1f, 0.0f, 30.0f);
			if (ImGui::IsItemHovered()) { ImGui::SetTooltip("How fast the camera swings behind a turning target. 0 = no smoothing."); }
			ImGui::TextDisabled("Set the camera Primary to view through it.");
		});

		DrawComponent<RigidBodyComponent>("Rigid Body", entity, [this, entity](RigidBodyComponent& component) mutable {
			const char* typeStrings[] = { "Static", "Dynamic", "Kinematic" };
			int type = (int)component.type;
			if (ImGui::Combo("Body Type", &type, typeStrings, 3)) { component.type = (RigidBodyComponent::Type)type; }
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Static: never moves. Dynamic: simulated.\nKinematic: follows its Transform (scripts/animation) and pushes dynamic bodies.");

			// Material presets: friction / restitution / density in one click.
			struct Preset { const char* name; float friction, restitution, density; };
			static const Preset presets[] = {
				{ "Default", 0.5f, 0.0f, 1000.0f }, { "Wood", 0.6f, 0.3f, 700.0f }, { "Metal", 0.4f, 0.15f, 7800.0f },
				{ "Stone", 0.8f, 0.1f, 2500.0f }, { "Ice", 0.02f, 0.05f, 917.0f }, { "Rubber", 1.2f, 0.8f, 1100.0f },
				{ "Bouncy Ball", 0.4f, 0.95f, 300.0f }, { "Foam", 0.7f, 0.1f, 50.0f },
			};
			if (ImGui::BeginCombo("Material Preset", "Apply...")){
				for (const auto& preset : presets){
					if (ImGui::Selectable(preset.name)){
						component.friction = preset.friction;
						component.restitution = preset.restitution;
						component.density = preset.density;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SliderFloat("Friction", &component.friction, 0.0f, 1.5f);
			ImGui::SliderFloat("Restitution", &component.restitution, 0.0f, 1.0f);

			if (component.type == RigidBodyComponent::Type::Dynamic){
				ImGui::DragFloat("Density", &component.density, 10.0f, 1.0f, 30000.0f, "%.0f kg/m3");
				ImGui::DragFloat("Mass Override", &component.mass, 0.5f, 0.0f, 100000.0f, component.mass > 0.0f ? "%.1f kg" : "off (from density)");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("> 0 sets the mass directly instead of density x volume.");
				ImGui::DragFloat3("Center of Mass", glm::value_ptr(component.centerOfMassOffset), 0.01f);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Local offset. A low center of mass makes objects hard to tip (weebles, cars).");
				ImGui::SliderFloat("Linear Damping", &component.linearDamping, 0.0f, 2.0f);
				ImGui::SliderFloat("Angular Damping", &component.angularDamping, 0.0f, 2.0f);
				ImGui::SliderFloat("Gravity Factor", &component.gravityFactor, -2.0f, 4.0f);
				ImGui::DragFloat3("Initial Velocity", glm::value_ptr(component.initialVelocity), 0.1f);
				ImGui::DragFloat3("Initial Spin", glm::value_ptr(component.initialAngularVelocity), 0.1f);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Initial angular velocity (rad/s) around world X/Y/Z.");

				ImGui::TextUnformatted("Lock Position");
				ImGui::SameLine(120); ImGui::Checkbox("X##lp", &component.lockPosition[0]);
				ImGui::SameLine(); ImGui::Checkbox("Y##lp", &component.lockPosition[1]);
				ImGui::SameLine(); ImGui::Checkbox("Z##lp", &component.lockPosition[2]);
				ImGui::TextUnformatted("Lock Rotation");
				ImGui::SameLine(120); ImGui::Checkbox("X##lr", &component.lockRotation[0]);
				ImGui::SameLine(); ImGui::Checkbox("Y##lr", &component.lockRotation[1]);
				ImGui::SameLine(); ImGui::Checkbox("Z##lr", &component.lockRotation[2]);
				if (ImGui::Button("2D (XY plane)")){
					component.lockPosition[0] = component.lockPosition[1] = false; component.lockPosition[2] = true;
					component.lockRotation[0] = component.lockRotation[1] = true; component.lockRotation[2] = false;
				}
				ImGui::SameLine();
				if (ImGui::Button("Upright")){
					component.lockRotation[0] = component.lockRotation[2] = true;
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lock X/Z rotation so it never tips over (characters made of rigid bodies).");
				ImGui::Checkbox("Upright (lock tilt)", &component.lockTilt);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only rotates about Y, so walking agents and characters never tip over.");

				ImGui::Checkbox("Continuous Collision", &component.continuousCollision);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("For fast movers (bullets) so they don't tunnel through walls.");
				ImGui::Checkbox("Allow Sleeping", &component.allowSleeping);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Resting bodies stop simulating (cheaper) until something touches them.");
				if (component.allowSleeping){
					ImGui::SameLine();
					ImGui::Checkbox("Start Asleep", &component.startAsleep);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("Frozen in place until something touches it\n(standing ragdoll dummies, pre-stacked walls).");
				}
			}
			ImGui::Checkbox("Sensor (Trigger)", &component.isSensor);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Detects overlaps (Lua OnTriggerEnter/Exit) without colliding.");

			const auto& physics = m_ActiveScene->PhysicsSettings();
			if (ImGui::BeginCombo("Layer", physics.layerNames[component.layer].c_str())){
				for (int i = 0; i < ScenePhysicsSettings::MaxLayers; i++){
					ImGui::PushID(i);
					if (ImGui::Selectable(physics.layerNames[i].c_str(), component.layer == i)) { component.layer = i; }
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}

			// Live state while playing.
			PhysicsWorld* world = m_ActiveScene->GetPhysicsWorld();
			if (world && component.runtimeBodyID != PhysicsWorld::InvalidBody){
				ImGui::Separator();
				glm::vec3 v = world->GetLinearVelocity(component.runtimeBodyID);
				ImGui::Text("Mass %.2f kg | speed %.2f m/s | %s", world->GetMass(component.runtimeBodyID),
					glm::length(v), world->IsAwake(component.runtimeBodyID) ? "awake" : "sleeping");
			}
			else if (!world){
				ImGui::TextDisabled("Applied on Play. Shape = all collider components.");
			}
			if (!entity.HasComponent<BoxColliderComponent>() && !entity.HasComponent<SphereColliderComponent>()
				&& !entity.HasComponent<CapsuleColliderComponent>() && !entity.HasComponent<CylinderColliderComponent>()
				&& !entity.HasComponent<MeshColliderComponent>() && !entity.HasComponent<CompoundColliderComponent>())
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "No collider: add a Box/Sphere/Capsule/Cylinder/Mesh Collider.");
		});

		DrawComponent<BoxColliderComponent>("Box Collider", entity, [entity](BoxColliderComponent& component) mutable {
			ImGui::DragFloat3("Half Extents", glm::value_ptr(component.halfExtents), 0.02f, 0.01f, 1000.0f, "%.2f");
			ImGui::DragFloat3("Offset", glm::value_ptr(component.offset), 0.02f, 0.0f, 0.0f, "%.2f");
			DragAngles("Rotation", component.rotation);
			glm::vec3 lo, hi;
			if (ImGui::Button("Fit to Shape") && GetEntityLocalBounds(entity, lo, hi)){
				component.halfExtents = glm::max((hi - lo) * 0.5f, glm::vec3(0.01f));
				component.offset = (hi + lo) * 0.5f;
				component.rotation = glm::vec3(0.0f);
			}
			ImGui::TextDisabled("Local units, scaled by Transform on Play");
		});

		DrawComponent<SphereColliderComponent>("Sphere Collider", entity, [entity](SphereColliderComponent& component) mutable {
			ImGui::DragFloat("Radius", &component.radius, 0.02f, 0.01f, 100.0f, "%.2f");
			ImGui::DragFloat3("Offset", glm::value_ptr(component.offset), 0.02f, 0.0f, 0.0f, "%.2f");
			glm::vec3 lo, hi;
			if (ImGui::Button("Fit to Shape") && GetEntityLocalBounds(entity, lo, hi)){
				glm::vec3 half = (hi - lo) * 0.5f;
				component.radius = std::max(0.01f, std::max(half.x, std::max(half.y, half.z)));
				component.offset = (hi + lo) * 0.5f;
			}
			ImGui::TextDisabled("Scaled by the largest Transform scale axis on Play");
		});

		DrawComponent<CapsuleColliderComponent>("Capsule Collider", entity, [entity](CapsuleColliderComponent& component) mutable {
			ImGui::DragFloat("Radius", &component.radius, 0.02f, 0.01f, 100.0f, "%.2f");
			ImGui::DragFloat("Half Height", &component.halfHeight, 0.02f, 0.0f, 100.0f, "%.2f");
			ImGui::DragFloat3("Offset", glm::value_ptr(component.offset), 0.02f, 0.0f, 0.0f, "%.2f");
			DragAngles("Rotation", component.rotation);
			glm::vec3 lo, hi;
			if (ImGui::Button("Fit to Shape") && GetEntityLocalBounds(entity, lo, hi)){
				glm::vec3 half = (hi - lo) * 0.5f;
				component.radius = std::max(0.01f, std::max(half.x, half.z));
				component.halfHeight = std::max(0.0f, half.y - component.radius);
				component.offset = (hi + lo) * 0.5f;
				component.rotation = glm::vec3(0.0f);
			}
			ImGui::TextDisabled("Along local Y. Total height = 2 x (half height + radius).");
		});

		DrawComponent<CylinderColliderComponent>("Cylinder Collider", entity, [entity](CylinderColliderComponent& component) mutable {
			ImGui::DragFloat("Radius", &component.radius, 0.02f, 0.01f, 100.0f, "%.2f");
			ImGui::DragFloat("Half Height", &component.halfHeight, 0.02f, 0.01f, 100.0f, "%.2f");
			ImGui::DragFloat3("Offset", glm::value_ptr(component.offset), 0.02f, 0.0f, 0.0f, "%.2f");
			DragAngles("Rotation", component.rotation);
			glm::vec3 lo, hi;
			if (ImGui::Button("Fit to Shape") && GetEntityLocalBounds(entity, lo, hi)){
				glm::vec3 half = (hi - lo) * 0.5f;
				component.radius = std::max(0.01f, std::max(half.x, half.z));
				component.halfHeight = std::max(0.01f, half.y);
				component.offset = (hi + lo) * 0.5f;
				component.rotation = glm::vec3(0.0f);
			}
			ImGui::TextDisabled("Along local Y.");
		});

		DrawComponent<JointComponent>("Joints", entity, [this, entity](JointComponent& joints) mutable {
			int removeIndex = -1;
			for (int index = 0; index < (int)joints.joints.size(); index++){
				JointComponent::Joint& component = joints.joints[index];
				ImGui::PushID(index);
				std::string header = "Joint " + std::to_string(index + 1) + ": " + s_JointTypeNames[(int)component.type]
					+ (component.connectedTag.empty() ? std::string(" -> world") : " -> " + component.connectedTag);
				bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50.0f);
				ImGui::BeginDisabled(m_ActiveScene->IsRunning());
				if (ImGui::SmallButton("Remove")) { removeIndex = index; }
				ImGui::EndDisabled();
				if (open){
				int type = (int)component.type;
				if (ImGui::Combo("Joint Type", &type, s_JointTypeNames, 7)) { component.type = (JointComponent::Type)type; }

				char buf[128];
				std::strncpy(buf, component.connectedTag.c_str(), sizeof(buf) - 1);
				buf[sizeof(buf) - 1] = 0;
				if (ImGui::InputText("Connected Body (Tag)", buf, sizeof(buf))) { component.connectedTag = buf; }
				if (!component.connectedTag.empty() && !m_ActiveScene->FindEntityByTag(component.connectedTag))
					ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "No entity with this tag.");
				else
					ImGui::TextDisabled("Empty tag = anchored to the world.");

				const bool distance = component.type == JointComponent::Type::Distance;
				ImGui::DragFloat3(distance ? "World Anchor" : "Pivot (world)", glm::value_ptr(component.pivot), 0.05f);
				if (ImGui::Button("Pivot = Entity Position"))
					component.pivot = entity.GetComponent<TransformComponent>().Translation;
				if (!distance){
					ImGui::SameLine();
					Entity other = component.connectedTag.empty() ? Entity{} : m_ActiveScene->FindEntityByTag(component.connectedTag);
					if (ImGui::Button("Midpoint") && other)
						component.pivot = 0.5f * (entity.GetComponent<TransformComponent>().Translation
							+ other.GetComponent<TransformComponent>().Translation);
				}

				if (distance){
					ImGui::TextDisabled("Anchor is used when world-anchored; else the other body's center.");
					ImGui::DragFloat("Min Distance", &component.minDistance, 0.05f, -1.0f, 100.0f, "%.2f");
					ImGui::DragFloat("Max Distance", &component.maxDistance, 0.05f, -1.0f, 100.0f, "%.2f");
					ImGui::TextDisabled("-1 keeps the starting distance. Min 0 = slack rope.");
					ImGui::DragFloat("Spring Frequency", &component.springFrequency, 0.05f, 0.0f, 30.0f, component.springFrequency > 0.0f ? "%.2f Hz" : "rigid");
					if (component.springFrequency > 0.0f)
						ImGui::SliderFloat("Spring Damping", &component.springDamping, 0.0f, 1.0f);
				}
				if (component.type == JointComponent::Type::Hinge || component.type == JointComponent::Type::Slider
					|| component.type == JointComponent::Type::Cone || component.type == JointComponent::Type::SwingTwist){
					ImGui::DragFloat3("Axis (world)", glm::value_ptr(component.axis), 0.05f);
					ImGui::SameLine();
					if (ImGui::SmallButton("X")) { component.axis = { 1, 0, 0 }; } ImGui::SameLine();
					if (ImGui::SmallButton("Y")) { component.axis = { 0, 1, 0 }; } ImGui::SameLine();
					if (ImGui::SmallButton("Z")) { component.axis = { 0, 0, 1 }; }
				}
				if (component.type == JointComponent::Type::Hinge){
					ImGui::Checkbox("Limit Angles", &component.limitAngles);
					if (component.limitAngles){
						float minDeg = glm::degrees(component.minAngle);
						float maxDeg = glm::degrees(component.maxAngle);
						if (ImGui::DragFloat("Min Angle", &minDeg, 1.0f, -180.0f, 0.0f, "%.0f deg"))
							component.minAngle = glm::radians(minDeg);
						if (ImGui::DragFloat("Max Angle", &maxDeg, 1.0f, 0.0f, 180.0f, "%.0f deg"))
							component.maxAngle = glm::radians(maxDeg);
					}
				}
				if (component.type == JointComponent::Type::Slider){
					ImGui::Checkbox("Limit Travel", &component.limitSlide);
					if (component.limitSlide){
						ImGui::DragFloat("Min", &component.minSlide, 0.05f, -100.0f, 0.0f, "%.2f m");
						ImGui::DragFloat("Max", &component.maxSlide, 0.05f, 0.0f, 100.0f, "%.2f m");
					}
				}
				if (component.type == JointComponent::Type::SwingTwist){
					ImGui::DragFloat3("Plane Axis", glm::value_ptr(component.planeAxis), 0.05f);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("Second reference axis (made perpendicular to Axis). Swing 1 bends around it.");
					float s1 = glm::degrees(component.swingAngle1), s2 = glm::degrees(component.swingAngle2);
					float t0 = glm::degrees(component.twistMin), t1 = glm::degrees(component.twistMax);
					if (ImGui::SliderFloat("Swing 1", &s1, 0.0f, 180.0f, "%.0f deg")) { component.swingAngle1 = glm::radians(s1); }
					if (ImGui::SliderFloat("Swing 2", &s2, 0.0f, 180.0f, "%.0f deg")) { component.swingAngle2 = glm::radians(s2); }
					if (ImGui::SliderFloat("Twist Min", &t0, -180.0f, 0.0f, "%.0f deg")) { component.twistMin = glm::radians(t0); }
					if (ImGui::SliderFloat("Twist Max", &t1, 0.0f, 180.0f, "%.0f deg")) { component.twistMax = glm::radians(t1); }
					ImGui::DragFloat("Friction", &component.friction, 0.1f, 0.0f, 1e5f, "%.1f N.m");
				}
				if (component.type == JointComponent::Type::Cone){
					float deg = glm::degrees(component.coneAngle);
					if (ImGui::SliderFloat("Cone Half Angle", &deg, 0.0f, 180.0f, "%.0f deg")) { component.coneAngle = glm::radians(deg); }
				}
				if (component.type == JointComponent::Type::Hinge || component.type == JointComponent::Type::Slider){
					const bool hinge = component.type == JointComponent::Type::Hinge;
					const char* motorStrings[] = { "Off", "Velocity", "Position" };
					int motor = (int)component.motor;
					if (ImGui::Combo("Motor", &motor, motorStrings, 3)) { component.motor = (JointComponent::Motor)motor; }
					if (component.motor == JointComponent::Motor::Velocity)
						ImGui::DragFloat("Target Speed", &component.motorTarget, 0.05f, 0.0f, 0.0f, hinge ? "%.2f rad/s" : "%.2f m/s");
					else if (component.motor == JointComponent::Motor::Position){
						if (hinge){
							float deg = glm::degrees(component.motorTarget);
							if (ImGui::DragFloat("Target Angle", &deg, 1.0f, -180.0f, 180.0f, "%.0f deg")) { component.motorTarget = glm::radians(deg); }
						}
						else ImGui::DragFloat("Target Position", &component.motorTarget, 0.02f, 0.0f, 0.0f, "%.2f m");
					}
					if (component.motor != JointComponent::Motor::Off)
						ImGui::DragFloat(hinge ? "Max Torque" : "Max Force", &component.motorMaxForce, 1.0f, 0.0f, 1e7f,
							component.motorMaxForce > 0.0f ? "%.0f" : "unlimited");
					ImGui::DragFloat("Friction", &component.friction, 0.5f, 0.0f, 1e6f, hinge ? "%.1f N.m" : "%.1f N");
				}
				ImGui::DragFloat("Break Force", &component.breakForce, 10.0f, 0.0f, 1e8f, component.breakForce > 0.0f ? "%.0f N" : "unbreakable");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("The joint snaps when it has to hold more than this force (Lua OnJointBreak).");
				ImGui::Checkbox("Collide Connected", &component.collideConnected);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Let the two jointed bodies collide with each other.");

				PhysicsWorld* world = m_ActiveScene->GetPhysicsWorld();
				if (world){
					if (world->IsJointAlive(component.runtimeJointID)) ImGui::Text("Value: %.3f", world->GetJointValue(component.runtimeJointID));
					else ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Broken / not connected");
				}
				else ImGui::TextDisabled("Connected on Play; needs a Rigid Body + collider.");
				}
				ImGui::PopID();
			}
			if (removeIndex >= 0) { joints.joints.erase(joints.joints.begin() + removeIndex); }
			ImGui::BeginDisabled(m_ActiveScene->IsRunning());
			if (ImGui::Button("+ Add Joint")){
				auto& j = joints.Add();
				j.pivot = entity.GetComponent<TransformComponent>().Translation;
			}
			ImGui::EndDisabled();
			if (joints.joints.empty()) { ImGui::TextDisabled("No joints yet."); }
		});

		DrawComponent<CompoundColliderComponent>("Compound Collider", entity, [this](CompoundColliderComponent& component){
			static const char* shapeNames[] = { "Box", "Sphere", "Capsule", "Cylinder" };
			int removeIndex = -1;
			for (int i = 0; i < (int)component.shapes.size(); i++){
				auto& shape = component.shapes[i];
				ImGui::PushID(i);
				int type = (int)shape.type;
				ImGui::SetNextItemWidth(110.0f);
				if (ImGui::Combo("##type", &type, shapeNames, 4)) { shape.type = (CompoundColliderComponent::Shape::Type)type; }
				ImGui::SameLine();
				ImGui::Text("Shape %d", i + 1);
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
				ImGui::BeginDisabled(m_ActiveScene->IsRunning());
				if (ImGui::SmallButton("x")) { removeIndex = i; }
				ImGui::EndDisabled();
				if (shape.type == CompoundColliderComponent::Shape::Type::Box)
					ImGui::DragFloat3("Half Extents", glm::value_ptr(shape.halfExtents), 0.02f, 0.01f, 1000.0f, "%.2f");
				else
					ImGui::DragFloat("Radius", &shape.radius, 0.02f, 0.01f, 100.0f, "%.2f");
				if (shape.type == CompoundColliderComponent::Shape::Type::Capsule || shape.type == CompoundColliderComponent::Shape::Type::Cylinder)
					ImGui::DragFloat("Half Height", &shape.halfHeight, 0.02f, 0.0f, 100.0f, "%.2f");
				ImGui::DragFloat3("Offset", glm::value_ptr(shape.offset), 0.02f, 0.0f, 0.0f, "%.2f");
				if (shape.type != CompoundColliderComponent::Shape::Type::Sphere) { DragAngles("Rotation", shape.rotation); }
				ImGui::Separator();
				ImGui::PopID();
			}
			if (removeIndex >= 0) { component.shapes.erase(component.shapes.begin() + removeIndex); }
			ImGui::BeginDisabled(m_ActiveScene->IsRunning());
			for (int t = 0; t < 4; t++){
				if (t > 0) { ImGui::SameLine(); }
				if (ImGui::Button((std::string("+ ") + shapeNames[t]).c_str())) { component.Add((CompoundColliderComponent::Shape::Type)t); }
			}
			ImGui::EndDisabled();
			ImGui::TextDisabled("Any number of shapes; combines with the other colliders.");
		});

		DrawComponent<VehicleComponent>("Vehicle", entity, [this](VehicleComponent& component){
			ImGui::Checkbox("Keyboard Control", &component.keyboardControl);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("W/S or Up/Down: throttle / reverse, A/D or Left/Right: steer, Space: handbrake.\nOff: drive it from Lua with SetVehicleInput(forward, right, brake, handbrake).");
			const char* drives[] = { "Front-wheel", "Rear-wheel", "All-wheel" };
			int drive = (int)component.drive;
			if (ImGui::Combo("Drive", &drive, drives, 3)) { component.drive = (VehicleComponent::Drive)drive; }
			if (ImGui::TreeNode("Wheels")){
				ImGui::DragFloat("Half Track", &component.halfTrack, 0.01f, 0.1f, 5.0f, "%.2f m");
				ImGui::DragFloat("Front Axle Z", &component.frontAxle, 0.01f, -10.0f, 10.0f, "%.2f m");
				ImGui::DragFloat("Rear Axle Z", &component.rearAxle, 0.01f, -10.0f, 10.0f, "%.2f m");
				ImGui::DragFloat("Attach Height", &component.wheelHeight, 0.01f, -5.0f, 5.0f, "%.2f m");
				ImGui::DragFloat("Radius", &component.wheelRadius, 0.01f, 0.05f, 3.0f, "%.2f m");
				ImGui::DragFloat("Width", &component.wheelWidth, 0.01f, 0.02f, 2.0f, "%.2f m");
				static const char* corner[4] = { "Front Left", "Front Right", "Rear Left", "Rear Right" };
				for (int i = 0; i < 4; i++){
					char buf[128];
					std::strncpy(buf, component.wheelTags[i].c_str(), sizeof(buf) - 1);
					buf[sizeof(buf) - 1] = 0;
					if (ImGui::InputText((std::string(corner[i]) + " Entity").c_str(), buf, sizeof(buf))) { component.wheelTags[i] = buf; }
				}
				ImGui::TextDisabled("Meters in the entity's frame (not scaled). Front is -Z.");
				ImGui::TreePop();
			}
			if (ImGui::TreeNode("Suspension")){
				ImGui::DragFloat("Min Length", &component.suspensionMin, 0.01f, 0.0f, 3.0f, "%.2f m");
				ImGui::DragFloat("Max Length", &component.suspensionMax, 0.01f, 0.0f, 3.0f, "%.2f m");
				ImGui::DragFloat("Stiffness", &component.suspensionFrequency, 0.05f, 0.1f, 20.0f, "%.2f Hz");
				ImGui::SliderFloat("Damping", &component.suspensionDamping, 0.0f, 2.0f);
				ImGui::Checkbox("Anti-roll Bars", &component.antiRollBars);
				ImGui::TreePop();
			}
			if (ImGui::TreeNode("Engine & Brakes")){
				ImGui::DragFloat("Engine Torque", &component.engineTorque, 5.0f, 1.0f, 20000.0f, "%.0f N.m");
				ImGui::DragFloat("Max RPM", &component.maxRPM, 50.0f, 1500.0f, 20000.0f, "%.0f");
				float steer = glm::degrees(component.maxSteerAngle);
				if (ImGui::SliderFloat("Max Steer", &steer, 0.0f, 70.0f, "%.0f deg")) { component.maxSteerAngle = glm::radians(steer); }
				ImGui::DragFloat("Brake Torque", &component.brakeTorque, 10.0f, 0.0f, 50000.0f, "%.0f N.m");
				ImGui::DragFloat("Handbrake Torque", &component.handBrakeTorque, 10.0f, 0.0f, 50000.0f, "%.0f N.m");
				ImGui::TreePop();
			}
			if (m_ActiveScene->IsRunning())
				ImGui::Text("%.0f km/h | gear %d | %.0f rpm", std::abs(component.speed) * 3.6f, component.gear, component.rpm);
			else
				ImGui::TextDisabled("Needs a Dynamic Rigid Body + collider (the chassis).");
		});

		DrawComponent<RagdollComponent>("Ragdoll", entity, [this, entity](RagdollComponent& component) mutable {
			ImGui::Checkbox("Start Active", &component.startActive);
			ImGui::DragFloat("Density", &component.density, 5.0f, 50.0f, 5000.0f, "%.0f kg/m3");
			ImGui::SliderFloat("Friction", &component.friction, 0.0f, 1.5f);
			ImGui::SliderFloat("Limb Thickness", &component.limbThickness, 0.05f, 0.5f);
			ImGui::SliderFloat("Torso Thickness", &component.torsoThickness, 0.1f, 1.0f);
			ImGui::DragFloat("Joint Friction", &component.jointFriction, 0.1f, 0.0f, 100.0f, "%.1f N.m");
			ImGui::SliderFloat("Joint Limits", &component.jointLimitScale, 0.1f, 2.0f, "x%.2f");
			ImGui::Checkbox("Entity Follows Pelvis", &component.followHips);
			if (m_ActiveScene->IsRunning()){
				if (ImGui::Button(component.active ? "Stand Up (animate)" : "Go Limp"))
					component.request = component.active ? 0 : 1;
				ImGui::SameLine();
				ImGui::Text(component.active ? "%d bodies" : "animated", (int)component.parts.size());
			}
			else{
				ImGui::TextDisabled("Needs a rigged Mesh + Animation. Lua: SetRagdoll(true).");
			}
			(void)entity;
		});

		DrawComponent<CharacterControllerComponent>("Character Controller", entity, [this](CharacterControllerComponent& component){
			ImGui::DragFloat("Radius", &component.radius, 0.01f, 0.05f, 10.0f, "%.2f");
			ImGui::DragFloat("Half Height", &component.halfHeight, 0.01f, 0.0f, 10.0f, "%.2f");
			float slope = glm::degrees(component.maxSlopeAngle);
			if (ImGui::SliderFloat("Max Slope", &slope, 0.0f, 89.0f, "%.0f deg")) { component.maxSlopeAngle = glm::radians(slope); }
			ImGui::DragFloat("Step Height", &component.stepHeight, 0.01f, 0.0f, 2.0f, "%.2f m");
			ImGui::DragFloat("Move Speed", &component.moveSpeed, 0.05f, 0.0f, 50.0f, "%.1f m/s");
			ImGui::DragFloat("Jump Speed", &component.jumpSpeed, 0.05f, 0.0f, 50.0f, "%.1f m/s");
			ImGui::SliderFloat("Gravity Factor", &component.gravityFactor, 0.0f, 4.0f);
			ImGui::SliderFloat("Air Control", &component.airControl, 0.0f, 1.0f);
			ImGui::DragFloat("Mass", &component.mass, 1.0f, 1.0f, 10000.0f, "%.0f kg");
			ImGui::DragFloat("Max Push Force", &component.maxPushForce, 5.0f, 0.0f, 100000.0f, "%.0f N");
			const auto& physics = m_ActiveScene->PhysicsSettings();
			if (ImGui::BeginCombo("Layer", physics.layerNames[component.layer].c_str())){
				for (int i = 0; i < ScenePhysicsSettings::MaxLayers; i++){
					ImGui::PushID(i);
					if (ImGui::Selectable(physics.layerNames[i].c_str(), component.layer == i)) { component.layer = i; }
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
			ImGui::Checkbox("Keyboard Control", &component.keyboardControl);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("WASD / arrows move relative to the camera, Shift runs, Space jumps.\nOff: drive it from Lua with Move(vx, vz) / Jump().");
			ImGui::Checkbox("Face Movement", &component.faceMovement);
			if (m_ActiveScene->IsRunning())
				ImGui::Text("%s | speed %.2f m/s", component.grounded ? "grounded" : "airborne",
					glm::length(glm::vec2(component.velocity.x, component.velocity.z)));
			else
				ImGui::TextDisabled("Capsule centered on the entity; scaled by Transform.");
		});

		DrawComponent<ForceFieldComponent>("Force Field", entity, [](ForceFieldComponent& component){
			const char* shapes[] = { "Box", "Sphere" };
			const char* modes[] = { "Directional (wind / lift)", "Radial (attract / repel)" };
			int shape = (int)component.shape, mode = (int)component.mode;
			if (ImGui::Combo("Shape", &shape, shapes, 2)) { component.shape = (ForceFieldComponent::Shape)shape; }
			if (ImGui::Combo("Mode", &mode, modes, 2)) { component.mode = (ForceFieldComponent::Mode)mode; }
			if (component.shape == ForceFieldComponent::Shape::Box)
				ImGui::DragFloat3("Half Extents", glm::value_ptr(component.halfExtents), 0.05f, 0.05f, 1000.0f, "%.2f");
			else
				ImGui::DragFloat("Radius", &component.halfExtents.x, 0.05f, 0.05f, 1000.0f, "%.2f");
			if (component.mode == ForceFieldComponent::Mode::Directional)
				ImGui::DragFloat3("Direction (local)", glm::value_ptr(component.direction), 0.05f);
			ImGui::DragFloat("Strength", &component.strength, 0.1f, -500.0f, 500.0f, "%.1f m/s2");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Acceleration. ~10 along +Y cancels gravity. Radial: > 0 attracts, < 0 repels.");
			ImGui::DragFloat("Drag", &component.drag, 0.01f, 0.0f, 20.0f, "%.2f");
			ImGui::TextDisabled("Affects dynamic bodies inside; needs no Rigid Body.");
		});

		DrawComponent<TerrainComponent>("Terrain", entity, [](TerrainComponent& component){
			ImGui::ColorEdit3("Color", glm::value_ptr(component.material.diffuse));
			component.material.ambient = component.material.diffuse * 0.6f;
			ImGui::SliderFloat("Reflectivity", &component.material.reflectivity, 0.0f, 1.0f);
			DrawShadowFlags(component.material);
			ImGui::Checkbox("Collider on Play", &component.generateCollider);
			ImGui::TextDisabled("%d x %d grid, %.0f units. Sculpt via Settings > Terrain Sculpt.",
				component.resolution, component.resolution, component.size);
			if (ImGui::Button("Flatten")){
				std::fill(component.heights.begin(), component.heights.end(), 0.0f);
				component.dirty = true;
			}
			if (ImGui::TreeNode("Generate from Noise")){
				TerrainNoise& noise = component.noise;
				int seed = (int)noise.seed;
				if (ImGui::InputInt("Seed", &seed)) { noise.seed = (uint32_t)seed; }
				ImGui::DragFloat("Frequency", &noise.frequency, 0.001f, 0.001f, 2.0f, "%.3f");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Features per world unit: lower = broader hills.");
				ImGui::SliderInt("Octaves", &noise.octaves, 1, 10);
				ImGui::DragFloat("Height", &noise.height, 0.05f, 0.0f, 200.0f, "%.2f");
				ImGui::SliderFloat("Persistence", &noise.persistence, 0.0f, 1.0f, "%.2f");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much each finer octave keeps: higher = rougher.");
				ImGui::DragFloat("Lacunarity", &noise.lacunarity, 0.01f, 1.0f, 4.0f, "%.2f");
				ImGui::SliderFloat("Ridged", &noise.ridged, 0.0f, 1.0f, "%.2f");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = rolling hills, 1 = sharp ridges and valleys.");
				bool generate = ImGui::Button("Generate");
				ImGui::SameLine();
				if (ImGui::Button("New Seed")){
					noise.seed = std::random_device{}();
					generate = true;
				}
				if (generate){
					TerrainGenerator::Generate(component.resolution, component.size, noise, component.heights);
					component.dirty = true;
				}
				ImGui::Checkbox("New Terrain Every Episode", &component.randomizeEachEpisode);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Domain randomization: each RL episode regenerates the heights with a\n"
					                  "fresh seed (from Settings > Simulation > Random Seed). Agents keep their\n"
					                  "edit-time poses, so keep spawn areas clear of tall terrain.");
				ImGui::TreePop();
			}
		});

		DrawComponent<PoolComponent>("Pool", entity, [](PoolComponent& component){
			// Shape changes rebuild the basin (and, on the next Play, its collider).
			bool shape = false;
			shape |= ImGui::DragFloat2("Size", glm::value_ptr(component.size), 0.05f, 0.5f, 100.0f, "%.2f m");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Inner width (X) and length (Z). The entity's origin is the centre of the rim.");
			shape |= ImGui::DragFloat("Depth", &component.depth, 0.02f, 0.2f, 20.0f, "%.2f m");
			shape |= ImGui::DragFloat("Freeboard", &component.freeboard, 0.01f, 0.0f, 2.0f, "%.2f m");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rim down to the still water surface.");
			shape |= ImGui::DragFloat("Coping", &component.coping, 0.01f, 0.05f, 2.0f, "%.2f m");
			if (shape) { component.dirty = true; }

			ImGui::Separator();
			ImGui::TextDisabled("Waves");
			ImGui::DragFloat("Cell Size", &component.cellSize, 0.002f, 0.01f, 0.5f, "%.3f m");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Simulation resolution: smaller = finer ripples, more work\n"
			                                              "(large grids can run on the GPU: Settings > OpenCL > Water Ripples).");
			ImGui::DragFloat("Wave Speed", &component.waveSpeed, 0.02f, 0.05f, 10.0f, "%.2f m/s");
			ImGui::DragFloat("Damping", &component.waveDamping, 0.01f, 0.0f, 10.0f, "%.2f /s");
			ImGui::SliderFloat("Wave Strength", &component.waveStrength, 0.0f, 1.0f, "%.2f");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Share of the water a body pushes aside that becomes waves.");
			ImGui::Checkbox("Bodies Make Waves", &component.interactive);

			ImGui::Separator();
			ImGui::TextDisabled("Look");
			ImGui::ColorEdit3("Tiles", glm::value_ptr(component.tileColor));
			ImGui::ColorEdit3("Grout", glm::value_ptr(component.groutColor));
			ImGui::DragFloat("Tile Size", &component.tileSize, 0.005f, 0.02f, 2.0f, "%.3f m");
			ImGui::ColorEdit3("Water Colour", glm::value_ptr(component.waterColor));
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Light the water scatters back: the colour of a deep or murky view.");
			ImGui::DragFloat("Clarity", &component.clarity, 0.1f, 0.5f, 200.0f, "%.1f m");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far one sees under water (a clean pool: 20-30 m).");
			ImGui::SliderFloat("Caustics", &component.causticStrength, 0.0f, 2.0f, "%.2f");

			ImGui::Separator();
			ImGui::TextDisabled("Physics (while playing)");
			ImGui::SliderFloat("Water Density", &component.buoyancy, 0.0f, 3.0f);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("In units of 1000 kg/m3 (1 = real water). 0 disables buoyancy.");
			ImGui::SliderFloat("Linear Drag", &component.linearDrag, 0.0f, 3.0f);
			ImGui::SliderFloat("Angular Drag", &component.angularDrag, 0.0f, 3.0f);
			ImGui::Checkbox("Basin Collider", &component.generateCollider);
			ImGui::Checkbox("Spray", &component.spray);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bodies hitting the water fast throw up droplets (particle fluid).");
			if (component.spray) { ImGui::SliderFloat("Spray Amount", &component.sprayAmount, 0.0f, 3.0f, "%.2f"); }
		});

		DrawComponent<FluidComponent>("Particle Fluid", entity, [](FluidComponent& component){
			ImGui::Checkbox("Starting Block", &component.fillBlock);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Liquid filling an axis-aligned box at the entity when Play starts (and every RL episode).");
			if (component.fillBlock) { ImGui::DragFloat3("Block Size", glm::value_ptr(component.blockSize), 0.02f, 0.0f, 50.0f, "%.2f m"); }
			ImGui::Checkbox("Pour", &component.pour);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("A stream from the entity along its local -Y (rotate the entity to aim it).");
			if (component.pour){
				ImGui::DragFloat("Pour Speed", &component.pourSpeed, 0.05f, 0.1f, 30.0f, "%.2f m/s");
				ImGui::DragFloat("Nozzle Radius", &component.nozzleRadius, 0.005f, 0.0f, 2.0f, "%.3f m");
			}
			ImGui::Separator();
			ImGui::TextDisabled("Simulation");
			ImGui::DragFloat("Particle Size", &component.spacing, 0.002f, 0.01f, 0.5f, "%.3f m");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Spacing at rest. Halving it means 8x the particles for the same volume.");
			ImGui::DragInt("Max Particles", &component.maxParticles, 100.0f, 100, 200000);
			if (component.fluid){
				ImGui::SameLine();
				ImGui::TextDisabled("%d live", component.fluid->Count());
			}
			ImGui::SliderInt("Iterations", &component.iterations, 1, 10);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Density solver passes per step: more = less compressible, slower.");
			ImGui::SliderFloat("Viscosity", &component.viscosity, 0.0f, 0.3f, "%.3f");
			ImGui::SliderFloat("Surface Tension", &component.surfaceTension, 0.0f, 5.0f, "%.2f");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Holds streams, sheets and drops together.");
			ImGui::SliderFloat("Anti-Clumping", &component.cohesion, 0.0f, 0.5f, "%.3f");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keeps particles from bunching up (PBF's artificial pressure).");
			ImGui::SliderFloat("Wall Friction", &component.friction, 0.0f, 1.0f, "%.2f");
			ImGui::DragFloat("Lifetime", &component.lifetime, 0.1f, 0.0f, 600.0f, component.lifetime > 0.0f ? "%.1f s" : "forever");
			ImGui::Separator();
			ImGui::TextDisabled("Look");
			ImGui::ColorEdit3("Colour", glm::value_ptr(component.color));
			ImGui::DragFloat("Clarity", &component.clarity, 0.05f, 0.05f, 100.0f, "%.2f m");
		});

		DrawComponent<WaterComponent>("Water", entity, [](WaterComponent& component){
			ImGui::ColorEdit3("Color", glm::value_ptr(component.color));
			ImGui::SliderFloat("Transparency", &component.alpha, 0.0f, 1.0f);
			ImGui::DragFloat("Wave Height", &component.amplitude, 0.01f, 0.0f, 2.0f);
			ImGui::DragFloat("Wave Scale", &component.waveScale, 0.01f, 0.05f, 3.0f);
			ImGui::DragFloat("Wave Speed", &component.speed, 0.05f, 0.0f, 6.0f);
			ImGui::SliderFloat("Choppiness", &component.choppiness, 0.0f, 1.2f);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gerstner crest sharpness. 0 = smooth sine swells.");
			ImGui::SliderFloat("Foam", &component.foam, 0.0f, 1.5f);
			ImGui::Separator();
			ImGui::Checkbox("Interactive Ripples", &component.interactiveRipples);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Simulated splash/wake ripples from physics bodies.");
			if (component.interactiveRipples){
				const int sizes[] = { 64, 128, 256, 512, 1024 };
				const char* labels[] = { "64 x 64", "128 x 128", "256 x 256", "512 x 512", "1024 x 1024" };
				int current = 1;
				for (int i = 0; i < 5; i++) { if (sizes[i] == component.rippleResolution) { current = i; } }
				if (ImGui::Combo("Ripple Resolution", &current, labels, 5)){
					component.rippleResolution = sizes[current];
					component.ripples.reset(); // rebuilt at the new size next frame
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Cells per side of the ripple simulation: finer wakes cost more.\n"
					                  "512 and up can run on the GPU (Settings > OpenCL > Water Ripples).");
			}
			ImGui::Separator();
			ImGui::TextDisabled("Physics coupling (while playing):");
			ImGui::SliderFloat("Water Density", &component.buoyancy, 0.0f, 3.0f);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("In units of 1000 kg/m3 (1 = real water). 0 disables.\nBodies with a lower Rigid Body density float; denser ones sink.");
			ImGui::SliderFloat("Linear Drag", &component.linearDrag, 0.0f, 3.0f);
			ImGui::SliderFloat("Angular Drag", &component.angularDrag, 0.0f, 3.0f);
			ImGui::TextDisabled("Position at the water level over a terrain basin to fill a lake.");
		});

		DrawComponent<MeshColliderComponent>("Mesh Collider", entity, [](MeshColliderComponent& component){
			ImGui::Checkbox("Convex hull", &component.convex);
			ImGui::TextDisabled(component.convex
				? "Convex hull of the mesh (works for dynamic bodies)."
				: "Exact triangle mesh (STATIC bodies only).");
			ImGui::TextDisabled("Built from the Mesh on Play; needs a Rigid Body + Mesh.");
		});

		DrawComponent<BoneAttachmentComponent>("Bone Attachment", entity, [](BoneAttachmentComponent& component){
			char target[128], bone[128];
			std::snprintf(target, sizeof(target), "%s", component.target.c_str());
			std::snprintf(bone, sizeof(bone), "%s", component.bone.c_str());
			if (ImGui::InputText("Target", target, sizeof(target))) { component.target = target; }
			if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Tag of the entity with the animated model."); }
			if (ImGui::InputText("Bone", bone, sizeof(bone))) { component.bone = bone; }
			if (ImGui::IsItemHovered()) { ImGui::SetTooltip("e.g. RightHand, LeftHand, Head (Mixamo prefixes are optional)."); }
			ImGui::DragFloat3("Offset", glm::value_ptr(component.offset), 0.005f);
			glm::vec3 degrees = glm::degrees(component.rotation);
			if (ImGui::DragFloat3("Rotation", glm::value_ptr(degrees), 0.5f)) { component.rotation = glm::radians(degrees); }
			ImGui::TextDisabled("Position/rotation follow the bone; Scale is kept.");
		});

		DrawComponent<WireframeComponent>("Wireframe", entity, [](auto& component){
			(void)component;
			ImGui::TextDisabled("Object is drawn as a wireframe.\nRemove this component to hide it.");
		});

		DrawComponent<AgentComponent>("Agent", entity, [this, entity](AgentComponent& component) mutable {
			const bool running = m_ActiveScene->IsRunning();
			if (running)
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(EditorUI::Accent()), "Spaces are fixed while playing; changes apply on the next Play.");

			char behavior[64];
			std::strncpy(behavior, component.behavior.c_str(), sizeof(behavior) - 1);
			behavior[sizeof(behavior) - 1] = 0;
			if (ImGui::InputText("Behavior", behavior, sizeof(behavior))) { component.behavior = behavior; }
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Name reported to the client (Python) for this agent.");

			// Policy as a segmented control: it is the most important choice here.
			ImGui::TextDisabled("Policy");
			{
				const char* labels[] = { "Keyboard", "Random", "Python" };
				const char* tips[] = {
					"Drive it yourself with WASD / arrow keys (or the script's Heuristic()) to test the environment.",
					"Uniformly random actions: a baseline for rewards.",
					"Actions come from your RL code over the bridge (Gymnasium API, see python/).",
				};
				int policy = (int)component.policy;
				if (EditorUI::Segmented("##policy", &policy, labels, 3, 0.0f, tips))
					component.policy = (AgentComponent::Policy)policy;
				// Live switch: the running agent follows the new policy at its next decision.
				if (Environment* env = m_ActiveScene->GetEnvironment())
					if (AgentState* state = env->FindAgent((entt::entity)entity))
						state->policy = component.policy;
			}

			EditorUI::SectionLabel("Actions");
			{
				const char* controls[] = { "Move (walk + turn)", "Force (push)", "Custom (Lua OnAction)" };
				int control = (int)component.control;
				if (ImGui::Combo("Control", &control, controls, IM_ARRAYSIZE(controls)))
					component.control = (AgentComponent::Control)control;
				int type = (int)component.actionType;
				ImGui::RadioButton("Discrete", &type, 0); ImGui::SameLine();
				ImGui::RadioButton("Continuous", &type, 1);
				component.actionType = (AgentComponent::ActionType)type;
				if (component.control == AgentComponent::Control::Custom){
					ImGui::DragInt(type == 0 ? "Choices" : "Dimensions", &component.customActionSize, 0.1f, 1, 64);
				}
				std::vector<std::string> names = Environment::DescribeActions(component);
				std::string summary;
				for (size_t i = 0; i < names.size(); i++) { summary += (i ? ", " : "") + names[i]; }
				ImGui::TextDisabled("%d %s: %s", (int)names.size(), type == 0 ? "choices" : "values in [-1, 1]", summary.c_str());

				if (component.control == AgentComponent::Control::Move){
					ImGui::DragFloat("Move Speed", &component.moveSpeed, 0.05f, 0.1f, 30.0f, "%.2f m/s");
					ImGui::DragFloat("Turn Speed", &component.turnSpeed, 0.05f, 0.1f, 20.0f, "%.2f rad/s");
				}
				else if (component.control == AgentComponent::Control::Force){
					ImGui::DragFloat("Force", &component.force, 0.5f, 0.0f, 1000.0f, "%.1f N");
					ImGui::DragFloat("Speed Scale", &component.moveSpeed, 0.05f, 0.1f, 30.0f, "%.2f m/s");
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("Velocity observations are divided by this.");
				}
				ImGui::DragInt("Decision Period", &component.decisionPeriod, 0.1f, 1, 120, "%d steps");
				float hz = 1.0f / std::max(1e-4f, m_ActiveScene->Simulation().fixedTimestep) / (float)std::max(1, component.decisionPeriod);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Physics steps each action is held for (%.1f decisions per simulated second).", hz);
			}

			EditorUI::SectionLabel("Observations");
			{
				if (ImGui::BeginTable("##obs", 2)){
					ImGui::TableNextColumn(); ImGui::Checkbox("Velocity", &component.observeVelocity);
					ImGui::TableNextColumn(); ImGui::Checkbox("Heading", &component.observeHeading);
					ImGui::TableNextColumn(); ImGui::Checkbox("Target", &component.observeTarget);
					ImGui::TableNextColumn(); ImGui::Checkbox("Position", &component.observePosition);
					ImGui::EndTable();
				}
				char target[64];
				std::strncpy(target, component.targetTag.c_str(), sizeof(target) - 1);
				target[sizeof(target) - 1] = 0;
				if (ImGui::InputText("Target Tag", target, sizeof(target))) { component.targetTag = target; }
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("The nearest entity with this tag is the target (observations + approach reward).");
				ImGui::DragFloat("Scale", &component.observationScale, 0.1f, 0.1f, 1000.0f, "%.1f m = 1.0");
				ImGui::DragInt("Custom Values", &component.customObservationSize, 0.1f, 0, 256);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Values the script adds with AddObservation(...) in CollectObservations().");

				std::vector<std::string> labels = Environment::DescribeObservations(*m_ActiveScene, (entt::entity)entity);
				if (ImGui::TreeNodeEx("##layout", ImGuiTreeNodeFlags_SpanAvailWidth, "Observation vector: %d values", (int)labels.size())){
					if (!entity.HasComponent<RaySensorComponent>())
						ImGui::TextDisabled("Add a Ray Sensor to let the agent see walls and goals.");
					// Collapse each ray's values into one line to keep the list short.
					for (size_t i = 0; i < labels.size(); i++){
						if (labels[i].rfind("ray", 0) == 0 && labels[i].find(".distance") == std::string::npos) { continue; }
						if (labels[i].rfind("ray", 0) == 0){
							auto& sensor = entity.GetComponent<RaySensorComponent>();
							ImGui::BulletText("[%d] %s + %d tag flags", (int)i, labels[i].c_str(), (int)sensor.detectableTags.size());
						}
						else { ImGui::BulletText("[%d] %s", (int)i, labels[i].c_str()); }
					}
					ImGui::TreePop();
				}
			}

			EditorUI::SectionLabel("Rewards & Episode");
			{
				ImGui::DragFloat("Step Reward", &component.stepReward, 0.0005f, -1.0f, 1.0f, "%.4f");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Added every decision. Small negative values reward finishing quickly.");
				ImGui::DragFloat("Approach Reward", &component.approachReward, 0.005f, -10.0f, 10.0f, "%.3f / m");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reward per metre moved toward the target (shaping). 0 = sparse rewards only.");
				ImGui::DragFloat("Fall Height", &component.fallHeight, 0.1f, -1000.0f, 1000.0f, "%.1f m");
				ImGui::DragFloat("Fall Reward", &component.fallReward, 0.01f, -100.0f, 100.0f, "%.2f");
				ImGui::DragInt("Max Steps", &component.maxSteps, 1.0f, 0, 100000, component.maxSteps == 0 ? "unlimited" : "%d decisions");
				ImGui::TextDisabled("Goals and hazards: add Reward Zones. Scripts: AddReward(r), EndEpisode().");
			}
		});

		DrawComponent<RaySensorComponent>("Ray Sensor", entity, [](RaySensorComponent& component){
			ImGui::SliderInt("Rays", &component.rays, 1, 32);
			ImGui::SliderFloat("Field of View", &component.fov, 0.0f, 360.0f, "%.0f deg");
			ImGui::DragFloat("Length", &component.length, 0.1f, 0.5f, 200.0f, "%.1f m");
			ImGui::DragFloat("Height Offset", &component.heightOffset, 0.02f, -10.0f, 10.0f, "%.2f m");

			ImGui::TextDisabled("Detectable tags (one-hot per ray)");
			for (size_t i = 0; i < component.detectableTags.size(); i++){
				ImGui::PushID((int)i);
				// Tag chip in its overlay colour, with a remove button.
				ImVec2 p = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 6.0f, p.y + ImGui::GetFrameHeight() * 0.5f), 4.0f, EditorUI::CategoryColor((int)i));
				ImGui::SetCursorScreenPos(ImVec2(p.x + 16.0f, p.y));
				char buf[64];
				std::strncpy(buf, component.detectableTags[i].c_str(), sizeof(buf) - 1);
				buf[sizeof(buf) - 1] = 0;
				ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
				if (ImGui::InputText("##tag", buf, sizeof(buf))) { component.detectableTags[i] = buf; }
				ImGui::SameLine();
				bool remove = EditorUI::IconButton("##remove", EditorUI::Icon::Close, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()), false, "Remove tag");
				ImGui::PopID();
				if (remove){ component.detectableTags.erase(component.detectableTags.begin() + i); break; }
			}
			if (ImGui::SmallButton("+ Add Tag")) { component.detectableTags.push_back("Tag"); }
			ImGui::TextDisabled("%d values (%d rays x %d)", component.rays * component.ValuesPerRay(), component.rays, component.ValuesPerRay());
		});

		DrawComponent<RewardZoneComponent>("Reward Zone", entity, [](RewardZoneComponent& component){
			// Presets for the common cases.
			ImGui::TextDisabled("Preset");
			ImGui::SameLine();
			if (ImGui::SmallButton("Goal"))       { component.reward = 1.0f;  component.trigger = RewardZoneComponent::Trigger::OnEnter;     component.endEpisode = true; }
			ImGui::SameLine();
			if (ImGui::SmallButton("Hazard"))     { component.reward = -1.0f; component.trigger = RewardZoneComponent::Trigger::OnEnter;     component.endEpisode = true; }
			ImGui::SameLine();
			if (ImGui::SmallButton("Checkpoint")) { component.reward = 0.2f;  component.trigger = RewardZoneComponent::Trigger::OnEnter;     component.endEpisode = false; }
			ImGui::SameLine();
			if (ImGui::SmallButton("Area"))       { component.reward = 0.1f;  component.trigger = RewardZoneComponent::Trigger::WhileInside; component.endEpisode = false; }

			ImGui::DragFloat3("Half Extents", glm::value_ptr(component.halfExtents), 0.05f, 0.01f, 1000.0f, "%.2f");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Local size; multiplied by the entity's scale.");
			ImVec4 color = component.reward >= 0.0f ? ImVec4(0.36f, 0.85f, 0.5f, 1.0f) : ImVec4(0.95f, 0.4f, 0.4f, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::DragFloat("Reward", &component.reward, 0.01f, -100.0f, 100.0f, "%+.3f");
			ImGui::PopStyleColor();
			const char* triggers[] = { "On enter (once)", "While inside (per second)" };
			int trigger = (int)component.trigger;
			if (ImGui::Combo("Trigger", &trigger, triggers, IM_ARRAYSIZE(triggers)))
				component.trigger = (RewardZoneComponent::Trigger)trigger;
			ImGui::Checkbox("Ends Episode", &component.endEpisode);
			char tag[64];
			std::strncpy(tag, component.agentTag.c_str(), sizeof(tag) - 1);
			tag[sizeof(tag) - 1] = 0;
			if (ImGui::InputTextWithHint("Only Agent", "any agent", tag, sizeof(tag))) { component.agentTag = tag; }
			ImGui::Checkbox("Visible to Ray Sensors", &component.visibleToRays);
		});

		DrawComponent<RandomizerComponent>("Spawn Randomizer", entity, [](RandomizerComponent& component){
			ImGui::DragFloat3("Position Range", glm::value_ptr(component.positionRange), 0.05f, 0.0f, 1000.0f, "+/-%.2f");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Each episode starts at the edit-time position plus a uniform random offset.");
			ImGui::Checkbox("Random Heading", &component.randomYaw);
			ImGui::TextDisabled("Applied on every episode reset (domain randomization).");
		});

		DrawComponent<LuaScriptComponent>("Lua Scripts", entity, [this, entity](LuaScriptComponent& component){
			Entity e = entity;
			ImGui::TextDisabled("Each script runs on its own while playing.");
			int edit = -1, remove = -1;
			for (int i = 0; i < (int)component.scripts.size(); i++){
				LuaScriptEntry& script = component.scripts[i];
				ImGui::PushID(i);
				if (ImGui::Checkbox("##enabled", &script.enabled)) { m_ActiveScene->ReloadScripts(e); }
				if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Enabled"); }
				ImGui::SameLine();
				if (script.enabled) { ImGui::TextUnformatted(script.name.c_str()); }
				else { ImGui::TextDisabled("%s (off)", script.name.c_str()); }
				const float buttons = ImGui::CalcTextSize("Edit").x + ImGui::CalcTextSize("X").x
				                    + ImGui::GetStyle().FramePadding.x * 4.0f + ImGui::GetStyle().ItemSpacing.x;
				ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - buttons));
				if (ImGui::SmallButton("Edit")) { edit = i; }
				ImGui::SameLine();
				if (ImGui::SmallButton("X")) { remove = i; }
				if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Remove script"); }
				ImGui::PopID();
			}
			if (ImGui::Button("+ Add Script")) { ImGui::OpenPopup("##addscript"); }
			if (ImGui::BeginPopup("##addscript")){
				for (const ScriptTemplate& t : GetScriptTemplates()){
					if (ImGui::MenuItem(t.name)) { edit = Phoenix::ScriptEditorPanel::AddScript(e, t); }
					if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", t.description); }
				}
				ImGui::EndPopup();
			}
			if (edit >= 0){
				m_ShowScriptEditor = true;
				m_ScriptPanel.Open(e, edit);
			}
			if (remove >= 0) { m_ScriptPanel.RemoveScript(e, remove); }
		});

	}

}