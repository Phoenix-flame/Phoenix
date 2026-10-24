#include <Phoenix/Physics/FireSim.h>
#include <Phoenix/Compute/Compute.h>
#include "../Compute/ComputeCL.h"
#include "../Compute/SharedSource.h"
#include "../Compute/Workers.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

// Arrays the kernels read and write live in device memory there and are plain arrays
// here (see ParticleFluid.cpp).
#define PHX_GLOBAL

namespace Phoenix{

    // Per-cell passes of the fire, shared with the kernels (see SharedSource.h). Fields
    // are float4 per cell, x fastest then y then z. Cells outside the grid: the floor
    // (below y = 0) is a wall, every other side is open air.
    PHX_SHARED_SOURCE(s_FireSource,
    enum{
        FIRE_DT, FIRE_H, FIRE_TIME,
        FIRE_SRC_X, FIRE_SRC_Y, FIRE_SRC_Z, FIRE_SRC_RH, FIRE_SRC_RV,
        FIRE_FUEL, FIRE_EXIT, FIRE_BURN, FIRE_HEAT, FIRE_IGNITION, FIRE_COOLING,
        FIRE_BUOYANCY, FIRE_SMOKE, FIRE_SMOKE_WEIGHT, FIRE_SMOKE_KEEP,
        FIRE_VORTICITY, FIRE_TURBULENCE, FIRE_DRAG,
        FIRE_WIND_X, FIRE_WIND_Y, FIRE_WIND_Z, FIRE_BURNING,
        FIRE_PARAMS
    };

    static int phx_fire_index(int x, int y, int z, int nx, int ny) { return (z * ny + y) * nx + x; }

    static phx_uint phx_fire_hash(int x, int y, int z, phx_uint seed){
        phx_uint h = (phx_uint)x * 0x8da6b343u ^ (phx_uint)y * 0xd8163841u ^ (phx_uint)z * 0xcb1ab31fu ^ seed;
        h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
        return h;
    }

    static float phx_fire_lerp(float a, float b, float t) { return a + (b - a) * t; }

    // Smooth value noise in [0, 1).
    static float phx_fire_noise(float x, float y, float z, phx_uint seed){
        float fx = phx_floor(x), fy = phx_floor(y), fz = phx_floor(z);
        int ix = (int)fx, iy = (int)fy, iz = (int)fz;
        float tx = x - fx, ty = y - fy, tz = z - fz;
        tx = tx * tx * (3.0f - 2.0f * tx);
        ty = ty * ty * (3.0f - 2.0f * ty);
        tz = tz * tz * (3.0f - 2.0f * tz);
        float k = 1.0f / 16777216.0f;
        float c000 = (float)(phx_fire_hash(ix, iy, iz, seed) >> 8) * k;
        float c100 = (float)(phx_fire_hash(ix + 1, iy, iz, seed) >> 8) * k;
        float c010 = (float)(phx_fire_hash(ix, iy + 1, iz, seed) >> 8) * k;
        float c110 = (float)(phx_fire_hash(ix + 1, iy + 1, iz, seed) >> 8) * k;
        float c001 = (float)(phx_fire_hash(ix, iy, iz + 1, seed) >> 8) * k;
        float c101 = (float)(phx_fire_hash(ix + 1, iy, iz + 1, seed) >> 8) * k;
        float c011 = (float)(phx_fire_hash(ix, iy + 1, iz + 1, seed) >> 8) * k;
        float c111 = (float)(phx_fire_hash(ix + 1, iy + 1, iz + 1, seed) >> 8) * k;
        float a = phx_fire_lerp(phx_fire_lerp(c000, c100, tx), phx_fire_lerp(c010, c110, tx), ty);
        float b = phx_fire_lerp(phx_fire_lerp(c001, c101, tx), phx_fire_lerp(c011, c111, tx), ty);
        return phx_fire_lerp(a, b, tz);
    }

    // Squared distance to the source region in its radii (< 1 inside).
    static float phx_fire_source(int x, int y, int z, PHX_GLOBAL const float* prm){
        float dx = ((float)x - prm[FIRE_SRC_X]) / prm[FIRE_SRC_RH];
        float dy = ((float)y - prm[FIRE_SRC_Y]) / prm[FIRE_SRC_RV];
        float dz = ((float)z - prm[FIRE_SRC_Z]) / prm[FIRE_SRC_RH];
        return dx * dx + dy * dy + dz * dz;
    }

    // Velocity component c at a cell that may lie one step outside the grid: the open
    // sides repeat the edge cell; the floor mirrors the vertical component (a wall).
    static float phx_fire_vel(PHX_GLOBAL const float* vel, int x, int y, int z, int c, int nx, int ny, int nz){
        int below = y < 0;
        x = x < 0 ? 0 : (x >= nx ? nx - 1 : x);
        y = y < 0 ? 0 : (y >= ny ? ny - 1 : y);
        z = z < 0 ? 0 : (z >= nz ? nz - 1 : z);
        float v = vel[phx_fire_index(x, y, z, nx, ny) * 4 + c];
        return (below && c == 1) ? -v : v;
    }

    static int phx_fire_interior(int x, int y, int z, int nx, int ny, int nz){
        return x > 0 && x < nx - 1 && y > 0 && y < ny - 1 && z > 0 && z < nz - 1;
    }

    // The velocities of the six neighbours (-x, +x, -y, +y, -z, +z; xyz each).
    static void phx_fire_neighbours(PHX_GLOBAL const float* vel, int x, int y, int z, int nx, int ny, int nz, float* n){
        if (phx_fire_interior(x, y, z, nx, ny, nz)){
            int i = phx_fire_index(x, y, z, nx, ny) * 4, sx = 4, sy = nx * 4, sz = nx * ny * 4;
            for (int c = 0; c < 3; c++){
                n[c] = vel[i - sx + c];      n[3 + c] = vel[i + sx + c];
                n[6 + c] = vel[i - sy + c];  n[9 + c] = vel[i + sy + c];
                n[12 + c] = vel[i - sz + c]; n[15 + c] = vel[i + sz + c];
            }
            return;
        }
        for (int c = 0; c < 3; c++){
            n[c] = phx_fire_vel(vel, x - 1, y, z, c, nx, ny, nz);
            n[3 + c] = phx_fire_vel(vel, x + 1, y, z, c, nx, ny, nz);
            n[6 + c] = phx_fire_vel(vel, x, y - 1, z, c, nx, ny, nz);
            n[9 + c] = phx_fire_vel(vel, x, y + 1, z, c, nx, ny, nz);
            n[12 + c] = phx_fire_vel(vel, x, y, z - 1, c, nx, ny, nz);
            n[15 + c] = phx_fire_vel(vel, x, y, z + 1, c, nx, ny, nz);
        }
    }

    // The pressures of the six neighbours (same order): zero in the open air, the cell's
    // own value across the floor (no flow through it).
    static void phx_fire_pressures(PHX_GLOBAL const float* p, int x, int y, int z, int nx, int ny, int nz, float* n){
        int i = phx_fire_index(x, y, z, nx, ny), sy = nx, sz = nx * ny;
        if (phx_fire_interior(x, y, z, nx, ny, nz)){
            n[0] = p[i - 1]; n[1] = p[i + 1]; n[2] = p[i - sy]; n[3] = p[i + sy]; n[4] = p[i - sz]; n[5] = p[i + sz];
            return;
        }
        n[0] = x > 0 ? p[i - 1] : 0.0f;
        n[1] = x < nx - 1 ? p[i + 1] : 0.0f;
        n[2] = y > 0 ? p[i - sy] : p[i];
        n[3] = y < ny - 1 ? p[i + sy] : 0.0f;
        n[4] = z > 0 ? p[i - sz] : 0.0f;
        n[5] = z < nz - 1 ? p[i + sz] : 0.0f;
    }

    // Trilinear velocity at a point in cell units (cell centres at whole numbers), the
    // edge cells repeated outside.
    static void phx_fire_sample_vel(PHX_GLOBAL const float* vel, float x, float y, float z, int nx, int ny, int nz, float* out){
        float mx = (float)(nx - 1), my = (float)(ny - 1), mz = (float)(nz - 1);
        x = x < 0.0f ? 0.0f : (x > mx ? mx : x);
        y = y < 0.0f ? 0.0f : (y > my ? my : y);
        z = z < 0.0f ? 0.0f : (z > mz ? mz : z);
        float fx = phx_floor(x), fy = phx_floor(y), fz = phx_floor(z);
        int ix = (int)fx, iy = (int)fy, iz = (int)fz;
        float tx = x - fx, ty = y - fy, tz = z - fz;
        int ox = ix < nx - 1 ? 4 : 0, oy = iy < ny - 1 ? nx * 4 : 0, oz = iz < nz - 1 ? nx * ny * 4 : 0;
        int b = phx_fire_index(ix, iy, iz, nx, ny) * 4;
        for (int c = 0; c < 3; c++){
            int k = b + c;
            float c00 = phx_fire_lerp(vel[k], vel[k + ox], tx);
            float c10 = phx_fire_lerp(vel[k + oy], vel[k + oy + ox], tx);
            float c01 = phx_fire_lerp(vel[k + oz], vel[k + oz + ox], tx);
            float c11 = phx_fire_lerp(vel[k + oz + oy], vel[k + oz + oy + ox], tx);
            out[c] = phx_fire_lerp(phx_fire_lerp(c00, c10, ty), phx_fire_lerp(c01, c11, ty), tz);
        }
    }

    // The eight cells around a point in cell units (float offsets, -1 outside the grid)
    // and its trilinear weights.
    static void phx_fire_corners(float x, float y, float z, int nx, int ny, int nz, int* idx, float* t){
        x = x < -1.0f ? -1.0f : (x > (float)nx ? (float)nx : x);
        y = y < -1.0f ? -1.0f : (y > (float)ny ? (float)ny : y);
        z = z < -1.0f ? -1.0f : (z > (float)nz ? (float)nz : z);
        float fx = phx_floor(x), fy = phx_floor(y), fz = phx_floor(z);
        int ix = (int)fx, iy = (int)fy, iz = (int)fz;
        t[0] = x - fx; t[1] = y - fy; t[2] = z - fz;
        for (int k = 0; k < 8; k++){
            int cx = ix + (k & 1), cy = iy + ((k >> 1) & 1), cz = iz + ((k >> 2) & 1);
            int inside = cx >= 0 && cx < nx && cy >= 0 && cy < ny && cz >= 0 && cz < nz;
            idx[k] = inside ? phx_fire_index(cx, cy, cz, nx, ny) * 4 : -1;
        }
    }

    // Trilinear sample of the advected scalars (temperature, fuel, smoke; zero outside:
    // fresh air). The reaction rate (w) is recomputed every step, so it isn't carried.
    static void phx_fire_sample_scal(PHX_GLOBAL const float* f, float x, float y, float z, int nx, int ny, int nz, float* out){
        float fx = phx_floor(x), fy = phx_floor(y), fz = phx_floor(z);
        if (fx >= 0.0f && fy >= 0.0f && fz >= 0.0f && fx < (float)(nx - 1) && fy < (float)(ny - 1) && fz < (float)(nz - 1)){
            int ix = (int)fx, iy = (int)fy, iz = (int)fz;
            float tx = x - fx, ty = y - fy, tz = z - fz;
            int ox = 4, oy = nx * 4, oz = nx * ny * 4;
            int b = phx_fire_index(ix, iy, iz, nx, ny) * 4;
            for (int c = 0; c < 3; c++){
                int k = b + c;
                float c00 = phx_fire_lerp(f[k], f[k + ox], tx);
                float c10 = phx_fire_lerp(f[k + oy], f[k + oy + ox], tx);
                float c01 = phx_fire_lerp(f[k + oz], f[k + oz + ox], tx);
                float c11 = phx_fire_lerp(f[k + oz + oy], f[k + oz + oy + ox], tx);
                out[c] = phx_fire_lerp(phx_fire_lerp(c00, c10, ty), phx_fire_lerp(c01, c11, ty), tz);
            }
            out[3] = 0.0f;
            return;
        }
        int idx[8];
        float t[3];
        phx_fire_corners(x, y, z, nx, ny, nz, idx, t);
        for (int c = 0; c < 3; c++){
            float v[8];
            for (int k = 0; k < 8; k++) { v[k] = idx[k] >= 0 ? f[idx[k] + c] : 0.0f; }
            float c00 = phx_fire_lerp(v[0], v[1], t[0]), c10 = phx_fire_lerp(v[2], v[3], t[0]);
            float c01 = phx_fire_lerp(v[4], v[5], t[0]), c11 = phx_fire_lerp(v[6], v[7], t[0]);
            out[c] = phx_fire_lerp(phx_fire_lerp(c00, c10, t[1]), phx_fire_lerp(c01, c11, t[1]), t[2]);
        }
        out[3] = 0.0f;
    }

    // Where the gas now at cell (x, y, z) came from (s = dt / h; negative traces
    // forwards), by a midpoint step through the velocity field.
    static void phx_fire_trace(PHX_GLOBAL const float* vel, int x, int y, int z, int nx, int ny, int nz, float s, float* p){
        int i = phx_fire_index(x, y, z, nx, ny) * 4;
        float v[3];
        phx_fire_sample_vel(vel, (float)x - 0.5f * s * vel[i], (float)y - 0.5f * s * vel[i + 1], (float)z - 0.5f * s * vel[i + 2],
                            nx, ny, nz, v);
        p[0] = (float)x - s * v[0];
        p[1] = (float)y - s * v[1];
        p[2] = (float)z - s * v[2];
    }

    static void phx_fire_curl(int x, int y, int z, PHX_GLOBAL const float* vel, int nx, int ny, int nz, PHX_GLOBAL const float* prm,
                              PHX_GLOBAL float* curl){
        int i = phx_fire_index(x, y, z, nx, ny);
        float n[18];
        phx_fire_neighbours(vel, x, y, z, nx, ny, nz, n);
        float s = 0.5f / prm[FIRE_H];
        float dwdy = (n[11] - n[8]) * s, dvdz = (n[16] - n[13]) * s;
        float dudz = (n[15] - n[12]) * s, dwdx = (n[5] - n[2]) * s;
        float dvdx = (n[4] - n[1]) * s, dudy = (n[9] - n[6]) * s;
        float wx = dwdy - dvdz, wy = dudz - dwdx, wz = dvdx - dudy;
        curl[i * 4] = wx;
        curl[i * 4 + 1] = wy;
        curl[i * 4 + 2] = wz;
        curl[i * 4 + 3] = phx_sqrt(wx * wx + wy * wy + wz * wz);
    }

    // Buoyancy, vorticity confinement, turbulence, the source's outflow and drag towards
    // the wind.
    static void phx_fire_forces(int x, int y, int z, PHX_GLOBAL const float* vel, PHX_GLOBAL const float* curl, PHX_GLOBAL const float* scal,
                                int nx, int ny, int nz, PHX_GLOBAL const float* prm, PHX_GLOBAL float* out){
        int i = phx_fire_index(x, y, z, nx, ny);
        float dt = prm[FIRE_DT], h = prm[FIRE_H], t = prm[FIRE_TIME];
        float vx = vel[i * 4], vy = vel[i * 4 + 1], vz = vel[i * 4 + 2];
        float T = scal[i * 4], S = scal[i * 4 + 2];

        vy += dt * (prm[FIRE_BUOYANCY] * T - prm[FIRE_SMOKE_WEIGHT] * S);

        // Confinement: push along N x curl, N pointing up the gradient of |curl|.
        int sx = x > 0 && x < nx - 1 ? 1 : 0, sy = y > 0 && y < ny - 1 ? nx : 0, sz = z > 0 && z < nz - 1 ? nx * ny : 0;
        float gx = curl[(i + sx) * 4 + 3] - curl[(i - sx) * 4 + 3];
        float gy = curl[(i + sy) * 4 + 3] - curl[(i - sy) * 4 + 3];
        float gz = curl[(i + sz) * 4 + 3] - curl[(i - sz) * 4 + 3];
        float gl = phx_sqrt(gx * gx + gy * gy + gz * gz);
        if (gl > 1e-6f){
            float ex = gx / gl, ey = gy / gl, ez = gz / gl;
            float wx = curl[i * 4], wy = curl[i * 4 + 1], wz = curl[i * 4 + 2];
            float e = prm[FIRE_VORTICITY] * h * dt;
            vx += e * (ey * wz - ez * wy);
            vy += e * (ez * wx - ex * wz);
            vz += e * (ex * wy - ey * wx);
        }

        // Turbulence in hot gas: smooth noise rising with the flame.
        if (T > 0.01f){
            float f = 0.6f, px = (float)x * f + t * 0.7f, py = (float)y * f - t * 6.0f, pz = (float)z * f - t * 0.5f;
            float a = dt * prm[FIRE_TURBULENCE] * (T < 1.0f ? T : 1.0f) * 2.0f;
            vx += a * (phx_fire_noise(px, py, pz, 17u) - 0.5f);
            vy += a * (phx_fire_noise(px, py, pz, 29u) - 0.5f);
            vz += a * (phx_fire_noise(px, py, pz, 43u) - 0.5f);
        }

        // The source exhales hot gas, its rate pulsing a little (flames puff).
        float d2 = phx_fire_source(x, y, z, prm);
        if (prm[FIRE_BURNING] > 0.5f && d2 < 1.0f){
            float pulse = 0.75f + 0.5f * phx_fire_noise(t * 6.0f, 0.5f, 0.5f, 101u);
            float up = prm[FIRE_EXIT] * (1.0f - d2) * pulse;
            vy = vy > up ? vy : up;
        }

        float drag = prm[FIRE_DRAG];
        vx += (prm[FIRE_WIND_X] - vx) * drag;
        vy += (prm[FIRE_WIND_Y] - vy) * drag;
        vz += (prm[FIRE_WIND_Z] - vz) * drag;
        out[i * 4] = vx;
        out[i * 4 + 1] = vy;
        out[i * 4 + 2] = vz;
        out[i * 4 + 3] = 0.0f;
    }

    static void phx_fire_divergence(int x, int y, int z, PHX_GLOBAL const float* vel, int nx, int ny, int nz, PHX_GLOBAL const float* prm,
                                    PHX_GLOBAL float* div){
        float n[18];
        phx_fire_neighbours(vel, x, y, z, nx, ny, nz, n);
        div[phx_fire_index(x, y, z, nx, ny)] = ((n[3] - n[0]) + (n[10] - n[7]) + (n[17] - n[14])) * (0.5f / prm[FIRE_H]);
    }

    // One Jacobi sweep of the pressure Poisson equation (laplacian p = divergence).
    static void phx_fire_jacobi(int x, int y, int z, PHX_GLOBAL const float* p, PHX_GLOBAL const float* div, int nx, int ny, int nz,
                                PHX_GLOBAL const float* prm, PHX_GLOBAL float* out){
        int i = phx_fire_index(x, y, z, nx, ny);
        float n[6];
        phx_fire_pressures(p, x, y, z, nx, ny, nz, n);
        float h = prm[FIRE_H];
        out[i] = (n[0] + n[1] + n[2] + n[3] + n[4] + n[5] - h * h * div[i]) / 6.0f;
    }

    // Subtract the pressure gradient: the velocity becomes (nearly) divergence-free.
    static void phx_fire_project(int x, int y, int z, PHX_GLOBAL const float* vel, PHX_GLOBAL const float* p, int nx, int ny, int nz,
                                 PHX_GLOBAL const float* prm, PHX_GLOBAL float* out){
        int i = phx_fire_index(x, y, z, nx, ny);
        float n[6];
        phx_fire_pressures(p, x, y, z, nx, ny, nz, n);
        float s = 0.5f / prm[FIRE_H];
        out[i * 4] = vel[i * 4] - (n[1] - n[0]) * s;
        out[i * 4 + 1] = vel[i * 4 + 1] - (n[3] - n[2]) * s;
        out[i * 4 + 2] = vel[i * 4 + 2] - (n[5] - n[4]) * s;
        out[i * 4 + 3] = 0.0f;
    }

    // Where the gas at each cell came from this step (cell units), traced once and
    // shared by the scalar and velocity advection.
    static void phx_fire_departure(int x, int y, int z, PHX_GLOBAL const float* vel, int nx, int ny, int nz, float s,
                                   PHX_GLOBAL float* out){
        int i = phx_fire_index(x, y, z, nx, ny);
        float p[3];
        phx_fire_trace(vel, x, y, z, nx, ny, nz, s, p);
        out[i * 4] = p[0];
        out[i * 4 + 1] = p[1];
        out[i * 4 + 2] = p[2];
        out[i * 4 + 3] = 0.0f;
    }

    // Semi-Lagrangian advection: the scalars f, or (self = 1) the velocity, read at the
    // departure points.
    static void phx_fire_advect(int x, int y, int z, PHX_GLOBAL const float* departure, PHX_GLOBAL const float* f,
                                int nx, int ny, int nz, int self, PHX_GLOBAL float* out){
        int i = phx_fire_index(x, y, z, nx, ny);
        float v[4];
        float px = departure[i * 4], py = departure[i * 4 + 1], pz = departure[i * 4 + 2];
        if (self){
            phx_fire_sample_vel(f, px, py, pz, nx, ny, nz, v);
            v[3] = 0.0f;
        }
        else{
            phx_fire_sample_scal(f, px, py, pz, nx, ny, nz, v);
        }
        out[i * 4] = v[0];
        out[i * 4 + 1] = v[1];
        out[i * 4 + 2] = v[2];
        out[i * 4 + 3] = v[3];
    }

    // The MacCormack back-step: the advected scalars carried forwards again (s = dt / h).
    static void phx_fire_advect_back(int x, int y, int z, PHX_GLOBAL const float* vel, PHX_GLOBAL const float* f,
                                     int nx, int ny, int nz, float s, PHX_GLOBAL float* out){
        int i = phx_fire_index(x, y, z, nx, ny);
        float p[3], v[4];
        phx_fire_trace(vel, x, y, z, nx, ny, nz, -s, p);
        phx_fire_sample_scal(f, p[0], p[1], p[2], nx, ny, nz, v);
        out[i * 4] = v[0];
        out[i * 4 + 1] = v[1];
        out[i * 4 + 2] = v[2];
        out[i * 4 + 3] = v[3];
    }

    // MacCormack: the forward result corrected by half the round-trip error, limited to
    // the range of the cells it was drawn from (so it never overshoots).
    static void phx_fire_maccormack(int x, int y, int z, PHX_GLOBAL const float* departure, PHX_GLOBAL const float* f,
                                    PHX_GLOBAL const float* fwd, PHX_GLOBAL const float* back, int nx, int ny, int nz,
                                    PHX_GLOBAL float* out){
        int i = phx_fire_index(x, y, z, nx, ny);
        float t[3];
        int idx[8];
        phx_fire_corners(departure[i * 4], departure[i * 4 + 1], departure[i * 4 + 2], nx, ny, nz, idx, t);
        for (int c = 0; c < 3; c++){
            float lo = FLT_MAX, hi = -FLT_MAX;
            for (int k = 0; k < 8; k++){
                float v = idx[k] >= 0 ? f[idx[k] + c] : 0.0f;
                lo = v < lo ? v : lo;
                hi = v > hi ? v : hi;
            }
            float r = fwd[i * 4 + c] + 0.5f * (f[i * 4 + c] - back[i * 4 + c]);
            out[i * 4 + c] = r < lo ? lo : (r > hi ? hi : r);
        }
        out[i * 4 + 3] = 0.0f;
    }

    // Feed the source, burn, cool and thin the smoke. In place: each cell reads only itself.
    static void phx_fire_react(int x, int y, int z, PHX_GLOBAL float* scal, int nx, int ny, PHX_GLOBAL const float* prm){
        int i = phx_fire_index(x, y, z, nx, ny);
        float dt = prm[FIRE_DT], t = prm[FIRE_TIME];
        float T = scal[i * 4], Y = scal[i * 4 + 1], S = scal[i * 4 + 2];

        float d2 = phx_fire_source(x, y, z, prm);
        if (prm[FIRE_BURNING] > 0.5f && d2 < 1.0f){
            float w = 1.0f - d2;
            float n = phx_fire_noise((float)x * 0.5f, (float)y * 0.5f - t * 3.0f, (float)z * 0.5f, 7u);
            float fuel = prm[FIRE_FUEL] * w * (0.5f + n);
            float hot = prm[FIRE_IGNITION] * 2.0f * w;
            Y = Y > fuel ? Y : fuel;
            T = T > hot ? T : hot;
        }

        float burn = T > prm[FIRE_IGNITION] ? Y * prm[FIRE_BURN] : 0.0f;
        Y -= burn;
        T += burn * prm[FIRE_HEAT];
        S += burn * prm[FIRE_SMOKE];
        // Radiative cooling, implicit in T^3 so large steps can't overshoot below zero.
        T = T / (1.0f + prm[FIRE_COOLING] * dt * T * T * T);
        S *= prm[FIRE_SMOKE_KEEP];
        scal[i * 4] = T;
        scal[i * 4 + 1] = Y;
        scal[i * 4 + 2] = S;
        scal[i * 4 + 3] = burn / dt;
    }
    )

