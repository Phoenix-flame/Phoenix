#pragma once
#include <Phoenix/core/base.h>
#include <glm/glm.hpp>
#include <cstdint>

namespace Phoenix{

    // Thin wrapper around a Jolt PhysicsSystem. All Jolt types are hidden behind a
    // pimpl so the rest of the engine never includes Jolt headers.
    class PhysicsWorld{
    public:
        enum class BodyType { Static = 0, Dynamic = 1, Kinematic = 2 };

        static constexpr uint32_t InvalidBody = 0xffffffff;

        PhysicsWorld();
        ~PhysicsWorld();

        // Create a box-shaped body. Returns a runtime body id (InvalidBody on failure).
        uint32_t CreateBox(const glm::vec3& position, const glm::vec3& rotationEuler,
                           const glm::vec3& halfExtents, BodyType type);

        void RemoveBody(uint32_t bodyID);

        // Call once after all bodies are created and before the first Step.
        void OptimizeBroadPhase();

        void Step(float dt);

        // Reads back a body's world transform. rotationEuler is in radians (XYZ).
        void GetBodyTransform(uint32_t bodyID, glm::vec3& outPosition, glm::vec3& outRotationEuler) const;

        // Global Jolt setup/teardown. Init is idempotent and called lazily.
        static void Init();
        static void Shutdown();
    private:
        struct Impl;
        Scope<Impl> m_Impl;
    };
}
