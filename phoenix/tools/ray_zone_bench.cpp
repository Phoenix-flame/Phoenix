// Checks the OpenCL ray sensor backend against the CPU one and times both.
//   ray_zone_bench [agents=64] [rays=32] [zones=256] [iterations=50]
// Agents and zones are spread over a grid of arenas, like parallel training arenas.
#include <Phoenix/AI/RayZoneCaster.h>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace Phoenix;

int main(int argc, char** argv){
    const int agents = argc > 1 ? std::atoi(argv[1]) : 64;
    const int raysPerAgent = argc > 2 ? std::atoi(argv[2]) : 32;
    const int zoneCount = argc > 3 ? std::atoi(argv[3]) : 256;
    const int iterations = argc > 4 ? std::atoi(argv[4]) : 50;

    std::mt19937 rng(7);
    auto uniform = [&](float a, float b){ return std::uniform_real_distribution<float>(a, b)(rng); };
    const int side = (int)std::ceil(std::sqrt((float)agents));
    const float arena = 20.0f;

    std::vector<RayZoneCaster::Zone> zones;
    for (int z = 0; z < zoneCount; z++){
        int cell = z % (side * side);
        glm::vec3 center((cell % side) * arena + uniform(-8, 8), uniform(0, 1), (cell / side) * arena + uniform(-8, 8));
        glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
        model = glm::rotate(model, uniform(0, 6.283f), glm::vec3(0, 1, 0));
        model = glm::scale(model, glm::vec3(uniform(0.5f, 2.0f), 1.0f, uniform(0.5f, 2.0f)));
        zones.push_back({ glm::inverse(model), glm::vec3(uniform(0.3f, 1.5f), 1.0f, uniform(0.3f, 1.5f)) });
    }
    std::vector<RayZoneCaster::Ray> rays;
    for (int a = 0; a < agents; a++){
        glm::vec3 origin((a % side) * arena + uniform(-8, 8), 0.5f, (a / side) * arena + uniform(-8, 8));
        float yaw = uniform(0, 6.283f);
        for (int r = 0; r < raysPerAgent; r++){
            float angle = yaw + 1.2f - 2.4f * (float)r / (float)std::max(1, raysPerAgent - 1);
            rays.push_back({ origin, glm::vec3(-std::sin(angle), 0.0f, -std::cos(angle)) * 12.0f, -1 });
        }
    }

    RayZoneCaster caster;
    caster.minDeviceWork = 0;
    if (!caster.UseOpenCL(true)){
        std::printf("OpenCL unavailable: %s\n", caster.Status().c_str());
        return 1;
    }
    std::printf("device: %s\n%d rays x %d zones\n", caster.Status().c_str(), (int)rays.size(), zoneCount);

    std::vector<RayZoneCaster::Hit> cpu, gpu;
    using Clock = std::chrono::steady_clock;
    auto time = [&](auto&& run){
        run(); // warm-up (kernel/buffer setup)
        auto start = Clock::now();
        for (int i = 0; i < iterations; i++) { run(); }
        return std::chrono::duration<double, std::micro>(Clock::now() - start).count() / iterations;
    };
    double cpuUs = time([&]{ RayZoneCaster::CastCPU(zones, rays, cpu); });
    double gpuUs = time([&]{ caster.Cast(zones, rays, gpu); });
    if (!caster.LastCastOnDevice()){
        std::printf("OpenCL launch failed: %s\n", caster.Status().c_str());
        return 1;
    }

    int hits = 0, zoneMismatch = 0, tMismatch = 0;
    float maxDiff = 0.0f;
    for (size_t i = 0; i < rays.size(); i++){
        hits += cpu[i].zone >= 0;
        if (cpu[i].zone != gpu[i].zone) { zoneMismatch++; continue; }
        if (cpu[i].t != gpu[i].t) { tMismatch++; maxDiff = std::max(maxDiff, std::abs(cpu[i].t - gpu[i].t)); }
    }
    std::printf("hits: %d / %d rays\n", hits, (int)rays.size());
    std::printf("mismatches: %d zone, %d distance (max |dt| %g)\n", zoneMismatch, tMismatch, maxDiff);
    std::printf("cpu: %.1f us   opencl: %.1f us   (%.2fx)\n", cpuUs, gpuUs, cpuUs / gpuUs);
    return zoneMismatch == 0 ? 0 : 2;
}
