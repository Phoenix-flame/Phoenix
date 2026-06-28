#include <Phoenix/renderer/Mesh.h>
#include <Phoenix/core/log.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <chrono>

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

    // ---- Worker-thread parsing (no GL calls) ----

    static void ProcessMeshData(aiMesh* mesh, const aiScene* scene, const std::string& directory, std::vector<MeshData>& out){
        MeshData data;
        data.vertices.reserve(mesh->mNumVertices);

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

            data.vertices.push_back(vertex);
        }

        for (unsigned int i = 0; i < mesh->mNumFaces; i++){
            const aiFace& face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++)
                data.indices.push_back(face.mIndices[j]);
        }

        if (mesh->mMaterialIndex >= 0){
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0){
                aiString name;
                material->GetTexture(aiTextureType_DIFFUSE, 0, &name);
                data.diffusePath = directory + "/" + std::string(name.C_Str());
            }
        }

        out.push_back(std::move(data));
    }

    static void ProcessNodeData(aiNode* node, const aiScene* scene, const std::string& directory, std::vector<MeshData>& out){
        for (unsigned int i = 0; i < node->mNumMeshes; i++)
            ProcessMeshData(scene->mMeshes[node->mMeshes[i]], scene, directory, out);

        for (unsigned int i = 0; i < node->mNumChildren; i++)
            ProcessNodeData(node->mChildren[i], scene, directory, out);
    }

    static std::vector<MeshData> ParseModel(std::string path){
        std::vector<MeshData> meshes;

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate
            | aiProcess_GenSmoothNormals
            | aiProcess_FlipUVs
            | aiProcess_JoinIdenticalVertices);

        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode){
            PHX_CORE_ERROR("Assimp failed to load '{0}': {1}", path, importer.GetErrorString());
            return meshes;
        }

        std::string directory = path.substr(0, path.find_last_of('/'));
        ProcessNodeData(scene->mRootNode, scene, directory, meshes);
        return meshes;
    }

    // ---- Main-thread model handle ----

    Model::Model(const std::string& path) : m_Path(path){
        m_Future = std::async(std::launch::async, ParseModel, path);
    }

    Model::~Model() = default;

    void Model::Update(){
        if (m_Uploaded || !m_Future.valid())
            return;
        if (m_Future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        std::vector<MeshData> data = m_Future.get();
        for (auto& md : data){
            Ref<Mesh> mesh = CreateRef<Mesh>(md.vertices, md.indices);
            if (!md.diffusePath.empty())
                mesh->SetDiffuseMap(Texture2D::Create(md.diffusePath));
            m_Meshes.push_back(mesh);
        }

        m_Uploaded = true;
        PHX_CORE_INFO("Loaded model '{0}' ({1} meshes)", m_Path, m_Meshes.size());
    }
}
