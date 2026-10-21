#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bge/math/math.h"

namespace bge {

// Shapes the built-in solver understands. The interface is deliberately small
// so a third party solver (Jolt) can replace the implementation later.
enum class ShapeType : uint8_t {
    Sphere = 0,
    Box = 1,
};

struct PhysicsBodyDesc {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Quat rotation{};
    ShapeType shape = ShapeType::Box;
    Vec3 half_extents{0.5f, 0.5f, 0.5f};  // box
    float radius = 0.5f;                 // sphere
    float mass = 1.0f;
    float restitution = 0.15f;
    float friction = 0.5f;
    bool is_static = false;
    bool sleeping = false;
};

// A small deterministic rigid body solver: semi implicit Euler integration,
// sphere/box collision against other bodies and a static ground plane.
//
// It is not a replacement for Jolt, but it gives the runtime real collision
// behaviour, and the public interface is what a Jolt backend would implement.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    void set_gravity(const Vec3& gravity) { gravity_ = gravity; }
    const Vec3& gravity() const { return gravity_; }

    // The static ground plane. Bodies rest on it.
    void set_ground(bool enabled, float height = 0.0f) {
        ground_enabled_ = enabled;
        ground_height_ = height;
    }

    uint32_t create_body(const PhysicsBodyDesc& desc);
    void destroy_body(uint32_t body);
    bool valid(uint32_t body) const { return body < bodies_.size() && !bodies_[body].removed; }

    void set_transform(uint32_t body, const Vec3& position, const Quat& rotation);
    void set_velocity(uint32_t body, const Vec3& velocity);
    void apply_impulse(uint32_t body, const Vec3& impulse);
    void apply_force(uint32_t body, const Vec3& force);

    Vec3 position(uint32_t body) const;
    Quat rotation(uint32_t body) const;
    Vec3 velocity(uint32_t body) const;
    bool is_sleeping(uint32_t body) const;

    // Advances the simulation by a fixed step.
    void step(float delta_seconds);

    size_t body_count() const { return bodies_.size(); }
    uint64_t step_count() const { return steps_; }

private:
    struct Body {
        PhysicsBodyDesc desc;
        Vec3 position{0.0f, 0.0f, 0.0f};
        Quat rotation{};
        Vec3 velocity{0.0f, 0.0f, 0.0f};
        Vec3 angular_velocity{0.0f, 0.0f, 0.0f};
        Vec3 force{0.0f, 0.0f, 0.0f};
        bool removed = false;
        bool sleeping = false;
        float sleep_timer = 0.0f;
    };

    struct Contact {
        uint32_t a = 0;
        uint32_t b = 0;   // invalid when the contact is with the ground
        Vec3 normal{0.0f, 1.0f, 0.0f};
        float penetration = 0.0f;
        Vec3 point{0.0f, 0.0f, 0.0f};
    };

    Vec3 support(const Body& body, const Vec3& direction) const;
    void integrate(Body& body, float dt);
    void collect_contacts(std::vector<Contact>& out) const;
    void resolve_contact(Contact& contact, float dt);
    void apply_ground(Body& body, float dt) const;

    std::vector<Body> bodies_;
    Vec3 gravity_{0.0f, -9.81f, 0.0f};
    bool ground_enabled_ = true;
    float ground_height_ = 0.0f;
    uint64_t steps_ = 0;
};

} // namespace bge
