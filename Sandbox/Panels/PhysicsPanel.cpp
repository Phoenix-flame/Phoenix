#include "SceneEditor.h"
#include <Phoenix/imGui/imgui.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/Physics/Physics.h>
#include <Phoenix/Scene/PhysicsPrefabs.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>

namespace Phoenix{

    void SceneEditor::PhysicsPanel(){
        if (!m_ShowPhysics) { return; }
        ImGui::Begin("Physics");
        ScenePhysicsSettings& settings = m_ActiveScene->PhysicsSettings();
        PhysicsDebugSettings& debug = m_ActiveScene->PhysicsDebug();
        const bool running = m_ActiveScene->IsRunning();

        // Live stats while playing.
        if (PhysicsWorld* world = m_ActiveScene->GetPhysicsWorld()){
            ImGui::Text("Bodies: %u (%u awake)  Joints: %u", world->GetBodyCount(),
                world->GetActiveBodyCount(), world->GetJointCount());
            ImGui::Separator();
        }

        if (ImGui::CollapsingHeader("Debug Draw", ImGuiTreeNodeFlags_DefaultOpen)){
            ImGui::Checkbox("Show Colliders", &debug.showColliders);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Green static, blue dynamic (dim = sleeping), orange kinematic,\n"
                                  "yellow sensor, magenta character, cyan force field, red joints.");
            ImGui::BeginDisabled(!debug.showColliders);
            ImGui::Checkbox("Joints", &debug.showJoints); ImGui::SameLine();
            ImGui::Checkbox("Characters", &debug.showCharacters); ImGui::SameLine();
            ImGui::Checkbox("Force Fields", &debug.showForceFields);
            ImGui::Checkbox("Contact Points (playing)", &debug.showContacts);
            ImGui::EndDisabled();
        }

        if (ImGui::CollapsingHeader("World", ImGuiTreeNodeFlags_DefaultOpen)){
            ImGui::BeginDisabled(running);
            // Gravity and the step rate are the scene's simulation settings (also in
            // Settings > Simulation and the Python API).
            SimulationSettings& sim = m_ActiveScene->Simulation();
            ImGui::DragFloat3("Gravity", glm::value_ptr(sim.gravity), 0.05f, -100.0f, 100.0f, "%.2f");
            if (ImGui::Button("Earth")) { sim.gravity = { 0.0f, -9.81f, 0.0f }; } ImGui::SameLine();
            if (ImGui::Button("Moon"))  { sim.gravity = { 0.0f, -1.62f, 0.0f }; } ImGui::SameLine();
            if (ImGui::Button("Zero-G")) { sim.gravity = glm::vec3(0.0f); }
            ImGui::SliderInt("Velocity Iterations", &settings.velocitySteps, 2, 32);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Higher = stiffer stacks and chains, more CPU.");
            ImGui::SliderInt("Position Iterations", &settings.positionSteps, 1, 16);
            int hz = (int)std::lround(1.0f / std::max(1e-4f, sim.fixedTimestep));
            if (ImGui::SliderInt("Step Rate", &hz, 20, 240, "%d Hz"))
                sim.fixedTimestep = 1.0f / (float)std::max(1, hz);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Physics always runs in fixed steps: the same inputs give the same result at any frame rate.");
            ImGui::SliderFloat("Substep Rate", &settings.substepRate, 30.0f, 240.0f, "%.0f Hz");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Collision steps per second. Raise for fast, small objects.");
            ImGui::EndDisabled();
            if (running) { ImGui::TextDisabled("Applied on Play (stop to edit)."); }
        }

