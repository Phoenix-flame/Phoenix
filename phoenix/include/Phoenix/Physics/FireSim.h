#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace Phoenix{

    struct FireSettings{
        glm::ivec3 resolution = glm::ivec3(16, 32, 16);     // cells (x, y up, z)
        float cellSize = 0.025f;                            // metres
        glm::vec3 source = glm::vec3(0.2f, 0.075f, 0.2f);   // burning region centre (m, from the grid's min corner)
        glm::vec2 sourceRadius = glm::vec2(0.09f, 0.05f);   // horizontal, vertical (m)

        float fuel = 1.5f;          // fuel concentration fed into the source
        float exitSpeed = 0.5f;     // m/s, upward speed of the gas leaving the source
        float burnRate = 7.0f;      // share of the fuel burnt per second once ignited
        float heat = 2.2f;          // temperature gained per unit of fuel burnt
        float ignition = 0.25f;     // temperature at which fuel burns
        float cooling = 10.0f;      // radiative cooling (dT/dt = -cooling T^4)
        float buoyancy = 4.0f;      // m/s^2 of lift per unit of temperature
        float smoke = 0.35f;        // smoke made per unit of fuel burnt
        float smokeWeight = 0.5f;   // m/s^2 of sink per unit of smoke
        float smokeLife = 1.2f;     // seconds for smoke to thin out to a third
        float vorticity = 1.6f;     // vorticity confinement: keeps the small curls alive
        float turbulence = 4.0f;    // m/s^2 of noise force where the gas is hot
        float drag = 0.5f;          // 1/s, relaxation of the gas towards the wind
        int pressureIterations = 12;

        float emberRate = 0.0f;     // embers thrown per second
    };

    // A glowing ember: position (m, grid frame), velocity, temperature (1 = flame) and age.
    struct Ember{
        glm::vec3 position;
        glm::vec3 velocity;
        float temperature;
        float age;
        float life;
        float size;
    };

    // Fire and smoke on a small grid of gas (Nguyen, Fedkiw & Jensen 2002, in the form
    // of Fedkiw, Stam & Jensen's smoke): fuel is fed into a source region, burns once hot
    // enough, heats the gas and makes smoke; hot gas rises (buoyancy) and cools by
    // radiation (T^4). Velocities are kept divergence-free (Jacobi pressure solve), curls
    // are sustained by vorticity confinement, and the scalars are advected with
    // MacCormack for crisp flame edges. Every cell update is a pure function of the
    // previous pass, so the CPU path (all cores) and the OpenCL path give bit-identical
    // results. The grid is axis-aligned (flames rise along +Y whatever holds them).
    class FireSim{
    public:
        FireSim();
        ~FireSim();

        FireSim(const FireSim&) = delete;
        FireSim& operator=(const FireSim&) = delete;

        // Reallocates (and clears) when the resolution changes.
        void SetSettings(const FireSettings& settings);
        const FireSettings& GetSettings() const { return m_Settings; }
        void Clear();

        // One step of dt seconds. wind: velocity of the air around the fire relative to
        // the grid (m/s), e.g. minus the velocity of a torch being carried. burning:
        // whether the source is fed (an unfed fire dies down by itself).
        void Step(float dt, const glm::vec3& wind, bool burning);
        // Steps several fires at once: those on the device in turn, the rest spread over
        // the CPU cores (whole fires per core when there are several).
        struct StepRequest{ FireSim* sim; float dt; glm::vec3 wind; bool burning; };
        static void StepAll(const std::vector<StepRequest>& requests);

        glm::ivec3 Dims() const { return m_Dims; }
        float CellSize() const { return m_Settings.cellSize; }
        glm::vec3 Size() const { return glm::vec3(m_Dims) * m_Settings.cellSize; }
        int Cells() const { return m_Dims.x * m_Dims.y * m_Dims.z; }
        // Per cell: temperature, fuel, smoke, reaction rate (burnt fuel per second).
        const std::vector<glm::vec4>& Scalars() const { return m_Scalars; }
        const std::vector<glm::vec4>& Velocities() const { return m_Velocities; }
        // Trilinear lookups at a point (m, grid frame); zero outside.
        glm::vec4 SampleScalars(const glm::vec3& p) const;
        glm::vec3 SampleVelocity(const glm::vec3& p) const;

        // Radiated power (sum of T^4 over the flame, in cell volumes of m^3) and its
        // centroid (m, grid frame): what drives the fire's light and its flicker.
        float Power() const { return m_Power; }
        glm::vec3 PowerCentroid() const { return m_Centroid; }
        // Power-weighted mean temperature of the flame.
        float FlameTemperature() const { return m_FlameTemperature; }

        const std::vector<Ember>& Embers() const { return m_Embers; }
        // Shifts the embers (grid frame): when the grid follows a moving source, pass the
        // opposite of its move so they stay put in the world.
        void MoveEmbers(const glm::vec3& offset) { for (Ember& e : m_Embers) { e.position += offset; } }
        uint32_t StepCount() const { return m_Step; }

        // Run on the shared OpenCL device. Returns whether it is in use.
        bool UseOpenCL(bool enable);
        bool OnDevice() const { return m_Device != nullptr; }

        // Engine-wide switch (Settings > OpenCL): fires of at least minDeviceCells cells
        // step on the device.
        static void SetDefaultOpenCL(bool enable) { s_DefaultOpenCL = enable; }
        static bool DefaultOpenCL() { return s_DefaultOpenCL; }
        static int minDeviceCells;

    private:
        struct Device;
        void PackParams(float dt, const glm::vec3& wind, bool burning, std::vector<float>& out) const;
        void StepCPU(const std::vector<float>& params, bool threaded);
        void Finish(float dt, bool burning);
        bool StepDevice(const std::vector<float>& params);
        void Measure();
        void StepEmbers(float dt, bool burning);

        FireSettings m_Settings;
        glm::ivec3 m_Dims = glm::ivec3(0);
        uint32_t m_Step = 0;
        std::vector<glm::vec4> m_Velocities, m_Scalars;
        // CPU scratch: second velocity, curl (xyz, |curl|; then the departure points),
        // three scalar buffers, pressure.
        std::vector<glm::vec4> m_VelocitiesB, m_Curl, m_ScalarsA, m_ScalarsB, m_ScalarsC;
        std::vector<float> m_Divergence, m_Pressure, m_PressureB;

        float m_Power = 0.0f, m_FlameTemperature = 0.0f;
        glm::vec3 m_Centroid = glm::vec3(0.0f);
        std::vector<Ember> m_Embers;
        float m_EmberDebt = 0.0f;
        uint32_t m_Random = 0x9e3779b9u;

        std::unique_ptr<Device> m_Device;
        static bool s_DefaultOpenCL;
    };
}
