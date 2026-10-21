#include "bge/render/mesh.h"

#include <algorithm>
#include <cmath>

namespace bge {

void MeshData::compute_bounds() {
    bounds = AABB{};
    for (const Vertex& v : vertices) bounds.expand(v.position);
}

MeshData make_cube(float size) {
    const float h = size * 0.5f;
    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    struct Face {
        Vec3 normal;
        Vec3 corners[4];
    };
    const Vec3 p000{-h, -h, -h}, p001{-h, -h, h}, p010{-h, h, -h}, p011{-h, h, h};
    const Vec3 p100{h, -h, -h}, p101{h, -h, h}, p110{h, h, -h}, p111{h, h, h};

    const Face faces[6] = {
        {{0, 0, 1}, {p001, p101, p111, p011}},   // +Z
        {{0, 0, -1}, {p100, p000, p010, p110}},  // -Z
        {{1, 0, 0}, {p101, p100, p110, p111}},   // +X
        {{-1, 0, 0}, {p000, p001, p011, p010}},  // -X
        {{0, 1, 0}, {p011, p111, p110, p010}},   // +Y
        {{0, -1, 0}, {p000, p100, p110, p010}},  // -Y (winding fixed below)
    };
    const Vec2 uvs[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    for (int f = 0; f < 6; ++f) {
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (int i = 0; i < 4; ++i) {
            mesh.vertices.push_back({faces[f].corners[i], faces[f].normal, uvs[i]});
        }
        // All faces are counter-clockwise when viewed from outside.
        mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
    }
    mesh.compute_bounds();
    return mesh;
}

MeshData make_plane(float size, float uv_scale) {
    const float h = size * 0.5f;
    MeshData mesh;
    mesh.vertices = {
        {{-h, 0, -h}, {0, 1, 0}, {0, 0}},
        {{h, 0, -h}, {0, 1, 0}, {uv_scale, 0}},
        {{h, 0, h}, {0, 1, 0}, {uv_scale, uv_scale}},
        {{-h, 0, h}, {0, 1, 0}, {0, uv_scale}},
    };
    mesh.indices = {0, 2, 1, 0, 3, 2};
    mesh.compute_bounds();
    return mesh;
}

MeshData make_sphere(float radius, int segments, int rings) {
    segments = std::max(segments, 3);
    rings = std::max(rings, 2);
    MeshData mesh;
    mesh.vertices.reserve(static_cast<size_t>((segments + 1) * (rings + 1)));
    mesh.indices.reserve(static_cast<size_t>(segments * rings * 6));

    for (int y = 0; y <= rings; ++y) {
        float v = static_cast<float>(y) / static_cast<float>(rings);
        float phi = v * PI;
        float sin_phi = std::sin(phi);
        float cos_phi = std::cos(phi);
        for (int x = 0; x <= segments; ++x) {
            float u = static_cast<float>(x) / static_cast<float>(segments);
            float theta = u * TWO_PI;
            Vec3 n{sin_phi * std::cos(theta), cos_phi, sin_phi * std::sin(theta)};
            mesh.vertices.push_back({n * radius, n, {u, 1.0f - v}});
        }
    }

    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            uint32_t a = static_cast<uint32_t>(y * (segments + 1) + x);
            uint32_t b = a + static_cast<uint32_t>(segments + 1);
            mesh.indices.insert(mesh.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    }
    mesh.compute_bounds();
    return mesh;
}

MeshData make_torus_knot(float scale, float tube_radius, int segments, int tube_segments) {
    segments = std::max(segments, 8);
    tube_segments = std::max(tube_segments, 3);
    MeshData mesh;
    mesh.vertices.reserve(static_cast<size_t>((segments + 1) * (tube_segments + 1)));
    mesh.indices.reserve(static_cast<size_t>(segments * tube_segments * 6));

    const float p = 2.0f, q = 3.0f;
    auto curve = [&](float u) -> Vec3 {
        float r = 2.0f + std::cos(q * u) * 0.75f;
        return Vec3{r * std::cos(p * u), std::sin(q * u) * 0.75f, r * std::sin(p * u)} * scale;
    };

    for (int i = 0; i <= segments; ++i) {
        float u = (static_cast<float>(i) / static_cast<float>(segments)) * TWO_PI;
        Vec3 center = curve(u);
        Vec3 tangent = normalize(curve(u + 0.001f) - curve(u - 0.001f));
        Vec3 reference = std::fabs(tangent.y) > 0.95f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        Vec3 bitangent = normalize(cross(tangent, reference));
        Vec3 normal = normalize(cross(bitangent, tangent));

        for (int j = 0; j <= tube_segments; ++j) {
            float v = (static_cast<float>(j) / static_cast<float>(tube_segments)) * TWO_PI;
            Vec3 dir = normal * std::cos(v) + bitangent * std::sin(v);
            Vec3 position = center + dir * (tube_radius * scale);
            mesh.vertices.push_back({position, dir, {static_cast<float>(i) / segments, static_cast<float>(j) / tube_segments}});
        }
    }

    for (int i = 0; i < segments; ++i) {
        for (int j = 0; j < tube_segments; ++j) {
            uint32_t a = static_cast<uint32_t>(i * (tube_segments + 1) + j);
            uint32_t b = a + static_cast<uint32_t>(tube_segments + 1);
            mesh.indices.insert(mesh.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    }
    mesh.compute_bounds();
    return mesh;
}

} // namespace bge
