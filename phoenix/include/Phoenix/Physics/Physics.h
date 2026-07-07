#pragma once
#include <Phoenix/core/base.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Phoenix{

    // Thin wrapper around a Jolt PhysicsSystem. All Jolt types are hidden behind a
    // pimpl so the rest of the engine never includes Jolt headers.
    class PhysicsWorld{
    public:
        enum class BodyType { Static = 0, Dynamic = 1, Kinematic = 2 };

        static constexpr uint32_t InvalidBody = 0xffffffff;

        // Surface/mass properties applied when a body is created.
        struct BodyProps{
            float friction = 0.5f;
            float restitution = 0.0f;        // bounciness (0 = dead, 1 = superball)
            float density = 1000.0f;         // kg/m^3 (mass follows from the shape volume)
            float linearDamping = 0.05f;
            float angularDamping = 0.05f;
            float gravityFactor = 1.0f;      // 0 floats, 1 normal, >1 sinks fast
            bool  continuousCollision = false; // for fast movers (bullets) so they don't tunnel
            bool  isSensor = false;          // detects contacts but has no collision response
            glm::vec3 initialVelocity = { 0.0f, 0.0f, 0.0f };
        };

        // A begin/end contact between two bodies, drained once per step by the Scene.
        struct ContactEvent{
            uint32_t bodyA;
            uint32_t bodyB;
            bool entered; // true = contact added, false = contact removed
        };

        PhysicsWorld();
        ~PhysicsWorld();

        // ---- Body creation (returns a runtime body id; InvalidBody on failure) ----

        uint32_t CreateBox(const glm::vec3& position, const glm::vec3& rotationEuler,
                           const glm::vec3& halfExtents, BodyType type, const BodyProps& props);

        uint32_t CreateSphere(const glm::vec3& position, const glm::vec3& rotationEuler,
                              float radius, BodyType type, const BodyProps& props);

        // Total height = 2 * (halfHeight + radius); halfHeight is the cylindrical part.
        uint32_t CreateCapsule(const glm::vec3& position, const glm::vec3& rotationEuler,
                               float halfHeight, float radius, BodyType type, const BodyProps& props);

        uint32_t CreateCylinder(const glm::vec3& position, const glm::vec3& rotationEuler,
                                float halfHeight, float radius, BodyType type, const BodyProps& props);

        // Create a convex-hull body from a point cloud (works for any body type).
        uint32_t CreateConvexHull(const std::vector<glm::vec3>& points,
                                  const glm::vec3& position, const glm::vec3& rotationEuler,
                                  BodyType type, const BodyProps& props);

        // Create a STATIC triangle-mesh body (accurate concave collision; static only).
        uint32_t CreateMesh(const std::vector<glm::vec3>& points, const std::vector<uint32_t>& indices,
                            const glm::vec3& position, const glm::vec3& rotationEuler,
                            const BodyProps& props);

        void RemoveBody(uint32_t bodyID);

        // ---- Constraints (pass InvalidBody as `other` to anchor to the world) ----
        // Constraints live as long as this world (they are torn down with the run).

        // Ball-and-socket: the two bodies stay pinned together at a world-space point.
        void AddPointConstraint(uint32_t body, uint32_t other, const glm::vec3& worldPoint);

        // Keeps the distance between two world-space attachment points within
        // [minDistance, maxDistance]. Pass -1 for either to use the starting distance.
        void AddDistanceConstraint(uint32_t body, uint32_t other,
                                   const glm::vec3& worldPointOnBody, const glm::vec3& worldPointOnOther,
                                   float minDistance = -1.0f, float maxDistance = -1.0f);

        // Rotation around a world-space axis through a world-space pivot (doors,
        // seesaws). Optional angle limits in radians around the resting pose.
        void AddHingeConstraint(uint32_t body, uint32_t other,
                                const glm::vec3& worldPivot, const glm::vec3& worldAxis,
                                bool limited = false, float minAngle = 0.0f, float maxAngle = 0.0f);

        // ---- Dynamics ----

        void ApplyImpulse(uint32_t bodyID, const glm::vec3& impulse);          // kg m/s, at center of mass
        void ApplyForce(uint32_t bodyID, const glm::vec3& force);              // applied for the next step
        void SetLinearVelocity(uint32_t bodyID, const glm::vec3& velocity);
        glm::vec3 GetLinearVelocity(uint32_t bodyID) const;
        void SetAngularVelocity(uint32_t bodyID, const glm::vec3& velocity);

        // Move a kinematic body to the target pose over dt, computing the velocities
        // needed so it properly pushes dynamic bodies out of the way.
        void MoveKinematic(uint32_t bodyID, const glm::vec3& position, const glm::vec3& rotationEuler, float dt);

        // First body hit along origin + direction * [0, maxDistance], or false.
        bool RayCast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                     uint32_t& outBody, glm::vec3& outPoint) const;

        // Drain the contact begin/end events recorded since the last call (Jolt fires
        // them from worker threads during Step; read this only between steps).
        std::vector<ContactEvent> ConsumeContactEvents();

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