    bool FireSim::s_DefaultOpenCL = false;
    int FireSim::minDeviceCells = 8000;

    FireSim::FireSim() { SetSettings(m_Settings); }

    void FireSim::SetSettings(const FireSettings& settings){
        m_Settings = settings;
        m_Settings.resolution = glm::clamp(settings.resolution, glm::ivec3(4), glm::ivec3(128));
        m_Settings.cellSize = std::max(settings.cellSize, 1e-3f);
        m_Settings.sourceRadius = glm::max(settings.sourceRadius, glm::vec2(1e-3f));
        m_Settings.pressureIterations = std::clamp(settings.pressureIterations, 1, 200);
        if (m_Settings.resolution != m_Dims){
            m_Dims = m_Settings.resolution;
            const size_t n = (size_t)Cells();
            for (auto* v : { &m_Velocities, &m_VelocitiesB, &m_Curl, &m_Scalars, &m_ScalarsA, &m_ScalarsB, &m_ScalarsC })
                v->assign(n, glm::vec4(0.0f));
            for (auto* v : { &m_Divergence, &m_Pressure, &m_PressureB }) { v->assign(n, 0.0f); }
            Clear();
        }
    }

    void FireSim::PackParams(float dt, const glm::vec3& wind, bool burning, std::vector<float>& out) const{
        const FireSettings& s = m_Settings;
        const float h = s.cellSize;
        out.assign(FIRE_PARAMS, 0.0f);
        out[FIRE_DT] = dt;
        out[FIRE_H] = h;
        out[FIRE_TIME] = (float)m_Step * dt;
        // The source in cell units (cell centres sit at whole numbers).
        out[FIRE_SRC_X] = s.source.x / h - 0.5f;
        out[FIRE_SRC_Y] = s.source.y / h - 0.5f;
        out[FIRE_SRC_Z] = s.source.z / h - 0.5f;
        out[FIRE_SRC_RH] = std::max(s.sourceRadius.x / h, 0.75f);
        out[FIRE_SRC_RV] = std::max(s.sourceRadius.y / h, 0.75f);
        out[FIRE_FUEL] = s.fuel;
        out[FIRE_EXIT] = s.exitSpeed;
        out[FIRE_BURN] = std::min(s.burnRate * dt, 1.0f);
        out[FIRE_HEAT] = s.heat;
        out[FIRE_IGNITION] = s.ignition;
        out[FIRE_COOLING] = s.cooling;
        out[FIRE_BUOYANCY] = s.buoyancy;
        out[FIRE_SMOKE] = s.smoke;
        out[FIRE_SMOKE_WEIGHT] = s.smokeWeight;
        out[FIRE_SMOKE_KEEP] = s.smokeLife > 0.0f ? std::exp(-dt * 1.0986123f / s.smokeLife) : 0.0f;
        out[FIRE_VORTICITY] = s.vorticity;
        out[FIRE_TURBULENCE] = s.turbulence;
        out[FIRE_DRAG] = 1.0f - std::exp(-std::max(s.drag, 0.0f) * dt);
        out[FIRE_WIND_X] = wind.x;
        out[FIRE_WIND_Y] = wind.y;
        out[FIRE_WIND_Z] = wind.z;
        out[FIRE_BURNING] = burning ? 1.0f : 0.0f;
    }

