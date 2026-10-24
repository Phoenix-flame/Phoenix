#pragma once
#include <glm/glm.hpp>
#include <string>

namespace Phoenix{
    // What fills the background behind the scene (per scene, serialized). Reflective
    // materials and water mirror the same sky: the panorama in Panorama mode, else the
    // gradient colours (also in Color mode, where only the backdrop is flat).
    struct SkySettings{
        enum class Mode { Color = 0, Gradient = 1, Panorama = 2 };
        Mode mode = Mode::Color;
        glm::vec3 color = glm::vec3(0.0f);                   // Color mode backdrop
        glm::vec3 zenith  = glm::vec3(0.15f, 0.35f, 0.75f);  // gradient: straight up
        glm::vec3 horizon = glm::vec3(0.55f, 0.60f, 0.70f);  // gradient: at the horizon
        glm::vec3 ground  = glm::vec3(0.10f, 0.10f, 0.12f);  // gradient: below the horizon
        bool sunDisc = true;     // gradient: a glowing disc where the first directional light shines from
        float sunSize = 1.5f;    // sun disc angular radius, degrees
        std::string panorama;    // equirectangular image (.jpg/.png/.hdr), path relative to the executable
        float rotation = 0.0f;   // panorama turn about +Y, degrees
        float intensity = 1.0f;  // brightness multiplier for the gradient / panorama
    };
}
