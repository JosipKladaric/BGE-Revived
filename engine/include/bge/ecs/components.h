#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "bge/ecs/world.h"
#include "bge/math/math.h"

namespace bge {

struct Transform {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Quat rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};

    // Derived each frame by Scene::update_transforms().
    Mat4 local{Mat4::identity()};
    Mat4 world{Mat4::identity()};
    Mat4 normal{Mat4::identity()};
};

struct Name {
    std::string value;
};

// A user defined property, the runtime equivalent of a Blender custom property.
struct Property {
    enum class Kind { Number, Flag, Text };
    Kind kind = Kind::Number;
    double number = 0.0;
    std::string text;
};

struct Properties {
    std::unordered_map<std::string, Property> values;
};

// A script attached to an object, the equivalent of a classic BGE controller.
struct Controller {
    std::string script;   // Python source
    std::string name;
    bool enabled = true;
};

struct Parent {
    Entity value{};
};

struct Children {
    std::vector<Entity> value;
};

struct MeshRenderer {
    uint32_t mesh = 0;
    uint32_t material = 0;
    bool cast_shadow = true;
    bool visible = true;
    Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
};

struct Camera {
    float fov_y = radians(60.0f);
    float near_plane = 0.1f;
    float far_plane = 800.0f;
    bool primary = true;

    // Blender-compatible camera controls.
    bool orthographic = false;
    float ortho_width = 6.0f;
    float ortho_height = 6.0f;
    float shift_x = 0.0f;
    float shift_y = 0.0f;
};

enum class LightType : uint32_t {
    Directional = 0, // Blender SUN: intensity is irradiance in W/m²
    Point = 1,       // Blender POINT: intensity is radiant power in W
    Spot = 2,        // Blender SPOT: power in W
    Area = 3,        // Blender AREA: power in W, approximated by the renderer
};

struct Light {
    LightType type = LightType::Directional;
    Vec3 direction{-0.35f, -1.0f, -0.45f}; // direction the light travels
    Vec3 color{1.0f, 0.94f, 0.84f};
    float intensity = 3.0f;
    float radius = 0.1f;    // point/spot soft radius (metres)
    Vec2 size{1.0f, 1.0f};  // area light dimensions (metres)
    float spot_size = radians(45.0f);
    float spot_blend = 0.15f;
    float range = 0.0f;     // 0 = no cutoff
    bool cast_shadow = true;
};

// Kept as an alias so simple demos can keep using the old descriptive name.
using DirectionalLight = Light;

// Demo/gameplay helper: rotates an entity around a local axis at a constant rate.
struct Rotator {
    Vec3 axis{0.0f, 1.0f, 0.0f};
    float speed = 0.8f;
};

struct Bob {
    float amplitude = 0.2f;
    float frequency = 0.6f;
    float phase = 0.0f;
    float base_y = 0.0f;
};

// A rigid body driven by the physics world. The transform of the entity is
// overwritten with the simulated transform every step.
// A sampled transform animation, exported from Blender. The matrices are world
// transforms sampled at regular intervals; the runtime interpolates them.
struct Animation {
    float fps = 30.0f;
    float start_time = 0.0f;
    std::vector<float> times;
    std::vector<std::array<float, 16>> matrices;
};

// A rigid body driven by the physics world. The transform of the entity is
// overwritten with the simulated transform every step.
struct RigidBody {
    uint32_t body = 0;   // handle inside the physics world
    bool is_static = false;
    bool sphere = false;
    float mass = 1.0f;
    float restitution = 0.15f;
    float friction = 0.5f;
    float radius = 0.5f;
    Vec3 half_extents{0.5f, 0.5f, 0.5f};
};

} // namespace bge
