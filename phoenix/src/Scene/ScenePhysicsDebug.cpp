#include <Phoenix/Scene/Scene.h>
#include <Phoenix/Scene/Entity.h>
#include <Phoenix/Scene/Component.h>
#include <Phoenix/Physics/Physics.h>
#include <Phoenix/renderer/renderer.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <map>
#include <cmath>

namespace Phoenix{

    // Line-list builders for the physics debug overlay. Every helper appends
    // segment pairs in world space, transformed by `m` (rotation + translation).
    namespace{
        using Lines = std::vector<glm::vec3>;

        glm::mat4 RotationXYZ(const glm::vec3& r){
            return glm::rotate(glm::mat4(1.0f), r.x, { 1, 0, 0 })
                 * glm::rotate(glm::mat4(1.0f), r.y, { 0, 1, 0 })
                 * glm::rotate(glm::mat4(1.0f), r.z, { 0, 0, 1 });
        }

        // Entity pose without scale: collider sizes are scaled explicitly, as on Run.
        glm::mat4 PoseOf(const TransformComponent& t){
            return glm::translate(glm::mat4(1.0f), t.Translation) * RotationXYZ(t.Rotation);
        }

        glm::mat4 LocalPose(const glm::vec3& offset, const glm::vec3& rotation){
            return glm::translate(glm::mat4(1.0f), offset) * RotationXYZ(rotation);
        }

        void Segment(Lines& out, const glm::mat4& m, const glm::vec3& a, const glm::vec3& b){
            out.push_back(glm::vec3(m * glm::vec4(a, 1.0f)));
            out.push_back(glm::vec3(m * glm::vec4(b, 1.0f)));
        }

        // Arc in the plane spanned by u, v around center, from angle a0 to a1.
        void Arc(Lines& out, const glm::mat4& m, const glm::vec3& center, const glm::vec3& u, const glm::vec3& v,
                 float radius, float a0, float a1, int segments){
            glm::vec3 prev = center + radius * (std::cos(a0) * u + std::sin(a0) * v);
            for (int i = 1; i <= segments; i++){
                float a = a0 + (a1 - a0) * (float)i / (float)segments;
                glm::vec3 p = center + radius * (std::cos(a) * u + std::sin(a) * v);
                Segment(out, m, prev, p);
                prev = p;
            }
        }

        void Circle(Lines& out, const glm::mat4& m, const glm::vec3& center, const glm::vec3& u, const glm::vec3& v, float radius){
            Arc(out, m, center, u, v, radius, 0.0f, glm::two_pi<float>(), 24);
        }

        const glm::vec3 X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);

