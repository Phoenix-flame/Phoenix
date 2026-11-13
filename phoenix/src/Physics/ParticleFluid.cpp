#include <Phoenix/Physics/ParticleFluid.h>
#include <Phoenix/Compute/Compute.h>
#include "../Compute/ComputeCL.h"
#include "../Compute/SharedSource.h"
#include "../Compute/Workers.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

// Arrays the kernels read and write live in device memory there and are plain arrays
// here (the shared source below is stringified before this expands, so it keeps the
// name for the OpenCL prelude to define as __global).
#define PHX_GLOBAL

namespace Phoenix{

    // A float's bits as an int (OpenCL's as_int; the prelude maps it there).
    static int as_int_bits(float f) { int i; std::memcpy(&i, &f, sizeof(int)); return i; }

    // Colliders are packed as COLLIDER_FLOATS floats: rows 0-2 of the inverse (world ->
    // local) transform, the size, the type (int bits), then the bounding sphere.
    static constexpr int COLLIDER_FLOATS = 20;

    // Per-particle solver steps, shared with the kernels (see SharedSource.h). The grid
    // (built on the host) gives every particle a cell; neighbours are scanned over the
    // 27 cells around it in a fixed order, and within a cell in sorted order, so every
    // sum adds up in the same order on any backend.
    PHX_SHARED_SOURCE(s_FluidSource,
    static float phx_poly6(float r2, float h2, float coef){
        float d = h2 - r2;
        return d > 0.0f ? coef * d * d * d : 0.0f;
    }

    // Density constraint of particle i (rest = 1) and its Lagrange multiplier. The
    // constraint is one-sided: it resists compression only. Particles at the free surface
    // have fewer neighbours and read as under-dense; pulling them in overshoots and
    // clumps (the classic PBF surface problem), so they are left to the anti-clumping
    // term and viscosity instead.
    static float phx_fluid_lambda(int i, PHX_GLOBAL const float* pred, PHX_GLOBAL const int* sorted,
                                  PHX_GLOBAL const int* cellStart, PHX_GLOBAL const int* cellOf,
                                  int dx, int dy, int dz, float h, float poly6, float spiky, float eps){
        float px = pred[i * 4], py = pred[i * 4 + 1], pz = pred[i * 4 + 2];
        int cell = cellOf[i];
        int cz = cell / (dx * dy), cy = (cell / dx) % dy, cx = cell % dx;
        float h2 = h * h;
        float rho = 0.0f, gx = 0.0f, gy = 0.0f, gz = 0.0f, sum2 = 0.0f;
        for (int z = (cz > 0 ? cz - 1 : 0); z <= (cz < dz - 1 ? cz + 1 : cz); z++)
        for (int y = (cy > 0 ? cy - 1 : 0); y <= (cy < dy - 1 ? cy + 1 : cy); y++)
        for (int x = (cx > 0 ? cx - 1 : 0); x <= (cx < dx - 1 ? cx + 1 : cx); x++){
            int c = (z * dy + y) * dx + x;
            for (int k = cellStart[c]; k < cellStart[c + 1]; k++){
                int j = sorted[k];
                float rx = px - pred[j * 4], ry = py - pred[j * 4 + 1], rz = pz - pred[j * 4 + 2];
                float r2 = rx * rx + ry * ry + rz * rz;
                if (r2 >= h2) { continue; }
                rho += phx_poly6(r2, h2, poly6);
                if (j == i) { continue; }
                float r = phx_sqrt(r2);
                if (r < 1e-6f) { continue; }
                float g = spiky * (h - r) * (h - r) / r;
                gx -= g * rx; gy -= g * ry; gz -= g * rz;
                sum2 += g * g * r2;
            }
        }
        float C = rho - 1.0f;
        if (C <= 0.0f) { return 0.0f; }
        return -C / (gx * gx + gy * gy + gz * gz + sum2 + eps);
    }

    // Signed distance from a local point to a collider shape, and its local normal.
    static float phx_fluid_sdf(int type, const float* s, float x, float y, float z, float* n){
        if (type == 1){   // sphere
            float l = phx_sqrt(x * x + y * y + z * z);
            float inv = l > 1e-6f ? 1.0f / l : 0.0f;
            n[0] = l > 1e-6f ? x * inv : 0.0f; n[1] = l > 1e-6f ? y * inv : 1.0f; n[2] = z * inv;
            return l - s[0];
        }
        if (type == 2){   // capsule along Y
            float cy = y < -s[0] ? -s[0] : (y > s[0] ? s[0] : y);
            float ax = x, ay = y - cy, az = z;
            float l = phx_sqrt(ax * ax + ay * ay + az * az);
            float inv = l > 1e-6f ? 1.0f / l : 0.0f;
            n[0] = ax * inv; n[1] = l > 1e-6f ? ay * inv : 1.0f; n[2] = az * inv;
            return l - s[1];
        }
        if (type == 3){   // cylinder along Y
            float rl = phx_sqrt(x * x + z * z);
            float qr = rl - s[1], qy = phx_abs(y) - s[0];
            float sy = y < 0.0f ? -1.0f : 1.0f;
            float inv = rl > 1e-6f ? 1.0f / rl : 0.0f;
            if (qr > 0.0f && qy > 0.0f){
                float l = phx_sqrt(qr * qr + qy * qy);
                n[0] = x * inv * qr / l; n[1] = sy * qy / l; n[2] = z * inv * qr / l;
                return l;
            }
            if (qr > qy){ n[0] = x * inv; n[1] = 0.0f; n[2] = z * inv; return qr; }
            n[0] = 0.0f; n[1] = sy; n[2] = 0.0f;
            return qy;
        }
        // box
        float ax = phx_abs(x) - s[0], ay = phx_abs(y) - s[1], az = phx_abs(z) - s[2];
        float sx = x < 0.0f ? -1.0f : 1.0f, sy = y < 0.0f ? -1.0f : 1.0f, sz = z < 0.0f ? -1.0f : 1.0f;
        if (ax > 0.0f || ay > 0.0f || az > 0.0f){
            float ox = ax > 0.0f ? ax : 0.0f, oy = ay > 0.0f ? ay : 0.0f, oz = az > 0.0f ? az : 0.0f;
            float l = phx_sqrt(ox * ox + oy * oy + oz * oz);
            if (l > 1e-6f){ n[0] = sx * ox / l; n[1] = sy * oy / l; n[2] = sz * oz / l; return l; }
        }
        n[0] = 0.0f; n[1] = 0.0f; n[2] = 0.0f;
        if (ax >= ay && ax >= az){ n[0] = sx; return ax; }
        if (ay >= az){ n[1] = sy; return ay; }
        n[2] = sz;
        return az;
    }

    // Position correction of particle i (density and anti-clumping), then pushed out of
    // the colliders. Writes the new predicted position, the last collider it touched and
    // the total push this step (summed over iterations: `first` starts the sum).
    //
    // Contacts take their normal from where the particle was at the start of the step
    // (always outside): the face it came from. Pushing out of the current position's
    // nearest face instead is ambiguous inside overlapping solids (a wall standing on a
    // floor: inside both, the wall's nearest face may be its bottom, which pushes the
    // particle down through the floor), and lets fast particles tunnel.
    static void phx_fluid_delta(int i, PHX_GLOBAL const float* pos, PHX_GLOBAL const float* pred, PHX_GLOBAL const float* lambda,
                                PHX_GLOBAL const int* sorted, PHX_GLOBAL const int* cellStart, PHX_GLOBAL const int* cellOf,
                                int dx, int dy, int dz, float h, float poly6, float spiky, float cohesion, float wdq,
                                PHX_GLOBAL const float* colliders, int colliderCount, float radius, float maxMove, int first,
                                PHX_GLOBAL float* outPred, PHX_GLOBAL int* outPushed, PHX_GLOBAL float* outPush){
        float px = pred[i * 4], py = pred[i * 4 + 1], pz = pred[i * 4 + 2];
        float li = lambda[i];
        int cell = cellOf[i];
        int cz = cell / (dx * dy), cy = (cell / dx) % dy, cx = cell % dx;
        float h2 = h * h;
        float ddx = 0.0f, ddy = 0.0f, ddz = 0.0f;
        for (int z = (cz > 0 ? cz - 1 : 0); z <= (cz < dz - 1 ? cz + 1 : cz); z++)
        for (int y = (cy > 0 ? cy - 1 : 0); y <= (cy < dy - 1 ? cy + 1 : cy); y++)
        for (int x = (cx > 0 ? cx - 1 : 0); x <= (cx < dx - 1 ? cx + 1 : cx); x++){
            int c = (z * dy + y) * dx + x;
            for (int k = cellStart[c]; k < cellStart[c + 1]; k++){
                int j = sorted[k];
                if (j == i) { continue; }
                float rx = px - pred[j * 4], ry = py - pred[j * 4 + 1], rz = pz - pred[j * 4 + 2];
                float r2 = rx * rx + ry * ry + rz * rz;
                if (r2 >= h2) { continue; }
                float r = phx_sqrt(r2);
                if (r < 1e-6f) { continue; }
                float ratio = phx_poly6(r2, h2, poly6) / wdq;
                ratio = ratio * ratio;
                float scorr = -cohesion * ratio * ratio;
                float g = (li + lambda[j] + scorr) * spiky * (h - r) * (h - r) / r;
                ddx -= g * rx; ddy -= g * ry; ddz -= g * rz;
            }
        }
        // At most `maxMove` per iteration: a large single correction could carry a
        // particle through a thin wall, and the push below would resolve it on the far side.
        float dl2 = ddx * ddx + ddy * ddy + ddz * ddz;
        if (dl2 > maxMove * maxMove){
            float k = maxMove / phx_sqrt(dl2);
            ddx *= k; ddy *= k; ddz *= k;
        }
        float qx = px + ddx, qy = py + ddy, qz = pz + ddz;

        int pushed = -1;
        float tx = 0.0f, ty = 0.0f, tz = 0.0f;
        for (int c = 0; c < colliderCount; c++){
            PHX_GLOBAL const float* m = colliders + c * 20;
            float bx = qx - m[16], by = qy - m[17], bz = qz - m[18];
            float reach = m[19] + radius;
            if (bx * bx + by * by + bz * bz > reach * reach) { continue; }
            float lx = (m[0] * qx + m[1] * qy) + (m[2] * qz + m[3]);
            float ly = (m[4] * qx + m[5] * qy) + (m[6] * qz + m[7]);
            float lz = (m[8] * qx + m[9] * qy) + (m[10] * qz + m[11]);
            float s[3] = { m[12], m[13], m[14] };
            float n[3];
            int type = as_int_bits(m[15]);
            float ox = pos[i * 4], oy = pos[i * 4 + 1], oz = pos[i * 4 + 2];
            float sx = (m[0] * ox + m[1] * oy) + (m[2] * oz + m[3]);
            float sy = (m[4] * ox + m[5] * oy) + (m[6] * oz + m[7]);
            float sz = (m[8] * ox + m[9] * oy) + (m[10] * oz + m[11]);
            float d0 = phx_fluid_sdf(type, s, sx, sy, sz, n);
            float d;
            if (d0 > 0.0f){
                // Distance to the plane of the face the particle came from.
                float px0 = sx - n[0] * d0, py0 = sy - n[1] * d0, pz0 = sz - n[2] * d0;
                d = (lx - px0) * n[0] + (ly - py0) * n[1] + (lz - pz0) * n[2];
            }
            else { d = phx_fluid_sdf(type, s, lx, ly, lz, n); }   // started inside: nearest face
            if (d >= radius) { continue; }
            // Local normal back to world: the inverse's columns are the rotation's rows.
            float wx = (m[0] * n[0] + m[4] * n[1]) + m[8] * n[2];
            float wy = (m[1] * n[0] + m[5] * n[1]) + m[9] * n[2];
            float wz = (m[2] * n[0] + m[6] * n[1]) + m[10] * n[2];
            float depth = radius - d;
            qx += wx * depth; qy += wy * depth; qz += wz * depth;
            tx += wx * depth; ty += wy * depth; tz += wz * depth;
            pushed = c;
        }
        outPred[i * 4] = qx; outPred[i * 4 + 1] = qy; outPred[i * 4 + 2] = qz; outPred[i * 4 + 3] = 0.0f;
        if (first != 0){
            outPushed[i] = pushed;
            outPush[i * 4] = tx; outPush[i * 4 + 1] = ty; outPush[i * 4 + 2] = tz; outPush[i * 4 + 3] = 0.0f;
        }
        else{
            if (pushed >= 0) { outPushed[i] = pushed; }
            outPush[i * 4] += tx; outPush[i * 4 + 1] += ty; outPush[i * 4 + 2] += tz;
        }
    }

    // Velocity from the step's displacement (capped), then XSPH smoothing. Particles
    // touching a collider keep no velocity into it and lose some along it (friction),
    // which also calms particles squeezed into corners between two contacts.
    static void phx_fluid_velocity(int i, PHX_GLOBAL const float* pos, PHX_GLOBAL const float* pred, float invDt,
                                   float maxSpeed, PHX_GLOBAL const int* pushed, PHX_GLOBAL const float* push,
                                   float friction, PHX_GLOBAL float* outVel){
        float vx = (pred[i * 4] - pos[i * 4]) * invDt;
        float vy = (pred[i * 4 + 1] - pos[i * 4 + 1]) * invDt;
        float vz = (pred[i * 4 + 2] - pos[i * 4 + 2]) * invDt;
        if (pushed[i] >= 0){
            float nx = push[i * 4], ny = push[i * 4 + 1], nz = push[i * 4 + 2];
            float l = phx_sqrt(nx * nx + ny * ny + nz * nz);
            if (l > 1e-9f){
                nx /= l; ny /= l; nz /= l;
                float vn = vx * nx + vy * ny + vz * nz;
                float tx = vx - vn * nx, ty = vy - vn * ny, tz = vz - vn * nz;
                float keep = 1.0f - friction;
                vn = vn > 0.0f ? vn : 0.0f;
                vx = vn * nx + tx * keep; vy = vn * ny + ty * keep; vz = vn * nz + tz * keep;
            }
        }
        float s2 = vx * vx + vy * vy + vz * vz;
        if (s2 > maxSpeed * maxSpeed){
            float k = maxSpeed / phx_sqrt(s2);
            vx *= k; vy *= k; vz *= k;
        }
        outVel[i * 4] = vx; outVel[i * 4 + 1] = vy; outVel[i * 4 + 2] = vz; outVel[i * 4 + 3] = 0.0f;
    }

    // XSPH: blend towards the neighbours' velocity (viscosity). Surface tension: a pull
    // towards neighbours further than the rest spacing (zero at the spacing and at the
    // kernel radius, strongest between), so streams and drops hold together.
    static void phx_fluid_xsph(int i, PHX_GLOBAL const float* pred, PHX_GLOBAL const float* vel,
                               PHX_GLOBAL const int* sorted, PHX_GLOBAL const int* cellStart, PHX_GLOBAL const int* cellOf,
                               int dx, int dy, int dz, float h, float poly6, float viscosity, float tension, float spacing,
                               PHX_GLOBAL float* outVel){
        float px = pred[i * 4], py = pred[i * 4 + 1], pz = pred[i * 4 + 2];
        float vx = vel[i * 4], vy = vel[i * 4 + 1], vz = vel[i * 4 + 2];
        int cell = cellOf[i];
        int cz = cell / (dx * dy), cy = (cell / dx) % dy, cx = cell % dx;
        float h2 = h * h;
        float ax = 0.0f, ay = 0.0f, az = 0.0f, tx = 0.0f, ty = 0.0f, tz = 0.0f;
        float band = 0.5f * (h - spacing);
        for (int z = (cz > 0 ? cz - 1 : 0); z <= (cz < dz - 1 ? cz + 1 : cz); z++)
        for (int y = (cy > 0 ? cy - 1 : 0); y <= (cy < dy - 1 ? cy + 1 : cy); y++)
        for (int x = (cx > 0 ? cx - 1 : 0); x <= (cx < dx - 1 ? cx + 1 : cx); x++){
            int c = (z * dy + y) * dx + x;
            for (int k = cellStart[c]; k < cellStart[c + 1]; k++){
                int j = sorted[k];
                if (j == i) { continue; }
                float rx = px - pred[j * 4], ry = py - pred[j * 4 + 1], rz = pz - pred[j * 4 + 2];
                float r2 = rx * rx + ry * ry + rz * rz;
                if (r2 >= h2) { continue; }
                float w = phx_poly6(r2, h2, poly6);
                ax += (vel[j * 4] - vx) * w;
                ay += (vel[j * 4 + 1] - vy) * w;
                az += (vel[j * 4 + 2] - vz) * w;
                float r = phx_sqrt(r2);
                if (r > spacing){
                    float pull = (r - spacing) * (h - r) / (band * band * r);
                    tx -= rx * pull; ty -= ry * pull; tz -= rz * pull;
                }
            }
        }
        outVel[i * 4] = vx + viscosity * ax + tension * tx;
        outVel[i * 4 + 1] = vy + viscosity * ay + tension * ty;
        outVel[i * 4 + 2] = vz + viscosity * az + tension * tz;
        outVel[i * 4 + 3] = 0.0f;
    }
    )

