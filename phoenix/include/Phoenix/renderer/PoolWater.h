#pragma once
#include <Phoenix/renderer/RippleGrid.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Phoenix{

    // The water of one pool: a RippleGrid spanning the pool's interior wall to wall
    // (vertex (i, j) at x = -width/2 + i * width/(W-1), likewise z), plus its GPU side
    // (a height texture and a caustics texture the renderer fills). Heights are the
    // surface's offset from the still water level, in metres.
    class PoolWater{
    public:
        PoolWater(const glm::vec2& size, float cellSize);
        ~PoolWater();

        PoolWater(const PoolWater&) = delete;
        PoolWater& operator=(const PoolWater&) = delete;

        // Whether this was built for these settings (else rebuild it).
        bool Matches(const glm::vec2& size, float cellSize) const { return size == m_Size && cellSize == m_CellSize; }

        // Advance dt seconds of waves travelling at waveSpeed m/s, their height decaying
        // by `damping` per second. Takes as many substeps as stability needs (at most 16;
        // beyond that the waves are slowed instead).
        void Step(float dt, float waveSpeed, float damping);
        // Bodies in the water: `columns` holds, per grid vertex, the height of the water
        // column they occupy (metres). The surface rises by the change since the last
        // call, so water pushed aside by a body spreads as waves and comes back when it
        // leaves. Pass an empty vector when nothing is in the water.
        void SetDisplacement(const std::vector<float>& columns);
        // Forget all motion. The next SetDisplacement is taken as the resting state, so
        // bodies already in the water then make no waves.
        void Reset();

        // Surface offset at a point of the pool's local xz plane (clamped to the walls).
        float SurfaceHeight(float x, float z) const;
        // Grid vertex nearest to a local point, and back.
        glm::ivec2 CellAt(float x, float z) const;
        glm::vec2 CellPosition(int i, int j) const;

        RippleGrid& Grid() { return m_Grid; }
        const RippleGrid& Grid() const { return m_Grid; }
        glm::vec2 GetSize() const { return m_Size; }
        float GetSpacing() const { return m_Spacing; }

        // GL side (main thread): upload the heights; textures for the renderer.
        void Upload();
        uint32_t GetHeightTexture() const { return m_HeightTexture; }
        uint32_t GetCausticsTexture() const { return m_CausticsTexture; }
        uint32_t GetCausticsFramebuffer() const { return m_CausticsFBO; }
        glm::ivec2 GetCausticsSize() const { return m_CausticsSize; }

    private:
        glm::vec2 m_Size;
        float m_CellSize;
        float m_Spacing;            // world distance between grid vertices (the smaller axis)
        RippleGrid m_Grid;
        std::vector<float> m_Columns;   // last displacement, per vertex
        bool m_Settle = true;           // next displacement is the resting state
        uint32_t m_HeightTexture = 0;
        uint32_t m_CausticsTexture = 0, m_CausticsFBO = 0;
        glm::ivec2 m_CausticsSize = { 512, 512 };
    };
}
