// Checks the OpenCL water ripples and terrain generation against their CPU paths (they
// must match bit for bit) and times both across grid sizes.
//   compute_bench [frames=120]
#include <Phoenix/Compute/Compute.h>
#include <Phoenix/renderer/WaterRipples.h>
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
    return ok ? 0 : 2;
}
