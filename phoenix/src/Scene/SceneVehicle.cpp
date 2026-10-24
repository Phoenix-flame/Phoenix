#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Entity.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/Physics/Physics.h>
#include <Phoenix/Math/Math.h>
#include <Phoenix/core/Input.h>
#include <Phoenix/core/keyCodes.h>
#include <Phoenix/core/log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Phoenix{

    // Vehicles: a VehicleComponent turns its entity's Dynamic rigid body into a
    // Jolt wheeled vehicle when the simulation starts.
    void Scene::CreateVehicles(){
        auto view = m_Registry.view<VehicleComponent, RigidBodyComponent>();
        for (auto entity : view){
            auto& v = view.get<VehicleComponent>(entity);
            auto& rb = view.get<RigidBodyComponent>(entity);
            v.runtimeVehicleID = PhysicsWorld::InvalidVehicle;
            if (rb.runtimeBodyID == PhysicsWorld::InvalidBody || rb.type != RigidBodyComponent::Type::Dynamic){
                PHX_CORE_WARN("Vehicle '{0}' needs a Dynamic Rigid Body with a collider",
                    m_Registry.any_of<TagComponent>(entity) ? m_Registry.get<TagComponent>(entity).Tag : "?");
                continue;
            }
            PhysicsWorld::VehicleDesc d;
            const float x = v.halfTrack, y = v.wheelHeight;
            d.wheelPositions[0] = { -x, y, v.frontAxle };
            d.wheelPositions[1] = {  x, y, v.frontAxle };
            d.wheelPositions[2] = { -x, y, v.rearAxle };
            d.wheelPositions[3] = {  x, y, v.rearAxle };
            d.wheelRadius = v.wheelRadius;
            d.wheelWidth = v.wheelWidth;
            d.suspensionMinLength = v.suspensionMin;
            d.suspensionMaxLength = v.suspensionMax;
            d.suspensionFrequency = v.suspensionFrequency;
            d.suspensionDamping = v.suspensionDamping;
            d.maxSteerAngle = v.maxSteerAngle;
            d.engineTorque = v.engineTorque;
            d.maxRPM = v.maxRPM;
            d.brakeTorque = v.brakeTorque;
            d.handBrakeTorque = v.handBrakeTorque;
            d.drive = (int)v.drive;
            d.antiRollBars = v.antiRollBars;
            v.runtimeVehicleID = m_PhysicsWorld->CreateVehicle(rb.runtimeBodyID, d);
            v.forward = v.right = v.brake = v.handBrake = 0.0f;
        }
    }

    // Driver input for this frame: keyboard (when enabled) or whatever Lua set.
    void Scene::UpdateVehiclesPreStep(){
        auto view = m_Registry.view<VehicleComponent>();
        for (auto entity : view){
            auto& v = view.get<VehicleComponent>(entity);
            if (v.runtimeVehicleID == PhysicsWorld::InvalidVehicle) { continue; }
            if (v.keyboardControl){
                float forward = 0.0f, right = 0.0f;
                if (Input::IsKeyPressed(Key::W) || Input::IsKeyPressed(Key::Up))    { forward += 1.0f; }
                if (Input::IsKeyPressed(Key::S) || Input::IsKeyPressed(Key::Down))  { forward -= 1.0f; }
                if (Input::IsKeyPressed(Key::D) || Input::IsKeyPressed(Key::Right)) { right += 1.0f; }
                if (Input::IsKeyPressed(Key::A) || Input::IsKeyPressed(Key::Left))  { right -= 1.0f; }
                v.forward = forward;
                v.right = right;
                v.brake = 0.0f;
                v.handBrake = Input::IsKeyPressed(Key::Space) ? 1.0f : 0.0f;
            }
            // Pressing against the direction of travel brakes first, then reverses /
            // accelerates once (nearly) stopped, like an arcade automatic.
            float forward = v.forward, brake = v.brake;
            float speed = m_PhysicsWorld->GetVehicleSpeed(v.runtimeVehicleID);
            if ((forward > 0.0f && speed < -1.0f) || (forward < 0.0f && speed > 1.0f)){
                brake = std::max(brake, std::abs(forward));
                forward = 0.0f;
            }
            m_PhysicsWorld->SetVehicleInput(v.runtimeVehicleID, forward, v.right, brake, v.handBrake);
        }
    }

    // Readouts and wheel visuals. Wheel poses are taken relative to the chassis's
    // latest simulated pose and re-applied to its (interpolated) rendered pose, so
    // the wheels stay attached between fixed steps.
    void Scene::UpdateVehiclesPostStep(){
        auto view = m_Registry.view<VehicleComponent, RigidBodyComponent, TransformComponent>();
        for (auto entity : view){
            auto& v = view.get<VehicleComponent>(entity);
            if (v.runtimeVehicleID == PhysicsWorld::InvalidVehicle) { continue; }
            auto& rb = view.get<RigidBodyComponent>(entity);
            auto& t = view.get<TransformComponent>(entity);
            v.speed = m_PhysicsWorld->GetVehicleSpeed(v.runtimeVehicleID);
            v.rpm = m_PhysicsWorld->GetVehicleRPM(v.runtimeVehicleID);
            v.gear = m_PhysicsWorld->GetVehicleGear(v.runtimeVehicleID);

            glm::mat4 simulated = glm::translate(glm::mat4(1.0f), rb.currPosition) * glm::mat4_cast(rb.currRotation);
            glm::mat4 rendered = glm::translate(glm::mat4(1.0f), t.Translation) * glm::mat4_cast(Math::EulerXYZToQuat(t.Rotation));
            glm::mat4 correction = rendered * glm::inverse(simulated);
            for (int i = 0; i < 4; i++){
                if (v.wheelTags[i].empty()) { continue; }
                Entity wheel = FindEntityByTag(v.wheelTags[i]);
                if (!wheel || (entt::entity)wheel == entity) { continue; }
                glm::vec3 position; glm::quat rotation;
                if (!m_PhysicsWorld->GetWheelPose(v.runtimeVehicleID, i, position, rotation)) { continue; }
                glm::mat4 world = correction * glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation);
                auto& wt = wheel.GetComponent<TransformComponent>();
                wt.Translation = glm::vec3(world[3]);
                wt.Rotation = Math::QuatToEulerXYZ(glm::quat_cast(glm::mat3(world)), wt.Rotation);
            }
        }
    }
}
