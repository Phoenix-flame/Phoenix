#pragma once
#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Entity.h>
#include <glm/glm.hpp>

// Ready-made building blocks for RL environments, shared by the hierarchy's
// Create > AI menu and the AI showcases. Tags matter: agents target the nearest
// "Goal" by default and their ray sensors detect "Goal", "Wall" and "Hazard".
namespace AIPrefabs{

    // Upright walker: box body (rotation locked to Y), Move control, 7-ray sensor,
    // Python (external) policy, randomized heading.
    Phoenix::Entity CreateAgent(Phoenix::Scene& scene, const glm::vec3& position, const char* name = "Agent");

    // Rolling ball pushed by forces (continuous actions), no ray sensor.
    Phoenix::Entity CreateRollerAgent(Phoenix::Scene& scene, const glm::vec3& position, const char* name = "Roller Agent");

    // Glowing pad with a +1 Reward Zone that ends the episode (tag "Goal").
    Phoenix::Entity CreateGoal(Phoenix::Scene& scene, const glm::vec3& position);

    // Red pad with a -1 Reward Zone that ends the episode (tag "Hazard").
    Phoenix::Entity CreateHazard(Phoenix::Scene& scene, const glm::vec3& position, const glm::vec2& size = { 2.0f, 2.0f });

    // Static wall (tag "Wall").
    Phoenix::Entity CreateWall(Phoenix::Scene& scene, const glm::vec3& position, const glm::vec3& size);

    // Floor + four walls centred on `center`. Returns the floor.
    Phoenix::Entity CreateArena(Phoenix::Scene& scene, const glm::vec3& center, float size, const char* name = "Arena");
}
