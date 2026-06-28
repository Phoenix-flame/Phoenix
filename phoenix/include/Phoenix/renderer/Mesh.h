#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/renderer/VertexArray.h>
#include <Phoenix/renderer/Texture.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace Phoenix{

    struct Vertex{
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoords;
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
    private:
        Ref<VertexArray> m_VertexArray;
        uint32_t m_IndexCount = 0;
        Ref<Texture2D> m_DiffuseMap;
    };

    // A model loaded from disk (via Assimp): one or more meshes. Assimp types are
    // kept out of this header; all of that lives in Mesh.cpp.
    class Model{
    public:
        Model(const std::string& path);

        const std::vector<Ref<Mesh>>& GetMeshes() const { return m_Meshes; }
        const std::string& GetPath() const { return m_Path; }
        bool IsLoaded() const { return !m_Meshes.empty(); }
    private:
        std::vector<Ref<Mesh>> m_Meshes;
        std::string m_Path;
    };
}
