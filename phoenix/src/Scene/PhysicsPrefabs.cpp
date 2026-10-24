#include <Phoenix/Scene/PhysicsPrefabs.h>
#include <Phoenix/Scene/Component.h>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace Phoenix::PhysicsPrefabs{

    static void Matte(Material& m, const glm::vec3& color){
        m.ambient = color * 0.5f;
        m.diffuse = color;
        m.specular = glm::vec3(0.2f);
        m.shininess = 16.0f;
    }

    Entity CreateRagdollDummy(Scene& scene, const std::string& name, const glm::vec3& feet, const glm::vec3& color){
        struct PartDef{
            const char* part;
            PrimitiveComponent::Type shape;
            glm::vec3 center, scale;
            const char* parent;    // nullptr = root
            glm::vec3 pivot;       // joint position (relative to feet)
            glm::vec3 axis;        // twist axis (along the child segment)
            float swing1, swing2, twist; // degrees
        };
        const glm::vec3 down(0.0f, -1.0f, 0.0f), up(0.0f, 1.0f, 0.0f);
        const PartDef parts[] = {
            { "Pelvis",         PrimitiveComponent::Type::Cube,     { 0.0f, 1.00f, 0.0f }, { 0.36f, 0.22f, 0.20f }, nullptr,  {}, {}, 0, 0, 0 },
            { "Chest",          PrimitiveComponent::Type::Cube,     { 0.0f, 1.36f, 0.0f }, { 0.42f, 0.46f, 0.24f }, "Pelvis", { 0.0f, 1.12f, 0.0f }, up, 20, 20, 15 },
            { "Head",           PrimitiveComponent::Type::Sphere,   { 0.0f, 1.76f, 0.0f }, glm::vec3(0.26f),        "Chest",  { 0.0f, 1.61f, 0.0f }, up, 40, 40, 45 },
            { "Upper Arm L",    PrimitiveComponent::Type::Cylinder, { -0.28f, 1.39f, 0.0f }, { 0.10f, 0.30f, 0.10f }, "Chest", { -0.28f, 1.54f, 0.0f }, down, 75, 75, 35 },
            { "Lower Arm L",    PrimitiveComponent::Type::Cylinder, { -0.28f, 1.09f, 0.0f }, { 0.09f, 0.28f, 0.09f }, "Upper Arm L", { -0.28f, 1.24f, 0.0f }, down, 65, 10, 30 },
            { "Upper Arm R",    PrimitiveComponent::Type::Cylinder, { 0.28f, 1.39f, 0.0f }, { 0.10f, 0.30f, 0.10f }, "Chest", { 0.28f, 1.54f, 0.0f }, down, 75, 75, 35 },
            { "Lower Arm R",    PrimitiveComponent::Type::Cylinder, { 0.28f, 1.09f, 0.0f }, { 0.09f, 0.28f, 0.09f }, "Upper Arm R", { 0.28f, 1.24f, 0.0f }, down, 65, 10, 30 },
            { "Upper Leg L",    PrimitiveComponent::Type::Cylinder, { -0.10f, 0.70f, 0.0f }, { 0.13f, 0.40f, 0.13f }, "Pelvis", { -0.10f, 0.90f, 0.0f }, down, 60, 30, 10 },
            { "Lower Leg L",    PrimitiveComponent::Type::Cylinder, { -0.10f, 0.28f, 0.0f }, { 0.11f, 0.42f, 0.11f }, "Upper Leg L", { -0.10f, 0.49f, 0.0f }, down, 60, 5, 5 },
            { "Upper Leg R",    PrimitiveComponent::Type::Cylinder, { 0.10f, 0.70f, 0.0f }, { 0.13f, 0.40f, 0.13f }, "Pelvis", { 0.10f, 0.90f, 0.0f }, down, 60, 30, 10 },
            { "Lower Leg R",    PrimitiveComponent::Type::Cylinder, { 0.10f, 0.28f, 0.0f }, { 0.11f, 0.42f, 0.11f }, "Upper Leg R", { 0.10f, 0.49f, 0.0f }, down, 60, 5, 5 },
        };

        Entity pelvis;
        for (const auto& def : parts){
            Entity e = scene.CreateEntity(name + " " + def.part);
            auto& p = e.AddComponent<PrimitiveComponent>(def.shape);
            Matte(p.material, def.shape == PrimitiveComponent::Type::Cube ? color * 0.75f : color);
            auto& t = e.GetComponent<TransformComponent>();
            t.Translation = feet + def.center;
            t.Scale = def.scale;
            auto& rb = e.AddComponent<RigidBodyComponent>();
            rb.density = 985.0f;
            rb.friction = 0.7f;
            rb.angularDamping = 0.6f;
            rb.startAsleep = true; // stands until something hits it
            switch (def.shape){
                case PrimitiveComponent::Type::Cube:     e.AddComponent<BoxColliderComponent>(); break;
                case PrimitiveComponent::Type::Sphere:   e.AddComponent<SphereColliderComponent>(); break;
                default:                                 e.AddComponent<CylinderColliderComponent>(); break;
            }
            if (def.parent){
                auto& j = e.AddComponent<JointComponent>().Add(JointComponent::Type::SwingTwist);
                j.connectedTag = name + " " + def.parent;
                j.pivot = feet + def.pivot;
                j.axis = def.axis;
                j.planeAxis = { 1.0f, 0.0f, 0.0f };
                j.swingAngle1 = glm::radians(def.swing1);
                j.swingAngle2 = glm::radians(def.swing2);
                j.twistMin = -glm::radians(def.twist);
                j.twistMax = glm::radians(def.twist);
                j.friction = 1.5f; // damps flailing
            }
            else{
                pelvis = e;
            }
        }
        return pelvis;
    }

    Entity CreateCar(Scene& scene, const std::string& name, const glm::vec3& ground, float yaw,
                     const glm::vec3& color, bool keyboardControl){
        const float c = std::cos(yaw), s = std::sin(yaw);
        auto place = [&](const glm::vec3& local){ // car-local -> world (yaw about Y)
            return ground + glm::vec3(c * local.x + s * local.z, local.y, -s * local.x + c * local.z);
        };

        Entity chassis = scene.CreateEntity(name);
        auto& p = chassis.AddComponent<PrimitiveComponent>(PrimitiveComponent::Type::Cube);
        Matte(p.material, color);
        p.material.specular = glm::vec3(0.8f);
        p.material.shininess = 64.0f;
        p.material.reflectivity = 0.25f;
        auto& t = chassis.GetComponent<TransformComponent>();
        t.Translation = place({ 0.0f, 0.95f, 0.0f });
        t.Rotation = { 0.0f, yaw, 0.0f };
        t.Scale = { 1.8f, 0.6f, 4.0f };
        auto& rb = chassis.AddComponent<RigidBodyComponent>();
        rb.mass = 1200.0f;
        rb.centerOfMassOffset = { 0.0f, -0.35f, 0.0f }; // low: hard to roll over
        rb.friction = 0.5f;
        rb.allowSleeping = false;
        chassis.AddComponent<BoxColliderComponent>();

        auto& v = chassis.AddComponent<VehicleComponent>();
        v.keyboardControl = keyboardControl;
        const char* corner[4] = { "FL", "FR", "RL", "RR" };
        for (int i = 0; i < 4; i++){
            v.wheelTags[i] = name + " Wheel " + corner[i];
            Entity wheel = scene.CreateEntity(v.wheelTags[i]);
            auto& wp = wheel.AddComponent<PrimitiveComponent>(PrimitiveComponent::Type::Cylinder);
            Matte(wp.material, glm::vec3(0.08f));
            auto& wt = wheel.GetComponent<TransformComponent>();
            float x = (i % 2 == 0) ? -v.halfTrack : v.halfTrack;
            float z = i < 2 ? v.frontAxle : v.rearAxle;
            wt.Translation = place({ x, v.wheelRadius, z });
            wt.Rotation = { 0.0f, yaw, glm::half_pi<float>() }; // axle along X
            wt.Scale = { 2.0f * v.wheelRadius, v.wheelWidth, 2.0f * v.wheelRadius };
        }
        return chassis;
    }
}
