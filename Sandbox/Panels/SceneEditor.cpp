#include "SceneEditor.h"
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/imGui/imgui_internal.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/imGui/ImGuiLayer.h>
#include <glm/gtc/type_ptr.hpp>
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

    // ---------- Lua inline-editor helpers ----------

    // Candidates offered by Tab-completion (engine API + common Lua keywords).
    static const char* s_LuaCompletions[] = {
        "GetTranslation", "SetTranslation", "GetRotation", "SetRotation",
        "SetScale", "SetColor", "SetEmissive", "OnUpdate",
        "PlayAnimation", "CrossFade", "PauseAnimation", "ResumeAnimation", "StopAnimation",
        "SetAnimationTime", "GetAnimationTime", "GetAnimationDuration", "SetAnimationSpeed",
        "SetAnimationLoop", "IsAnimationPlaying", "IsAnimationFinished", "GetAnimationName",
        "HasAnimation", "OnAnimationEvent",
        "ApplyImpulse", "ApplyForce", "SetVelocity", "GetVelocity", "SetAngularVelocity",
        "RayCast", "OnCollisionEnter", "OnCollisionExit",
        "IsKeyDown", "IsKeyPressed", "Translate", "Rotate", "MoveForward",
        "Key.Up", "Key.Down", "Key.Left", "Key.Right", "Key.Space", "Key.LeftShift",
        "function", "local", "return", "then", "else", "elseif", "while", "for",
        "math.sin", "math.cos", "math.abs", "math.rad", "math.random",
    };

    static bool LuaIsKeyword(const std::string& w){
        static const std::unordered_set<std::string> kw = {
            "and","break","do","else","elseif","end","false","for","function","goto",
            "if","in","local","nil","not","or","repeat","return","then","true","until","while"
        };
        return kw.find(w) != kw.end();
    }
    static bool LuaIsApi(const std::string& w){
        static const std::unordered_set<std::string> api = {
            "GetTranslation","SetTranslation","GetRotation","SetRotation","SetScale",
            "SetColor","SetEmissive","OnUpdate","math","print",
            "PlayAnimation","CrossFade","PauseAnimation","ResumeAnimation","StopAnimation",
            "SetAnimationTime","GetAnimationTime","GetAnimationDuration","SetAnimationSpeed",
            "SetAnimationLoop","IsAnimationPlaying","IsAnimationFinished","GetAnimationName",
            "HasAnimation","OnAnimationEvent",
            "ApplyImpulse","ApplyForce","SetVelocity","GetVelocity","SetAngularVelocity",
            "RayCast","OnCollisionEnter","OnCollisionExit",
            "IsKeyDown","IsKeyPressed","Translate","Rotate","MoveForward","Key"
        };
        return api.find(w) != api.end();
    }

    // Tab-completion callback: complete the word at the cursor to the longest common
    // prefix of the matching candidates (full word when there's a single match).
    static int LuaEditorCallback(ImGuiInputTextCallbackData* data){
        if (data->EventFlag != ImGuiInputTextFlags_CallbackCompletion) { return 0; }
        int start = data->CursorPos;
        while (start > 0){
            char c = data->Buf[start - 1];
            if (std::isalnum((unsigned char)c) || c == '_' || c == '.') { start--; }
            else { break; }
        }
        int wordLen = data->CursorPos - start;
        if (wordLen <= 0){
            // Not on a word: indent instead.
            data->InsertChars(data->CursorPos, "    ");
            return 0;
        }

        std::string word(data->Buf + start, wordLen);
        std::vector<std::string> matches;
        for (const char* cand : s_LuaCompletions){
            if (std::strncmp(cand, word.c_str(), (size_t)wordLen) == 0) { matches.push_back(cand); }
        }
        if (matches.empty()) { return 0; }

        std::string prefix = matches[0];
        for (const auto& m : matches){
            size_t j = 0;
            while (j < prefix.size() && j < m.size() && prefix[j] == m[j]) { j++; }
            prefix.resize(j);
        }
        if ((int)prefix.size() > wordLen){
            data->DeleteChars(start, wordLen);
            data->InsertChars(start, prefix.c_str());
        }
        return 0;
    }

    // Draw syntax-highlighted glyphs over an (invisible-text) InputText, at `origin`
    // (screen-space top-left of the text). The InputText still owns editing, cursor
    // and selection; this just colours what the user sees.
    static void DrawLuaColoredOverlay(const char* text, ImVec2 origin){
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize();
        float lineH = ImGui::GetTextLineHeight();

        const ImU32 cDef = ImGui::GetColorU32(ImVec4(0.86f, 0.86f, 0.86f, 1.0f));
        const ImU32 cKey = ImGui::GetColorU32(ImVec4(0.45f, 0.60f, 0.95f, 1.0f));
        const ImU32 cApi = ImGui::GetColorU32(ImVec4(0.30f, 0.80f, 0.85f, 1.0f));
        const ImU32 cStr = ImGui::GetColorU32(ImVec4(0.90f, 0.62f, 0.35f, 1.0f));
        const ImU32 cCom = ImGui::GetColorU32(ImVec4(0.45f, 0.72f, 0.45f, 1.0f));
        const ImU32 cNum = ImGui::GetColorU32(ImVec4(0.72f, 0.60f, 0.90f, 1.0f));

        ImVec2 pos = origin;
        auto draw = [&](const char* a, const char* b, ImU32 col){
            if (a == b) { return; }
            dl->AddText(font, fontSize, pos, col, a, b);
            pos.x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, a, b).x;
        };

        std::string src(text);
        std::size_t lineStart = 0;
        while (lineStart <= src.size()){
            std::size_t eol = src.find('\n', lineStart);
            std::size_t lineEnd = (eol == std::string::npos) ? src.size() : eol;
            const char* L = src.c_str();
            std::size_t i = lineStart;
            while (i < lineEnd){
                char c = L[i];
                if (c == '-' && i + 1 < lineEnd && L[i + 1] == '-'){
                    draw(L + i, L + lineEnd, cCom);
                    i = lineEnd;
                }
                else if (c == '"' || c == '\''){
                    std::size_t j = i + 1;
                    while (j < lineEnd && L[j] != c) { j++; }
                    if (j < lineEnd) { j++; }
                    draw(L + i, L + j, cStr);
                    i = j;
                }
                else if (std::isdigit((unsigned char)c)){
                    std::size_t j = i;
                    while (j < lineEnd && (std::isalnum((unsigned char)L[j]) || L[j] == '.')) { j++; }
                    draw(L + i, L + j, cNum);
                    i = j;
                }
                else if (std::isalpha((unsigned char)c) || c == '_'){
                    std::size_t j = i;
                    while (j < lineEnd && (std::isalnum((unsigned char)L[j]) || L[j] == '_')) { j++; }
                    std::string w(L + i, L + j);
                    draw(L + i, L + j, LuaIsKeyword(w) ? cKey : (LuaIsApi(w) ? cApi : cDef));
                    i = j;
                }
                else{
                    std::size_t j = i;
                    while (j < lineEnd){
                        char d = L[j];
                        bool starts = std::isalnum((unsigned char)d) || d == '_' || d == '"' || d == '\''
                                   || (d == '-' && j + 1 < lineEnd && L[j + 1] == '-');
                        if (starts) { break; }
                        j++;
                    }
                    if (j == i) { j++; }
                    draw(L + i, L + j, cDef);
                    i = j;
                }
            }
            pos.x = origin.x;
            pos.y += lineH;
            if (eol == std::string::npos) { break; }
            lineStart = eol + 1;
        }
    }

    // A syntax-coloured, editable Lua code field (overlay over a transparent
    // InputText, sized to its content so the overlay stays aligned while scrolling).
    static void LuaCodeEditor(const char* id, std::string& source){
        static char buffer[16384];
        std::strncpy(buffer, source.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        int lineCount = 1;
        for (const char* p = buffer; *p; ++p) { if (*p == '\n') { lineCount++; } }

        const ImGuiStyle& style = ImGui::GetStyle();
        float height = (lineCount + 1) * ImGui::GetTextLineHeight() + style.FramePadding.y * 2.0f;

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // hide real glyphs
        bool changed = ImGui::InputTextMultiline(id, buffer, sizeof(buffer),
            ImVec2(-FLT_MIN, height), ImGuiInputTextFlags_CallbackCompletion, LuaEditorCallback);
        ImGui::PopStyleColor();
        if (changed) { source = buffer; }

        ImVec2 origin = ImGui::GetItemRectMin();
        origin.x += style.FramePadding.x;
        origin.y += style.FramePadding.y;
        DrawLuaColoredOverlay(buffer, origin);

        // The real caret uses ImGuiCol_Text (which we made transparent), so draw our
        // own at the active input's cursor position.
        ImGuiContext& g = *ImGui::GetCurrentContext();
        ImGuiID fieldId = ImGui::GetID(id);
        if (g.InputTextState.ID == fieldId){
            int cursor = g.InputTextState.GetCursorPos(); // ASCII: == char index
            int line = 0, lineStart = 0;
            for (int i = 0; i < cursor && buffer[i]; i++){
                if (buffer[i] == '\n'){ line++; lineStart = i + 1; }
            }
            float lineH = ImGui::GetTextLineHeight();
            float caretX = origin.x + ImGui::CalcTextSize(buffer + lineStart, buffer + cursor).x;
            float caretY = origin.y + line * lineH;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(caretX, caretY), ImVec2(caretX, caretY + lineH),
                ImGui::GetColorU32(ImVec4(0.95f, 0.95f, 0.95f, 1.0f)), 1.0f);
        }
    }

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
        if (ImGui::BeginMenu("Shape")){
            struct ShapeDef { const char* label; const char* name; PrimitiveComponent::Type type; };
            const ShapeDef shapes[] = {
                { "Cube",              "Cube",     PrimitiveComponent::Type::Cube     },
                { "Sphere",            "Sphere",   PrimitiveComponent::Type::Sphere   },
                { "Pillar (Cylinder)", "Pillar",   PrimitiveComponent::Type::Cylinder },
                { "Cone",              "Cone",     PrimitiveComponent::Type::Cone     },
                { "Plane",             "Plane",    PrimitiveComponent::Type::Plane    },
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
        if (!m_ShowScriptEditor) { return; }

        ImGui::Begin("Script Editor", &m_ShowScriptEditor);
        if (m_SelectedEntity && m_SelectedEntity.HasComponent<LuaScriptComponent>()){
            auto& tag = m_SelectedEntity.GetComponent<TagComponent>().Tag;
            ImGui::PushFont(ImGuiLayer::GetFont(ImGuiLayer::FontType::Bold));
            ImGui::Text("%s", tag.c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("Lua   |   Tab: autocomplete / indent   Ctrl+Z: undo   Ctrl+C/V/X");
            ImGui::Separator();
            LuaCodeEditor("##ScriptEditorField", m_SelectedEntity.GetComponent<LuaScriptComponent>().source);
        }
        else{
            EditorUI::EmptyState("No script to edit", "Select an entity with a Lua Script component.");
        }
        ImGui::End();
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
        else if (entity.HasComponent<WaterComponent>()){
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
            AddComponentItem<JointComponent>(entity, "Joint");
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
		});

        DrawComponent<PrimitiveComponent>("Shape", entity, [](PrimitiveComponent& component){
            const char* kinds[] = { "Cube", "Sphere", "Cylinder", "Cone", "Plane" };
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
        });

        DrawComponent<DirLightComponent>("Directional Light", entity, [](auto& component){
			ImGui::Checkbox("Active", &component.isActive);
			ImGui::Checkbox("Casts Shadow", &component.castsShadow);

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
			ImGui::TextDisabled("Set the camera Primary to view through it.");
		});

		DrawComponent<RigidBodyComponent>("Rigid Body", entity, [](auto& component){
			const char* typeStrings[] = { "Static", "Dynamic", "Kinematic" };
			const char* currentType = typeStrings[(int)component.type];
			if (ImGui::BeginCombo("Body Type", currentType)){
				for (int i = 0; i < 3; i++){
					bool isSelected = currentType == typeStrings[i];
					if (ImGui::Selectable(typeStrings[i], isSelected))
						component.type = (RigidBodyComponent::Type)i;
					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::SliderFloat("Friction", &component.friction, 0.0f, 1.5f);
			ImGui::SliderFloat("Restitution", &component.restitution, 0.0f, 1.0f);
			ImGui::DragFloat("Density", &component.density, 10.0f, 1.0f, 30000.0f, "%.0f kg/m3");
			ImGui::SliderFloat("Linear Damping", &component.linearDamping, 0.0f, 2.0f);
			ImGui::SliderFloat("Angular Damping", &component.angularDamping, 0.0f, 2.0f);
			ImGui::SliderFloat("Gravity Factor", &component.gravityFactor, 0.0f, 4.0f);
			float vel[] = { component.initialVelocity.x, component.initialVelocity.y, component.initialVelocity.z };
			if (ImGui::DragFloat3("Initial Velocity", vel, 0.1f)){
				component.initialVelocity = { vel[0], vel[1], vel[2] };
			}
			ImGui::Checkbox("Continuous Collision", &component.continuousCollision);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("For fast movers (bullets) so they don't tunnel through walls.");
			ImGui::Checkbox("Upright (lock tilt)", &component.lockTilt);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only rotates about Y, so walking agents and characters never tip over.");
			ImGui::Checkbox("Sensor (Trigger)", &component.isSensor);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Detects contacts (Lua OnCollisionEnter/Exit) without colliding.");
			ImGui::TextDisabled("Applied on Play");
		});

		DrawComponent<BoxColliderComponent>("Box Collider", entity, [](auto& component){
			float extents[] = { component.halfExtents.x, component.halfExtents.y, component.halfExtents.z };
			if (ImGui::DragFloat3("Half Extents", extents, 0.05f, 0.0f, 0.0f, "%.2f")){
				component.halfExtents.x = extents[0];
				component.halfExtents.y = extents[1];
				component.halfExtents.z = extents[2];
			}
			ImGui::TextDisabled("Scaled by Transform on Play");
		});

		DrawComponent<SphereColliderComponent>("Sphere Collider", entity, [](SphereColliderComponent& component){
			ImGui::DragFloat("Radius", &component.radius, 0.05f, 0.01f, 100.0f, "%.2f");
			ImGui::TextDisabled("Scaled by the largest Transform scale axis on Play");
		});

		DrawComponent<CapsuleColliderComponent>("Capsule Collider", entity, [](CapsuleColliderComponent& component){
			ImGui::DragFloat("Radius", &component.radius, 0.05f, 0.01f, 100.0f, "%.2f");
			ImGui::DragFloat("Half Height", &component.halfHeight, 0.05f, 0.01f, 100.0f, "%.2f");
			ImGui::TextDisabled("Vertical (Y). Total height = 2 x (half height + radius).");
		});

		DrawComponent<CylinderColliderComponent>("Cylinder Collider", entity, [](CylinderColliderComponent& component){
			ImGui::DragFloat("Radius", &component.radius, 0.05f, 0.01f, 100.0f, "%.2f");
			ImGui::DragFloat("Half Height", &component.halfHeight, 0.05f, 0.01f, 100.0f, "%.2f");
			ImGui::TextDisabled("Vertical (Y) cylinder.");
		});

		DrawComponent<JointComponent>("Joint", entity, [](JointComponent& component){
			const char* typeStrings[] = { "Point (ball-socket)", "Distance", "Hinge" };
			const char* currentType = typeStrings[(int)component.type];
			if (ImGui::BeginCombo("Joint Type", currentType)){
				for (int i = 0; i < 3; i++){
					bool isSelected = currentType == typeStrings[i];
					if (ImGui::Selectable(typeStrings[i], isSelected))
						component.type = (JointComponent::Type)i;
					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			char buf[128];
			std::strncpy(buf, component.connectedTag.c_str(), sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = 0;
			if (ImGui::InputText("Connected Body (Tag)", buf, sizeof(buf))) { component.connectedTag = buf; }
			ImGui::TextDisabled("Empty tag = anchored to the world.");

			if (component.type != JointComponent::Type::Distance){
				float pivot[] = { component.pivot.x, component.pivot.y, component.pivot.z };
				if (ImGui::DragFloat3("Pivot (world)", pivot, 0.1f)){
					component.pivot = { pivot[0], pivot[1], pivot[2] };
				}
			}
			if (component.type == JointComponent::Type::Distance){
				float pivot[] = { component.pivot.x, component.pivot.y, component.pivot.z };
				if (ImGui::DragFloat3("World Anchor", pivot, 0.1f)){
					component.pivot = { pivot[0], pivot[1], pivot[2] };
				}
				ImGui::TextDisabled("Used when no connected body; else the other body's center.");
				ImGui::DragFloat("Min Distance", &component.minDistance, 0.05f, -1.0f, 100.0f, "%.2f");
				ImGui::DragFloat("Max Distance", &component.maxDistance, 0.05f, -1.0f, 100.0f, "%.2f");
				ImGui::TextDisabled("-1 keeps the starting distance.");
			}
			if (component.type == JointComponent::Type::Hinge){
				float axis[] = { component.axis.x, component.axis.y, component.axis.z };
				if (ImGui::DragFloat3("Axis (world)", axis, 0.05f)){
					component.axis = { axis[0], axis[1], axis[2] };
				}
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
			ImGui::TextDisabled("Connected on Play; needs a Rigid Body + collider.");
		});

		DrawComponent<TerrainComponent>("Terrain", entity, [](TerrainComponent& component){
			ImGui::ColorEdit3("Color", glm::value_ptr(component.material.diffuse));
			component.material.ambient = component.material.diffuse * 0.6f;
			ImGui::SliderFloat("Reflectivity", &component.material.reflectivity, 0.0f, 1.0f);
			ImGui::Checkbox("Collider on Play", &component.generateCollider);
			ImGui::TextDisabled("%d x %d grid, %.0f units. Sculpt via Settings > Terrain Sculpt.",
				component.resolution, component.resolution, component.size);
			if (ImGui::Button("Flatten")){
				std::fill(component.heights.begin(), component.heights.end(), 0.0f);
				component.dirty = true;
			}
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

		DrawComponent<LuaScriptComponent>("Lua Script", entity, [this](LuaScriptComponent& component){
			ImGui::TextWrapped("%d chars. Edit in the Script Editor window (syntax-coloured).",
				(int)component.source.size());
			if (ImGui::Button("Open Script Editor")){
				m_ShowScriptEditor = true;
			}
			if (ImGui::TreeNode("API reference")){
				ImGui::TextDisabled("Runs while playing (Play / F5). Define OnUpdate(dt).");
				ImGui::BulletText("GetTranslation() -> x, y, z");
				ImGui::BulletText("SetTranslation(x, y, z)");
				ImGui::BulletText("GetRotation() -> x, y, z   (radians)");
				ImGui::BulletText("SetRotation(x, y, z)");
				ImGui::BulletText("SetScale(x, y, z)");
				ImGui::BulletText("SetColor(r, g, b)");
				ImGui::BulletText("SetEmissive(r, g, b [, strength])  -- glow");
				ImGui::Separator();
				ImGui::TextDisabled("Physics (needs a Rigid Body, while playing):");
				ImGui::BulletText("ApplyImpulse(x, y, z) / ApplyForce(x, y, z)");
				ImGui::BulletText("SetVelocity(x, y, z) / GetVelocity() -> x, y, z");
				ImGui::BulletText("SetAngularVelocity(x, y, z)");
				ImGui::BulletText("RayCast(ox,oy,oz, dx,dy,dz, maxDist)\n-> tag, hx, hy, hz (or nil)");
				ImGui::BulletText("OnCollisionEnter(otherTag) -- callback");
				ImGui::BulletText("OnCollisionExit(otherTag)  -- callback");
				ImGui::Separator();
				ImGui::TextDisabled("RL environment (on an Agent entity):");
				ImGui::BulletText("AddReward(r) / EndEpisode()");
				ImGui::BulletText("GetEpisodeStep() / GetCumulativeReward() / GetEpisode()");
				ImGui::BulletText("OnEpisodeBegin()  -- callback after each reset");
				ImGui::BulletText("CollectObservations() + AddObservation(v, ...)");
				ImGui::BulletText("OnAction(actions)  -- Custom control, 1-based table");
				ImGui::BulletText("Heuristic() -> values  -- Keyboard policy, Custom");
				ImGui::TreePop();
			}
		});

	}

}