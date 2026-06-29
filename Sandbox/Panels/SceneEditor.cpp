#include "SceneEditor.h"
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/imGui/imgui_internal.h>
#include <Phoenix/Scene/Component.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <cctype>
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
            "SetColor","SetEmissive","OnUpdate","math","print"
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
			if (ImGui::IsKeyPressed(ImGuiKey_Delete))
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

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
			float lineHeight = ImGui::GetIO().FontDefault->FontSize + ImGui::GetStyle().FramePadding.y * 2.0;
			ImGui::Separator();
			bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), treeNodeFlags, name.c_str(), nullptr);
			ImGui::PopStyleVar();
			ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
			if (ImGui::Button("+", ImVec2{ lineHeight, lineHeight })){
				ImGui::OpenPopup("ComponentSettings");
			}

			bool removeComponent = false;
			if (ImGui::BeginPopup("ComponentSettings")){
				if (ImGui::MenuItem("Remove component"))
					removeComponent = true;

				ImGui::EndPopup();
			}

			if (open){
				uiFunction(component);
				ImGui::TreePop();
			}

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
                    PHX_CORE_ASSERT("This entity already has the Camera Component!");
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Cube")){
                if (!m_SelectedEntity.HasComponent<CubeComponent>())
                    m_SelectedEntity.AddComponent<CubeComponent>();
                else
                    PHX_CORE_ASSERT("This entity already has the Cube Component!");
                ImGui::CloseCurrentPopup();
            }
			if (ImGui::MenuItem("Point Light")){
                if (!m_SelectedEntity.HasComponent<PointLightComponent>())
                    m_SelectedEntity.AddComponent<PointLightComponent>();
                else
                    PHX_CORE_ASSERT("This entity already has the Cube Component!");
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
            if (ImGui::MenuItem("Mesh Collider")){
                if (!m_SelectedEntity.HasComponent<MeshColliderComponent>())
                    m_SelectedEntity.AddComponent<MeshColliderComponent>();
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
			ImGui::TextDisabled("Applied on Run");
		});

		DrawComponent<BoxColliderComponent>("Box Collider", entity, [](auto& component){
			float extents[] = { component.halfExtents.x, component.halfExtents.y, component.halfExtents.z };
			if (ImGui::DragFloat3("Half Extents", extents, 0.05f, 0.0f, 0.0f, "%.2f")){
				component.halfExtents.x = extents[0];
				component.halfExtents.y = extents[1];
				component.halfExtents.z = extents[2];
			}
			ImGui::TextDisabled("Scaled by Transform on Run");
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
				ImGui::TreePop();
			}
		});

	}

}