    void FireSim::Step(float dt, const glm::vec3& wind, bool burning){
        StepAll({ { this, dt, wind, burning } });
    }

    void FireSim::StepAll(const std::vector<StepRequest>& requests){
        struct Job{ FireSim* sim; float dt; bool burning; std::vector<float> params; };
        std::vector<Job> cpu;
        for (const StepRequest& r : requests){
            FireSim* sim = r.sim;
            if (!sim || r.dt <= 0.0f || sim->Cells() == 0) { continue; }
            Job job{ sim, r.dt, r.burning, {} };
            sim->PackParams(r.dt, r.wind, r.burning, job.params);
            if (sim->m_Device && sim->StepDevice(job.params)) { sim->Finish(r.dt, r.burning); continue; }
            if (sim->m_Device) { sim->m_Device.reset(); }   // device trouble: the CPU takes over
            cpu.push_back(std::move(job));
        }
        // One fire: its passes spread over the cores. Several: one fire per core at a time.
        if (cpu.size() == 1){
            cpu[0].sim->StepCPU(cpu[0].params, true);
            cpu[0].sim->Finish(cpu[0].dt, cpu[0].burning);
            return;
        }
        Workers::Get().For((int)cpu.size(), [&](int begin, int end){
            for (int j = begin; j < end; j++){
                cpu[(size_t)j].sim->StepCPU(cpu[(size_t)j].params, false);
                cpu[(size_t)j].sim->Finish(cpu[(size_t)j].dt, cpu[(size_t)j].burning);
            }
        }, 1);
    }

