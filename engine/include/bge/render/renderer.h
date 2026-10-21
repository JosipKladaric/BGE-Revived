#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "bge/math/math.h"
#include "bge/render/mesh.h"

namespace bge {

class Window;
class Scene;
struct CameraState;

using MeshHandle = uint32_t;
using MaterialHandle = uint32_t;
using TextureHandle = uint32_t;

struct MaterialDesc {
    Vec4 base_color{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    Vec3 emissive{0.0f, 0.0f, 0.0f};
    float uv_scale = 1.0f;
    TextureHandle albedo = 0;
    bool use_albedo = false;
    TextureHandle normal = 0;
    bool use_normal = false;
    float normal_strength = 1.0f;
    bool blend = false;
    bool alpha_clip = false;
};

struct RenderStats {
    uint32_t draw_calls = 0;
    uint32_t shadow_draw_calls = 0;
    uint32_t instances = 0;
    uint32_t triangles = 0;
    uint32_t culled = 0;
    float cpu_ms = 0.0f;
};

struct RendererDesc {
    std::string asset_root = "assets";
    int shadow_size = 2048;
    bool shadows = true;
    bool fxaa = true;
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(Window& window, const RendererDesc& desc);
    void shutdown();
    void resize(int width, int height);

    void begin_frame();
    void render(Scene& scene, const CameraState& camera);

    MeshHandle create_mesh(const MeshData& data);
    MaterialHandle create_material(const MaterialDesc& material);
    void set_material(MaterialHandle handle, const MaterialDesc& material);

    // CPU side copies of the uploaded data, used when saving a scene back out.
    bool get_mesh_data(MeshHandle handle, MeshData& out) const;
    bool get_material(MaterialHandle handle, MaterialDesc& out) const;
    bool get_texture_path(TextureHandle handle, std::string& out) const;
    size_t mesh_count() const;
    size_t material_count() const;

    TextureHandle load_texture(const std::string& path, bool srgb = true);
    TextureHandle load_environment_texture(const std::string& path);
    TextureHandle create_checker_texture(int size, const Vec3& a, const Vec3& b, int cells);

    const RenderStats& stats() const;
    int width() const;
    int height() const;
    bool initialized() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bge
