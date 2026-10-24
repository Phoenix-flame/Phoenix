#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/renderer/RippleGrid.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace Phoenix{

    // Interactive ripples for a water surface: a RippleGrid stepped at a fixed rate and
    // uploaded to a float texture that the water shader adds to its procedural
    // (Gerstner) waves. Impulses come from physics bodies splashing in / moving
    // through the water.
    //
    // The grid maps to the water surface's local [0,1]x[0,1] UV space.
    class WaterRipples{
    public:
        explicit WaterRipples(int resolution = 128);
        ~WaterRipples();

        WaterRipples(const WaterRipples&) = delete;
        WaterRipples& operator=(const WaterRipples&) = delete;

        void AddImpulse(float u, float v, float radius, float strength) { m_Grid.AddImpulse(u, v, radius, strength); }

        // Advance the wave equation (fixed internal substeps for stability).
        void Step(float dt);

        // Upload the current heights into the GL texture. Main (GL) thread only.
        void Upload();

        // Bilinear height readback (for buoyancy/splash queries). uv clamped.
        float SampleHeight(float u, float v) const { return m_Grid.SampleHeight(u, v); }

        uint32_t GetTextureID() const { return m_Texture; }
        int GetResolution() const { return m_Grid.GetResolution(); }
        bool OnDevice() const { return m_Grid.OnDevice(); }

        // Step grids of at least minDeviceResolution cells per side on the OpenCL device
        // (smaller ones are faster on the CPU than the launches and read-back). Applies to
        // every water surface.
        static void SetUseOpenCL(bool enable) { s_UseOpenCL = enable; }
        static bool UsesOpenCL() { return s_UseOpenCL; }
        static int minDeviceResolution;

    private:
        RippleGrid m_Grid;
        uint32_t m_Texture = 0;
        float m_Accumulator = 0.0f;

        static bool s_UseOpenCL;
    };
}
