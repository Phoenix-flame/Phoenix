#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/Scene/Scene.h>
#include <string>

namespace Phoenix{

    // Serializes a Scene (entities + components) to/from a YAML file.
    // Deserialize expects to load into a freshly created (empty) Scene.
    class SceneSerializer{
    public:
        SceneSerializer(const Ref<Scene>& scene);

        void Serialize(const std::string& filepath);
        bool Deserialize(const std::string& filepath);
    private:
        Ref<Scene> m_Scene;
    };
}
