#include "SceneEditor.h"
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/imGui/imgui_internal.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/Physics/Physics.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <cctype>
#include <type_traits>
#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>

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
        "ApplyImpulseAtPoint", "ApplyAngularImpulse", "ApplyForceAtPoint", "ApplyTorque",
        "GetAngularVelocity", "GetMass", "SetGravityFactor", "SetFriction", "SetRestitution",
        "WakeUp", "IsSleeping", "Teleport", "SphereCast", "OverlapSphere", "OverlapBox",
        "Move", "Jump", "IsGrounded", "SetMotor", "SetMotorTarget", "GetJointValue",
        "BreakJoint", "IsJointBroken", "OnTriggerEnter", "OnTriggerExit", "OnJointBreak",
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

    void SceneEditor::ScenePanel(){
        ImGui::Begin("Scene", nullptr, (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize) & ImGuiWindowFlags_None);
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
        for (auto entityID : entities)
		{
			Entity entity{ entityID , m_ActiveScene.get() };
			EntityNode(entity);
		}

        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
		{
			m_SelectedEntity = {};
		}

		// Right-click on blank space
		if (ImGui::BeginPopupContextWindow(NULL, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)){
			if (ImGui::MenuItem("Create Empty Entity"))
			{
				m_SelectedEntity = m_ActiveScene->CreateEntity("Empty Entity");
			}

			if (ImGui::MenuItem("Create Point Light"))
			{
				try
				{
					m_SelectedEntity = m_ActiveScene->CreatePointLightEntity("Point Light");
				}
				catch(const std::exception& e)
				{
					PHX_ERROR(e.what());
				}
				
			}
				
			
			if (ImGui::MenuItem("Create Directional Light"))
				m_SelectedEntity = m_ActiveScene->CreateDirLightEntity("Directional Light");

			if (ImGui::MenuItem("Create Terrain")){
				m_SelectedEntity = m_ActiveScene->CreateEntity("Terrain");
				auto& terrain = m_SelectedEntity.AddComponent<TerrainComponent>();
				terrain.material.ambient  = glm::vec3(0.25f, 0.35f, 0.20f);
				terrain.material.diffuse  = glm::vec3(0.35f, 0.50f, 0.28f);
				terrain.material.specular = glm::vec3(0.1f);
				terrain.material.shininess = 8.0f;
			}

			if (ImGui::MenuItem("Create Water")){
				m_SelectedEntity = m_ActiveScene->CreateEntity("Water");
				m_SelectedEntity.AddComponent<WaterComponent>();
			}

			CreatePhysicsObjectMenu();

			if (ImGui::BeginMenu("Create Shape")){
				struct ShapeDef { const char* label; const char* name; PrimitiveComponent::Type type; };
				const ShapeDef shapes[] = {
					{ "Sphere",          "Sphere",   PrimitiveComponent::Type::Sphere   },
					{ "Pillar (Cylinder)", "Pillar", PrimitiveComponent::Type::Cylinder },
					{ "Cone",            "Cone",     PrimitiveComponent::Type::Cone     },
					{ "Plane",           "Plane",    PrimitiveComponent::Type::Plane    },
					{ "Cube",            "Cube",     PrimitiveComponent::Type::Cube     },
				};
				for (const auto& s : shapes){
					if (ImGui::MenuItem(s.label)){
						m_SelectedEntity = m_ActiveScene->CreateEntity(s.name);
						m_SelectedEntity.AddComponent<PrimitiveComponent>(s.type);
					}
				}
				ImGui::EndMenu();
			}

			ImGui::EndPopup();
		}

        ImGui::End();
    }


    void SceneEditor::EntityPanel(){
        ImGui::Begin("Properties");
		if (m_SelectedEntity){
			DrawComponents(m_SelectedEntity);
		}

		ImGui::End();
    }

    void SceneEditor::ScriptEditorPanel(){
        if (!m_ShowScriptEditor) { return; }

        ImGui::Begin("Script Editor", &m_ShowScriptEditor);
        if (m_SelectedEntity && m_SelectedEntity.HasComponent<LuaScriptComponent>()){
            auto& tag = m_SelectedEntity.GetComponent<TagComponent>().Tag;
            ImGui::Text("%s", tag.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("| Tab: autocomplete/indent  Ctrl+Z: undo  Ctrl+C/V/X");
            ImGui::Separator();
            LuaCodeEditor("##ScriptEditorField", m_SelectedEntity.GetComponent<LuaScriptComponent>().source);
        }
        else{
            ImGui::TextDisabled("Select an entity with a Lua Script component.");
        }
        ImGui::End();
    }

    void SceneEditor::TimelinePanel(){
        if (!m_ShowTimeline) { return; }
        ImGui::Begin("Timeline", &m_ShowTimeline);

        Entity e = m_SelectedEntity;
        if (!e || !e.HasComponent<AnimationComponent>() || !e.HasComponent<MeshComponent>()){
            ImGui::TextDisabled("Select an entity with an Animation component.");
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
        dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(35, 35, 40, 255), 3.0f);
        if (dur > 0.0f){
            float px = p0.x + w * (cur / dur);
            dl->AddLine(ImVec2(px, p0.y), ImVec2(px, p0.y + h), IM_COL32(255, 200, 0, 255), 2.0f);
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

    void SceneEditor::EntityNode(Entity entity){
        auto& tag = entity.GetComponent<TagComponent>().Tag;
		
		ImGuiTreeNodeFlags flags = ((m_SelectedEntity == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

		bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, tag.c_str(), nullptr);
		if (ImGui::IsItemClicked()){
			m_SelectedEntity = entity;
		}
		

		bool entityDeleted = false;
		if (ImGui::BeginPopupContextItem()){
			if (ImGui::MenuItem("Delete Entity"))
				entityDeleted = true;

			ImGui::EndPopup();	
		}

		if (m_SelectedEntity == entity && (!entityDeleted))
		{
			// Ignore Delete while typing (e.g. in the Tag field), or it deletes the entity.
			if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput)
			{
				entityDeleted = true;
			}
		}

		if (opened){
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
			bool opened = ImGui::TreeNodeEx((void*)9817239, flags, tag.c_str(), nullptr);
			if (opened)
				ImGui::TreePop();
			ImGui::TreePop();
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

			// Scope every ID below (tree node, "+" button, popup) to this component type.
			// TreeNodeEx only pushes its ID while open, so without this the "+" buttons and
			// "ComponentSettings" popups of collapsed components collided, and "Remove"
			// could remove a different component.
			ImGui::PushID((void*)typeid(T).hash_code());

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
			float lineHeight = ImGui::GetIO().FontDefault->FontSize + ImGui::GetStyle().FramePadding.y * 2.0;
			ImGui::Separator();
			bool open = ImGui::TreeNodeEx("##component", treeNodeFlags, "%s", name.c_str());
			ImGui::PopStyleVar();
			ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
			if (ImGui::Button("+", ImVec2{ lineHeight, lineHeight })){
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
				uiFunction(component);
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
        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 4, 4 });

        float lineHeight = ImGui::GetIO().FontDefault->FontSize + ImGui::GetStyle().FramePadding.y * 2.0;
        ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
        ImGui::PushFont(boldFont);
        if (ImGui::Button(label1.c_str(), buttonSize))
            values.x = resetValue;
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat((std::string("##") + label1).c_str(), &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.8f, 0.3f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
        ImGui::PushFont(boldFont);
        if (ImGui::Button(label2.c_str(), buttonSize))
            values.y = resetValue;
        ImGui::PopFont();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat((std::string("##") + label2).c_str(), &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.35f, 0.9f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
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
			int total_w = ImGui::GetContentRegionAvail().x;
			if (entity.HasComponent<TagComponent>()){
				auto& tag = entity.GetComponent<TagComponent>().Tag;
				char buffer[256];
				memset(buffer, 0, sizeof(buffer));
				std::strncpy(buffer, tag.c_str(), sizeof(buffer));
				if (ImGui::InputText("##Tag", buffer, sizeof(buffer))){
					tag = std::string(buffer);
				}
			}
			ImGui::SameLine(total_w - 90);
			ImGui::PushItemWidth(-1);
			ImGui::SetNextItemWidth(total_w);
			if (ImGui::Button("Add Component"))
				ImGui::OpenPopup("AddComponent");
		}

		if (ImGui::BeginPopup("AddComponent")){
            if (ImGui::MenuItem("Camera")){
                if (!m_SelectedEntity.HasComponent<CameraComponent>())
                    m_SelectedEntity.AddComponent<CameraComponent>();
                else
                    PHX_WARN("This entity already has the Camera Component");
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Camera Follow")){
                if (!m_SelectedEntity.HasComponent<CameraFollowComponent>())
                    m_SelectedEntity.AddComponent<CameraFollowComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Cube")){
                if (!m_SelectedEntity.HasComponent<CubeComponent>())
                    m_SelectedEntity.AddComponent<CubeComponent>();
                else
                    PHX_WARN("This entity already has the Cube Component");
                ImGui::CloseCurrentPopup();
            }
			if (ImGui::MenuItem("Point Light")){
                // The shader only lights with the first MAX_NUM_POINT_LIGHTS; more are ignored.
                if (m_ActiveScene->GetNumberOfPointLights() >= Scene::MAX_NUM_POINT_LIGHTS)
                    PHX_ERROR("Reached maximum number of point lights ({0})", Scene::MAX_NUM_POINT_LIGHTS);
                else if (!m_SelectedEntity.HasComponent<PointLightComponent>())
                    m_SelectedEntity.AddComponent<PointLightComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Rigid Body")){
                if (!m_SelectedEntity.HasComponent<RigidBodyComponent>())
                    m_SelectedEntity.AddComponent<RigidBodyComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Box Collider")){
                if (!m_SelectedEntity.HasComponent<BoxColliderComponent>())
                    m_SelectedEntity.AddComponent<BoxColliderComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Sphere Collider")){
                if (!m_SelectedEntity.HasComponent<SphereColliderComponent>())
                    m_SelectedEntity.AddComponent<SphereColliderComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Capsule Collider")){
                if (!m_SelectedEntity.HasComponent<CapsuleColliderComponent>())
                    m_SelectedEntity.AddComponent<CapsuleColliderComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Cylinder Collider")){
                if (!m_SelectedEntity.HasComponent<CylinderColliderComponent>())
                    m_SelectedEntity.AddComponent<CylinderColliderComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Mesh Collider")){
                if (!m_SelectedEntity.HasComponent<MeshColliderComponent>())
                    m_SelectedEntity.AddComponent<MeshColliderComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Joint")){
                if (!m_SelectedEntity.HasComponent<JointComponent>())
                    m_SelectedEntity.AddComponent<JointComponent>().Add().pivot =
                        m_SelectedEntity.GetComponent<TransformComponent>().Translation;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Compound Collider")){
                if (!m_SelectedEntity.HasComponent<CompoundColliderComponent>())
                    m_SelectedEntity.AddComponent<CompoundColliderComponent>().Add(CompoundColliderComponent::Shape::Type::Box);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Vehicle")){
                if (!m_SelectedEntity.HasComponent<VehicleComponent>())
                    m_SelectedEntity.AddComponent<VehicleComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Ragdoll")){
                if (!m_SelectedEntity.HasComponent<RagdollComponent>())
                    m_SelectedEntity.AddComponent<RagdollComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Character Controller")){
                if (!m_SelectedEntity.HasComponent<CharacterControllerComponent>())
                    m_SelectedEntity.AddComponent<CharacterControllerComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Force Field")){
                if (!m_SelectedEntity.HasComponent<ForceFieldComponent>())
                    m_SelectedEntity.AddComponent<ForceFieldComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Wireframe")){
                if (!m_SelectedEntity.HasComponent<WireframeComponent>())
                    m_SelectedEntity.AddComponent<WireframeComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Lua Script")){
                if (!m_SelectedEntity.HasComponent<LuaScriptComponent>())
                    m_SelectedEntity.AddComponent<LuaScriptComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Shape")){
                if (!m_SelectedEntity.HasComponent<PrimitiveComponent>())
                    m_SelectedEntity.AddComponent<PrimitiveComponent>();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Animation")){
                if (!m_SelectedEntity.HasComponent<AnimationComponent>())
                    m_SelectedEntity.AddComponent<AnimationComponent>();
                ImGui::CloseCurrentPopup();
            }

			ImGui::EndPopup();
		}

		ImGui::PopItemWidth();

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
				ImGui::TextDisabled("Applied on Run. Shape = all collider components.");
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
			ImGui::TextDisabled("Local units, scaled by Transform on Run");
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
			ImGui::TextDisabled("Scaled by the largest Transform scale axis on Run");
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
				else ImGui::TextDisabled("Connected on Run; needs a Rigid Body + collider.");
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
			ImGui::Checkbox("Collider on Run", &component.generateCollider);
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
			ImGui::TextDisabled("Built from the Mesh on Run; needs a Rigid Body + Mesh.");
		});

		DrawComponent<WireframeComponent>("Wireframe", entity, [](auto& component){
			(void)component;
			ImGui::TextDisabled("Object is drawn as a wireframe.\nRemove this component to hide it.");
		});

		DrawComponent<LuaScriptComponent>("Lua Script", entity, [this](LuaScriptComponent& component){
			ImGui::TextWrapped("%d chars. Edit in the Script Editor window (syntax-coloured).",
				(int)component.source.size());
			if (ImGui::Button("Open Script Editor")){
				m_ShowScriptEditor = true;
			}
			if (ImGui::TreeNode("API reference")){
				ImGui::TextDisabled("Runs while playing (Run). Define OnUpdate(dt).");
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
				ImGui::TreePop();
			}
		});

	}

}