    // ---- ParticleFluid ------------------------------------------------------------

    bool ParticleFluid::s_DefaultOpenCL = false;
    int ParticleFluid::minDeviceParticles = 2000;

    ParticleFluid::ParticleFluid(int maxParticles)
        : m_Max(std::max(1, maxParticles)){
        Calibrate();
    }

    void ParticleFluid::SetSettings(const FluidSettings& settings){
        const bool respace = settings.spacing != m_Settings.spacing;
        m_Settings = settings;
        m_Settings.spacing = std::clamp(settings.spacing, 0.005f, 1.0f);
        m_Settings.iterations = std::clamp(settings.iterations, 1, 16);
        if (respace) { Calibrate(); }
    }

    float ParticleFluid::ParticleMass() const{
        float d = m_Settings.spacing;
        return 1000.0f * d * d * d;
    }

    // Kernel constants for the spacing, normalised so that particles on a cubic lattice
    // at the rest spacing sum to density 1 exactly (the resting state).
    void ParticleFluid::Calibrate(){
        const float d = m_Settings.spacing;
        m_H = 2.0f * d;
        const float h = m_H, h2 = h * h, pi = 3.14159265358979f;
        const float poly6Raw = 315.0f / (64.0f * pi * std::pow(h, 9.0f));
        const float spikyRaw = 45.0f / (pi * std::pow(h, 6.0f));
        double density = 0.0, gradient = 0.0;
        for (int z = -2; z <= 2; z++)
            for (int y = -2; y <= 2; y++)
                for (int x = -2; x <= 2; x++){
                    float r2 = (float)(x * x + y * y + z * z) * d * d;
                    if (r2 >= h2) { continue; }
                    density += poly6Raw * std::pow(h2 - r2, 3.0f);
                    if (x == 0 && y == 0 && z == 0) { continue; }
                    float r = std::sqrt(r2);
                    double g = spikyRaw * (h - r) * (h - r);
                    gradient += g * g;
                }
        m_Scale = (float)(1.0 / density);
        m_Poly6 = poly6Raw * m_Scale;
        m_Spiky = spikyRaw * m_Scale;
        const float latticeGradient = (float)(gradient * (double)m_Scale * m_Scale);
        m_Epsilon = 0.01f * latticeGradient;
        m_CohesionScale = 1.0f / latticeGradient;
        float r2 = 0.04f * h2;  // anti-clumping reference distance: 0.2 h
        m_WDeltaQ = m_Poly6 * (h2 - r2) * (h2 - r2) * (h2 - r2);
    }

