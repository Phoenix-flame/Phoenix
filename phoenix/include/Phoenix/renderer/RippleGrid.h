#pragma once
#include <memory>
#include <vector>

namespace Phoenix{

    // A damped 2D wave equation on a width x height heightfield, with no GL state
    // (WaterRipples and PoolWater draw it). The edges reflect waves, like walls. Large
    // grids can step on the shared OpenCL device; results are bit-identical to the
    // CPU, so the backend never changes the water.
    class RippleGrid{
    public:
        RippleGrid(int width, int height);
        explicit RippleGrid(int resolution) : RippleGrid(resolution, resolution) {}
        ~RippleGrid();

        RippleGrid(const RippleGrid&) = delete;
        RippleGrid& operator=(const RippleGrid&) = delete;

        // Per-substep constants: c2 = (wave speed * substep / cell size)^2, at most 0.5 for
        // the explicit scheme to stay stable; damping scales the heights every substep.
        void SetWaveParameters(float c2, float damping);

        // Press the surface down (or up, negative strength) around uv with a smooth
        // falloff. radius is in u units (0..1 across the width).
        void AddImpulse(float u, float v, float radius, float strength);
        // Raise one cell (negative lowers it), e.g. by the water a body pushed aside.
        void AddHeight(int x, int y, float delta);
        // Advance `substeps` fixed wave steps.
        void Step(int substeps);
        // Bilinear height readback (uv clamped).
        float SampleHeight(float u, float v) const;
        // Forget all motion (flat surface at rest).
        void Reset();

        const std::vector<float>& Heights() const { return m_Cur; }
        float Cell(int x, int y) const { return m_Cur[(size_t)y * m_W + x]; }
        int GetWidth() const { return m_W; }
        int GetHeight() const { return m_H; }
        int GetResolution() const { return m_W; }

        // Step on the OpenCL device when one is set up. Returns whether it is in use.
        bool UseOpenCL(bool enable);
        bool OnDevice() const { return m_Device != nullptr; }

    private:
        struct Device;
        void StepCPU();
        void MarkDirty(int rowMin, int rowMax);

        int m_W, m_H;
        float m_C2 = 0.32f, m_Damping = 0.987f;
        std::vector<float> m_Prev, m_Cur, m_Next;
        std::unique_ptr<Device> m_Device;   // owns the authoritative prev/cur while set
        int m_DirtyRowMin = 0, m_DirtyRowMax = -1;   // rows of m_Cur changed since the device last saw them
    };
}
