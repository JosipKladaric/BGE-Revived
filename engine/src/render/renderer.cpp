#include "bge/render/renderer.h"

#include "bge/core/log.h"
#include "bge/platform/window.h"
#include "bge/render/shader.h"
#include "bge/render/texture.h"
#include "bge/scene/scene.h"

#include <glad/gl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <vector>

namespace bge {
namespace {

struct MeshGPU {
    uint32_t vao = 0;
    uint32_t vbo = 0;
    uint32_t ebo = 0;
    uint32_t index_count = 0;
    AABB bounds;
    MeshData data;  // CPU copy, kept so scenes can be saved back to disk
};

struct MaterialGPU {
    MaterialDesc desc;
};

struct InstanceData {
    Mat4 model;
    Vec4 normal0;
    Vec4 normal1;
    Vec4 normal2;
    Vec4 tint;
};

struct DrawItem {
    MeshHandle mesh = 0;
    MaterialHandle material = 0;
    bool cast_shadow = true;
    Vec4 tint{1, 1, 1, 1};
    Mat4 model{Mat4::identity()};
    Mat4 normal{Mat4::identity()};
    AABB bounds;
};

float milliseconds_since(std::chrono::steady_clock::time_point start) {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<float, std::milli>(now - start).count();
}

} // namespace

struct Renderer::Impl {
    RendererDesc desc;
    int width = 0;
    int height = 0;
    RenderStats stats;
    uint64_t frame_counter = 0;

    std::vector<MeshGPU> meshes;
    std::vector<MaterialGPU> materials;
    std::vector<TextureGPU> textures;

    Shader pbr;
    Shader depth;
    Shader sky;
    Shader post;

    uint32_t instance_vbo = 0;
    uint32_t fullscreen_vao = 0;

    uint32_t hdr_fbo = 0;
    uint32_t hdr_color = 0;
    uint32_t hdr_depth = 0;

    uint32_t shadow_fbo = 0;
    uint32_t shadow_map = 0;
    int shadow_size = 0;

    bool ready = false;

