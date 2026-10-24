#include <Phoenix/Scene/TerrainGenerator.h>
#include <Phoenix/Compute/Compute.h>
#include "../Compute/ComputeCL.h"
#include "../Compute/SharedSource.h"
#include <string>

namespace Phoenix{

    // Fractal Perlin noise, shared with the kernel (see SharedSource.h). Hashing is
    // 32-bit integer math and gradients come from a fixed set of 8, so there is no
    // trig or library call whose rounding could differ between CPU and device.
    PHX_SHARED_SOURCE(s_TerrainSource,
    static phx_uint phx_hash(int x, int y, phx_uint seed){
        phx_uint h = (phx_uint)x * 0x8da6b343u ^ (phx_uint)y * 0xd8163841u ^ seed * 0xcb1ab31fu;
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        h ^= h >> 16;
        return h;
    }

    static float phx_grad(phx_uint h, float x, float y){
        switch (h & 7u){
            case 0u: return x + y;
            case 1u: return -x + y;
            case 2u: return x - y;
            case 3u: return -x - y;
            case 4u: return x;
            case 5u: return -x;
            case 6u: return y;
            default: return -y;
        }
    }

    static float phx_fade(float t){ return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
    static float phx_mix(float a, float b, float t){ return a + t * (b - a); }

    static float phx_perlin(float x, float y, phx_uint seed){
        float fx = phx_floor(x), fy = phx_floor(y);
        int ix = (int)fx, iy = (int)fy;
        float dx = x - fx, dy = y - fy;
        float u = phx_fade(dx), v = phx_fade(dy);
        float n00 = phx_grad(phx_hash(ix, iy, seed), dx, dy);
        float n10 = phx_grad(phx_hash(ix + 1, iy, seed), dx - 1.0f, dy);
        float n01 = phx_grad(phx_hash(ix, iy + 1, seed), dx, dy - 1.0f);
        float n11 = phx_grad(phx_hash(ix + 1, iy + 1, seed), dx - 1.0f, dy - 1.0f);
        return phx_mix(phx_mix(n00, n10, u), phx_mix(n01, n11, u), v);
    }

    static float phx_terrain_at(int x, int z, int n, float size, phx_uint seed, int octaves, float frequency,
                                float height, float persistence, float lacunarity, float ridged){
        float halfSize = size * 0.5f;
        float fx = n > 1 ? (float)x / (float)(n - 1) : 0.0f;
        float fz = n > 1 ? (float)z / (float)(n - 1) : 0.0f;
        float wx = fx * size - halfSize, wz = fz * size - halfSize;
        float sum = 0.0f, norm = 0.0f, amplitude = 1.0f, f = frequency;
        for (int o = 0; o < octaves; o++){
            float v = phx_perlin(wx * f, wz * f, seed + (phx_uint)o * 0x9e3779b9u);
            float r = 1.0f - phx_abs(v);
            r = r * r * 2.0f - 1.0f;
            sum += (v + ridged * (r - v)) * amplitude;
            norm += amplitude;
            amplitude *= persistence;
            f *= lacunarity;
        }
        return norm > 0.0f ? sum / norm * height : 0.0f;
    }
    )

    namespace TerrainGenerator{

        int minDeviceResolution = 64;
        static bool s_UseOpenCL = false;
        static bool s_LastOnDevice = false;

        void SetUseOpenCL(bool enable) { s_UseOpenCL = enable; }
        bool UsesOpenCL() { return s_UseOpenCL; }
        bool LastOnDevice() { return s_LastOnDevice; }

        void GenerateCPU(int resolution, float size, const TerrainNoise& noise, std::vector<float>& heights){
            const int n = resolution;
            heights.resize((size_t)n * n);
            for (int z = 0; z < n; z++)
                for (int x = 0; x < n; x++)
                    heights[(size_t)z * n + x] = phx_terrain_at(x, z, n, size, noise.seed, noise.octaves, noise.frequency,
                        noise.height, noise.persistence, noise.lacunarity, noise.ridged);
        }

#ifdef PHOENIX_OPENCL
        static const char* s_TerrainKernel = R"CLC(
__kernel void terrain_heights(__global float* heights, int n, float size, uint seed, int octaves, float frequency,
                              float height, float persistence, float lacunarity, float ridged){
    int i = get_global_id(0);
    if (i >= n * n) { return; }
    int z = i / n, x = i - z * n;
    heights[i] = phx_terrain_at(x, z, n, size, seed, octaves, frequency, height, persistence, lacunarity, ridged);
}
)CLC";

        // Built on first use; the kernel and output buffer are kept for the next call.
        struct Device{
            cl_kernel kernel = nullptr;
            cl_mem heights = nullptr;
            size_t capacity = 0;
        };

        static Device* GetDevice(){
            static Device* device = []() -> Device*{
                std::string error;
                cl_program program = Compute::BuildProgram({ s_TerrainSource, s_TerrainKernel }, error);
                if (!program) { return nullptr; }
                cl_int err;
                cl_kernel kernel = clCreateKernel(program, "terrain_heights", &err);
                if (err != CL_SUCCESS) { return nullptr; }
                return new Device{ kernel };   // lives as long as the shared device
            }();
            return device;
        }

        static bool GenerateDevice(int n, float size, const TerrainNoise& noise, std::vector<float>& heights){
            Device* d = GetDevice();
            if (!d) { return false; }
            const size_t count = (size_t)n * n;
            if (!Compute::Reserve(d->heights, d->capacity, count, sizeof(float), CL_MEM_WRITE_ONLY)) { return false; }
            cl_uint seed = noise.seed;
            cl_int ni = n, octaves = noise.octaves;
            cl_int err = clSetKernelArg(d->kernel, 0, sizeof(cl_mem), &d->heights);
            err |= clSetKernelArg(d->kernel, 1, sizeof(cl_int), &ni);
            err |= clSetKernelArg(d->kernel, 2, sizeof(float), &size);
            err |= clSetKernelArg(d->kernel, 3, sizeof(cl_uint), &seed);
            err |= clSetKernelArg(d->kernel, 4, sizeof(cl_int), &octaves);
            err |= clSetKernelArg(d->kernel, 5, sizeof(float), &noise.frequency);
            err |= clSetKernelArg(d->kernel, 6, sizeof(float), &noise.height);
            err |= clSetKernelArg(d->kernel, 7, sizeof(float), &noise.persistence);
            err |= clSetKernelArg(d->kernel, 8, sizeof(float), &noise.lacunarity);
            err |= clSetKernelArg(d->kernel, 9, sizeof(float), &noise.ridged);
            err |= Compute::Launch(d->kernel, count);
            heights.resize(count);
            err |= clEnqueueReadBuffer(Compute::GetDevice()->queue, d->heights, CL_TRUE, 0, count * sizeof(float), heights.data(), 0, nullptr, nullptr);
            return err == CL_SUCCESS;
        }
#endif

        void Generate(int resolution, float size, const TerrainNoise& noise, std::vector<float>& heights){
            s_LastOnDevice = false;
#ifdef PHOENIX_OPENCL
            if (s_UseOpenCL && resolution >= minDeviceResolution && Compute::Available()
                && GenerateDevice(resolution, size, noise, heights)){
                s_LastOnDevice = true;
                return;
            }
#endif
            GenerateCPU(resolution, size, noise, heights);
        }
    }
}