    void ParticleFluid::Clear(){
        m_Positions.clear();
        m_Velocities.clear();
        m_Ages.clear();
    }

    bool ParticleFluid::AddParticle(const glm::vec3& position, const glm::vec3& velocity){
        if ((int)m_Positions.size() >= m_Max) { return false; }
        m_Positions.push_back(glm::vec4(position, 0.0f));
        m_Velocities.push_back(glm::vec4(velocity, 0.0f));
        m_Ages.push_back(0.0f);
        return true;
    }

    int ParticleFluid::AddBlock(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& velocity){
        const float d = m_Settings.spacing;
        glm::ivec3 n = glm::max(glm::ivec3(glm::floor(halfExtents * 2.0f / d)), glm::ivec3(1));
        glm::vec3 start = center - glm::vec3(n - 1) * (0.5f * d);
        int added = 0;
        for (int z = 0; z < n.z; z++)
            for (int y = 0; y < n.y; y++)
                for (int x = 0; x < n.x; x++){
                    if (!AddParticle(start + glm::vec3(x, y, z) * d, velocity)) { return added; }
                    added++;
                }
        return added;
    }

    // Stable counting sort of particles into a dense grid over their predicted bounds.
    // Cells are at least the kernel radius (bigger when the fluid is spread far, to bound
    // the grid size), so the 27 cells around a particle hold all its neighbours.
    void ParticleFluid::BuildGrid(){
        const int n = Count();
        glm::vec3 lo(3.0e38f), hi(-3.0e38f);
        for (int i = 0; i < n; i++){
            lo = glm::min(lo, glm::vec3(m_Predicted[i]));
            hi = glm::max(hi, glm::vec3(m_Predicted[i]));
        }
        const glm::vec3 extent = glm::max(hi - lo, glm::vec3(0.0f)) + 1e-3f;
        m_CellSize = std::max(m_H, std::cbrt(extent.x * extent.y * extent.z / 2.0e6f));
        m_GridOrigin = lo;
        m_GridDims = glm::max(glm::ivec3(glm::floor(extent / m_CellSize)) + 1, glm::ivec3(1));
        const size_t cells = (size_t)m_GridDims.x * m_GridDims.y * m_GridDims.z;
        m_CellOf.resize(n);
        m_CellStart.assign(cells + 1, 0);
        for (int i = 0; i < n; i++){
            glm::ivec3 c = glm::clamp(glm::ivec3(glm::floor((glm::vec3(m_Predicted[i]) - lo) / m_CellSize)), glm::ivec3(0), m_GridDims - 1);
            m_CellOf[i] = (c.z * m_GridDims.y + c.y) * m_GridDims.x + c.x;
            m_CellStart[(size_t)m_CellOf[i] + 1]++;
        }
        for (size_t c = 0; c < cells; c++) { m_CellStart[c + 1] += m_CellStart[c]; }
        m_Sorted.resize(n);
        std::vector<int> fill(m_CellStart.begin(), m_CellStart.end() - 1);
        for (int i = 0; i < n; i++) { m_Sorted[(size_t)fill[(size_t)m_CellOf[i]]++] = i; }
    }