    bool init(Window& window, const RendererDesc& renderer_desc) {
        desc = renderer_desc;
        if (!gladLoaderLoadGL()) {
            BGE_ERROR("Failed to load OpenGL function pointers");
            return false;
        }

        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        BGE_INFO("Renderer: OpenGL %d.%d · %s", major, minor,
                 reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glDisable(GL_BLEND);
        glDisable(GL_FRAMEBUFFER_SRGB);

        glCreateVertexArrays(1, &fullscreen_vao);
        glCreateBuffers(1, &instance_vbo);
        glNamedBufferData(instance_vbo, sizeof(InstanceData) * 64, nullptr, GL_STREAM_DRAW);

        textures.push_back(make_white_texture());
        materials.push_back(MaterialGPU{MaterialDesc{}});
        meshes.push_back(MeshGPU{});

        const std::string dir = desc.asset_root + "/shaders";
        if (!pbr.load(dir + "/pbr.vert", dir + "/pbr.frag")) return false;
        if (!depth.load(dir + "/depth.vert", dir + "/depth.frag")) return false;
        if (!sky.load(dir + "/fullscreen.vert", dir + "/sky.frag")) return false;
        if (!post.load(dir + "/fullscreen.vert", dir + "/post.frag")) return false;

        create_shadow_target(desc.shadow_size);
        resize_targets(window.width(), window.height());

        ready = true;
        BGE_INFO("Renderer initialised (shadow map %dx%d, HDR RGBA16F)", shadow_size, shadow_size);
        return true;
    }

    void destroy() {
        for (MeshGPU& m : meshes) {
            if (m.vao) glDeleteVertexArrays(1, &m.vao);
            if (m.vbo) glDeleteBuffers(1, &m.vbo);
            if (m.ebo) glDeleteBuffers(1, &m.ebo);
        }
        meshes.clear();

        for (TextureGPU& t : textures) {
            if (t.id) glDeleteTextures(1, &t.id);
        }
        textures.clear();

        if (hdr_fbo) glDeleteFramebuffers(1, &hdr_fbo);
        if (hdr_color) glDeleteTextures(1, &hdr_color);
        if (hdr_depth) glDeleteTextures(1, &hdr_depth);
        if (shadow_fbo) glDeleteFramebuffers(1, &shadow_fbo);
        if (shadow_map) glDeleteTextures(1, &shadow_map);
        if (instance_vbo) glDeleteBuffers(1, &instance_vbo);
        if (fullscreen_vao) glDeleteVertexArrays(1, &fullscreen_vao);

        hdr_fbo = hdr_color = hdr_depth = 0;
        shadow_fbo = shadow_map = 0;
        instance_vbo = fullscreen_vao = 0;

        pbr.destroy();
        depth.destroy();
        sky.destroy();
        post.destroy();
        ready = false;
    }

    void create_shadow_target(int size) {
        shadow_size = std::max(size, 512);
        if (shadow_fbo) glDeleteFramebuffers(1, &shadow_fbo);
        if (shadow_map) glDeleteTextures(1, &shadow_map);
        shadow_fbo = 0;
        shadow_map = 0;

        glCreateTextures(GL_TEXTURE_2D, 1, &shadow_map);
        glTextureStorage2D(shadow_map, 1, GL_DEPTH_COMPONENT24, shadow_size, shadow_size);
        glTextureParameteri(shadow_map, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(shadow_map, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(shadow_map, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTextureParameteri(shadow_map, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTextureParameterfv(shadow_map, GL_TEXTURE_BORDER_COLOR, border);
        glTextureParameteri(shadow_map, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTextureParameteri(shadow_map, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

        glCreateFramebuffers(1, &shadow_fbo);
        glNamedFramebufferTexture(shadow_fbo, GL_DEPTH_ATTACHMENT, shadow_map, 0);
        glNamedFramebufferDrawBuffer(shadow_fbo, GL_NONE);
        glNamedFramebufferReadBuffer(shadow_fbo, GL_NONE);
        if (glCheckNamedFramebufferStatus(shadow_fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            BGE_ERROR("Shadow framebuffer incomplete");
        }
    }

    void resize_targets(int new_width, int new_height) {
        new_width = std::max(new_width, 1);
        new_height = std::max(new_height, 1);
        if (hdr_fbo && new_width == width && new_height == height) return;

        if (hdr_fbo) glDeleteFramebuffers(1, &hdr_fbo);
        if (hdr_color) glDeleteTextures(1, &hdr_color);
        if (hdr_depth) glDeleteTextures(1, &hdr_depth);
        hdr_fbo = hdr_color = hdr_depth = 0;

        width = new_width;
        height = new_height;

        glCreateTextures(GL_TEXTURE_2D, 1, &hdr_color);
        glTextureStorage2D(hdr_color, 1, GL_RGBA16F, width, height);
        glTextureParameteri(hdr_color, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(hdr_color, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(hdr_color, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(hdr_color, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glCreateTextures(GL_TEXTURE_2D, 1, &hdr_depth);
        glTextureStorage2D(hdr_depth, 1, GL_DEPTH_COMPONENT24, width, height);
        glTextureParameteri(hdr_depth, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(hdr_depth, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glCreateFramebuffers(1, &hdr_fbo);
        glNamedFramebufferTexture(hdr_fbo, GL_COLOR_ATTACHMENT0, hdr_color, 0);
        glNamedFramebufferTexture(hdr_fbo, GL_DEPTH_ATTACHMENT, hdr_depth, 0);
        glNamedFramebufferDrawBuffer(hdr_fbo, GL_COLOR_ATTACHMENT0);
        if (glCheckNamedFramebufferStatus(hdr_fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            BGE_ERROR("HDR framebuffer incomplete");
        }
        BGE_DEBUG("Render targets resized: %dx%d", width, height);
    }

    void upload_instances(const std::vector<InstanceData>& data) {
        if (data.empty()) return;
        glNamedBufferData(instance_vbo, static_cast<GLsizeiptr>(data.size() * sizeof(InstanceData)),
                          data.data(), GL_STREAM_DRAW);
    }

    void bind_material(MaterialHandle handle) {
        const MaterialDesc& m = materials[handle].desc;
        pbr.set_vec4("u_base_color", m.base_color);
        pbr.set_float("u_metallic", m.metallic);
        pbr.set_float("u_roughness", std::max(m.roughness, 0.045f));
        pbr.set_vec3("u_emissive", m.emissive);
        pbr.set_float("u_uv_scale", m.uv_scale);
        pbr.set_int("u_use_albedo", m.use_albedo ? 1 : 0);
        pbr.set_int("u_use_normal", m.use_normal ? 1 : 0);
        pbr.set_float("u_normal_strength", m.normal_strength);
        pbr.set_int("u_alpha_clip", m.alpha_clip ? 1 : 0);
        pbr.set_int("u_albedo_tex", 1);
        pbr.set_int("u_normal_tex", 2);
        uint32_t texture = 0;
        if (m.use_albedo && m.albedo < textures.size()) texture = textures[m.albedo].id;
        else texture = textures[0].id;
        glBindTextureUnit(1, texture);
        uint32_t normal = 0;
        if (m.use_normal && m.normal < textures.size()) normal = textures[m.normal].id;
        else normal = textures[0].id;
        glBindTextureUnit(2, normal);
    }

    void draw_batches(const std::vector<DrawItem>& items, bool shadow_pass) {
        if (items.empty()) return;
        uint32_t batch_start = 0;
        for (uint32_t i = 1; i <= items.size(); ++i) {
            bool boundary = (i == items.size()) ||
                            (items[i].mesh != items[batch_start].mesh) ||
                            (!shadow_pass && items[i].material != items[batch_start].material);
            if (!boundary) continue;

            const MeshGPU& mesh = meshes[items[batch_start].mesh];
            if (!shadow_pass) bind_material(items[batch_start].material);
            uint32_t count = i - batch_start;
            glBindVertexArray(mesh.vao);
            glDrawElementsInstancedBaseInstance(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT,
                                                nullptr, static_cast<GLsizei>(count), batch_start);
            if (shadow_pass) {
                ++stats.shadow_draw_calls;
            } else {
                ++stats.draw_calls;
                stats.triangles += (mesh.index_count / 3) * count;
            }
            batch_start = i;
        }
    }

    void draw_frame(Scene& scene, const CameraState& camera) {
        auto start = std::chrono::steady_clock::now();
        stats = {};
        if (!ready || width <= 0 || height <= 0) return;

        CameraState cam = camera;
        cam.forward = normalize(cam.forward);
        if (length_sq(cam.forward) < 1e-6f) cam.forward = {0, 0, -1};
        cam.up = normalize(cam.up);
        if (length_sq(cam.up) < 1e-6f) cam.up = {0, 1, 0};

        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const Mat4 view = cam.view();
        const Mat4 projection = cam.projection(aspect);
        const Mat4 view_projection = projection * view;
        const Frustum frustum = Frustum::from_matrix(view_projection);

        std::vector<DrawItem> items;
        items.reserve(1024);
        scene.world().each<Transform, MeshRenderer>([&](Entity, Transform& transform, MeshRenderer& renderer) {
            if (renderer.mesh == 0 || renderer.mesh >= meshes.size()) return;
            if (!renderer.visible) return;
            const MeshGPU& mesh = meshes[renderer.mesh];
            AABB bounds = mesh.bounds.transformed(transform.world);
            if (!frustum.intersects(bounds)) {
                ++stats.culled;
                return;
            }
            DrawItem item;
            item.mesh = renderer.mesh;
            item.material = (renderer.material < materials.size()) ? renderer.material : 0;
            item.cast_shadow = renderer.cast_shadow;
            item.tint = renderer.tint;
            item.model = transform.world;
            item.normal = transform.normal;
            item.bounds = bounds;
            items.push_back(item);
        });

        std::sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) {
            if (a.mesh != b.mesh) return a.mesh < b.mesh;
            return a.material < b.material;
        });
        stats.instances = static_cast<uint32_t>(items.size());

        std::vector<DrawItem> opaque_items;
        std::vector<DrawItem> transparent_items;
        opaque_items.reserve(items.size());
        for (const DrawItem& item : items) {
            bool blend = item.material < materials.size() && materials[item.material].desc.blend;
            if (blend) transparent_items.push_back(item);
            else opaque_items.push_back(item);
        }
        std::sort(transparent_items.begin(), transparent_items.end(),
                  [&](const DrawItem& a, const DrawItem& b) {
                      float da = length_sq(a.model.translation() - cam.position);
                      float db = length_sq(b.model.translation() - cam.position);
                      return da > db;
                  });

        // Gather Blender-compatible lights. The first shadow-casting sun drives the
        // directional shadow map; every light still contributes to direct shading.
        struct LightData {
            Light light;
            Vec3 position;
        };
        std::vector<LightData> lights;
        scene.world().each<Transform, Light>([&](Entity, Transform& transform, Light& l) {
            lights.push_back({l, transform.world.translation()});
        });
        if (lights.empty()) {
            Light fallback;
            fallback.direction = {0.35f, -1.0f, 0.45f};
            fallback.color = {1.0f, 0.95f, 0.9f};
            fallback.intensity = 2.0f;
            lights.push_back({fallback, {0, 10, 0}});
        }

        int shadow_light = -1;
        int sun_light = -1;
        for (int i = 0; i < static_cast<int>(lights.size()); ++i) {
            if (lights[i].light.type != LightType::Directional) continue;
            if (sun_light < 0) sun_light = i;
            if (shadow_light < 0 && lights[i].light.cast_shadow) shadow_light = i;
        }
        if (sun_light < 0) {
            // Pick the strongest non-directional light so the environment still has
            // a plausible highlight direction when a scene only contains lamps.
            float best = -1.0f;
            for (int i = 0; i < static_cast<int>(lights.size()); ++i) {
                if (lights[i].light.intensity > best) {
                    best = lights[i].light.intensity;
                    sun_light = i;
                }
            }
        }

        Vec3 shadow_dir{0, -1, 0};
        if (shadow_light >= 0) {
            shadow_dir = normalize(lights[shadow_light].light.direction);
            if (length_sq(shadow_dir) < 1e-6f) shadow_dir = {0, -1, 0};
        }
        Vec3 sun_dir{0, 1, 0};
        Vec3 sun_color{1, 1, 1};
        if (sun_light >= 0) {
            sun_dir = -normalize(lights[sun_light].light.direction);
            if (length_sq(sun_dir) < 1e-6f) sun_dir = {0, 1, 0};
            sun_color = lights[sun_light].light.color * lights[sun_light].light.intensity;
        }

        // Cap the forward light list. Sorting by intensity keeps the important
        // lights when a Blender scene has more lamps than the shader supports.
        constexpr int MAX_LIGHTS = 16;
        if (static_cast<int>(lights.size()) > MAX_LIGHTS) {
            std::sort(lights.begin(), lights.end(), [](const LightData& a, const LightData& b) {
                return a.light.intensity > b.light.intensity;
            });
            lights.resize(MAX_LIGHTS);
            if (shadow_light >= MAX_LIGHTS) shadow_light = -1;
            if (sun_light >= MAX_LIGHTS) sun_light = 0;
            if (shadow_light >= 0) shadow_dir = normalize(lights[shadow_light].light.direction);
            if (sun_light >= 0) sun_dir = -normalize(lights[sun_light].light.direction);
        }

        Mat4 light_vp = Mat4::identity();
        bool shadows_active = desc.shadows && shadow_light >= 0;

        if (shadows_active) {
            Vec3 focus = cam.position + cam.forward * 12.0f;
            focus.y = 0.0f;
            Vec3 up = std::fabs(shadow_dir.y) > 0.95f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
            Vec3 eye = focus - shadow_dir * 55.0f;
            Mat4 light_view = Mat4::look_at(eye, focus, up);
            Mat4 light_proj = Mat4::ortho(-30.0f, 30.0f, -30.0f, 30.0f, 1.0f, 130.0f);
            light_vp = light_proj * light_view;
            Frustum light_frustum = Frustum::from_matrix(light_vp);

            std::vector<DrawItem> shadow_items;
            shadow_items.reserve(items.size());
            for (const DrawItem& item : items) {
                bool blend = item.material < materials.size() && materials[item.material].desc.blend;
                if (!blend && item.cast_shadow && light_frustum.intersects(item.bounds)) shadow_items.push_back(item);
            }
            std::sort(shadow_items.begin(), shadow_items.end(), [](const DrawItem& a, const DrawItem& b) {
                return a.mesh < b.mesh;
            });

            std::vector<InstanceData> shadow_instances;
            shadow_instances.reserve(shadow_items.size());
            for (const DrawItem& item : shadow_items) {
                shadow_instances.push_back({item.model,
                                            Vec4{item.normal.column(0), 0.0f},
                                            Vec4{item.normal.column(1), 0.0f},
                                            Vec4{item.normal.column(2), 0.0f},
                                            item.tint});
            }
            upload_instances(shadow_instances);

            glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo);
            glViewport(0, 0, shadow_size, shadow_size);
            glClear(GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(2.0f, 4.0f);
            depth.use();
            depth.set_mat4("u_light_vp", light_vp);
            draw_batches(shadow_items, true);
            glDisable(GL_POLYGON_OFFSET_FILL);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, hdr_fbo);
        glViewport(0, 0, width, height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Sky
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        const bool use_environment = scene.environment_texture > 0 &&
                                     scene.environment_texture < textures.size() &&
                                     textures[scene.environment_texture].id != 0;
        const uint32_t environment_texture = use_environment ? textures[scene.environment_texture].id : textures[0].id;
        sky.use();
        sky.set_vec3("u_forward", cam.forward);
        sky.set_vec3("u_right", cam.right());
        sky.set_vec3("u_up", cam.corrected_up());
        sky.set_float("u_tan_half_fov", std::tan(cam.fov_y * 0.5f));
        sky.set_float("u_aspect", aspect);
        sky.set_vec3("u_sky_top", scene.sky_color);
        sky.set_vec3("u_sky_horizon", scene.horizon_color);
        sky.set_vec3("u_ground", scene.ground_color);
        sky.set_vec3("u_sun_dir", sun_dir);
        sky.set_vec3("u_sun_color", sun_color);
        sky.set_int("u_flat", scene.flat_sky ? 1 : 0);
        sky.set_int("u_use_environment", use_environment ? 1 : 0);
        sky.set_float("u_environment_strength", scene.environment_strength);
        sky.set_int("u_environment_tex", 3);
        glBindTextureUnit(3, environment_texture);
        glBindVertexArray(fullscreen_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Opaque PBR
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        pbr.use();
        pbr.set_mat4("u_view_proj", view_projection);
        pbr.set_vec3("u_camera_pos", cam.position);
        pbr.set_mat4("u_light_vp", light_vp);
        pbr.set_int("u_has_shadows", shadows_active ? 1 : 0);
        pbr.set_int("u_shadow_light", shadows_active ? shadow_light : -1);
        pbr.set_vec3("u_shadow_dir", shadow_dir);
        pbr.set_vec3("u_sun_dir", sun_dir);
        pbr.set_vec3("u_sun_color", sun_color);

        const int light_count = std::min(static_cast<int>(lights.size()), MAX_LIGHTS);
        std::vector<int> light_types(static_cast<size_t>(light_count));
        std::vector<Vec3> light_positions(static_cast<size_t>(light_count));
        std::vector<Vec3> light_directions(static_cast<size_t>(light_count));
        std::vector<Vec3> light_colors(static_cast<size_t>(light_count));
        std::vector<float> light_energy(static_cast<size_t>(light_count));
        std::vector<float> light_radius(static_cast<size_t>(light_count));
        std::vector<Vec2> light_size(static_cast<size_t>(light_count));
        std::vector<float> light_spot_cos(static_cast<size_t>(light_count));
        std::vector<float> light_spot_inner(static_cast<size_t>(light_count));
        std::vector<float> light_range(static_cast<size_t>(light_count));
        for (int i = 0; i < light_count; ++i) {
            const LightData& data = lights[static_cast<size_t>(i)];
            const Light& l = data.light;
            light_types[static_cast<size_t>(i)] = static_cast<int>(l.type);
            light_positions[static_cast<size_t>(i)] = data.position;
            Vec3 dir = normalize(l.direction);
            if (length_sq(dir) < 1e-6f) dir = {0, -1, 0};
            light_directions[static_cast<size_t>(i)] = dir;
            light_colors[static_cast<size_t>(i)] = l.color;
            light_energy[static_cast<size_t>(i)] = l.intensity;
            light_radius[static_cast<size_t>(i)] = std::max(l.radius, 0.001f);
            light_size[static_cast<size_t>(i)] = {std::max(l.size.x, 0.001f), std::max(l.size.y, 0.001f)};
            float half_angle = clampf(l.spot_size * 0.5f, 0.001f, HALF_PI - 0.001f);
            light_spot_cos[static_cast<size_t>(i)] = std::cos(half_angle);
            float inner_angle = half_angle * (1.0f - clampf(l.spot_blend, 0.0f, 0.99f));
            light_spot_inner[static_cast<size_t>(i)] = std::cos(std::max(inner_angle, 0.0001f));
            light_range[static_cast<size_t>(i)] = std::max(l.range, 0.0f);
        }
        pbr.set_int("u_light_count", light_count);
        pbr.set_int_array("u_light_type", light_types.data(), light_count);
        pbr.set_vec3_array("u_light_position", light_positions.data(), light_count);
        pbr.set_vec3_array("u_light_direction", light_directions.data(), light_count);
        pbr.set_vec3_array("u_light_color", light_colors.data(), light_count);
        pbr.set_float_array("u_light_energy", light_energy.data(), light_count);
        pbr.set_float_array("u_light_radius", light_radius.data(), light_count);
        pbr.set_vec2_array("u_light_size", light_size.data(), light_count);
        pbr.set_float_array("u_light_spot_cos", light_spot_cos.data(), light_count);
        pbr.set_float_array("u_light_spot_inner", light_spot_inner.data(), light_count);
        pbr.set_float_array("u_light_range", light_range.data(), light_count);

        pbr.set_vec3("u_sky_ambient", scene.sky_color * scene.ambient_intensity);
        pbr.set_vec3("u_ground_ambient", scene.ground_color * scene.ambient_intensity);
        pbr.set_vec3("u_env_top", scene.sky_color * 3.0f + scene.horizon_color * 0.25f);
        pbr.set_vec3("u_env_horizon", scene.horizon_color * 1.4f);
        pbr.set_vec3("u_env_ground", scene.ground_color * 1.5f + scene.horizon_color * 0.06f + sun_color * 0.02f);
        pbr.set_int("u_use_environment", use_environment ? 1 : 0);
        pbr.set_float("u_environment_strength", scene.environment_strength);
        pbr.set_int("u_environment_tex", 3);
        glBindTextureUnit(3, environment_texture);
        pbr.set_vec3("u_fog_color", scene.fog_color);
        pbr.set_float("u_fog_density", scene.fog_density);
        pbr.set_int("u_shadow_map", 0);
        pbr.set_int("u_albedo_tex", 1);
        if (shadow_map) glBindTextureUnit(0, shadow_map);

        auto build_instances = [](const std::vector<DrawItem>& source) {
            std::vector<InstanceData> data;
            data.reserve(source.size());
            for (const DrawItem& item : source) {
                data.push_back({item.model,
                                Vec4{item.normal.column(0), 0.0f},
                                Vec4{item.normal.column(1), 0.0f},
                                Vec4{item.normal.column(2), 0.0f},
                                item.tint});
            }
            return data;
        };

        std::vector<InstanceData> opaque_instances = build_instances(opaque_items);
        upload_instances(opaque_instances);
        draw_batches(opaque_items, false);

        if (!transparent_items.empty()) {
            std::vector<InstanceData> transparent_instances = build_instances(transparent_items);
            upload_instances(transparent_instances);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            draw_batches(transparent_items, false);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        // Post-process to the default framebuffer.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        post.use();
        glBindTextureUnit(0, hdr_color);
        post.set_int("u_scene", 0);
        post.set_float("u_exposure", scene.exposure);
        post.set_int("u_fxaa", desc.fxaa ? 1 : 0);
        post.set_int("u_view_transform", scene.view_transform);
        post.set_int("u_grade", scene.post_grade ? 1 : 0);
        post.set_vec2("u_texel", Vec2{1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height)});
        post.set_float("u_vignette", scene.vignette);
        glBindVertexArray(fullscreen_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindVertexArray(0);
        glUseProgram(0);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);

        stats.cpu_ms = milliseconds_since(start);
    }
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;

bool Renderer::init(Window& window, const RendererDesc& desc) {
    return impl_->init(window, desc);
}

void Renderer::shutdown() {
    if (impl_) impl_->destroy();
}

void Renderer::resize(int width, int height) {
    if (impl_) impl_->resize_targets(width, height);
}

void Renderer::begin_frame() {
    if (!impl_) return;
    impl_->stats = {};
    ++impl_->frame_counter;
    if (impl_->frame_counter % 30 == 0) {
        impl_->pbr.reload_if_changed();
        impl_->depth.reload_if_changed();
        impl_->sky.reload_if_changed();
        impl_->post.reload_if_changed();
    }
}

void Renderer::render(Scene& scene, const CameraState& camera) {
    if (impl_) impl_->draw_frame(scene, camera);
}

MeshHandle Renderer::create_mesh(const MeshData& data) {
    if (!impl_ || !impl_->ready || data.vertices.empty() || data.indices.empty()) return 0;

    MeshGPU mesh;
    mesh.index_count = static_cast<uint32_t>(data.indices.size());
    mesh.bounds = data.bounds;
    mesh.data = data;
    if (!mesh.bounds.valid()) mesh.bounds = AABB{{-1, -1, -1}, {1, 1, 1}};

    glCreateVertexArrays(1, &mesh.vao);
    glCreateBuffers(1, &mesh.vbo);
    glCreateBuffers(1, &mesh.ebo);
    glNamedBufferData(mesh.vbo, static_cast<GLsizeiptr>(data.vertices.size() * sizeof(Vertex)),
                      data.vertices.data(), GL_STATIC_DRAW);
    glNamedBufferData(mesh.ebo, static_cast<GLsizeiptr>(data.indices.size() * sizeof(uint32_t)),
                      data.indices.data(), GL_STATIC_DRAW);

    glVertexArrayVertexBuffer(mesh.vao, 0, mesh.vbo, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(mesh.vao, mesh.ebo);

    glEnableVertexArrayAttrib(mesh.vao, 0);
    glVertexArrayAttribFormat(mesh.vao, 0, 3, GL_FLOAT, GL_FALSE, static_cast<GLuint>(offsetof(Vertex, position)));
    glVertexArrayAttribBinding(mesh.vao, 0, 0);

    glEnableVertexArrayAttrib(mesh.vao, 1);
    glVertexArrayAttribFormat(mesh.vao, 1, 3, GL_FLOAT, GL_FALSE, static_cast<GLuint>(offsetof(Vertex, normal)));
    glVertexArrayAttribBinding(mesh.vao, 1, 0);

    glEnableVertexArrayAttrib(mesh.vao, 2);
    glVertexArrayAttribFormat(mesh.vao, 2, 2, GL_FLOAT, GL_FALSE, static_cast<GLuint>(offsetof(Vertex, uv)));
    glVertexArrayAttribBinding(mesh.vao, 2, 0);

    // Per-instance model matrix (locations 4-7), normal matrix (8-10), tint (11).
    glVertexArrayVertexBuffer(mesh.vao, 1, impl_->instance_vbo, 0, sizeof(InstanceData));
    for (int i = 0; i < 4; ++i) {
        GLuint loc = 4 + static_cast<GLuint>(i);
        glEnableVertexArrayAttrib(mesh.vao, loc);
        glVertexArrayAttribFormat(mesh.vao, loc, 4, GL_FLOAT, GL_FALSE,
                                  static_cast<GLuint>(offsetof(InstanceData, model) + i * 16));
        glVertexArrayAttribBinding(mesh.vao, loc, 1);
    }
    for (int i = 0; i < 3; ++i) {
        GLuint loc = 8 + static_cast<GLuint>(i);
        glEnableVertexArrayAttrib(mesh.vao, loc);
        glVertexArrayAttribFormat(mesh.vao, loc, 3, GL_FLOAT, GL_FALSE,
                                  static_cast<GLuint>(offsetof(InstanceData, normal0) + i * 16));
        glVertexArrayAttribBinding(mesh.vao, loc, 1);
    }
    glEnableVertexArrayAttrib(mesh.vao, 11);
    glVertexArrayAttribFormat(mesh.vao, 11, 4, GL_FLOAT, GL_FALSE, static_cast<GLuint>(offsetof(InstanceData, tint)));
    glVertexArrayAttribBinding(mesh.vao, 11, 1);
    glVertexArrayBindingDivisor(mesh.vao, 1, 1);

    impl_->meshes.push_back(mesh);
    return static_cast<MeshHandle>(impl_->meshes.size() - 1);
}

MaterialHandle Renderer::create_material(const MaterialDesc& material) {
    if (!impl_) return 0;
    impl_->materials.push_back(MaterialGPU{material});
    return static_cast<MaterialHandle>(impl_->materials.size() - 1);
}

bool Renderer::get_mesh_data(MeshHandle handle, MeshData& out) const {
    if (!impl_ || handle == 0 || handle >= impl_->meshes.size()) return false;
    out = impl_->meshes[handle].data;
    return true;
}

bool Renderer::get_material(MaterialHandle handle, MaterialDesc& out) const {
    if (!impl_ || handle >= impl_->materials.size()) return false;
    out = impl_->materials[handle].desc;
    return true;
}

size_t Renderer::mesh_count() const { return impl_ ? impl_->meshes.size() : 0; }
size_t Renderer::material_count() const { return impl_ ? impl_->materials.size() : 0; }

void Renderer::set_material(MaterialHandle handle, const MaterialDesc& material) {
    if (!impl_ || handle == 0 || handle >= impl_->materials.size()) return;
    impl_->materials[handle].desc = material;
}

TextureHandle Renderer::load_texture(const std::string& path, bool srgb) {
    if (!impl_) return 0;
    TextureGPU texture = bge::load_texture(path, srgb);
    if (!texture.id) return 0;
    texture.path = path;
    impl_->textures.push_back(texture);
    return static_cast<TextureHandle>(impl_->textures.size() - 1);
}

TextureHandle Renderer::load_environment_texture(const std::string& path) {
    if (!impl_) return 0;
    TextureGPU texture = bge::load_texture_equirect(path);
    if (!texture.id) return 0;
    texture.path = path;
    impl_->textures.push_back(texture);
    return static_cast<TextureHandle>(impl_->textures.size() - 1);
}

bool Renderer::get_texture_path(TextureHandle handle, std::string& out) const {
    if (!impl_ || handle >= impl_->textures.size()) return false;
    out = impl_->textures[handle].path;
    return !out.empty();
}

TextureHandle Renderer::create_checker_texture(int size, const Vec3& a, const Vec3& b, int cells) {
    if (!impl_) return 0;
    TextureGPU texture = make_checker_texture(size, a, b, cells);
    if (!texture.id) return 0;
    impl_->textures.push_back(texture);
    return static_cast<TextureHandle>(impl_->textures.size() - 1);
}

const RenderStats& Renderer::stats() const { return impl_->stats; }
int Renderer::width() const { return impl_ ? impl_->width : 0; }
int Renderer::height() const { return impl_ ? impl_->height : 0; }
bool Renderer::initialized() const { return impl_ && impl_->ready; }

} // namespace bge