        void Box(Lines& out, const glm::mat4& m, const glm::vec3& h){
            glm::vec3 c[8];
            for (int i = 0; i < 8; i++)
                c[i] = { (i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z };
            const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7} };
            for (const auto& e : edges) { Segment(out, m, c[e[0]], c[e[1]]); }
        }

        void Sphere(Lines& out, const glm::mat4& m, float r){
            Circle(out, m, {}, X, Y, r);
            Circle(out, m, {}, Y, Z, r);
            Circle(out, m, {}, Z, X, r);
        }

        void Cylinder(Lines& out, const glm::mat4& m, float r, float hh){
            Circle(out, m, { 0, hh, 0 }, X, Z, r);
            Circle(out, m, { 0, -hh, 0 }, X, Z, r);
            for (const glm::vec3& d : { X, -X, Z, -Z }) { Segment(out, m, d * r + Y * hh, d * r - Y * hh); }
        }

        void Capsule(Lines& out, const glm::mat4& m, float r, float hh){
            Cylinder(out, m, r, hh);
            const float pi = glm::pi<float>();
            Arc(out, m, { 0, hh, 0 }, X, Y, r, 0.0f, pi, 12);
            Arc(out, m, { 0, hh, 0 }, Z, Y, r, 0.0f, pi, 12);
            Arc(out, m, { 0, -hh, 0 }, X, -Y, r, 0.0f, pi, 12);
            Arc(out, m, { 0, -hh, 0 }, Z, -Y, r, 0.0f, pi, 12);
        }

        void Cross(Lines& out, const glm::vec3& p, float size){
            Segment(out, glm::mat4(1.0f), p - X * size, p + X * size);
            Segment(out, glm::mat4(1.0f), p - Y * size, p + Y * size);
            Segment(out, glm::mat4(1.0f), p - Z * size, p + Z * size);
        }
    }

    void Scene::DrawPhysicsDebug(){
        // Batched by colour: one draw per colour.
        std::map<std::tuple<float, float, float>, Lines> batches;
        auto lines = [&](const glm::vec3& c) -> Lines& { return batches[{ c.r, c.g, c.b }]; };

        const glm::vec3 staticColor(0.35f, 0.85f, 0.35f);
        const glm::vec3 dynamicColor(0.25f, 0.65f, 1.0f);
        const glm::vec3 sleepingColor(0.35f, 0.4f, 0.6f);
        const glm::vec3 kinematicColor(1.0f, 0.6f, 0.2f);
        const glm::vec3 sensorColor(1.0f, 0.9f, 0.2f);
        const glm::vec3 characterColor(0.95f, 0.35f, 0.95f);
        const glm::vec3 fieldColor(0.2f, 1.0f, 1.0f);
        const glm::vec3 jointColor(1.0f, 0.3f, 0.3f);
        const glm::vec3 contactColor(1.0f, 0.1f, 0.1f);

        // Colliders, coloured by body type / state.
        auto rbView = m_Registry.view<RigidBodyComponent, TransformComponent>();
        for (auto entity : rbView){
            auto& rb = rbView.get<RigidBodyComponent>(entity);
            auto& t = rbView.get<TransformComponent>(entity);
            glm::vec3 color = rb.isSensor ? sensorColor
                            : rb.type == RigidBodyComponent::Type::Static ? staticColor
                            : rb.type == RigidBodyComponent::Type::Kinematic ? kinematicColor : dynamicColor;
            if (m_PhysicsWorld && rb.type == RigidBodyComponent::Type::Dynamic && !rb.isSensor
                && rb.runtimeBodyID != PhysicsWorld::InvalidBody && !m_PhysicsWorld->IsAwake(rb.runtimeBodyID))
                color = sleepingColor;
            Lines& out = lines(color);
            const glm::mat4 pose = PoseOf(t);
            const glm::vec3& s = t.Scale;

            if (auto* box = m_Registry.try_get<BoxColliderComponent>(entity))
                Box(out, pose * LocalPose(box->offset * s, box->rotation), box->halfExtents * s);
            if (auto* sphere = m_Registry.try_get<SphereColliderComponent>(entity))
                Sphere(out, pose * LocalPose(sphere->offset * s, {}), sphere->radius * std::max(s.x, std::max(s.y, s.z)));
            if (auto* capsule = m_Registry.try_get<CapsuleColliderComponent>(entity))
                Capsule(out, pose * LocalPose(capsule->offset * s, capsule->rotation),
                    capsule->radius * std::max(s.x, s.z), capsule->halfHeight * s.y);
            if (auto* cylinder = m_Registry.try_get<CylinderColliderComponent>(entity))
                Cylinder(out, pose * LocalPose(cylinder->offset * s, cylinder->rotation),
                    cylinder->radius * std::max(s.x, s.z), cylinder->halfHeight * s.y);
            if (m_Registry.any_of<MeshColliderComponent>(entity)){
                // Mesh colliders: their geometry is the mesh itself; mark the bounds.
                glm::vec3 lo(-0.5f), hi(0.5f);
                if (auto* mesh = m_Registry.try_get<MeshComponent>(entity); mesh && mesh->model){
                    if (!mesh->model->GetLocalBounds(lo, hi)) { lo = glm::vec3(-0.5f); hi = glm::vec3(0.5f); }
                }
                Box(out, pose * glm::translate(glm::mat4(1.0f), (lo + hi) * 0.5f * s), (hi - lo) * 0.5f * s);
            }
        }

        if (m_PhysicsDebug.showCharacters){
            auto view = m_Registry.view<CharacterControllerComponent, TransformComponent>();
            for (auto entity : view){
                auto& cc = view.get<CharacterControllerComponent>(entity);
                auto& t = view.get<TransformComponent>(entity);
                glm::mat4 pose = glm::translate(glm::mat4(1.0f), t.Translation); // capsule stays upright
                float r = cc.radius * std::max(t.Scale.x, t.Scale.z), hh = cc.halfHeight * t.Scale.y;
                Capsule(lines(characterColor), pose, r, hh);
                // Facing (-Z after yaw) and, while playing, a green dot when grounded.
                glm::vec3 facing(-std::sin(t.Rotation.y), 0.0f, -std::cos(t.Rotation.y));
                Segment(lines(characterColor), glm::mat4(1.0f), t.Translation, t.Translation + facing * (r + 0.4f));
                if (m_PhysicsWorld && cc.grounded) { Cross(lines(staticColor), t.Translation - Y * (hh + r), 0.15f); }
            }
        }

        if (m_PhysicsDebug.showForceFields){
            auto view = m_Registry.view<ForceFieldComponent, TransformComponent>();
            for (auto entity : view){
                auto& f = view.get<ForceFieldComponent>(entity);
                auto& t = view.get<TransformComponent>(entity);
                glm::mat4 pose = PoseOf(t);
                Lines& out = lines(fieldColor);
                if (f.shape == ForceFieldComponent::Shape::Sphere)
                    Sphere(out, pose, f.halfExtents.x * std::max(t.Scale.x, std::max(t.Scale.y, t.Scale.z)));
                else
                    Box(out, pose, f.halfExtents * t.Scale);
                if (f.mode == ForceFieldComponent::Mode::Directional && glm::dot(f.direction, f.direction) > 1e-8f){
                    glm::vec3 d = glm::normalize(f.direction) * (f.strength >= 0.0f ? 1.0f : -1.0f);
                    glm::vec3 side = std::abs(d.y) < 0.9f ? glm::normalize(glm::cross(d, Y)) : X;
                    Segment(out, pose, -d, d);                       // arrow shaft
                    Segment(out, pose, d, d * 0.6f + side * 0.25f);  // arrow head
                    Segment(out, pose, d, d * 0.6f - side * 0.25f);
                }
                else{
                    Cross(out, t.Translation, 0.4f);
                }
            }
        }

        if (m_PhysicsDebug.showJoints){
            auto view = m_Registry.view<JointComponent, TransformComponent>();
            for (auto entity : view){
                auto& t = view.get<TransformComponent>(entity);
                for (const auto& j : view.get<JointComponent>(entity).joints){
                    if (m_PhysicsWorld && j.runtimeJointID == PhysicsWorld::InvalidJoint) { continue; } // broken / failed
                    Lines& out = lines(jointColor);
                    Entity other = j.connectedTag.empty() ? Entity{} : FindEntityByTag(j.connectedTag);
                    glm::vec3 otherPoint = (other && other.HasComponent<TransformComponent>())
                        ? other.GetComponent<TransformComponent>().Translation : j.pivot;
                    if (m_PhysicsWorld || j.type == JointComponent::Type::Distance){
                        // Playing (pivots move with the bodies) or a distance joint: the link.
                        Segment(out, glm::mat4(1.0f), t.Translation, otherPoint);
                        Cross(out, otherPoint, 0.1f);
                        continue;
                    }
                    Cross(out, j.pivot, 0.15f);
                    Segment(out, glm::mat4(1.0f), t.Translation, j.pivot);
                    if (other) { Segment(out, glm::mat4(1.0f), j.pivot, otherPoint); }
                    if (j.type != JointComponent::Type::Point && j.type != JointComponent::Type::Fixed){
                        glm::vec3 axis = glm::dot(j.axis, j.axis) > 1e-8f ? glm::normalize(j.axis) : Y;
                        float len = j.type == JointComponent::Type::Slider && j.limitSlide
                            ? std::max(std::abs(j.minSlide), std::abs(j.maxSlide)) : 0.8f;
                        Segment(out, glm::mat4(1.0f), j.pivot - axis * len, j.pivot + axis * len);
                    }
                }
            }
        }

        // Ragdoll bodies (they have no components of their own).
        if (m_PhysicsWorld){
            for (auto entity : m_Registry.view<RagdollComponent>()){
                const auto& rd = m_Registry.get<RagdollComponent>(entity);
                for (const auto& part : rd.parts){
                    glm::vec3 position; glm::quat rotation;
                    m_PhysicsWorld->GetBodyPose(part.body, position, rotation);
                    Cross(lines(m_PhysicsWorld->IsAwake(part.body) ? dynamicColor : sleepingColor), position, 0.08f);
                    if (part.parentPart >= 0){
                        glm::vec3 parent; glm::quat unused;
                        m_PhysicsWorld->GetBodyPose(rd.parts[part.parentPart].body, parent, unused);
                        Segment(lines(jointColor), glm::mat4(1.0f), position, parent);
                    }
                }
            }
        }

        // Vehicles: wheel circles at the suspension attachment points.
        {
            auto view = m_Registry.view<VehicleComponent, TransformComponent>();
            for (auto entity : view){
                auto& v = view.get<VehicleComponent>(entity);
                glm::mat4 pose = PoseOf(view.get<TransformComponent>(entity));
                const float x = v.halfTrack;
                const glm::vec3 wheels[4] = { { -x, v.wheelHeight, v.frontAxle }, { x, v.wheelHeight, v.frontAxle },
                                              { -x, v.wheelHeight, v.rearAxle },  { x, v.wheelHeight, v.rearAxle } };
                for (const auto& w : wheels){
                    glm::vec3 center = w - Y * v.suspensionMax * 0.5f; // mid suspension travel
                    Circle(lines(kinematicColor), pose, center, Z, Y, v.wheelRadius);
                    Segment(lines(kinematicColor), pose, w, center);
                }
            }
        }

        if (m_PhysicsWorld && m_PhysicsDebug.showContacts){
            Lines& out = lines(contactColor);
            for (const auto& [point, normal] : m_DebugContacts){
                Cross(out, point, 0.05f);
                Segment(out, glm::mat4(1.0f), point, point + normal * 0.3f);
            }
        }

        for (const auto& [color, segments] : batches)
            Renderer::DrawLines(segments, { std::get<0>(color), std::get<1>(color), std::get<2>(color) });
    }
}
