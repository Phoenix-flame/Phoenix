#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/core/assert.h>
#include <Phoenix/Scene/SceneCamera.h>
#include <glm/gtc/matrix_transform.hpp>
#include <Phoenix/renderer/shader.h>
#include <Phoenix/renderer/VertexArray.h>
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

    struct MeshComponent{
        MeshComponent() = default;
        MeshComponent(const MeshComponent& other) = default;
    };


    struct CubeComponent{
        glm::vec3 ambient = glm::vec3(1.0f, 0.5f, 0.31f);
        glm::vec3 diffuse = glm::vec3(1.0f, 0.5f, 0.31f);
        glm::vec3 specular = glm::vec3(0.5f, 0.5f, 0.5f);
        float shininess = 32.0f;
        CubeComponent() = default;
        CubeComponent(const CubeComponent&) = default;
    };

    
    struct LightComponent{
        glm::vec3 ambient = glm::vec3(1.0f);
        glm::vec3 diffuse = glm::vec3(0.2f) * ambient;
        glm::vec3 specular = glm::vec3(1.0f);
        LightComponent() = default;
        LightComponent(const LightComponent&) = default;
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