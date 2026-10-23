#pragma once
#include <Phoenix/core/base.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Phoenix{

    // Query filtering: skip one body (usually the caster) and/or limit to layers.
    // Sensors are never hit unless includeSensors is set. (Declared outside
    // PhysicsWorld so it can be used as a default argument inside it.)
    struct PhysicsQueryFilter{
        uint32_t ignoreBody = 0xffffffff; // PhysicsWorld::InvalidBody
        uint16_t layerMask = 0xffff;      // PhysicsWorld::AllLayers
        bool includeSensors = false;
    };

    // Thin wrapper around a Jolt PhysicsSystem. All Jolt types are hidden behind a
    // pimpl so the rest of the engine never includes Jolt headers.
    class PhysicsWorld{
    public:
        enum class BodyType { Static = 0, Dynamic = 1, Kinematic = 2 };

        static constexpr uint32_t InvalidBody = 0xffffffff;
        static constexpr uint32_t InvalidJoint = 0xffffffff;
        static constexpr uint32_t InvalidCharacter = 0xffffffff;

        // User collision layers. Each body is on one layer; layerMasks[i] bit j says
        // whether layer i collides with layer j (both directions must agree).
        static constexpr int MaxLayers = 16;
        static constexpr uint16_t AllLayers = 0xffff;

        // World-wide settings, fixed when the world is created.
        struct Settings{
            glm::vec3 gravity = { 0.0f, -9.81f, 0.0f };
            int velocitySteps = 10;   // solver iterations; raise for stiffer stacks/chains
            int positionSteps = 2;
            float maxSubstepRate = 60.0f; // collision steps per second (Step splits dt)
            uint16_t layerMasks[MaxLayers];
            Settings() { for (auto& m : layerMasks) { m = AllLayers; } }
        };

        // One collision shape of a body, in the body's local space. A body made of
        // several shapes (or one with an offset) becomes a compound shape.
        struct ShapeDesc{
            enum class Type { Box = 0, Sphere, Capsule, Cylinder, ConvexHull, TriangleMesh };
            Type type = Type::Box;
            glm::vec3 halfExtents = { 0.5f, 0.5f, 0.5f }; // box
            float radius = 0.5f;                          // sphere / capsule / cylinder
            float halfHeight = 0.5f;                      // capsule / cylinder (Y axis)
            glm::vec3 offset = { 0.0f, 0.0f, 0.0f };      // local position in the body
            glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };    // local XYZ Euler (radians)
            std::vector<glm::vec3> points;                // hull / mesh vertices
            std::vector<uint32_t> indices;                // mesh triangles (static bodies only)
        };

        // Surface/mass properties applied when a body is created.
        struct BodyProps{
            float friction = 0.5f;
            float restitution = 0.0f;        // bounciness (0 = dead, 1 = superball)
            float density = 1000.0f;         // kg/m^3 (mass follows from the shape volume)
            float mass = 0.0f;               // kg; > 0 overrides the density-derived mass
            glm::vec3 centerOfMassOffset = { 0.0f, 0.0f, 0.0f }; // shifts the COM (local)
            float linearDamping = 0.05f;
            float angularDamping = 0.05f;
            float gravityFactor = 1.0f;      // 0 floats, 1 normal, >1 sinks fast
            bool  continuousCollision = false; // for fast movers (bullets) so they don't tunnel
            bool  isSensor = false;          // detects contacts but has no collision response
            bool  allowSleeping = true;      // resting bodies stop simulating until touched
            bool  lockPosition[3] = { false, false, false }; // world X/Y/Z translation
            bool  lockRotation[3] = { false, false, false }; // world X/Y/Z rotation
            int   layer = 0;                 // collision layer (0..MaxLayers-1)
            glm::vec3 initialVelocity = { 0.0f, 0.0f, 0.0f };
            glm::vec3 initialAngularVelocity = { 0.0f, 0.0f, 0.0f };
        };

        // A begin/end contact between two bodies, drained once per step by the Scene.
        // Point/normal/speed are only filled for `entered` events. The normal points
        // from bodyA towards bodyB; speed is the approach speed along it (m/s).
        struct ContactEvent{
            uint32_t bodyA;
            uint32_t bodyB;
            bool entered; // true = contact added, false = contact removed
            bool sensor;  // one of the bodies is a sensor (trigger)
            glm::vec3 point = { 0.0f, 0.0f, 0.0f };
            glm::vec3 normal = { 0.0f, 0.0f, 0.0f };
            float speed = 0.0f;
        };

        // Result of a ray / shape cast.
        struct Hit{
            uint32_t body = InvalidBody;
            glm::vec3 point = { 0.0f, 0.0f, 0.0f };
            glm::vec3 normal = { 0.0f, 1.0f, 0.0f };
            float distance = 0.0f;
        };

        using QueryFilter = PhysicsQueryFilter;

        enum class JointType { Fixed = 0, Point = 1, Distance = 2, Hinge = 3, Slider = 4, Cone = 5 };
        enum class MotorMode { Off = 0, Velocity = 1, Position = 2 };

        // A constraint between two bodies. Anchors/axes are in WORLD space of the
        // starting layout; the joint keeps the bodies' current relative pose.
        struct JointDesc{
            JointType type = JointType::Point;
            glm::vec3 pointA = { 0.0f, 0.0f, 0.0f }; // pivot (and distance anchor on body A)
            glm::vec3 pointB = { 0.0f, 0.0f, 0.0f }; // distance anchor on body B
            glm::vec3 axis = { 0.0f, 0.0f, 1.0f };   // hinge / slider / cone twist axis
            // Hinge: angle limits (radians). Slider: position limits (m). Cone: maxLimit
            // is the half cone angle (radians).
            bool  limited = false;
            float minLimit = 0.0f;
            float maxLimit = 0.0f;
            // Distance: [min, max] range (-1 = starting distance) and an optional soft
            // spring on the limits (frequency 0 = rigid).
            float minDistance = -1.0f;
            float maxDistance = -1.0f;
            float springFrequency = 0.0f; // Hz
            float springDamping = 0.5f;   // 0 = bouncy .. 1 = critically damped
            // Hinge / slider motor: target velocity (rad/s, m/s) or position (rad, m).
            MotorMode motorMode = MotorMode::Off;
            float motorTarget = 0.0f;
            float motorMaxForce = 0.0f;   // N or N·m; 0 = unlimited
            float friction = 0.0f;        // hinge / slider resisting force/torque
            float breakForce = 0.0f;      // N; joint snaps above this (0 = unbreakable)
            bool  collideConnected = false; // let the two jointed bodies collide
        };

        // Kinematic capsule character (walks, climbs steps, slides off steep slopes,
        // pushes dynamic bodies). Moved by SetCharacterInput, updated inside Step.
        struct CharacterDesc{
            float radius = 0.3f;
            float halfHeight = 0.6f;     // cylindrical part; total = 2*(halfHeight+radius)
            float maxSlopeAngle = 0.87f; // radians (~50 deg)
            float stepHeight = 0.35f;    // stairs it can walk up (0 = off)
            float stickToFloor = 0.5f;   // snap down this far when walking down slopes
            float mass = 70.0f;          // kg, for pushing bodies
            float maxStrength = 100.0f;  // N, max push force
            float gravityFactor = 1.0f;
            float airControl = 0.3f;     // 0..1 how much input steers while airborne
            int   layer = 0;
        };

        explicit PhysicsWorld(const Settings& settings = Settings());
        ~PhysicsWorld();

        // ---- Bodies (returns a runtime body id; InvalidBody on failure) ----

        // Create a body from one or more shapes. Triangle meshes are only valid on
        // static bodies (they are turned into convex hulls otherwise).
        uint32_t CreateBody(const std::vector<ShapeDesc>& shapes, const glm::vec3& position,
                            const glm::vec3& rotationEuler, BodyType type, const BodyProps& props);

        // Removes and destroys a body, along with any joints attached to it.
        void RemoveBody(uint32_t bodyID);

        // ---- Joints (pass InvalidBody as bodyB to anchor to the world) ----

        uint32_t AddJoint(uint32_t bodyA, uint32_t bodyB, const JointDesc& desc);
        void RemoveJoint(uint32_t joint);
        bool IsJointAlive(uint32_t joint) const;
        // Hinge/slider motor control at runtime.
        void SetJointMotor(uint32_t joint, MotorMode mode, float target);
        // Hinge angle (rad) / slider position (m) / distance (m); 0 for other types.
        float GetJointValue(uint32_t joint) const;
        // Joints that snapped (break force exceeded) since the last call.
        std::vector<uint32_t> ConsumeBrokenJoints();

        // ---- Characters ----

        uint32_t CreateCharacter(const CharacterDesc& desc, const glm::vec3& position, float yaw);
        void RemoveCharacter(uint32_t character);
        // Desired horizontal velocity (m/s, world space; Y ignored) and a jump request
        // (applied only when grounded) for the next Step.
        void SetCharacterInput(uint32_t character, const glm::vec3& velocity, bool jump, float jumpSpeed);
        void SetCharacterPosition(uint32_t character, const glm::vec3& position);
        glm::vec3 GetCharacterPosition(uint32_t character) const;
        glm::vec3 GetCharacterVelocity(uint32_t character) const;
        bool IsCharacterGrounded(uint32_t character) const;
        glm::vec3 GetCharacterGroundNormal(uint32_t character) const;
        // The kinematic body that represents the character for raycasts/sensors/contacts.
        uint32_t GetCharacterBody(uint32_t character) const;

        // ---- Dynamics ----

        void ApplyImpulse(uint32_t bodyID, const glm::vec3& impulse);          // kg m/s, at center of mass
        void ApplyImpulseAtPoint(uint32_t bodyID, const glm::vec3& impulse, const glm::vec3& worldPoint);
        void ApplyAngularImpulse(uint32_t bodyID, const glm::vec3& impulse);   // kg m^2/s
        void ApplyForce(uint32_t bodyID, const glm::vec3& force);              // applied for the next step
        void ApplyForceAtPoint(uint32_t bodyID, const glm::vec3& force, const glm::vec3& worldPoint);
        void ApplyTorque(uint32_t bodyID, const glm::vec3& torque);            // applied for the next step
        void SetLinearVelocity(uint32_t bodyID, const glm::vec3& velocity);
        glm::vec3 GetLinearVelocity(uint32_t bodyID) const;
        void SetAngularVelocity(uint32_t bodyID, const glm::vec3& velocity);
        glm::vec3 GetAngularVelocity(uint32_t bodyID) const;
        float GetMass(uint32_t bodyID) const;                                  // 0 for static/kinematic
        void SetGravityFactor(uint32_t bodyID, float factor);
        void SetFriction(uint32_t bodyID, float friction);
        void SetRestitution(uint32_t bodyID, float restitution);
        // Teleport (no velocity change); wakes the body.
        void SetBodyTransform(uint32_t bodyID, const glm::vec3& position, const glm::vec3& rotationEuler);
        void WakeUp(uint32_t bodyID);
        bool IsAwake(uint32_t bodyID) const;
        bool IsSensor(uint32_t bodyID) const;

        // Move a kinematic body to the target pose over dt, computing the velocities
        // needed so it properly pushes dynamic bodies out of the way.
        void MoveKinematic(uint32_t bodyID, const glm::vec3& position, const glm::vec3& rotationEuler, float dt);

        // Apply one step of buoyancy + fluid drag to a body against a water surface
        // plane through surfacePoint with the given normal. buoyancy ~1 = neutral for
        // a body of the fluid's density; Jolt computes the submerged volume from the
        // shape. Returns true if the body intersected the fluid. Call before Step.
        bool ApplyBuoyancy(uint32_t bodyID, const glm::vec3& surfacePoint, const glm::vec3& surfaceNormal,
                           float buoyancy, float linearDrag, float angularDrag, float dt);

        // ---- Queries (direction need not be normalized) ----

        // First body hit along origin + normalize(direction) * [0, maxDistance].
        bool RayCast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                     Hit& outHit, const QueryFilter& filter = QueryFilter()) const;
        // Sweep a sphere along the ray; first body it touches.
        bool SphereCast(const glm::vec3& origin, const glm::vec3& direction, float radius, float maxDistance,
                        Hit& outHit, const QueryFilter& filter = QueryFilter()) const;
        // All bodies overlapping a sphere / an oriented box.
        std::vector<uint32_t> OverlapSphere(const glm::vec3& center, float radius,
                                            const QueryFilter& filter = QueryFilter()) const;
        std::vector<uint32_t> OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents,
                                         const glm::vec3& rotationEuler,
                                         const QueryFilter& filter = QueryFilter()) const;

        // Drain the contact begin/end events recorded since the last call (Jolt fires
        // them from worker threads during Step; read this only between steps).
        std::vector<ContactEvent> ConsumeContactEvents();

        // Call once after all bodies are created and before the first Step.
        void OptimizeBroadPhase();

        // Longest step simulated per frame; a longer frame (a hitch, a debugger break)
        // runs in slow motion instead of letting bodies tunnel or joints explode.
        static constexpr float MaxStep = 0.1f;

        // Advances characters and the simulation by dt (clamped to MaxStep), split into
        // collision steps of at most 1/maxSubstepRate s for stability.
        void Step(float dt);

        // Reads back a body's world transform. Rotation is XYZ Euler in radians; pass the
        // entity's current rotation in so the returned angles stay continuous with it.
        void GetBodyTransform(uint32_t bodyID, glm::vec3& outPosition, glm::vec3& inOutRotationEuler) const;

        const Settings& GetSettings() const;
        uint32_t GetBodyCount() const;
        uint32_t GetActiveBodyCount() const;
        uint32_t GetJointCount() const; // alive joints

        // Global Jolt setup/teardown. Init is idempotent and called lazily.
        static void Init();
        static void Shutdown();
    private:
        struct Impl;
        Scope<Impl> m_Impl;
    };
}
