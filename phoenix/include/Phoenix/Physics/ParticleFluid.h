#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace Phoenix{

    // A solid the fluid flows around: a box, sphere, capsule or cylinder (the last two
    // along local Y), e.g. a physics body leaf mirrored from Jolt.
    struct FluidCollider{
        enum class Type { Box = 0, Sphere, Capsule, Cylinder };
        Type type = Type::Box;
        glm::mat4 inverse = glm::mat4(1.0f);    // world -> local (rigid)
        glm::vec3 size = glm::vec3(0.5f);       // Box: half extents. Sphere: x = radius.
                                                // Capsule/Cylinder: x = half height, y = radius.
        glm::vec3 center = glm::vec3(0.0f);     // bounding sphere (world)
        float radius = 1.0f;
        uint32_t body = 0xffffffff;             // physics body pushed back (dynamic only)
        bool dynamic = false;
    };

    struct FluidSettings{
        float spacing = 0.05f;          // particle spacing at rest (m); the kernel radius is twice it
        int iterations = 4;             // density solver iterations per step
        float viscosity = 0.02f;        // XSPH velocity smoothing (0..0.2)
        float cohesion = 0.1f;          // anti-clumping pressure between close particles
        float friction = 0.1f;          // share of sliding speed lost per step against colliders
        float surfaceTension = 1.0f;    // pull between particles just apart (m/s^2 per neighbour)
        float lifetime = 0.0f;          // seconds before a particle is removed (0 = forever)
        glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
        float killY = -100.0f;          // particles falling below this are removed
    };

    // A liquid as particles, solved with Position Based Fluids (Macklin & Mueller 2013):
    // each step predicts positions under gravity, then iteratively moves particles to
    // keep the density at rest (an incompressible liquid), pushes them out of colliders,
    // and derives velocities, smoothed by XSPH viscosity. Neighbours come from a stable
    // counting sort into a grid, so the order of every sum is fixed: the CPU path (run on
    // all cores) and the OpenCL path give bit-identical results.
    class ParticleFluid{
    public:
        explicit ParticleFluid(int maxParticles);
        ~ParticleFluid();

        ParticleFluid(const ParticleFluid&) = delete;
        ParticleFluid& operator=(const ParticleFluid&) = delete;

        void SetSettings(const FluidSettings& settings);
        const FluidSettings& GetSettings() const { return m_Settings; }

        int Count() const { return (int)m_Positions.size(); }
        int Capacity() const { return m_Max; }
        void Clear();
        // Returns false when the fluid is full.
        bool AddParticle(const glm::vec3& position, const glm::vec3& velocity);
        // Fills a box (centre, half extents) at the rest spacing; returns how many fit.
        int AddBlock(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& velocity);

        // One step of dt seconds. Pushes on dynamic colliders come back as impulses
        // (kg m/s, applied at `points`), one per entry of `bodies`.
        struct BodyImpulse{ uint32_t body; glm::vec3 impulse; glm::vec3 point; };
        void Step(float dt, const std::vector<FluidCollider>& colliders, std::vector<BodyImpulse>& outImpulses);

        // Removes the particles for which keep(i) is false (stable: the rest keep their order).
        template<typename Keep>
        void Filter(Keep keep){
            size_t out = 0;
            for (size_t i = 0; i < m_Positions.size(); i++){
                if (!keep((int)i)) { continue; }
                m_Positions[out] = m_Positions[i];
                m_Velocities[out] = m_Velocities[i];
                m_Ages[out] = m_Ages[i];
                out++;
            }
            m_Positions.resize(out);
            m_Velocities.resize(out);
            m_Ages.resize(out);
        }

        const std::vector<glm::vec4>& Positions() const { return m_Positions; }   // xyz, w unused
        const std::vector<glm::vec4>& Velocities() const { return m_Velocities; }
        float ParticleRadius() const { return m_Settings.spacing * 0.5f; }
        float ParticleMass() const;     // kg (1000 kg/m3 times a spacing^3 cube)

        // Run on the shared OpenCL device. Returns whether it is in use.
        bool UseOpenCL(bool enable);
        bool OnDevice() const { return m_Device != nullptr; }

        // Engine-wide switch (Settings > OpenCL): the scene steps fluids of at least
        // minDeviceParticles particles on the device.
        static void SetDefaultOpenCL(bool enable) { s_DefaultOpenCL = enable; }
        static bool DefaultOpenCL() { return s_DefaultOpenCL; }
        static int minDeviceParticles;

    private:
        struct Device;
        void Calibrate();
        void BuildGrid();
        void StepCPU(float dt, const std::vector<float>& colliders, int colliderCount);
        bool StepDevice(float dt, const std::vector<float>& colliders, int colliderCount);

        int m_Max;
        FluidSettings m_Settings;
        std::vector<glm::vec4> m_Positions, m_Velocities;
        std::vector<float> m_Ages;

        // Solver state (one entry per particle unless noted).
        std::vector<glm::vec4> m_Predicted, m_PredictedNext, m_VelocitiesNext;
        std::vector<float> m_Lambda;
        std::vector<int> m_Pushed;          // last collider touched this step (-1 none)
        std::vector<glm::vec4> m_Push;      // total push from colliders this step (xyz)
        // Grid: cell of each particle, particle indices sorted by cell, and per cell the
        // first sorted slot (cells + 1 entries).
        std::vector<int> m_CellOf, m_Sorted, m_CellStart;
        glm::vec3 m_GridOrigin = glm::vec3(0.0f);
        glm::ivec3 m_GridDims = glm::ivec3(1);
        float m_CellSize = 0.1f;

        // Kernel constants (set by Calibrate from the spacing).
        float m_H = 0.1f, m_Poly6 = 0.0f, m_Spiky = 0.0f, m_Scale = 0.0f, m_Epsilon = 0.0f;
        float m_CohesionScale = 0.0f, m_WDeltaQ = 1.0f;

        std::unique_ptr<Device> m_Device;
        static bool s_DefaultOpenCL;
    };
}
