#include <Phoenix/renderer/Mesh.h>
#include <Phoenix/core/log.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>

#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <set>
#include <string>
#include <functional>

namespace Phoenix{

    Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices){
        m_IndexCount = (uint32_t)indices.size();

        // Retain positions + indices on the CPU for physics collider generation.
        m_Positions.reserve(vertices.size());
        for (const auto& v : vertices) { m_Positions.push_back(v.Position); }
        m_Indices = indices;

        m_VertexArray = CreateRef<VertexArray>();
        m_VertexArray->Bind();

        Ref<VertexBuffer> vertexBuffer = CreateRef<VertexBuffer>(
            (float*)vertices.data(), (int)(vertices.size() * sizeof(Vertex)));
        // Bone indices/weights are part of every vertex (defaulted for static meshes)
        // so the stride matches sizeof(Vertex) and the skinning shader can read them.
        BufferLayout layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoords" },
            { ShaderDataType::Float4, "a_BoneIDs" },
            { ShaderDataType::Float4, "a_Weights" }
        };
        vertexBuffer->SetLayout(layout);
        m_VertexArray->AddVertexBuffer(vertexBuffer);

        Ref<IndexBuffer> indexBuffer = CreateRef<IndexBuffer>(
            (uint32_t*)indices.data(), (uint32_t)indices.size());
        m_VertexArray->SetIndexBuffer(indexBuffer);
    }

    // ---- Assimp -> glm conversions ----

    static glm::mat4 ToGlm(const aiMatrix4x4& m){
        glm::mat4 r;
        r[0][0] = m.a1; r[1][0] = m.a2; r[2][0] = m.a3; r[3][0] = m.a4;
        r[0][1] = m.b1; r[1][1] = m.b2; r[2][1] = m.b3; r[3][1] = m.b4;
        r[0][2] = m.c1; r[1][2] = m.c2; r[2][2] = m.c3; r[3][2] = m.c4;
        r[0][3] = m.d1; r[1][3] = m.d2; r[2][3] = m.d3; r[3][3] = m.d4;
        return r;
    }
    static glm::vec3 ToGlm(const aiVector3D& v){ return { v.x, v.y, v.z }; }
    static glm::quat ToGlm(const aiQuaternion& q){ return glm::quat(q.w, q.x, q.y, q.z); }

    // ---- Worker-thread parsing (no GL calls) ----

    static void SetVertexBoneData(Vertex& v, int boneID, float weight){
        if (weight <= 0.0f) { return; }
        for (int i = 0; i < 4; i++){
            if (v.BoneIDs[i] < 0.0f){
                v.BoneIDs[i] = (float)boneID;
                v.Weights[i] = weight;
                return;
            }
        }
    }

    // Read this mesh's bones, assigning each a stable id in the shared boneInfoMap,
    // and distribute the per-vertex weights.
    static void ExtractBoneWeights(std::vector<Vertex>& vertices, aiMesh* mesh,
            std::map<std::string, BoneInfo>& boneInfoMap, int& boneCounter){
        for (unsigned int i = 0; i < mesh->mNumBones; i++){
            aiBone* bone = mesh->mBones[i];
            std::string name = bone->mName.C_Str();
            int boneID;
            auto it = boneInfoMap.find(name);
            if (it == boneInfoMap.end()){
                BoneInfo info;
                info.id = boneCounter;
                info.offset = ToGlm(bone->mOffsetMatrix);
                boneInfoMap[name] = info;
                boneID = boneCounter++;
            }
            else{
                boneID = it->second.id;
            }
            for (unsigned int w = 0; w < bone->mNumWeights; w++){
                unsigned int vid = bone->mWeights[w].mVertexId;
                if (vid < vertices.size())
                    SetVertexBoneData(vertices[vid], boneID, bone->mWeights[w].mWeight);
            }
        }
    }

    static void ProcessMeshData(aiMesh* mesh, const aiScene* scene, const std::string& directory,
            std::vector<MeshData>& out, std::map<std::string, BoneInfo>& boneInfoMap, int& boneCounter){
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

        ExtractBoneWeights(data.vertices, mesh, boneInfoMap, boneCounter);

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

    static void ProcessNodeData(aiNode* node, const aiScene* scene, const std::string& directory,
            std::vector<MeshData>& out, std::map<std::string, BoneInfo>& boneInfoMap, int& boneCounter){
        for (unsigned int i = 0; i < node->mNumMeshes; i++)
            ProcessMeshData(scene->mMeshes[node->mMeshes[i]], scene, directory, out, boneInfoMap, boneCounter);

        for (unsigned int i = 0; i < node->mNumChildren; i++)
            ProcessNodeData(node->mChildren[i], scene, directory, out, boneInfoMap, boneCounter);
    }

    static void ReadHierarchy(AssimpNodeData& dest, const aiNode* src){
        dest.name = src->mName.C_Str();
        dest.transformation = ToGlm(src->mTransformation);
        dest.children.reserve(src->mNumChildren);
        for (unsigned int i = 0; i < src->mNumChildren; i++){
            AssimpNodeData child;
            ReadHierarchy(child, src->mChildren[i]);
            dest.children.push_back(std::move(child));
        }
    }

    static Ref<Animation> LoadAnimation(const aiAnimation* anim, const aiScene* scene,
            std::map<std::string, BoneInfo>& boneInfoMap, int& boneCounter, const glm::mat4& globalInverse){
        auto out = CreateRef<Animation>();
        out->m_Name = anim->mName.C_Str();
        out->m_Duration = (float)anim->mDuration;
        out->m_TicksPerSecond = anim->mTicksPerSecond != 0.0 ? (float)anim->mTicksPerSecond : 25.0f;
        out->m_GlobalInverseTransform = globalInverse;
        ReadHierarchy(out->m_RootNode, scene->mRootNode);

        for (unsigned int i = 0; i < anim->mNumChannels; i++){
            aiNodeAnim* channel = anim->mChannels[i];
            std::string boneName = channel->mNodeName.C_Str();

            // A channel may target a node that wasn't a skinning bone in any mesh; give
            // it an id so the hierarchy walk still finds its animated local transform.
            if (boneInfoMap.find(boneName) == boneInfoMap.end())
                boneInfoMap[boneName].id = boneCounter++;
            int id = boneInfoMap[boneName].id;

            std::vector<KeyPosition> positions;
            positions.reserve(channel->mNumPositionKeys);
            for (unsigned int k = 0; k < channel->mNumPositionKeys; k++)
                positions.push_back({ ToGlm(channel->mPositionKeys[k].mValue), (float)channel->mPositionKeys[k].mTime });

            std::vector<KeyRotation> rotations;
            rotations.reserve(channel->mNumRotationKeys);
            for (unsigned int k = 0; k < channel->mNumRotationKeys; k++)
                rotations.push_back({ ToGlm(channel->mRotationKeys[k].mValue), (float)channel->mRotationKeys[k].mTime });

            std::vector<KeyScale> scales;
            scales.reserve(channel->mNumScalingKeys);
            for (unsigned int k = 0; k < channel->mNumScalingKeys; k++)
                scales.push_back({ ToGlm(channel->mScalingKeys[k].mValue), (float)channel->mScalingKeys[k].mTime });

            out->m_Bones.emplace_back(boneName, id, std::move(positions), std::move(rotations), std::move(scales));
        }

        out->m_BoneInfoMap = boneInfoMap; // snapshot (meshes' bones + channels' bones)
        return out;
    }

    static ModelData ParseModel(std::string path){
        ModelData result;

        Assimp::Importer importer;
        // Collapse FBX pivot helpers ("$AssimpFbx$" nodes) so each bone is a single node
        // with one full translation/rotation/scale channel -- which the keyframe sampler
        // expects. Without this, FBX (e.g. Mixamo) animations are scattered across
        // synthetic single-purpose nodes and reconstruct incorrectly (splayed limbs).
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate
            | aiProcess_GenSmoothNormals
            | aiProcess_FlipUVs
            | aiProcess_JoinIdenticalVertices
            | aiProcess_LimitBoneWeights);

        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode){
            PHX_CORE_ERROR("Assimp failed to load '{0}': {1}", path, importer.GetErrorString());
            return result;
        }

        std::string directory = path.substr(0, path.find_last_of('/'));
        int boneCounter = 0;
        ProcessNodeData(scene->mRootNode, scene, directory, result.meshes, result.boneInfoMap, boneCounter);

        glm::mat4 globalInverse = glm::inverse(ToGlm(scene->mRootNode->mTransformation));
        for (unsigned int i = 0; i < scene->mNumAnimations; i++)
            result.animations.push_back(LoadAnimation(scene->mAnimations[i], scene, result.boneInfoMap, boneCounter, globalInverse));

        return result;
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

        ModelData data = m_Future.get();
        m_BoneInfoMap = std::move(data.boneInfoMap);
        m_Animations = std::move(data.animations);

        for (auto& md : data.meshes){
            Ref<Mesh> mesh = CreateRef<Mesh>(md.vertices, md.indices);
            if (!md.diffusePath.empty())
                mesh->SetDiffuseMap(Texture2D::Create(md.diffusePath));
            m_Meshes.push_back(mesh);
        }

        m_Uploaded = true;
        PHX_CORE_INFO("Loaded model '{0}' ({1} meshes, {2} animations)", m_Path, m_Meshes.size(), m_Animations.size());

        // Skeleton diagnostics: if total bones exceed MAX_BONES the shader clamps the
        // overflow to identity, which scatters/splays the limbs that use those bones.
        size_t boneCount = m_BoneInfoMap.size();
        PHX_CORE_INFO("  skeleton: {0} bones (shader cap MAX_BONES={1})", boneCount, MAX_BONES);
        if (boneCount > (size_t)MAX_BONES)
            PHX_CORE_ERROR("  -> bone count {0} EXCEEDS MAX_BONES {1}: raise MAX_BONES and the u_BoneMatrices[] size in the shaders", boneCount, MAX_BONES);
        for (size_t i = 0; i < m_Animations.size(); i++)
            PHX_CORE_INFO("  clip {0}: '{1}' duration={2} ticks @ {3} tps", i, m_Animations[i]->GetName(), m_Animations[i]->GetDuration(), m_Animations[i]->GetTicksPerSecond());

        // Verify every skinning bone is reachable in the animation's node hierarchy.
        // Any that are missing keep an identity matrix in the shader, which splays the
        // limbs that depend on them.
        if (!m_Animations.empty()){
            std::set<std::string> nodeNames;
            std::function<void(const AssimpNodeData&)> collect = [&](const AssimpNodeData& n){
                nodeNames.insert(n.name);
                for (const auto& c : n.children) { collect(c); }
            };
            collect(m_Animations[0]->GetRootNode());
            int missing = 0;
            for (const auto& kv : m_BoneInfoMap){
                if (nodeNames.find(kv.first) == nodeNames.end()){
                    PHX_CORE_ERROR("  skinning bone '{0}' (id {1}) NOT in node hierarchy -> identity matrix", kv.first, kv.second.id);
                    missing++;
                }
            }
            PHX_CORE_INFO("  hierarchy has {0} nodes; {1}/{2} skinning bones unreachable", nodeNames.size(), missing, m_BoneInfoMap.size());

            const glm::mat4& gi = m_Animations[0]->m_GlobalInverseTransform;
            PHX_CORE_INFO("  globalInverse row0=[{0:.3f} {1:.3f} {2:.3f} {3:.3f}] row3=[{4:.3f} {5:.3f} {6:.3f} {7:.3f}]",
                gi[0][0], gi[1][0], gi[2][0], gi[3][0], gi[0][3], gi[1][3], gi[2][3], gi[3][3]);
            const glm::mat4& rt = m_Animations[0]->GetRootNode().transformation;
            PHX_CORE_INFO("  rootNode '{0}' row0=[{1:.3f} {2:.3f} {3:.3f} {4:.3f}]",
                m_Animations[0]->GetRootNode().name, rt[0][0], rt[1][0], rt[2][0], rt[3][0]);
        }
    }
}
