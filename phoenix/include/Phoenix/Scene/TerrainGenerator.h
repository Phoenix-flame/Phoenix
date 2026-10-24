#pragma once
#include <cstdint>
#include <vector>

namespace Phoenix{

    // Settings for noise-generated terrain: fractal Perlin noise (fBm), optionally
    // ridged. Heights are a pure function of these, the grid and the seed.
    struct TerrainNoise{
        uint32_t seed = 1;
        float frequency = 0.05f;    // base features per world unit
        int octaves = 5;            // layers of finer detail
        float height = 6.0f;        // peak height (world units, before the entity's scale)
        float persistence = 0.5f;   // amplitude kept per octave
        float lacunarity = 2.0f;    // frequency gained per octave
        float ridged = 0.0f;        // 0 = rolling hills, 1 = sharp ridges
    };

    // Fills a terrain heightfield from TerrainNoise. Large grids run on the shared
    // OpenCL device when enabled; the heights are bit-identical either way, so a seed
    // always gives the same landscape.
    namespace TerrainGenerator{
        // heights: resolution x resolution, row-major (z * resolution + x), over a square
        // of `size` world units centred on the origin (TerrainComponent's layout).
        void Generate(int resolution, float size, const TerrainNoise& noise, std::vector<float>& heights);
        // The CPU path alone (tests and benchmarks).
        void GenerateCPU(int resolution, float size, const TerrainNoise& noise, std::vector<float>& heights);

        // Use the OpenCL device for grids of at least minDeviceResolution per side.
        void SetUseOpenCL(bool enable);
        bool UsesOpenCL();
        extern int minDeviceResolution;
        // Whether the last Generate ran on the device.
        bool LastOnDevice();
    }
}
