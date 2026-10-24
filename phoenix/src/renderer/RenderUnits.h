#pragma once
#include <cstdint>

namespace Phoenix{
    // Texture units: 0 diffuse, 1 directional atlas, 2 normal map, 3 water ripples / pool
    // heights, 4 directional atlas (raw depth), 5 point atlas, 6 point atlas (raw depth),
    // 7 pool caustics, 8 scene colour copy, 9 scene depth copy.
    static const uint32_t DIR_SHADOW_UNIT = 1, DIR_DEPTH_UNIT = 4, POINT_SHADOW_UNIT = 5, POINT_DEPTH_UNIT = 6;
    static const uint32_t HEIGHT_UNIT = 3, CAUSTICS_UNIT = 7, SCENE_COLOR_UNIT = 8, SCENE_DEPTH_UNIT = 9;
}
