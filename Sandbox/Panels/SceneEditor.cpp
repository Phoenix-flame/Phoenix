#include "SceneEditor.h"
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/imGui/imgui_internal.h>
#include <Phoenix/Scene/Component.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <cctype>
#include <type_traits>
#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>

namespace Phoenix{

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
					{ "Capsule",         "Capsule",  PrimitiveComponent::Type::Capsule  },
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
        m_ScriptPanel.OnImGuiRender(m_SelectedEntity, &m_ShowScriptEditor);
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
                    m_SelectedEntity.AddComponent<JointComponent>();
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
			ImGui::Checkbox("Sensor (Trigger)", &component.isSensor);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Detects contacts (Lua OnCollisionEnter/Exit) without colliding.");
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

		DrawComponent<SphereColliderComponent>("Sphere Collider", entity, [](SphereColliderComponent& component){
			ImGui::DragFloat("Radius", &component.radius, 0.05f, 0.01f, 100.0f, "%.2f");
			ImGui::TextDisabled("Scaled by the largest Transform scale axis on Run");
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
			ImGui::TextDisabled("Connected on Run; needs a Rigid Body + collider.");
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