    static void PackColliders(const std::vector<FluidCollider>& colliders, std::vector<float>& out){
        out.assign(colliders.size() * COLLIDER_FLOATS, 0.0f);
        for (size_t c = 0; c < colliders.size(); c++){
            float* dst = &out[c * COLLIDER_FLOATS];
            const glm::mat4& m = colliders[c].inverse;
            for (int row = 0; row < 3; row++)
                for (int col = 0; col < 4; col++) { dst[row * 4 + col] = m[col][row]; }
            dst[12] = colliders[c].size.x;
            dst[13] = colliders[c].size.y;
            dst[14] = colliders[c].size.z;
            int type = (int)colliders[c].type;
            std::memcpy(&dst[15], &type, sizeof(int));
            dst[16] = colliders[c].center.x;
            dst[17] = colliders[c].center.y;
            dst[18] = colliders[c].center.z;
            dst[19] = colliders[c].radius;
        }
    }

    void ParticleFluid::Step(float dt, const std::vector<FluidCollider>& colliders, std::vector<BodyImpulse>& outImpulses){
        outImpulses.clear();
        const int n = Count();
        if (n == 0 || dt <= 0.0f) { return; }

        // Predict under gravity (host: the grid needs these positions).
        m_Predicted.resize(n);
        for (int i = 0; i < n; i++){
            m_Velocities[i] += glm::vec4(m_Settings.gravity * dt, 0.0f);
            m_Predicted[i] = glm::vec4(glm::vec3(m_Positions[i]) + glm::vec3(m_Velocities[i]) * dt, 0.0f);
        }
        BuildGrid();

        std::vector<float> packed;
        PackColliders(colliders, packed);
        m_Pushed.resize(n);
        m_Push.resize(n);
        if (!(m_Device && StepDevice(dt, packed, (int)colliders.size()))){
            if (m_Device) { m_Device.reset(); }   // device trouble: the CPU takes over
            StepCPU(dt, packed, (int)colliders.size());
        }

        // Pushes on dynamic colliders (from the last iteration) go back to their bodies,
        // summed in particle order: a particle pushed by dx gained m dx / dt of momentum,
        // and the body gets the opposite.
        const float mass = ParticleMass();
        std::vector<glm::vec3> sums(colliders.size(), glm::vec3(0.0f)), points(colliders.size(), glm::vec3(0.0f));
        std::vector<int> hits(colliders.size(), 0);
        for (int i = 0; i < n; i++){
            int c = m_Pushed[i];
            if (c < 0 || !colliders[(size_t)c].dynamic) { continue; }
            sums[(size_t)c] -= glm::vec3(m_Push[i]) * (mass / dt);
            points[(size_t)c] += glm::vec3(m_Positions[i]);
            hits[(size_t)c]++;
        }
        for (size_t c = 0; c < colliders.size(); c++)
            if (hits[c] > 0) { outImpulses.push_back({ colliders[c].body, sums[c], points[c] / (float)hits[c] }); }

        // Age, and drop what fell out of the world or outlived its lifetime.
        for (int i = 0; i < n; i++) { m_Ages[i] += dt; }
        const float lifetime = m_Settings.lifetime, killY = m_Settings.killY;
        Filter([&](int i){ return m_Positions[i].y > killY && (lifetime <= 0.0f || m_Ages[i] < lifetime); });
    }

