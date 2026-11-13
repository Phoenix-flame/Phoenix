#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace Phoenix{

    class FireSim;

    // GPU copy of a fire's grid: a 3D texture of temperature, fuel, smoke and reaction
    // rate per cell (RGBA16F, linear filtering, zero outside), for the volume renderer.
    class FireVolume{
    public:
        FireVolume() = default;
        ~FireVolume();

        FireVolume(const FireVolume&) = delete;
        FireVolume& operator=(const FireVolume&) = delete;

        // Main thread: copies the simulation's current cells.
        void Upload(const FireSim& sim);
        uint32_t GetTexture() const { return m_Texture; }

    private:
        uint32_t m_Texture = 0;
        glm::ivec3 m_Dims = glm::ivec3(0);
    };
}
