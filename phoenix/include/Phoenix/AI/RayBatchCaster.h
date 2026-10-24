#pragma once
#include <glm/glm.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Phoenix{

    // Batched ray casts for Ray Sensors: every ray of every agent observing this step,
    // against a list of shapes (Reward Zones, or physics bodies mirrored from Jolt), in
    // one call. Runs on the CPU, or on an OpenCL device when the engine is built with
    // PHOENIX_OPENCL and one is found. Both run the same per-shape code, so they agree
    // bit for bit (on devices with correctly rounded division and square root).
    class RayBatchCaster{
    public:
        struct Shape{
            // Zone: the Reward Zone test (entry fraction clamped to [0, 1]).
            // Box/Sphere/Capsule/Cylinder: Jolt's ray tests for those shapes.
            enum class Type { Zone = 0, Box, Sphere, Capsule, Cylinder };
            Type type = Type::Zone;
            glm::mat4 inverse = glm::mat4(1.0f);   // world -> shape local
            // Zone/Box: half extents. Sphere: x = radius.
            // Capsule/Cylinder (along local Y): x = half height, y = radius.
            glm::vec3 size = glm::vec3(0.5f);
            int owner = -1;                        // rays whose `ignore` matches skip it
            // World bounding sphere, for culling against ray groups (default: never culled).
            glm::vec3 center = glm::vec3(0.0f);
            float radius = 3.0e38f;
        };
        struct Ray{
            glm::vec3 origin;
            glm::vec3 delta;            // direction * length; hits are fractions of it
            int ignore = -1;            // skip shapes with this owner (the caster itself)
        };
        // Consecutive rays that are close together (one agent's fan). Each group only
        // tests the shapes whose bounding sphere touches the box around its rays, in
        // their original order, so grouping changes the cost but never the hits.
        struct Group{
            int firstRay = 0;
            int rayCount = 0;
        };
        struct Hit{
            float t = 1.0f;             // fraction along delta (0 when starting inside)
            int shape = -1;             // nearest shape hit (first in order on ties), -1 none
        };

        RayBatchCaster();
        ~RayBatchCaster();

        // Switch the OpenCL backend on or off. Returns whether it is in use: false when
        // the engine was built without OpenCL or no device/kernel could be set up.
        bool UseOpenCL(bool enable);
        bool UsingOpenCL() const;
        static bool OpenCLCompiled();
        // Device name, or why OpenCL is unavailable (for the log and the editor).
        const std::string& Status() const { return m_Status; }

        // Batches with fewer ray-shape tests (after culling) than this stay on the CPU,
        // where they are cheaper than the upload and kernel launch.
        size_t minDeviceWork = 16384;

        // out[i] is the nearest shape rays[i] hits with t < maxT (else t = maxT, shape -1).
        // Groups must cover every ray once; none means one group of all rays (no culling).
        void Cast(const std::vector<Shape>& shapes, const std::vector<Ray>& rays, const std::vector<Group>& groups,
                  float maxT, std::vector<Hit>& out);
        void CastCPU(const std::vector<Shape>& shapes, const std::vector<Ray>& rays, const std::vector<Group>& groups,
                     float maxT, std::vector<Hit>& out);
        // Which backend ran the last Cast (true = OpenCL), and how many ray-shape tests
        // it made after culling.
        bool LastCastOnDevice() const { return m_LastOnDevice; }
        size_t LastTests() const { return m_LastTests; }

    private:
        struct Device;
        struct Batch;
        // Packs shapes and rays, and culls each group's candidate shapes.
        void Prepare(const std::vector<Shape>& shapes, const std::vector<Ray>& rays, const std::vector<Group>& groups);
        void RunCPU(size_t rayCount, float maxT, std::vector<Hit>& out);

        std::unique_ptr<Device> m_Device;
        std::unique_ptr<Batch> m_Batch;
        std::string m_Status;
        bool m_LastOnDevice = false;
        size_t m_LastTests = 0;
    };
}
