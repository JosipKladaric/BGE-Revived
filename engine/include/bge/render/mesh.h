#pragma once

#include <cstdint>
#include <vector>

#include "bge/math/math.h"

namespace bge {

struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    AABB bounds;

    void compute_bounds();
};

MeshData make_cube(float size = 1.0f);
MeshData make_plane(float size = 1.0f, float uv_scale = 1.0f);
MeshData make_sphere(float radius = 1.0f, int segments = 48, int rings = 32);
MeshData make_torus_knot(float scale = 1.0f, float tube_radius = 0.18f, int segments = 256, int tube_segments = 24);

} // namespace bge
