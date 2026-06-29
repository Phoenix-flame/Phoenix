#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/renderer/VertexArray.h>
#include <Phoenix/renderer/Texture.h>
#include <Phoenix/renderer/Animation.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <map>
#include <future>

namespace Phoenix{

    // Up to 4 bone influences per vertex. BoneIDs default to -1 (no influence) so the
    // skinning loop in the shader can skip empty slots; weights default to 0.
    struct Vertex{
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoords;
        glm::vec4 BoneIDs = glm::vec4(-1.0f);
        glm::vec4 Weights = glm::vec4(0.0f);
    };

    // CPU-side data for one mesh, produced on a worker thread (no GL calls).
    struct MeshData{
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::string diffusePath; // absolute path; empty if the mesh has no diffuse map
    };

    // Everything the worker thread extracts from a model file: geometry plus the
    // skeleton/animation data (free of GL resources, so it crosses the thread).
    struct ModelData{
        std::vector<MeshData> meshes;
        std::map<std::string, BoneInfo> boneInfoMap;
        std::vector<Ref<Animation>> animations;
    };

    // A single drawable chunk of geometry: an interleaved vertex buffer
    // (position@0, normal@1, texcoord@2) plus an index buffer, wrapped in a VAO.
    class Mesh{
    public:
        Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

        const Ref<VertexArray>& GetVertexArray() const { return m_VertexArray; }
        uint32_t GetIndexCount() const { return m_IndexCount; }

        void SetDiffuseMap(const Ref<Texture2D>& diffuseMap) { m_DiffuseMap = diffuseMap; }
        const Ref<Texture2D>& GetDiffuseMap() const { return m_DiffuseMap; }

        // CPU-side geometry retained for physics collider generation.
        const std::vector<glm::vec3>& GetPositions() const { return m_Positions; }
        const std::vector<uint32_t>& GetIndices() const { return m_Indices; }
    private:
        Ref<VertexArray> m_VertexArray;
        uint32_t m_IndexCount = 0;
        Ref<Texture2D> m_DiffuseMap;
        std::vector<glm::vec3> m_Positions;
        std::vector<uint32_t> m_Indices;
    };

    // A model loaded from disk (via Assimp). Parsing runs on a background thread;
    // GPU resources are created lazily on the main thread in Update(). Until the
    // load completes GetMeshes() is empty, so nothing is drawn for it.
    class Model{
    public:
        Model(const std::string& path);
        ~Model();

        // Must be called on the main (render) thread each frame. Once the worker
        // has finished parsing, this uploads the geometry/textures to the GPU.
        void Update();
        bool IsReady() const { return m_Uploaded; }

        const std::vector<Ref<Mesh>>& GetMeshes() const { return m_Meshes; }
        const std::string& GetPath() const { return m_Path; }

        // Skeletal animation (empty for static models). Animations become available
        // once the async load completes (IsReady()).
        bool HasAnimations() const { return !m_Animations.empty(); }
        size_t GetAnimationCount() const { return m_Animations.size(); }
        Animation* GetAnimation(size_t index) const {
            return index < m_Animations.size() ? m_Animations[index].get() : nullptr;
        }
    private:
        std::string m_Path;
        std::vector<Ref<Mesh>> m_Meshes;
        std::map<std::string, BoneInfo> m_BoneInfoMap;
        std::vector<Ref<Animation>> m_Animations;
        std::future<ModelData> m_Future;
        bool m_Uploaded = false;
    };
}
