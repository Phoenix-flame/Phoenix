#include "AIPrefabs.h"
#include <Phoenix/Scene/Component.h>

using namespace Phoenix;

namespace AIPrefabs{

    static void Matte(Material& m, const glm::vec3& color, float shininess = 16.0f){
        m.ambient = color * 0.5f;
        m.diffuse = color;
        m.specular = glm::vec3(0.2f);
        m.shininess = shininess;
    }

    Entity CreateAgent(Scene& scene, const glm::vec3& position, const char* name){
        Entity e = scene.CreateEntity(name);
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = position;
        t.Scale = { 0.8f, 0.8f, 0.8f };

        auto& shape = e.AddComponent<CubeComponent>();
        Matte(shape.material, { 0.95f, 0.55f, 0.2f }, 32.0f);
        shape.material.emissive = { 1.0f, 0.5f, 0.15f };
        shape.material.emissiveStrength = 0.35f;

        auto& rb = e.AddComponent<RigidBodyComponent>();
        rb.type = RigidBodyComponent::Type::Dynamic;
        rb.lockTilt = true;
        rb.friction = 0.2f;
        rb.linearDamping = 0.5f;
        e.AddComponent<BoxColliderComponent>();

        auto& agent = e.AddComponent<AgentComponent>();
        agent.policy = AgentComponent::Policy::External;
        agent.control = AgentComponent::Control::Move;
        e.AddComponent<RaySensorComponent>();

        auto& random = e.AddComponent<RandomizerComponent>();
        random.positionRange = { 0.0f, 0.0f, 0.0f };
        random.randomYaw = true;
        return e;
    }

    Entity CreateRollerAgent(Scene& scene, const glm::vec3& position, const char* name){
        Entity e = scene.CreateEntity(name);
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = position;
        t.Scale = glm::vec3(0.9f);

        auto& shape = e.AddComponent<PrimitiveComponent>(PrimitiveComponent::Type::Sphere);
        Matte(shape.material, { 0.25f, 0.6f, 1.0f }, 48.0f);
        shape.material.emissive = { 0.2f, 0.5f, 1.0f };
        shape.material.emissiveStrength = 0.4f;
        shape.material.reflectivity = 0.2f;

        auto& rb = e.AddComponent<RigidBodyComponent>();
        rb.type = RigidBodyComponent::Type::Dynamic;
        rb.friction = 0.6f;
        rb.angularDamping = 0.3f;
        rb.density = 40.0f; // ~15 kg: a light ball the agent's force can actually steer
        e.AddComponent<SphereColliderComponent>();

        auto& agent = e.AddComponent<AgentComponent>();
        agent.policy = AgentComponent::Policy::External;
        agent.control = AgentComponent::Control::Force;
        agent.actionType = AgentComponent::ActionType::Continuous;
        agent.observeHeading = false;
        agent.moveSpeed = 6.0f;
        agent.force = 30.0f;
        agent.decisionPeriod = 4;
        agent.maxSteps = 600;
        agent.approachReward = 0.2f;
        agent.fallHeight = -1.5f;
        return e;
    }

    Entity CreateGoal(Scene& scene, const glm::vec3& position){
        Entity e = scene.CreateEntity("Goal");
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = position;
        t.Scale = { 1.6f, 0.12f, 1.6f };
        auto& shape = e.AddComponent<PrimitiveComponent>(PrimitiveComponent::Type::Cylinder);
        Matte(shape.material, { 0.2f, 0.9f, 0.45f });
        shape.material.emissive = { 0.2f, 1.0f, 0.45f };
        shape.material.emissiveStrength = 2.0f;

        auto& zone = e.AddComponent<RewardZoneComponent>();
        zone.halfExtents = { 0.5f, 12.0f, 0.5f }; // y scaled by 0.12 -> about 1.4 m tall
        zone.reward = 1.0f;
        zone.endEpisode = true;
        return e;
    }

    Entity CreateHazard(Scene& scene, const glm::vec3& position, const glm::vec2& size){
        Entity e = scene.CreateEntity("Hazard");
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = position;
        t.Scale = { size.x, 0.06f, size.y };
        auto& shape = e.AddComponent<CubeComponent>();
        Matte(shape.material, { 0.85f, 0.15f, 0.15f });
        shape.material.emissive = { 1.0f, 0.1f, 0.1f };
        shape.material.emissiveStrength = 1.2f;

        auto& zone = e.AddComponent<RewardZoneComponent>();
        zone.halfExtents = { 0.5f, 20.0f, 0.5f };
        zone.reward = -1.0f;
        zone.endEpisode = true;
        return e;
    }

    Entity CreateWall(Scene& scene, const glm::vec3& position, const glm::vec3& size){
        Entity e = scene.CreateEntity("Wall");
        auto& t = e.GetComponent<TransformComponent>();
        t.Translation = position;
        t.Scale = size;
        auto& shape = e.AddComponent<CubeComponent>();
        Matte(shape.material, { 0.32f, 0.35f, 0.42f });
        auto& rb = e.AddComponent<RigidBodyComponent>();
        rb.type = RigidBodyComponent::Type::Static;
        e.AddComponent<BoxColliderComponent>();
        return e;
    }

    Entity CreateArena(Scene& scene, const glm::vec3& center, float size, const char* name){
        Entity floor = scene.CreateEntity(name);
        auto& t = floor.GetComponent<TransformComponent>();
        t.Translation = center + glm::vec3(0.0f, -0.25f, 0.0f);
        t.Scale = { size, 0.5f, size };
        auto& shape = floor.AddComponent<CubeComponent>();
        Matte(shape.material, { 0.36f, 0.38f, 0.43f });
        shape.material.reflectivity = 0.08f;
        auto& rb = floor.AddComponent<RigidBodyComponent>();
        rb.type = RigidBodyComponent::Type::Static;
        rb.friction = 0.8f;
        floor.AddComponent<BoxColliderComponent>();

        const float half = size * 0.5f, height = 1.2f, thick = 0.4f;
        CreateWall(scene, center + glm::vec3(0.0f, height * 0.5f, -half), { size + thick, height, thick });
        CreateWall(scene, center + glm::vec3(0.0f, height * 0.5f,  half), { size + thick, height, thick });
        CreateWall(scene, center + glm::vec3(-half, height * 0.5f, 0.0f), { thick, height, size + thick });
        CreateWall(scene, center + glm::vec3( half, height * 0.5f, 0.0f), { thick, height, size + thick });
        return floor;
    }
}
