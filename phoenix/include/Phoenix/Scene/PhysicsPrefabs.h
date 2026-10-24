#pragma once
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Entity.h>
#include <glm/glm.hpp>
#include <string>

namespace Phoenix::PhysicsPrefabs{

    // A ~1.8 m humanoid ragdoll made of primitive shapes (box pelvis and chest, sphere
    // head, cylinder limbs) joined by swing-twist joints, standing with its feet at
    // `feet`. It starts asleep, so it stands until something touches it. Parts are tagged "<name> Pelvis", "<name> Chest", ... (names must be
    // unique in the scene for the joints to find each other). Returns the pelvis.
    Entity CreateRagdollDummy(Scene& scene, const std::string& name, const glm::vec3& feet,
                              const glm::vec3& color = glm::vec3(0.85f, 0.65f, 0.45f));

    // A drivable car: a box chassis with a VehicleComponent and four wheel entities
    // (tagged "<name> Wheel FL/FR/RL/RR") resting on the ground at `ground`, facing -Z
    // rotated by `yaw`. Returns the chassis.
    Entity CreateCar(Scene& scene, const std::string& name, const glm::vec3& ground, float yaw = 0.0f,
                     const glm::vec3& color = glm::vec3(0.85f, 0.15f, 0.12f), bool keyboardControl = true);
}