    void FireSim::Finish(float dt, bool burning){
        m_Step++;
        Measure();
        StepEmbers(dt, burning);
    }

    void FireSim::StepCPU(const std::vector<float>& params, bool threaded){
        const int nx = m_Dims.x, ny = m_Dims.y, nz = m_Dims.z, rows = ny * nz;
        const float* prm = params.data();
        const float s = prm[FIRE_DT] / prm[FIRE_H];
        auto& workers = Workers::Get();
        float* vel = &m_Velocities[0].x;
        float* velB = &m_VelocitiesB[0].x;
        float* curl = &m_Curl[0].x;
        float* scal = &m_Scalars[0].x;
        float* scalA = &m_ScalarsA[0].x;
        float* scalB = &m_ScalarsB[0].x;
        float* scalC = &m_ScalarsC[0].x;
        float* div = m_Divergence.data();
        // Every cell, a row of x at a time (rows split over the cores when threaded).
        auto each = [&](auto&& body){
            auto run = [&](int begin, int end){
                for (int r = begin; r < end; r++){
                    const int y = r % ny, z = r / ny;
                    for (int x = 0; x < nx; x++) { body(x, y, z); }
                }
            };
            if (threaded) { workers.For(rows, run, 8); }
            else          { run(0, rows); }
        };

        each([&](int x, int y, int z){ phx_fire_curl(x, y, z, vel, nx, ny, nz, prm, curl); });
        each([&](int x, int y, int z){ phx_fire_forces(x, y, z, vel, curl, scal, nx, ny, nz, prm, velB); });
        each([&](int x, int y, int z){ phx_fire_divergence(x, y, z, velB, nx, ny, nz, prm, div); });
        float* p = m_Pressure.data();
        float* pB = m_PressureB.data();
        for (int it = 0; it < m_Settings.pressureIterations; it++){
            each([&](int x, int y, int z){ phx_fire_jacobi(x, y, z, p, div, nx, ny, nz, prm, pB); });
            std::swap(p, pB);
        }
        if (p != m_Pressure.data()) { std::memcpy(m_Pressure.data(), p, sizeof(float) * (size_t)Cells()); }
        p = m_Pressure.data();
        each([&](int x, int y, int z){ phx_fire_project(x, y, z, velB, p, nx, ny, nz, prm, vel); });

        // Scalars ride the divergence-free velocity, then the velocity carries itself.
        each([&](int x, int y, int z){ phx_fire_departure(x, y, z, vel, nx, ny, nz, s, curl); });
        each([&](int x, int y, int z){ phx_fire_advect(x, y, z, curl, scal, nx, ny, nz, 0, scalA); });
        each([&](int x, int y, int z){ phx_fire_advect_back(x, y, z, vel, scalA, nx, ny, nz, s, scalB); });
        each([&](int x, int y, int z){ phx_fire_maccormack(x, y, z, curl, scal, scalA, scalB, nx, ny, nz, scalC); });
        each([&](int x, int y, int z){ phx_fire_advect(x, y, z, curl, vel, nx, ny, nz, 1, velB); });
        std::swap(m_Scalars, m_ScalarsC);
        std::swap(m_Velocities, m_VelocitiesB);
        scal = &m_Scalars[0].x;
        each([&](int x, int y, int z){ phx_fire_react(x, y, z, scal, nx, ny, prm); });
    }

