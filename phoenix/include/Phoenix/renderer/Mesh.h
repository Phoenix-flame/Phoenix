#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/renderer/VertexArray.h>
#include <Phoenix/renderer/Texture.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <future>

namespace Phoenix{

    struct Vertex{
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoords;
    };

    // CPU-side data for one mesh, produced on a worker thread (no GL calls).
    struct MeshData{
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::string diffusePath; // absolute path; empty if the mesh has no diffuse map
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
    private:
        std::string m_Path;
        std::vector<Ref<Mesh>> m_Meshes;
        std::future<std::vector<MeshData>> m_Future;
        bool m_Uploaded = false;
    };
}
