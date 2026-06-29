#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/Scene/Scene.h>
#include <string>

namespace YAML { class Node; }

namespace Phoenix{

    // Serializes a Scene (entities + components) to/from YAML (file or string).
    // Deserialize expects to load into a freshly created (empty) Scene.
    class SceneSerializer{
    public:
        SceneSerializer(const Ref<Scene>& scene);

        void Serialize(const std::string& filepath);
        bool Deserialize(const std::string& filepath);

        // In-memory variants, used by the editor undo/redo history.
        std::string SerializeToString();
        bool DeserializeFromString(const std::string& data);

        // Reconcile the existing scene to match the snapshot IN PLACE: update
        // changed components, add/remove as needed, create/destroy entities, and
        // reuse mesh Models whose path is unchanged (no reload). Used by undo/redo.
        bool ApplyFromString(const std::string& data);
    private:
        bool DeserializeNode(const YAML::Node& data);
        void ApplyEntity(Entity entity, const YAML::Node& node);
        void RecountPointLights();

        Ref<Scene> m_Scene;
    };
}