    void FireSim::Measure(){
        const int nx = m_Dims.x, ny = m_Dims.y, nz = m_Dims.z;
        const float h = m_Settings.cellSize, volume = h * h * h;
        double power = 0.0, temperature = 0.0;
        glm::dvec3 centroid(0.0);
        for (int z = 0; z < nz; z++)
            for (int y = 0; y < ny; y++)
                for (int x = 0; x < nx; x++){
                    float T = m_Scalars[(size_t)((z * ny + y) * nx + x)].x;
                    double e = (double)T * T * T * T;
                    power += e;
                    temperature += e * T;
                    centroid += e * glm::dvec3(x + 0.5, y + 0.5, z + 0.5);
                }
        m_Power = (float)(power * volume);
        m_FlameTemperature = power > 0.0 ? (float)(temperature / power) : 0.0f;
        m_Centroid = power > 0.0 ? glm::vec3(centroid / power) * h : m_Settings.source;
    }

    glm::vec4 FireSim::SampleScalars(const glm::vec3& p) const{
        float out[4];
        glm::vec3 c = p / m_Settings.cellSize - 0.5f;
        phx_fire_sample_scal(&m_Scalars[0].x, c.x, c.y, c.z, m_Dims.x, m_Dims.y, m_Dims.z, out);
        return { out[0], out[1], out[2], out[3] };
    }

