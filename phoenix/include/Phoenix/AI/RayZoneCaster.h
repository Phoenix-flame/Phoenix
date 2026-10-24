#pragma once
#include <glm/glm.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Phoenix{

    // Batched Ray Sensor tests against Reward Zones: every ray of every agent observing
    // this step, against every ray-visible zone, in one call. Runs on the CPU, or on an
    // OpenCL device when the engine is built with PHOENIX_OPENCL and one is found.
    // Physics bodies are not involved (Jolt casts those); callers keep the nearer hit.
    class RayZoneCaster{
    public:
        struct Zone{
            glm::mat4 inverse;          // world -> zone local (inverse of its transform)
            glm::vec3 halfExtents;
        };
        struct Ray{
            glm::vec3 origin;
            glm::vec3 delta;            // direction * length; hits are fractions of it
            int ignoreZone = -1;        // index of a zone this ray never sees (the agent's own)
        };
        struct Hit{
            float t = 1.0f;             // entry fraction (0 when starting inside)
            int zone = -1;              // nearest zone hit (first in order on ties), -1 none
        };

        RayZoneCaster();
        ~RayZoneCaster();

        // Switch the OpenCL backend on or off. Returns whether it is in use: false when
        // the engine was built without OpenCL or no device/kernel could be set up.
        bool UseOpenCL(bool enable);
        bool UsingOpenCL() const;
        static bool OpenCLCompiled();
        // Device name, or why OpenCL is unavailable (for the log and the editor).
        const std::string& Status() const { return m_Status; }

        // Batches with fewer ray-zone pairs than this stay on the CPU, where they are
        // cheaper than the upload and kernel launch.
        size_t minDeviceWork = 16384;

        // out[i] is the nearest zone hit of rays[i].
        void Cast(const std::vector<Zone>& zones, const std::vector<Ray>& rays, std::vector<Hit>& out);
        static void CastCPU(const std::vector<Zone>& zones, const std::vector<Ray>& rays, std::vector<Hit>& out);
        // Which backend ran the last Cast (true = OpenCL).
        bool LastCastOnDevice() const { return m_LastOnDevice; }

    private:
        struct Device;
        std::unique_ptr<Device> m_Device;
        std::string m_Status;
        bool m_LastOnDevice = false;
    };
}