    void ParticleFluid::StepCPU(float dt, const std::vector<float>& colliders, int colliderCount){
        const int n = Count();
        const int dx = m_GridDims.x, dy = m_GridDims.y, dz = m_GridDims.z;
        const float cohesion = m_Settings.cohesion * m_CohesionScale, radius = ParticleRadius();
        m_Lambda.resize(n);
        m_PredictedNext.resize(n);
        m_VelocitiesNext.resize(n);
        auto& workers = Workers::Get();
        float* pred = &m_Predicted[0].x;
        float* next = &m_PredictedNext[0].x;
        for (int it = 0; it < m_Settings.iterations; it++){
            workers.For(n, [&](int begin, int end){
                for (int i = begin; i < end; i++)
                    m_Lambda[i] = phx_fluid_lambda(i, pred, m_Sorted.data(), m_CellStart.data(), m_CellOf.data(),
                                                   dx, dy, dz, m_H, m_Poly6, m_Spiky, m_Epsilon);
            });
            workers.For(n, [&](int begin, int end){
                for (int i = begin; i < end; i++)
                    phx_fluid_delta(i, &m_Positions[0].x, pred, m_Lambda.data(), m_Sorted.data(), m_CellStart.data(), m_CellOf.data(),
                                    dx, dy, dz, m_H, m_Poly6, m_Spiky, cohesion, m_WDeltaQ,
                                    colliders.data(), colliderCount, radius, radius, it == 0 ? 1 : 0, next, m_Pushed.data(), &m_Push[0].x);
            });
            std::swap(pred, next);
        }
        // `pred` holds the final positions (in m_Predicted or m_PredictedNext).
        float* vel = &m_VelocitiesNext[0].x;
        const float* pos = &m_Positions[0].x;
        const float maxSpeed = 30.0f;
        workers.For(n, [&](int begin, int end){
            for (int i = begin; i < end; i++)
                phx_fluid_velocity(i, pos, pred, 1.0f / dt, maxSpeed, m_Pushed.data(), &m_Push[0].x, m_Settings.friction, vel);
        });
        float* out = &m_Velocities[0].x;
        const float viscosity = m_Settings.viscosity, tension = m_Settings.surfaceTension * dt, spacing = m_Settings.spacing;
        workers.For(n, [&](int begin, int end){
            for (int i = begin; i < end; i++)
                phx_fluid_xsph(i, pred, vel, m_Sorted.data(), m_CellStart.data(), m_CellOf.data(),
                               dx, dy, dz, m_H, m_Poly6, viscosity, tension, spacing, out);
        });
        std::memcpy(&m_Positions[0].x, pred, sizeof(float) * 4 * (size_t)n);
    }

#ifdef PHOENIX_OPENCL
    static const char* s_FluidKernels = R"CLC(
__kernel void fluid_lambda(__global const float* pred, __global const int* sorted, __global const int* cellStart,
                           __global const int* cellOf, int n, int dx, int dy, int dz, float h, float poly6, float spiky,
                           float eps, __global float* lambda){
    int i = get_global_id(0);
    if (i >= n) { return; }
    lambda[i] = phx_fluid_lambda(i, pred, sorted, cellStart, cellOf, dx, dy, dz, h, poly6, spiky, eps);
}

__kernel void fluid_delta(__global const float* pos, __global const float* pred, __global const float* lambda, __global const int* sorted,
                          __global const int* cellStart, __global const int* cellOf, int n, int dx, int dy, int dz,
                          float h, float poly6, float spiky, float cohesion, float wdq, __global const float* colliders,
                          int colliderCount, float radius, float maxMove, int first, __global float* outPred,
                          __global int* outPushed, __global float* outPush){
    int i = get_global_id(0);
    if (i >= n) { return; }
    phx_fluid_delta(i, pos, pred, lambda, sorted, cellStart, cellOf, dx, dy, dz, h, poly6, spiky, cohesion, wdq,
                    colliders, colliderCount, radius, maxMove, first, outPred, outPushed, outPush);
}

__kernel void fluid_velocity(__global const float* pos, __global const float* pred, float invDt, float maxSpeed,
                             __global const int* pushed, __global const float* push, float friction,
                             int n, __global float* outVel){
    int i = get_global_id(0);
    if (i >= n) { return; }
    phx_fluid_velocity(i, pos, pred, invDt, maxSpeed, pushed, push, friction, outVel);
}

__kernel void fluid_xsph(__global const float* pred, __global const float* vel, __global const int* sorted,
                         __global const int* cellStart, __global const int* cellOf, int n, int dx, int dy, int dz,
                         float h, float poly6, float viscosity, float tension, float spacing, __global float* outVel){
    int i = get_global_id(0);
    if (i >= n) { return; }
    phx_fluid_xsph(i, pred, vel, sorted, cellStart, cellOf, dx, dy, dz, h, poly6, viscosity, tension, spacing, outVel);
}
)CLC";

    static const char* s_FluidPrelude = "#define PHX_GLOBAL __global\n#define as_int_bits(x) as_int(x)\n";

    struct ParticleFluid::Device{
        cl_program program = nullptr;
        cl_kernel lambda = nullptr, delta = nullptr, velocity = nullptr, xsph = nullptr;
        cl_mem pos = nullptr, predA = nullptr, predB = nullptr, lam = nullptr, vel = nullptr, velOut = nullptr;
        cl_mem sorted = nullptr, cellStart = nullptr, cellOf = nullptr, colliders = nullptr, pushed = nullptr, push = nullptr;
        size_t capPos = 0, capA = 0, capB = 0, capLam = 0, capVel = 0, capVelOut = 0;
        size_t capSorted = 0, capCellStart = 0, capCellOf = 0, capColliders = 0, capPushed = 0, capPush = 0;

        ~Device(){
            for (cl_mem m : { pos, predA, predB, lam, vel, velOut, sorted, cellStart, cellOf, colliders, pushed, push })
                if (m) { clReleaseMemObject(m); }
            for (cl_kernel k : { lambda, delta, velocity, xsph }) { if (k) { clReleaseKernel(k); } }
            if (program) { clReleaseProgram(program); }
        }
    };

    bool ParticleFluid::UseOpenCL(bool enable){
        if (enable == (m_Device != nullptr)) { return enable; }
        if (!enable) { m_Device.reset(); return false; }
        if (!Compute::Available()) { return false; }
        auto device = std::make_unique<Device>();
        std::string error;
        device->program = Compute::BuildProgram({ s_FluidPrelude, s_FluidSource, s_FluidKernels }, error);
        if (!device->program) { return false; }
        cl_int err = CL_SUCCESS, e;
        device->lambda = clCreateKernel(device->program, "fluid_lambda", &e); err |= e;
        device->delta = clCreateKernel(device->program, "fluid_delta", &e); err |= e;
        device->velocity = clCreateKernel(device->program, "fluid_velocity", &e); err |= e;
        device->xsph = clCreateKernel(device->program, "fluid_xsph", &e); err |= e;
        if (err != CL_SUCCESS) { return false; }
        m_Device = std::move(device);
        return true;
    }

    template<typename T>
    static void Arg(cl_kernel k, cl_uint index, const T& value, cl_int& err) { err |= clSetKernelArg(k, index, sizeof(T), &value); }

    bool ParticleFluid::StepDevice(float dt, const std::vector<float>& colliders, int colliderCount){
        Device& d = *m_Device;
        using Compute::Reserve;
        using Compute::Write;
        const int n = Count();
        const size_t cells = m_CellStart.size();
        const cl_mem_flags rw = CL_MEM_READ_WRITE, ro = CL_MEM_READ_ONLY;
        if (!Reserve(d.pos, d.capPos, (size_t)n * 4, sizeof(float), ro) || !Reserve(d.predA, d.capA, (size_t)n * 4, sizeof(float), rw)
            || !Reserve(d.predB, d.capB, (size_t)n * 4, sizeof(float), rw) || !Reserve(d.lam, d.capLam, (size_t)n, sizeof(float), rw)
            || !Reserve(d.vel, d.capVel, (size_t)n * 4, sizeof(float), rw) || !Reserve(d.velOut, d.capVelOut, (size_t)n * 4, sizeof(float), rw)
            || !Reserve(d.sorted, d.capSorted, (size_t)n, sizeof(int), ro) || !Reserve(d.cellStart, d.capCellStart, cells, sizeof(int), ro)
            || !Reserve(d.cellOf, d.capCellOf, (size_t)n, sizeof(int), ro)
            || !Reserve(d.colliders, d.capColliders, std::max<size_t>(colliders.size(), 1), sizeof(float), ro)
            || !Reserve(d.pushed, d.capPushed, (size_t)n, sizeof(int), rw) || !Reserve(d.push, d.capPush, (size_t)n * 4, sizeof(float), rw))
            return false;

        cl_int err = Write(d.pos, &m_Positions[0].x, (size_t)n * 4 * sizeof(float));
        err |= Write(d.predA, &m_Predicted[0].x, (size_t)n * 4 * sizeof(float));
        err |= Write(d.sorted, m_Sorted.data(), (size_t)n * sizeof(int));
        err |= Write(d.cellStart, m_CellStart.data(), cells * sizeof(int));
        err |= Write(d.cellOf, m_CellOf.data(), (size_t)n * sizeof(int));
        err |= Write(d.colliders, colliders.data(), colliders.size() * sizeof(float));

        const cl_int ni = n, dx = m_GridDims.x, dy = m_GridDims.y, dz = m_GridDims.z, cc = colliderCount;
        const float cohesion = m_Settings.cohesion * m_CohesionScale, radius = ParticleRadius();
        cl_mem pred = d.predA, next = d.predB;
        for (int it = 0; it < m_Settings.iterations; it++){
            cl_kernel k = d.lambda;
            Arg(k, 0, pred, err); Arg(k, 1, d.sorted, err); Arg(k, 2, d.cellStart, err); Arg(k, 3, d.cellOf, err);
            Arg(k, 4, ni, err); Arg(k, 5, dx, err); Arg(k, 6, dy, err); Arg(k, 7, dz, err); Arg(k, 8, m_H, err);
            Arg(k, 9, m_Poly6, err); Arg(k, 10, m_Spiky, err); Arg(k, 11, m_Epsilon, err); Arg(k, 12, d.lam, err);
            err |= Compute::Launch(k, (size_t)n);

            k = d.delta;
            Arg(k, 0, d.pos, err); Arg(k, 1, pred, err); Arg(k, 2, d.lam, err); Arg(k, 3, d.sorted, err); Arg(k, 4, d.cellStart, err);
            Arg(k, 5, d.cellOf, err); Arg(k, 6, ni, err); Arg(k, 7, dx, err); Arg(k, 8, dy, err); Arg(k, 9, dz, err);
            Arg(k, 10, m_H, err); Arg(k, 11, m_Poly6, err); Arg(k, 12, m_Spiky, err); Arg(k, 13, cohesion, err);
            Arg(k, 14, m_WDeltaQ, err); Arg(k, 15, d.colliders, err); Arg(k, 16, cc, err); Arg(k, 17, radius, err);
            const cl_int first = it == 0 ? 1 : 0;
            Arg(k, 18, radius, err); Arg(k, 19, first, err); Arg(k, 20, next, err); Arg(k, 21, d.pushed, err); Arg(k, 22, d.push, err);
            err |= Compute::Launch(k, (size_t)n);
            std::swap(pred, next);
        }
        const float invDt = 1.0f / dt, maxSpeed = 30.0f, viscosity = m_Settings.viscosity;
        const float tension = m_Settings.surfaceTension * dt, spacing = m_Settings.spacing;
        cl_kernel k = d.velocity;
        Arg(k, 0, d.pos, err); Arg(k, 1, pred, err); Arg(k, 2, invDt, err); Arg(k, 3, maxSpeed, err);
        Arg(k, 4, d.pushed, err); Arg(k, 5, d.push, err); Arg(k, 6, m_Settings.friction, err);
        Arg(k, 7, ni, err); Arg(k, 8, d.vel, err);
        err |= Compute::Launch(k, (size_t)n);
        k = d.xsph;
        Arg(k, 0, pred, err); Arg(k, 1, d.vel, err); Arg(k, 2, d.sorted, err); Arg(k, 3, d.cellStart, err);
        Arg(k, 4, d.cellOf, err); Arg(k, 5, ni, err); Arg(k, 6, dx, err); Arg(k, 7, dy, err); Arg(k, 8, dz, err);
        Arg(k, 9, m_H, err); Arg(k, 10, m_Poly6, err); Arg(k, 11, viscosity, err); Arg(k, 12, tension, err);
        Arg(k, 13, spacing, err); Arg(k, 14, d.velOut, err);
        err |= Compute::Launch(k, (size_t)n);

        cl_command_queue queue = Compute::GetDevice()->queue;
        err |= clEnqueueReadBuffer(queue, pred, CL_FALSE, 0, (size_t)n * 4 * sizeof(float), &m_Positions[0].x, 0, nullptr, nullptr);
        err |= clEnqueueReadBuffer(queue, d.velOut, CL_FALSE, 0, (size_t)n * 4 * sizeof(float), &m_Velocities[0].x, 0, nullptr, nullptr);
        err |= clEnqueueReadBuffer(queue, d.pushed, CL_FALSE, 0, (size_t)n * sizeof(int), m_Pushed.data(), 0, nullptr, nullptr);
        err |= clEnqueueReadBuffer(queue, d.push, CL_TRUE, 0, (size_t)n * 4 * sizeof(float), &m_Push[0].x, 0, nullptr, nullptr);
        return err == CL_SUCCESS;
    }
#else
    struct ParticleFluid::Device{};
    bool ParticleFluid::UseOpenCL(bool) { return false; }
    bool ParticleFluid::StepDevice(float, const std::vector<float>&, int) { return false; }
#endif

    ParticleFluid::~ParticleFluid() = default;
}