    glm::vec3 FireSim::SampleVelocity(const glm::vec3& p) const{
        glm::vec3 c = p / m_Settings.cellSize - 0.5f;
        if (glm::any(glm::lessThan(c, glm::vec3(-0.5f))) || glm::any(glm::greaterThan(c, glm::vec3(m_Dims) - 0.5f)))
            return glm::vec3(0.0f);
        float out[3];
        phx_fire_sample_vel(&m_Velocities[0].x, c.x, c.y, c.z, m_Dims.x, m_Dims.y, m_Dims.z, out);
        return { out[0], out[1], out[2] };
    }

    // Embers: thrown from the source, carried by the gas (and by their own momentum
    // outside it), glowing as they cool.
    void FireSim::StepEmbers(float dt, bool burning){
        auto random = [this](){
            m_Random ^= m_Random << 13; m_Random ^= m_Random >> 17; m_Random ^= m_Random << 5;
            return (float)(m_Random >> 8) * (1.0f / 16777216.0f);
        };
        if (burning && m_Settings.emberRate > 0.0f){
            m_EmberDebt += m_Settings.emberRate * dt;
            while (m_EmberDebt >= 1.0f && m_Embers.size() < 256){
                m_EmberDebt -= 1.0f;
                float a = random() * 6.2831853f, r = std::sqrt(random()) * m_Settings.sourceRadius.x;
                Ember e;
                e.position = m_Settings.source + glm::vec3(std::cos(a) * r, (random() - 0.5f) * m_Settings.sourceRadius.y, std::sin(a) * r);
                e.velocity = SampleVelocity(e.position) + glm::vec3((random() - 0.5f) * 0.6f, 0.4f + random() * 0.8f, (random() - 0.5f) * 0.6f);
                e.temperature = 1.0f + random() * 0.4f;
                e.age = 0.0f;
                e.life = 0.6f + random() * 1.8f;
                e.size = 0.6f + random() * 0.8f;
                m_Embers.push_back(e);
            }
        }
        const float follow = 1.0f - std::exp(-dt * 6.0f), drag = 1.0f - std::exp(-dt * 1.5f);
        for (Ember& e : m_Embers){
            glm::vec3 c = e.position / m_Settings.cellSize;
            bool inside = glm::all(glm::greaterThanEqual(c, glm::vec3(0.0f))) && glm::all(glm::lessThan(c, glm::vec3(m_Dims)));
            e.velocity += inside ? (SampleVelocity(e.position) - e.velocity) * follow : -e.velocity * drag;
            e.velocity.y -= 1.5f * dt;   // heavier than the gas
            e.position += e.velocity * dt;
            e.temperature -= dt * 0.5f;
            e.age += dt;
        }
        m_Embers.erase(std::remove_if(m_Embers.begin(), m_Embers.end(),
            [](const Ember& e){ return e.age >= e.life || e.temperature <= 0.3f; }), m_Embers.end());
    }

#ifdef PHOENIX_OPENCL
    static const char* s_FireKernels = R"CLC(
#define FIRE_CELL int g = get_global_id(0); if (g >= nx * ny * nz) { return; } int x = g % nx, y = (g / nx) % ny, z = g / (nx * ny);
__kernel void fire_curl(__global const float* vel, int nx, int ny, int nz, __global const float* prm, __global float* curl){
    FIRE_CELL phx_fire_curl(x, y, z, vel, nx, ny, nz, prm, curl);
}
__kernel void fire_forces(__global const float* vel, __global const float* curl, __global const float* scal,
                          int nx, int ny, int nz, __global const float* prm, __global float* out){
    FIRE_CELL phx_fire_forces(x, y, z, vel, curl, scal, nx, ny, nz, prm, out);
}
__kernel void fire_divergence(__global const float* vel, int nx, int ny, int nz, __global const float* prm, __global float* div){
    FIRE_CELL phx_fire_divergence(x, y, z, vel, nx, ny, nz, prm, div);
}
__kernel void fire_jacobi(__global const float* p, __global const float* div, int nx, int ny, int nz,
                          __global const float* prm, __global float* out){
    FIRE_CELL phx_fire_jacobi(x, y, z, p, div, nx, ny, nz, prm, out);
}
__kernel void fire_project(__global const float* vel, __global const float* p, int nx, int ny, int nz,
                           __global const float* prm, __global float* out){
    FIRE_CELL phx_fire_project(x, y, z, vel, p, nx, ny, nz, prm, out);
}
__kernel void fire_departure(__global const float* vel, int nx, int ny, int nz, float s, __global float* out){
    FIRE_CELL phx_fire_departure(x, y, z, vel, nx, ny, nz, s, out);
}
__kernel void fire_advect(__global const float* departure, __global const float* f, int nx, int ny, int nz,
                          int self, __global float* out){
    FIRE_CELL phx_fire_advect(x, y, z, departure, f, nx, ny, nz, self, out);
}
__kernel void fire_advect_back(__global const float* vel, __global const float* f, int nx, int ny, int nz,
                               float s, __global float* out){
    FIRE_CELL phx_fire_advect_back(x, y, z, vel, f, nx, ny, nz, s, out);
}
__kernel void fire_maccormack(__global const float* departure, __global const float* f, __global const float* fwd,
                              __global const float* back, int nx, int ny, int nz, __global float* out){
    FIRE_CELL phx_fire_maccormack(x, y, z, departure, f, fwd, back, nx, ny, nz, out);
}
__kernel void fire_react(__global float* scal, int nx, int ny, int nz, __global const float* prm){
    FIRE_CELL phx_fire_react(x, y, z, scal, nx, ny, prm);
}
)CLC";

    static const char* s_FirePrelude = "#define PHX_GLOBAL __global\n";

    struct FireSim::Device{
        cl_program program = nullptr;
        cl_kernel curl = nullptr, forces = nullptr, divergence = nullptr, jacobi = nullptr, project = nullptr;
        cl_kernel departure = nullptr, advect = nullptr, advectBack = nullptr, maccormack = nullptr, react = nullptr;
        cl_mem vel = nullptr, velB = nullptr, curlBuf = nullptr, scal = nullptr, scalA = nullptr, scalB = nullptr, scalC = nullptr;
        cl_mem div = nullptr, p = nullptr, pB = nullptr, prm = nullptr;
        size_t capVel = 0, capVelB = 0, capCurl = 0, capScal = 0, capA = 0, capB = 0, capC = 0, capDiv = 0, capP = 0, capPB = 0, capPrm = 0;
        bool synced = false;   // the device holds the current velocity, scalars and pressure

        ~Device(){
            for (cl_mem m : { vel, velB, curlBuf, scal, scalA, scalB, scalC, div, p, pB, prm })
                if (m) { clReleaseMemObject(m); }
            for (cl_kernel k : { curl, forces, divergence, jacobi, project, departure, advect, advectBack, maccormack, react })
                if (k) { clReleaseKernel(k); }
            if (program) { clReleaseProgram(program); }
        }
    };

    bool FireSim::UseOpenCL(bool enable){
        if (enable == (m_Device != nullptr)) { return enable; }
        if (!enable) { m_Device.reset(); return false; }
        if (!Compute::Available()) { return false; }
        auto device = std::make_unique<Device>();
        std::string error;
        device->program = Compute::BuildProgram({ s_FirePrelude, s_FireSource, s_FireKernels }, error);
        if (!device->program) { return false; }
        cl_int err = CL_SUCCESS, e;
        device->curl = clCreateKernel(device->program, "fire_curl", &e); err |= e;
        device->forces = clCreateKernel(device->program, "fire_forces", &e); err |= e;
        device->divergence = clCreateKernel(device->program, "fire_divergence", &e); err |= e;
        device->jacobi = clCreateKernel(device->program, "fire_jacobi", &e); err |= e;
        device->project = clCreateKernel(device->program, "fire_project", &e); err |= e;
        device->departure = clCreateKernel(device->program, "fire_departure", &e); err |= e;
        device->advect = clCreateKernel(device->program, "fire_advect", &e); err |= e;
        device->advectBack = clCreateKernel(device->program, "fire_advect_back", &e); err |= e;
        device->maccormack = clCreateKernel(device->program, "fire_maccormack", &e); err |= e;
        device->react = clCreateKernel(device->program, "fire_react", &e); err |= e;
        if (err != CL_SUCCESS) { return false; }
        m_Device = std::move(device);
        return true;
    }

    template<typename T>
    static void FireArg(cl_kernel k, cl_uint index, const T& value, cl_int& err) { err |= clSetKernelArg(k, index, sizeof(T), &value); }

    bool FireSim::StepDevice(const std::vector<float>& params){
        Device& d = *m_Device;
        using Compute::Reserve;
        const size_t n = (size_t)Cells();
        const cl_mem_flags rw = CL_MEM_READ_WRITE;
        if (!Reserve(d.vel, d.capVel, n * 4, sizeof(float), rw) || !Reserve(d.velB, d.capVelB, n * 4, sizeof(float), rw)
            || !Reserve(d.curlBuf, d.capCurl, n * 4, sizeof(float), rw) || !Reserve(d.scal, d.capScal, n * 4, sizeof(float), rw)
            || !Reserve(d.scalA, d.capA, n * 4, sizeof(float), rw) || !Reserve(d.scalB, d.capB, n * 4, sizeof(float), rw)
            || !Reserve(d.scalC, d.capC, n * 4, sizeof(float), rw) || !Reserve(d.div, d.capDiv, n, sizeof(float), rw)
            || !Reserve(d.p, d.capP, n, sizeof(float), rw) || !Reserve(d.pB, d.capPB, n, sizeof(float), rw)
            || !Reserve(d.prm, d.capPrm, params.size(), sizeof(float), CL_MEM_READ_ONLY))
            return false;

        cl_int err = CL_SUCCESS;
        if (!d.synced){
            err |= Compute::Write(d.vel, &m_Velocities[0].x, n * 4 * sizeof(float));
            err |= Compute::Write(d.scal, &m_Scalars[0].x, n * 4 * sizeof(float));
            err |= Compute::Write(d.p, m_Pressure.data(), n * sizeof(float));
            d.synced = true;
        }
        err |= Compute::Write(d.prm, params.data(), params.size() * sizeof(float));

        const cl_int nx = m_Dims.x, ny = m_Dims.y, nz = m_Dims.z;
        const float s = params[FIRE_DT] / params[FIRE_H];
        const cl_int scalars = 0, self = 1;
        auto grid = [&](cl_kernel k, cl_uint first){ FireArg(k, first, nx, err); FireArg(k, first + 1, ny, err); FireArg(k, first + 2, nz, err); };

        cl_kernel k = d.curl;
        FireArg(k, 0, d.vel, err); grid(k, 1); FireArg(k, 4, d.prm, err); FireArg(k, 5, d.curlBuf, err);
        err |= Compute::Launch(k, n);
        k = d.forces;
        FireArg(k, 0, d.vel, err); FireArg(k, 1, d.curlBuf, err); FireArg(k, 2, d.scal, err); grid(k, 3);
        FireArg(k, 6, d.prm, err); FireArg(k, 7, d.velB, err);
        err |= Compute::Launch(k, n);
        k = d.divergence;
        FireArg(k, 0, d.velB, err); grid(k, 1); FireArg(k, 4, d.prm, err); FireArg(k, 5, d.div, err);
        err |= Compute::Launch(k, n);
        k = d.jacobi;
        for (int it = 0; it < m_Settings.pressureIterations; it++){
            FireArg(k, 0, d.p, err); FireArg(k, 1, d.div, err); grid(k, 2); FireArg(k, 5, d.prm, err); FireArg(k, 6, d.pB, err);
            err |= Compute::Launch(k, n);
            std::swap(d.p, d.pB);
            std::swap(d.capP, d.capPB);
        }
        k = d.project;
        FireArg(k, 0, d.velB, err); FireArg(k, 1, d.p, err); grid(k, 2); FireArg(k, 5, d.prm, err); FireArg(k, 6, d.vel, err);
        err |= Compute::Launch(k, n);

        k = d.departure;
        FireArg(k, 0, d.vel, err); grid(k, 1); FireArg(k, 4, s, err); FireArg(k, 5, d.curlBuf, err);
        err |= Compute::Launch(k, n);
        k = d.advect;
        FireArg(k, 0, d.curlBuf, err); FireArg(k, 1, d.scal, err); grid(k, 2); FireArg(k, 5, scalars, err); FireArg(k, 6, d.scalA, err);
        err |= Compute::Launch(k, n);
        k = d.advectBack;
        FireArg(k, 0, d.vel, err); FireArg(k, 1, d.scalA, err); grid(k, 2); FireArg(k, 5, s, err); FireArg(k, 6, d.scalB, err);
        err |= Compute::Launch(k, n);
        k = d.maccormack;
        FireArg(k, 0, d.curlBuf, err); FireArg(k, 1, d.scal, err); FireArg(k, 2, d.scalA, err); FireArg(k, 3, d.scalB, err); grid(k, 4);
        FireArg(k, 7, d.scalC, err);
        err |= Compute::Launch(k, n);
        k = d.advect;
        FireArg(k, 0, d.curlBuf, err); FireArg(k, 1, d.vel, err); grid(k, 2); FireArg(k, 5, self, err); FireArg(k, 6, d.velB, err);
        err |= Compute::Launch(k, n);
        std::swap(d.scal, d.scalC); std::swap(d.capScal, d.capC);
        std::swap(d.vel, d.velB); std::swap(d.capVel, d.capVelB);
        k = d.react;
        FireArg(k, 0, d.scal, err); grid(k, 1); FireArg(k, 4, d.prm, err);
        err |= Compute::Launch(k, n);

        cl_command_queue queue = Compute::GetDevice()->queue;
        err |= clEnqueueReadBuffer(queue, d.vel, CL_FALSE, 0, n * 4 * sizeof(float), &m_Velocities[0].x, 0, nullptr, nullptr);
        err |= clEnqueueReadBuffer(queue, d.scal, CL_TRUE, 0, n * 4 * sizeof(float), &m_Scalars[0].x, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) { d.synced = false; }
        return err == CL_SUCCESS;
    }
#else
    struct FireSim::Device{ bool synced = false; };
    bool FireSim::UseOpenCL(bool) { return false; }
    bool FireSim::StepDevice(const std::vector<float>&) { return false; }
#endif

    // After Device is complete (it marks the device copy stale).
    void FireSim::Clear(){
        std::fill(m_Velocities.begin(), m_Velocities.end(), glm::vec4(0.0f));
        std::fill(m_Scalars.begin(), m_Scalars.end(), glm::vec4(0.0f));
        std::fill(m_Pressure.begin(), m_Pressure.end(), 0.0f);
        m_Embers.clear();
        m_EmberDebt = 0.0f;
        m_Step = 0;
        m_Power = m_FlameTemperature = 0.0f;
        m_Centroid = glm::vec3(0.0f);
        if (m_Device) { m_Device->synced = false; }   // re-upload the cleared state
    }

    FireSim::~FireSim() = default;
}
