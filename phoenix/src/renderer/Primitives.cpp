#include <Phoenix/renderer/Primitives.h>
#include <cmath>

namespace Phoenix::Primitives{

    static const float PI = 3.14159265358979323846f;

    Ref<Mesh> Cube(){
        // 24 vertices (per-face normals), 12 triangles.
        const glm::vec3 faces[6][4] = {
            {{-0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f}}, // +Z
            {{ 0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f}}, // -Z
            {{ 0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f}}, // +X
            {{-0.5f,-0.5f,-0.5f},{-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f, 0.5f,-0.5f}}, // -X
            {{-0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f,-0.5f},{-0.5f, 0.5f,-0.5f}}, // +Y
            {{-0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f, 0.5f},{-0.5f,-0.5f, 0.5f}}, // -Y
        };
        const glm::vec3 normals[6] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
        const glm::vec2 uv[4] = {{0,0},{1,0},{1,1},{0,1}};

        std::vector<Vertex> v;
        std::vector<uint32_t> idx;
        for (int f = 0; f < 6; f++){
            uint32_t base = (uint32_t)v.size();
            for (int i = 0; i < 4; i++) { v.push_back({ faces[f][i], normals[f], uv[i] }); }
            idx.push_back(base); idx.push_back(base + 1); idx.push_back(base + 2);
            idx.push_back(base); idx.push_back(base + 2); idx.push_back(base + 3);
        }
        return CreateRef<Mesh>(v, idx);
    }

    Ref<Mesh> Sphere(int segments, int rings){
        std::vector<Vertex> v;
        std::vector<uint32_t> idx;
        for (int y = 0; y <= rings; y++){
            float vY = (float)y / rings;
            float theta = vY * PI;
            float st = std::sin(theta), ct = std::cos(theta);
            for (int x = 0; x <= segments; x++){
                float u = (float)x / segments;
                float phi = u * 2.0f * PI;
                glm::vec3 n(st * std::cos(phi), ct, st * std::sin(phi));
                v.push_back({ n * 0.5f, n, glm::vec2(u, vY) });
            }
        }
        int stride = segments + 1;
        for (int y = 0; y < rings; y++){
            for (int x = 0; x < segments; x++){
                uint32_t a = y * stride + x, b = a + 1, c = a + stride, d = c + 1;
                idx.push_back(a); idx.push_back(c); idx.push_back(b);
                idx.push_back(b); idx.push_back(c); idx.push_back(d);
            }
        }
        return CreateRef<Mesh>(v, idx);
    }

    Ref<Mesh> Cylinder(int segments){
        std::vector<Vertex> v;
        std::vector<uint32_t> idx;
        // side
        for (int i = 0; i <= segments; i++){
            float u = (float)i / segments, a = u * 2.0f * PI;
            float ca = std::cos(a), sa = std::sin(a);
            glm::vec3 n(ca, 0.0f, sa);
            v.push_back({ glm::vec3(ca * 0.5f,  0.5f, sa * 0.5f), n, glm::vec2(u, 1.0f) });
            v.push_back({ glm::vec3(ca * 0.5f, -0.5f, sa * 0.5f), n, glm::vec2(u, 0.0f) });
        }
        for (int i = 0; i < segments; i++){
            uint32_t t0 = i * 2, b0 = i * 2 + 1, t1 = (i + 1) * 2, b1 = (i + 1) * 2 + 1;
            idx.push_back(t0); idx.push_back(b0); idx.push_back(t1);
            idx.push_back(t1); idx.push_back(b0); idx.push_back(b1);
        }
        // top + bottom caps
        for (int cap = 0; cap < 2; cap++){
            float y = (cap == 0) ? 0.5f : -0.5f;
            glm::vec3 n(0.0f, (cap == 0) ? 1.0f : -1.0f, 0.0f);
            uint32_t center = (uint32_t)v.size();
            v.push_back({ glm::vec3(0.0f, y, 0.0f), n, glm::vec2(0.5f, 0.5f) });
            uint32_t start = (uint32_t)v.size();
            for (int i = 0; i <= segments; i++){
                float a = (float)i / segments * 2.0f * PI;
                v.push_back({ glm::vec3(std::cos(a) * 0.5f, y, std::sin(a) * 0.5f), n,
                              glm::vec2(0.5f + std::cos(a) * 0.5f, 0.5f + std::sin(a) * 0.5f) });
            }
            for (int i = 0; i < segments; i++){
                if (cap == 0){ idx.push_back(center); idx.push_back(start + i); idx.push_back(start + i + 1); }
                else         { idx.push_back(center); idx.push_back(start + i + 1); idx.push_back(start + i); }
            }
        }
        return CreateRef<Mesh>(v, idx);
    }

    Ref<Mesh> Cone(int segments){
        std::vector<Vertex> v;
        std::vector<uint32_t> idx;
        // side (base ring + apex), slope normals
        for (int i = 0; i <= segments; i++){
            float a = (float)i / segments * 2.0f * PI;
            glm::vec3 n = glm::normalize(glm::vec3(std::cos(a), 0.5f, std::sin(a)));
            v.push_back({ glm::vec3(std::cos(a) * 0.5f, -0.5f, std::sin(a) * 0.5f), n, glm::vec2((float)i / segments, 0.0f) });
        }
        uint32_t apexStart = (uint32_t)v.size();
        for (int i = 0; i <= segments; i++){
            float a = (float)i / segments * 2.0f * PI;
            glm::vec3 n = glm::normalize(glm::vec3(std::cos(a), 0.5f, std::sin(a)));
            v.push_back({ glm::vec3(0.0f, 0.5f, 0.0f), n, glm::vec2((float)i / segments, 1.0f) });
        }
        for (int i = 0; i < segments; i++){
            idx.push_back(i); idx.push_back(i + 1); idx.push_back(apexStart + i);
        }
        // base cap
        uint32_t center = (uint32_t)v.size();
        v.push_back({ glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(0, -1, 0), glm::vec2(0.5f, 0.5f) });
        uint32_t start = (uint32_t)v.size();
        for (int i = 0; i <= segments; i++){
            float a = (float)i / segments * 2.0f * PI;
            v.push_back({ glm::vec3(std::cos(a) * 0.5f, -0.5f, std::sin(a) * 0.5f), glm::vec3(0, -1, 0),
                          glm::vec2(0.5f + std::cos(a) * 0.5f, 0.5f + std::sin(a) * 0.5f) });
        }
        for (int i = 0; i < segments; i++){ idx.push_back(center); idx.push_back(start + i + 1); idx.push_back(start + i); }
        return CreateRef<Mesh>(v, idx);
    }

    Ref<Mesh> Plane(){
        std::vector<Vertex> v = {
            { {-0.5f, 0.0f, -0.5f}, {0, 1, 0}, {0, 0} },
            { { 0.5f, 0.0f, -0.5f}, {0, 1, 0}, {1, 0} },
            { { 0.5f, 0.0f,  0.5f}, {0, 1, 0}, {1, 1} },
            { {-0.5f, 0.0f,  0.5f}, {0, 1, 0}, {0, 1} },
        };
        std::vector<uint32_t> idx = { 0, 2, 1, 0, 3, 2 };
        return CreateRef<Mesh>(v, idx);
    }
}
