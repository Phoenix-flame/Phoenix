#pragma once
#include <Phoenix/core/base.h>
#include <Phoenix/renderer/Mesh.h>

namespace Phoenix::Primitives{

    // Unit-sized primitive meshes (fit in a [-0.5, 0.5] box) with normals + UVs.
    // Scale them with the entity transform.
    Ref<Mesh> Cube();
    Ref<Mesh> Sphere(int segments = 32, int rings = 16);
    Ref<Mesh> Cylinder(int segments = 32);
    Ref<Mesh> Cone(int segments = 32);
    Ref<Mesh> Plane();
}
