#pragma once

#include <string>
#include <string_view>

#include "bge/ecs/components.h"
#include "bge/ecs/world.h"
#include "bge/math/math.h"

namespace bge {

struct CameraState {
    Vec3 position{0.0f, 4.0f, 12.0f};
    Vec3 forward{0.0f, 0.0f, -1.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float fov_y = radians(60.0f);
    float near_plane = 0.1f;
    float far_plane = 800.0f;

    // Blender-compatible projection controls.
    bool orthographic = false;
    float ortho_width = 6.0f;
    float ortho_height = 6.0f;
    float shift_x = 0.0f;
    float shift_y = 0.0f;

    Vec3 right() const { return normalize(cross(forward, up)); }
    Vec3 corrected_up() const { return cross(right(), forward); }

    Mat4 view() const { return Mat4::look_at(position, position + forward, up); }
    Mat4 projection(float aspect) const;
    Mat4 view_projection(float aspect) const { return projection(aspect) * view(); }
};

class Scene {
public:
    Scene();

    World& world() { return world_; }
    const World& world() const { return world_; }

    Entity create_entity(std::string name, Entity parent = {});
    Entity create_mesh_entity(std::string name, uint32_t mesh, uint32_t material, Entity parent = {});
    Entity find(std::string_view name);

    // Removes every entity, keeping the camera and environment settings. Used
    // by the live link, which reloads a scene in place while the game runs.
    void clear();

    void set_parent(Entity child, Entity parent);
    void update_transforms();

    // Fixed-step gameplay/animation update.
    void fixed_update(float dt);

    // The clock that drives the sampled animations.
    float animation_time = 0.0f;

    // Copy the primary Camera component into the render camera state.
    void apply_camera_component();

    CameraState camera;

    Entity camera_entity{};
    Entity sun_entity{};

    Vec3 sky_color{0.42f, 0.62f, 0.92f};
    Vec3 horizon_color{0.82f, 0.88f, 1.0f};
    Vec3 ground_color{0.09f, 0.10f, 0.12f};
    Vec3 fog_color{0.55f, 0.68f, 0.82f};
    float fog_density = 0.008f;
    float exposure = 1.0f;
    float ambient_intensity = 0.35f;
    // Post-processing controls. Imported Blender scenes disable the stylised
    // vignette/grade so the image can be compared directly with Blender.
    int view_transform = 0; // 0 = ACES/AgX approximation, 1 = Standard (linear -> sRGB)
    float vignette = 0.22f;
    bool post_grade = true;

    // Imported-scene/environment controls. A flat sky matches Blender's Background
    // shader; gradient skies remain available for the procedural demo and tools.
    bool flat_sky = false;
    bool environment_is_hdr = false;
    uint32_t environment_texture = 0; // Renderer texture handle, 0 = none
    float environment_strength = 1.0f;

    std::string name;
    std::string source_path;

private:
    World world_;
};

void update_world_transform(World& world, Entity entity, const Mat4& parent_world);

} // namespace bge
