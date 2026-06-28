#include <Phoenix/renderer/Mesh.h>
#include <Phoenix/core/log.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace Phoenix{

    Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices){
        m_IndexCount = (uint32_t)indices.size();

        m_VertexArray = CreateRef<VertexArray>();
        m_VertexArray->Bind();

        Ref<VertexBuffer> vertexBuffer = CreateRef<VertexBuffer>(
            (float*)vertices.data(), (int)(vertices.size() * sizeof(Vertex)));
        BufferLayout layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoords" }
        };
        vertexBuffer->SetLayout(layout);
        m_VertexArray->AddVertexBuffer(vertexBuffer);

        Ref<IndexBuffer> indexBuffer = CreateRef<IndexBuffer>(
            (uint32_t*)indices.data(), (uint32_t)indices.size());
        m_VertexArray->SetIndexBuffer(indexBuffer);
    }

    static Ref<Mesh> ProcessMesh(aiMesh* mesh, const aiScene* scene, const std::string& directory){
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(mesh->mNumVertices);

        for (unsigned int i = 0; i < mesh->mNumVertices; i++){
            Vertex vertex;
            vertex.Position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };

            if (mesh->HasNormals())
                vertex.Normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };
            else
                vertex.Normal = { 0.0f, 0.0f, 0.0f };

            if (mesh->mTextureCoords[0])
                vertex.TexCoords = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
            else
                vertex.TexCoords = { 0.0f, 0.0f };

            vertices.push_back(vertex);
        }

        for (unsigned int i = 0; i < mesh->mNumFaces; i++){
            const aiFace& face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);
        }

        Ref<Mesh> result = CreateRef<Mesh>(vertices, indices);

        // Diffuse texture (map_Kd), resolved relative to the model file's directory.
        if (mesh->mMaterialIndex >= 0){
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0){
                aiString name;
                material->GetTexture(aiTextureType_DIFFUSE, 0, &name);
                std::string texPath = directory + "/" + std::string(name.C_Str());
                result->SetDiffuseMap(Texture2D::Create(texPath));
            }
        }

        return result;
    }

    static void ProcessNode(aiNode* node, const aiScene* scene, const std::string& directory, std::vector<Ref<Mesh>>& meshes){
        for (unsigned int i = 0; i < node->mNumMeshes; i++)
            meshes.push_back(ProcessMesh(scene->mMeshes[node->mMeshes[i]], scene, directory));

        for (unsigned int i = 0; i < node->mNumChildren; i++)
            ProcessNode(node->mChildren[i], scene, directory, meshes);
    }

    Model::Model(const std::string& path) : m_Path(path){
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate
            | aiProcess_GenSmoothNormals
            | aiProcess_FlipUVs
            | aiProcess_JoinIdenticalVertices);

        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode){
            PHX_CORE_ERROR("Assimp failed to load '{0}': {1}", path, importer.GetErrorString());
            return;
        }

        std::string directory = path.substr(0, path.find_last_of('/'));
        ProcessNode(scene->mRootNode, scene, directory, m_Meshes);
        PHX_CORE_INFO("Loaded model '{0}' ({1} meshes)", path, m_Meshes.size());
    }
}
