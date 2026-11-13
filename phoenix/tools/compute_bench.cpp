// Checks the OpenCL water ripples, terrain generation, particle fluid and fire against
// their CPU paths (they must match bit for bit) and times both.
//   compute_bench [frames=120]
#include <Phoenix/Compute/Compute.h>
#include <Phoenix/renderer/WaterRipples.h>
#include <Phoenix/Physics/ParticleFluid.h>
#include <Phoenix/Physics/FireSim.h>
#include <glm/gtc/matrix_transform.hpp>
#include <Phoenix/Scene/TerrainGenerator.h>
#include <Phoenix/core/log.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

using namespace Phoenix;
using Clock = std::chrono::steady_clock;

static double Microseconds(Clock::time_point start){
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

int main(int argc, char** argv){
    const int frames = argc > 1 ? std::atoi(argv[1]) : 120;
    Log::Init();
    if (!Compute::Available()){
        std::printf("OpenCL unavailable: %s\n", Compute::Status().c_str());
        return 1;
    }
    std::printf("device: %s\n", Compute::Status().c_str());
    bool ok = true;

    // Ripples: the same splashes on two grids, one stepped on each backend. Each frame
    // is one substep (60 Hz) plus a read of the heights, as the renderer does.
    std::printf("\nwater ripples, %d frames (per frame: 1 substep + heights back on the host)\n", frames);
    for (int res : { 128, 256, 512, 1024 }){
        RippleGrid cpu(res), gpu(res);
        if (!gpu.UseOpenCL(true)) { std::printf("device grid failed\n"); return 1; }
        std::mt19937 rng(3);
        std::uniform_real_distribution<float> uv(0.0f, 1.0f), strength(-0.3f, 0.3f);
        double cpuUs = 0.0, gpuUs = 0.0;
        for (int f = 0; f < frames; f++){
            if (f % 4 == 0){
                float u = uv(rng), v = uv(rng), s = strength(rng);
                cpu.AddImpulse(u, v, 0.03f, s);
                gpu.AddImpulse(u, v, 0.03f, s);
            }
            auto t0 = Clock::now();
            cpu.Step(1);
            cpuUs += Microseconds(t0);
            t0 = Clock::now();
            gpu.Step(1);
            gpuUs += Microseconds(t0);
        }
        bool same = gpu.OnDevice() && std::memcmp(cpu.Heights().data(), gpu.Heights().data(), cpu.Heights().size() * sizeof(float)) == 0;
        ok &= same;
        std::printf("  %4d x %-4d  cpu %8.1f us  opencl %8.1f us  (%5.2fx)  %s\n", res, res, cpuUs / frames, gpuUs / frames,
            cpuUs / gpuUs, same ? "identical" : "MISMATCH");
    }

    // Pools use rectangular grids with their own wave constants.
    {
        RippleGrid cpu(301, 171), gpu(301, 171);
        cpu.SetWaveParameters(0.21f, 0.993f);
        gpu.SetWaveParameters(0.21f, 0.993f);
        if (!gpu.UseOpenCL(true)) { std::printf("device grid failed\n"); return 1; }
        for (int f = 0; f < frames; f++){
            if (f % 5 == 0){
                cpu.AddHeight(37 + f % 200, 11 + f % 150, 0.02f);
                gpu.AddHeight(37 + f % 200, 11 + f % 150, 0.02f);
            }
            cpu.Step(3);
            gpu.Step(3);
        }
        bool same = gpu.OnDevice() && std::memcmp(cpu.Heights().data(), gpu.Heights().data(), cpu.Heights().size() * sizeof(float)) == 0;
        ok &= same;
        std::printf("  301 x 171 pool grid, 3 substeps/frame: %s\n", same ? "identical" : "MISMATCH");
    }

    // Terrain: one landscape per size on each backend.
    std::printf("\nterrain generation (5 octaves, ridged 0.5)\n");
    TerrainNoise noise;
    noise.seed = 1234;
    noise.ridged = 0.5f;
    TerrainGenerator::SetUseOpenCL(true);
    TerrainGenerator::minDeviceResolution = 0;
    for (int res : { 64, 128, 256, 512, 1024 }){
        std::vector<float> cpu, gpu;
        auto t0 = Clock::now();
        TerrainGenerator::GenerateCPU(res, 400.0f, noise, cpu);
        double cpuUs = Microseconds(t0);
        TerrainGenerator::Generate(res, 400.0f, noise, gpu); // warm-up (kernel build on the first)
        t0 = Clock::now();
        TerrainGenerator::Generate(res, 400.0f, noise, gpu);
        double gpuUs = Microseconds(t0);
        bool same = TerrainGenerator::LastOnDevice() && cpu.size() == gpu.size()
                 && std::memcmp(cpu.data(), gpu.data(), cpu.size() * sizeof(float)) == 0;
        ok &= same;
        std::printf("  %4d x %-4d  cpu %8.1f us  opencl %8.1f us  (%5.2fx)  %s\n", res, res, cpuUs, gpuUs, cpuUs / gpuUs,
            same ? "identical" : "MISMATCH");
    }

    // Particle fluid: a dam break in a 2 x 2 m tank (box colliders) with a sphere in it.
    std::printf("\nparticle fluid: dam break, %d steps at 60 Hz\n", frames);
    {
        std::vector<FluidCollider> tank;
        auto box = [&](glm::vec3 c, glm::vec3 half){
            FluidCollider k;
            k.type = FluidCollider::Type::Box;
            k.inverse = glm::inverse(glm::translate(glm::mat4(1.0f), c));
            k.size = half;
            k.center = c;
            k.radius = glm::length(half);
            tank.push_back(k);
        };
        box({ 0, -0.1f, 0 }, { 1.2f, 0.1f, 1.2f });
        box({ -1.1f, 1.0f, 0 }, { 0.1f, 1.0f, 1.2f });
        box({ 1.1f, 1.0f, 0 }, { 0.1f, 1.0f, 1.2f });
        box({ 0, 1.0f, -1.1f }, { 1.0f, 1.0f, 0.1f });
        box({ 0, 1.0f, 1.1f }, { 1.0f, 1.0f, 0.1f });
        FluidCollider ball;
        ball.type = FluidCollider::Type::Sphere;
        ball.inverse = glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(0.4f, 0.2f, 0.3f)));
        ball.size = glm::vec3(0.2f);
        ball.center = glm::vec3(0.4f, 0.2f, 0.3f);
        ball.radius = 0.2f;
        ball.dynamic = true;
        ball.body = 7;
        tank.push_back(ball);

        ParticleFluid cpu(20000), gpu(20000);
        for (ParticleFluid* f : { &cpu, &gpu }){
            f->AddBlock({ -0.6f, 0.62f, -0.5f }, { 0.375f, 0.6f, 0.375f }, glm::vec3(0.0f));
        }
        if (!gpu.UseOpenCL(true)) { std::printf("device fluid failed\n"); return 1; }
        std::vector<ParticleFluid::BodyImpulse> ic, ig;
        double cpuUs = 0.0, gpuUs = 0.0;
        glm::vec3 push(0.0f);
        for (int f = 0; f < frames; f++){
            auto t0 = Clock::now();
            cpu.Step(1.0f / 60.0f, tank, ic);
            cpuUs += Microseconds(t0);
            t0 = Clock::now();
            gpu.Step(1.0f / 60.0f, tank, ig);
            gpuUs += Microseconds(t0);
            for (const auto& b : ic) { push += b.impulse; }
        }
        bool same = gpu.OnDevice() && cpu.Count() == gpu.Count()
                 && std::memcmp(cpu.Positions().data(), gpu.Positions().data(), sizeof(glm::vec4) * cpu.Count()) == 0
                 && std::memcmp(cpu.Velocities().data(), gpu.Velocities().data(), sizeof(glm::vec4) * cpu.Count()) == 0;
        ok &= same;
        int outside = 0;
        float meanY = 0.0f, maxSpeed = 0.0f;
        for (int i = 0; i < cpu.Count(); i++){
            glm::vec3 p = cpu.Positions()[i];
            outside += std::abs(p.x) > 1.0f + 0.03f || std::abs(p.z) > 1.0f + 0.03f || p.y < -0.03f;
            meanY += p.y;
            maxSpeed = std::max(maxSpeed, glm::length(glm::vec3(cpu.Velocities()[i])));
        }
        meanY /= std::max(1, cpu.Count());
        const float volume = cpu.Count() * std::pow(cpu.GetSettings().spacing, 3.0f);
        std::printf("  %d particles: cpu %.1f ms/step  opencl %.1f ms/step  %s\n", cpu.Count(), cpuUs / frames / 1000.0,
            gpuUs / frames / 1000.0, same ? "identical" : "MISMATCH");
        std::printf("  after %.1f s: %d outside the tank, mean height %.3f m (a still %.2f m deep pool: %.3f), max speed %.2f m/s, push on the ball %.1f N s up\n",
            frames / 60.0f, outside, meanY, volume / 4.0f, volume / 8.0f, maxSpeed, push.y);
    }

    // Fire: a torch-sized and a campfire-sized grid, the same wind gusts on both backends.
    std::printf("\nfire, %d steps at 60 Hz\n", frames);
    for (glm::ivec3 res : { glm::ivec3(16, 40, 16), glm::ivec3(32, 64, 32) }){
        FireSettings settings;
        settings.resolution = res;
        settings.cellSize = 0.4f / res.x;
        settings.source = glm::vec3(0.2f, 0.06f, 0.2f);
        settings.emberRate = 20.0f;
        FireSim cpu, gpu;
        cpu.SetSettings(settings);
        gpu.SetSettings(settings);
        if (!gpu.UseOpenCL(true)) { std::printf("device fire failed\n"); return 1; }
        double cpuUs = 0.0, gpuUs = 0.0;
        float minPower = 1e30f, maxPower = 0.0f;
        for (int f = 0; f < frames; f++){
            glm::vec3 wind(std::sin(f * 0.05f) * 0.6f, 0.0f, 0.2f);
            bool burning = f < frames * 3 / 4;
            auto t0 = Clock::now();
            cpu.Step(1.0f / 60.0f, wind, burning);
            cpuUs += Microseconds(t0);
            t0 = Clock::now();
            gpu.Step(1.0f / 60.0f, wind, burning);
            gpuUs += Microseconds(t0);
            if (f > 60 && burning) { minPower = std::min(minPower, cpu.Power()); maxPower = std::max(maxPower, cpu.Power()); }
        }
        const size_t n = (size_t)cpu.Cells();
        bool same = gpu.OnDevice()
                 && std::memcmp(cpu.Scalars().data(), gpu.Scalars().data(), sizeof(glm::vec4) * n) == 0
                 && std::memcmp(cpu.Velocities().data(), gpu.Velocities().data(), sizeof(glm::vec4) * n) == 0;
        ok &= same;
        float maxT = 0.0f, maxV = 0.0f;
        for (size_t i = 0; i < n; i++){
            maxT = std::max(maxT, cpu.Scalars()[i].x);
            maxV = std::max(maxV, glm::length(glm::vec3(cpu.Velocities()[i])));
        }
        std::printf("  %d x %d x %d: cpu %.2f ms/step  opencl %.2f ms/step  %s\n", res.x, res.y, res.z,
            cpuUs / frames / 1000.0, gpuUs / frames / 1000.0, same ? "identical" : "MISMATCH");
        std::printf("    burning: power %.3g..%.3g (flicker), flame temperature %.2f; at the end: max T %.3f, max speed %.2f m/s, %d embers\n",
            minPower, maxPower, cpu.FlameTemperature(), maxT, maxV, (int)cpu.Embers().size());
    }
    return ok ? 0 : 2;
}
