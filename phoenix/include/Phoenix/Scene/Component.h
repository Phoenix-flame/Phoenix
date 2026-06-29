#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/core/assert.h>
#include <Phoenix/Scene/SceneCamera.h>
#include <glm/gtc/matrix_transform.hpp>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/VertexArray.h>
#include <Phoenix/renderer/Mesh.h>
#include <Phoenix/Scene/ScriptableEntity.h>
namespace Phoenix{

    struct TagComponent{
		std::string Tag;

		TagComponent() = default;
		TagComponent(const TagComponent&) = default;
		TagComponent(const std::string& tag) : Tag(tag) {}
	};

    struct CameraComponent{
        SceneCamera camera;
        bool primary = false;
        CameraComponent() = default;
        CameraComponent(const CameraComponent& other) = default;
    };

    struct Material
    {
        glm::vec3 ambient = glm::vec3(1.0f, 0.5f, 0.31f);
        glm::vec3 diffuse = glm::vec3(1.0f, 0.5f, 0.31f);
        glm::vec3 specular = glm::vec3(0.5f, 0.5f, 0.5f);
        float shininess = 32.0f;
        // Self-illumination for the bloom/glow effect. emissive * emissiveStrength is
        // added to the lit colour; strength > 1 pushes it into HDR so it blooms.
        glm::vec3 emissive = glm::vec3(0.0f);
        float emissiveStrength = 0.0f;
        // Environment (sky/ground) reflection blended in by this amount (0 = none,
        // 1 = mirror). Reflects a procedural environment, not the scene itself.
        float reflectivity = 0.0f;
    };

    struct MeshComponent{
        Ref<Model> model;
        Material material;
        MeshComponent() = default;
        MeshComponent(const MeshComponent& other) = default;
        MeshComponent(const Ref<Model>& model) : model(model) {}
    };

    struct CubeComponent{
        Material material;
        CubeComponent() = default;
        CubeComponent(const CubeComponent&) = default;
    };

    // Physics. Type values match PhysicsWorld::BodyType. runtimeBodyID is filled in
    // when the scene's physics simulation starts (0xffffffff = no body / not running).
    struct RigidBodyComponent{
        enum class Type { Static = 0, Dynamic = 1, Kinematic = 2 };
        Type type = Type::Dynamic;
        uint32_t runtimeBodyID = 0xffffffff;
        RigidBodyComponent() = default;
        RigidBodyComponent(const RigidBodyComponent&) = default;
    };

    struct BoxColliderComponent{
        glm::vec3 halfExtents = { 0.5f, 0.5f, 0.5f };
        BoxColliderComponent() = default;
        BoxColliderComponent(const BoxColliderComponent&) = default;
    };

    // When present, the entity is drawn as a wireframe ("mesh network"). The bool
    // exists only so the type is non-empty (entt returns void from emplace<> for
    // empty types, which AddComponent's `T&` return can't bind to).
    struct WireframeComponent{
        bool enabled = true;
        WireframeComponent() = default;
        WireframeComponent(const WireframeComponent&) = default;
    };

    // A Lua script (edited inline, serialized). It runs while the scene is playing;
    // the live runtime is owned by the Scene, not stored here.
    struct LuaScriptComponent{
        std::string source =
            "-- runs while playing (press Run)\n"
            "local t = 0.0\n"
            "function OnUpdate(dt)\n"
            "    t = t + dt\n"
            "    local x, y, z = GetTranslation()\n"
            "    SetTranslation(x, y, z)\n"
            "end\n";
        LuaScriptComponent() = default;
        LuaScriptComponent(const LuaScriptComponent&) = default;
    };


    struct DirLightComponent{
        // diffuse is the dominant term so the light is actually directional; its own
        // ambient is small since the scene has a separate global ambient. (Previously
        // ambient=1.0 flooded everything flat, making the light look like it did nothing.)
        glm::vec3 ambient = glm::vec3(0.05f);
        glm::vec3 diffuse = glm::vec3(0.8f);
        glm::vec3 specular = glm::vec3(1.0f);
        bool isActive = true;
        DirLightComponent() = default;
        DirLightComponent(const DirLightComponent&) = default;
    };

    struct PointLightComponent: public DirLightComponent{
        float constant = 1.0f;
        float linear = 0.02f;
        float quadratic = 0.099f; 
        void SetConstant(float constant) { this->constant = constant;} 
        void SetLinear(float linear) { this->linear = linear;} 
        void SetQuadratic(float quadratic) { this->quadratic = quadratic;} 
        PointLightComponent() = default;
        PointLightComponent(const PointLightComponent&) = default;
    };


    struct TransformComponent{
        glm::vec3 Translation = {0.0f, 0.0f, 0.0f};
        glm::vec3 Rotation = {0.0f, 0.0f, 0.0f};
        glm::vec3 Scale = {1.0f, 1.0f, 1.0f};
        TransformComponent() = default;
		TransformComponent(const TransformComponent&) = default;
		TransformComponent(const glm::vec3& translation) : Translation(translation) {}

		glm::mat4 GetTransform() const{
			glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), Rotation.x, { 1, 0, 0 })
				* glm::rotate(glm::mat4(1.0f), Rotation.y, { 0, 1, 0 })
				* glm::rotate(glm::mat4(1.0f), Rotation.z, { 0, 0, 1 });

			return glm::translate(glm::mat4(1.0f), Translation)
				* rotation
				* glm::scale(glm::mat4(1.0f), Scale);
		}
    };


    struct NativeScriptComponent{
        ScriptableEntity* Instance = nullptr;

		ScriptableEntity*(*InstantiateScript)();
		void (*DestroyScript)(NativeScriptComponent*);

		template<typename T>
		void Bind(){
			InstantiateScript = []() { return static_cast<ScriptableEntity*>(new T()); };
			DestroyScript = [](NativeScriptComponent* nsc) { delete nsc->Instance; nsc->Instance = nullptr; };
		}
    };
}