#pragma once
#include <Phoenix/core/base.h>
#include <cstdint>
#include <vector>

namespace Phoenix{

    // Interactive ripple simulation for a water surface: a damped 2D wave equation
    // integrated on a small CPU heightfield, uploaded to a float texture that the
    // water shader adds to its procedural (Gerstner) waves. Impulses come from
    // physics bodies splashing in / moving through the water.
    //
    // The grid maps to the water surface's local [0,1]x[0,1] UV space.
    class WaterRipples{
    public:
        explicit WaterRipples(int resolution = 128);
        ~WaterRipples();

        WaterRipples(const WaterRipples&) = delete;
        WaterRipples& operator=(const WaterRipples&) = delete;

        // Press the surface down (or up, negative strength) around uv with a smooth
        // falloff. radius is in UV units (0..1 across the surface).
        void AddImpulse(float u, float v, float radius, float strength);

        // Advance the wave equation (fixed internal substeps for stability).
        void Step(float dt);

        // Upload the current heights into the GL texture. Main (GL) thread only.
        void Upload();

        // Bilinear height readback (for buoyancy/splash queries). uv clamped.
        float SampleHeight(float u, float v) const;

        uint32_t GetTextureID() const { return m_Texture; }
        int GetResolution() const { return m_Res; }

    private:
        void StepOnce();

        int m_Res;
        std::vector<float> m_Prev, m_Cur, m_Next;
        uint32_t m_Texture = 0;
        float m_Accumulator = 0.0f;
    };
}