        if (ImGui::CollapsingHeader("Collision Layers")){
            ImGui::BeginDisabled(running);
            static int visibleLayers = 6;
            ImGui::SliderInt("Layers shown", &visibleLayers, 2, ScenePhysicsSettings::MaxLayers);
            for (int i = 0; i < visibleLayers; i++){
                char buf[64];
                std::strncpy(buf, settings.layerNames[i].c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = 0;
                ImGui::PushID(i);
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::InputText("##name", buf, sizeof(buf))) { settings.layerNames[i] = buf; }
                ImGui::SameLine();
                ImGui::Text("%d", i);
                ImGui::PopID();
            }
            // Symmetric matrix: tick = the two layers collide (upper triangle only).
            ImGui::TextDisabled("Collision matrix (row collides with column):");
            if (ImGui::BeginTable("##layerMatrix", visibleLayers + 1, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)){
                ImGui::TableSetupColumn("");
                for (int j = 0; j < visibleLayers; j++) { ImGui::TableSetupColumn(std::to_string(j).c_str()); }
                ImGui::TableHeadersRow();
                for (int i = 0; i < visibleLayers; i++){
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(settings.layerNames[i].c_str());
                    for (int j = 0; j < visibleLayers; j++){
                        ImGui::TableSetColumnIndex(j + 1);
                        if (j < i) { continue; }
                        ImGui::PushID(i * 64 + j);
                        bool collide = settings.LayersCollide(i, j);
                        if (ImGui::Checkbox("##c", &collide)) { settings.SetLayersCollide(i, j, collide); }
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
            ImGui::EndDisabled();
        }

        if (ImGui::CollapsingHeader("Selected Body") && m_SelectedEntity){
            Entity e = m_SelectedEntity;
            PhysicsWorld* world = m_ActiveScene->GetPhysicsWorld();
            uint32_t body = m_ActiveScene->GetEntityBody(e);
            if (!world || body == PhysicsWorld::InvalidBody){
                ImGui::TextDisabled("Play the scene and select a body to poke it.");
            }
            else{
                static float strength = 5.0f;
                ImGui::SliderFloat("Impulse (m/s)", &strength, 0.5f, 40.0f);
                float mass = world->GetMass(body);
                auto kick = [&](const glm::vec3& dir){ world->ApplyImpulse(body, dir * strength * mass); };
                if (ImGui::Button("Up")) { kick({ 0, 1, 0 }); } ImGui::SameLine();
                if (ImGui::Button("+X")) { kick({ 1, 0, 0 }); } ImGui::SameLine();
                if (ImGui::Button("-X")) { kick({ -1, 0, 0 }); } ImGui::SameLine();
                if (ImGui::Button("+Z")) { kick({ 0, 0, 1 }); } ImGui::SameLine();
                if (ImGui::Button("-Z")) { kick({ 0, 0, -1 }); } ImGui::SameLine();
                if (ImGui::Button("Spin")) { world->ApplyAngularImpulse(body, glm::vec3(0, strength * mass, 0)); }
                if (ImGui::Button("Stop")){
                    world->SetLinearVelocity(body, glm::vec3(0.0f));
                    world->SetAngularVelocity(body, glm::vec3(0.0f));
                }
            }
        }
        ImGui::End();
    }

    // ---- One-click physics objects (hierarchy right-click > Create Physics Object) ----

    static void Matte(Material& m, const glm::vec3& color){
        m.ambient = color * 0.5f;
        m.diffuse = color;
        m.specular = glm::vec3(0.2f);
        m.shininess = 16.0f;
    }

    // Spawn point: a few meters in front of the active camera, at least at y = 1.
    static glm::vec3 SpawnPoint(Scene& scene){
        glm::mat4 cameraWorld = glm::inverse(scene.GetActiveView());
        glm::vec3 pos = glm::vec3(cameraWorld[3]) - glm::vec3(cameraWorld[2]) * 8.0f;
        pos.y = std::max(pos.y, 1.0f);
        return pos;
    }

    void SceneEditor::CreatePhysicsObjectMenu(){
        if (!ImGui::BeginMenu("Physics Object")) { return; }
        Scene& scene = *m_ActiveScene;
        const glm::vec3 at = SpawnPoint(scene);

        auto shape = [&](const char* name, PrimitiveComponent::Type type, const glm::vec3& color, const glm::vec3& scale){
            Entity e = scene.CreateEntity(name);
            Matte(e.AddComponent<PrimitiveComponent>(type).material, color);
            auto& t = e.GetComponent<TransformComponent>();
            t.Translation = at;
            t.Scale = scale;
            return e;
        };

        if (ImGui::MenuItem("Dynamic Box")){
            Entity e = shape("Box", PrimitiveComponent::Type::Cube, { 0.75f, 0.55f, 0.3f }, glm::vec3(1.0f));
            e.AddComponent<RigidBodyComponent>().density = 700.0f;
            e.AddComponent<BoxColliderComponent>();
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Dynamic Sphere")){
            Entity e = shape("Ball", PrimitiveComponent::Type::Sphere, { 0.9f, 0.3f, 0.25f }, glm::vec3(1.0f));
            auto& rb = e.AddComponent<RigidBodyComponent>();
            rb.restitution = 0.5f;
            rb.angularDamping = 0.3f; // so it eventually stops rolling
            e.AddComponent<SphereColliderComponent>();
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Dynamic Cylinder (barrel)")){
            Entity e = shape("Barrel", PrimitiveComponent::Type::Cylinder, { 0.3f, 0.5f, 0.75f }, { 0.8f, 1.2f, 0.8f });
            e.AddComponent<RigidBodyComponent>().density = 400.0f;
            e.AddComponent<CylinderColliderComponent>();
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Box Stack (pyramid)")){
            int n = 0;
            for (int row = 0; row < 4; row++){
                for (int i = 0; i < 4 - row; i++){
                    Entity e = scene.CreateEntity("Stack Box " + std::to_string(++n));
                    Matte(e.AddComponent<PrimitiveComponent>(PrimitiveComponent::Type::Cube).material,
                          glm::vec3(0.7f, 0.5f, 0.3f) * (0.85f + 0.05f * row));
                    e.GetComponent<TransformComponent>().Translation =
                        { at.x - (3 - row) * 0.5f + i, 0.5f + row, at.z };
                    e.AddComponent<RigidBodyComponent>().density = 400.0f;
                    e.AddComponent<BoxColliderComponent>();
                }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Static Floor")){
            Entity e = shape("Floor", PrimitiveComponent::Type::Cube, { 0.42f, 0.44f, 0.48f }, { 40.0f, 0.5f, 40.0f });
            e.GetComponent<TransformComponent>().Translation = { at.x, -0.25f, at.z };
            auto& rb = e.AddComponent<RigidBodyComponent>();
            rb.type = RigidBodyComponent::Type::Static;
            rb.friction = 0.8f;
            e.AddComponent<BoxColliderComponent>();
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Static Ramp")){
            Entity e = shape("Ramp", PrimitiveComponent::Type::Cube, { 0.5f, 0.42f, 0.34f }, { 6.0f, 0.3f, 3.0f });
            auto& t = e.GetComponent<TransformComponent>();
            t.Translation.y = 1.3f;
            t.Rotation.z = glm::radians(-20.0f);
            auto& rb = e.AddComponent<RigidBodyComponent>();
            rb.type = RigidBodyComponent::Type::Static;
            e.AddComponent<BoxColliderComponent>();
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Moving Platform (kinematic)")){
            Entity e = shape("Moving Platform", PrimitiveComponent::Type::Cube, { 0.6f, 0.3f, 0.7f }, { 3.0f, 0.3f, 3.0f });
            e.AddComponent<RigidBodyComponent>().type = RigidBodyComponent::Type::Kinematic;
            e.AddComponent<BoxColliderComponent>();
            e.AddComponent<LuaScriptComponent>().scripts[0].source =
                "-- Kinematic platform: the script moves the Transform, the body follows\n"
                "-- and carries whatever stands on it.\n"
                "local t = 0\n"
                "local x0, y0, z0\n"
                "function OnUpdate(dt)\n"
                "    if not x0 then x0, y0, z0 = GetTranslation() end\n"
                "    t = t + dt\n"
                "    SetTranslation(x0 + 3 * math.sin(t * 0.8), y0, z0)\n"
                "end\n";
            m_SelectedEntity = e;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Trigger Zone")){
            Entity e = shape("Trigger Zone", PrimitiveComponent::Type::Cube, { 1.0f, 0.9f, 0.2f }, { 3.0f, 2.0f, 3.0f });
            e.AddComponent<WireframeComponent>();
            auto& rb = e.AddComponent<RigidBodyComponent>();
            rb.type = RigidBodyComponent::Type::Static;
            rb.isSensor = true;
            e.AddComponent<BoxColliderComponent>();
            e.AddComponent<LuaScriptComponent>().scripts[0].source =
                "-- Sensor: reports overlaps without blocking anything.\n"
                "function OnTriggerEnter(other)\n"
                "    SetColor(0.2, 1.0, 0.3)\n"
                "end\n"
                "function OnTriggerExit(other)\n"
                "    SetColor(1.0, 0.9, 0.2)\n"
                "end\n";
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Wind Zone (force field)")){
            Entity e = scene.CreateEntity("Wind Zone");
            e.GetComponent<TransformComponent>().Translation = at;
            auto& f = e.AddComponent<ForceFieldComponent>();
            f.halfExtents = { 2.0f, 3.0f, 2.0f };
            f.direction = { 0.0f, 1.0f, 0.0f };
            f.strength = 14.0f;
            f.drag = 0.5f;
            scene.PhysicsDebug().showColliders = true; // invisible otherwise
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Black Hole (radial field)")){
            Entity e = scene.CreateEntity("Black Hole");
            e.GetComponent<TransformComponent>().Translation = at;
            auto& f = e.AddComponent<ForceFieldComponent>();
            f.shape = ForceFieldComponent::Shape::Sphere;
            f.mode = ForceFieldComponent::Mode::Radial;
            f.halfExtents = glm::vec3(6.0f);
            f.strength = 40.0f;
            f.drag = 0.8f;
            scene.PhysicsDebug().showColliders = true;
            m_SelectedEntity = e;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Chain (6 links)")){
            const glm::vec3 top = at + glm::vec3(0.0f, 5.0f, 0.0f);
            std::string prev;
            for (int i = 0; i < 6; i++){
                std::string name = "Chain Link " + std::to_string(i + 1);
                Entity e = scene.CreateEntity(name);
                Matte(e.AddComponent<PrimitiveComponent>(PrimitiveComponent::Type::Sphere).material, glm::vec3(0.6f));
                auto& t = e.GetComponent<TransformComponent>();
                t.Translation = top - glm::vec3(0.0f, 0.6f * (i + 1), 0.0f);
                t.Scale = glm::vec3(0.45f);
                e.AddComponent<RigidBodyComponent>().density = 5000.0f;
                e.AddComponent<SphereColliderComponent>();
                auto& j = e.AddComponent<JointComponent>().Add();
                j.type = JointComponent::Type::Point;
                j.connectedTag = prev; // first link: world
                j.pivot = t.Translation + glm::vec3(0.0f, 0.3f, 0.0f);
                prev = name;
            }
        }
        if (ImGui::MenuItem("Hinged Door")){
            Entity e = shape("Door", PrimitiveComponent::Type::Cube, { 0.55f, 0.38f, 0.22f }, { 1.2f, 2.2f, 0.12f });
            auto& t = e.GetComponent<TransformComponent>();
            t.Translation.y = 1.15f;
            e.AddComponent<RigidBodyComponent>().density = 500.0f;
            e.AddComponent<BoxColliderComponent>();
            auto& j = e.AddComponent<JointComponent>().Add();
            j.type = JointComponent::Type::Hinge;
            j.pivot = t.Translation - glm::vec3(0.6f, 0.0f, 0.0f);
            j.axis = { 0.0f, 1.0f, 0.0f };
            j.friction = 5.0f; // swings, then settles
            m_SelectedEntity = e;
        }
        if (ImGui::MenuItem("Spinning Wheel (motor)")){
            Entity e = shape("Wheel", PrimitiveComponent::Type::Cylinder, { 0.25f, 0.25f, 0.3f }, { 2.0f, 0.3f, 2.0f });
            auto& t = e.GetComponent<TransformComponent>();
            t.Translation.y = 2.0f;
            t.Rotation.x = glm::radians(90.0f); // face along Z
            e.AddComponent<RigidBodyComponent>();
            e.AddComponent<CylinderColliderComponent>();
            auto& j = e.AddComponent<JointComponent>().Add();
            j.type = JointComponent::Type::Hinge;
            j.pivot = t.Translation;
            j.axis = { 0.0f, 0.0f, 1.0f };
            j.motor = JointComponent::Motor::Velocity;
            j.motorTarget = 2.0f;
            m_SelectedEntity = e;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Ragdoll Dummy")){
            int n = 1;
            while (scene.FindEntityByTag("Dummy " + std::to_string(n) + " Pelvis")) { n++; }
            glm::vec3 feet = at;
            feet.y = 0.0f;
            m_SelectedEntity = PhysicsPrefabs::CreateRagdollDummy(scene, "Dummy " + std::to_string(n), feet);
        }
        if (ImGui::MenuItem("Car (WASD)")){
            int n = 1;
            while (scene.FindEntityByTag("Car " + std::to_string(n))) { n++; }
            glm::vec3 ground = at;
            ground.y = 0.0f;
            m_SelectedEntity = PhysicsPrefabs::CreateCar(scene, "Car " + std::to_string(n), ground);
        }
        if (ImGui::MenuItem("Player Character")){
            Entity e = shape("Player", PrimitiveComponent::Type::Cylinder, { 0.2f, 0.7f, 0.9f }, { 0.7f, 1.8f, 0.7f });
            e.GetComponent<TransformComponent>().Translation.y = 1.2f;
            auto& cc = e.AddComponent<CharacterControllerComponent>();
            // Local units of the unit cylinder: radius 0.5 x 0.7 = 0.35 m, total height
            // 2 x (0.3 x 1.8 + 0.35) ~= 1.8 m, matching the visual.
            cc.radius = 0.5f;
            cc.halfHeight = 0.3f;
            cc.keyboardControl = true;
            m_SelectedEntity = e;
        }
        ImGui::EndMenu();
    }
}
