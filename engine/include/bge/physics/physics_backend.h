#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "bge/math/math.h"

namespace bge {

// The description of a rigid body, shared by every physics backend.
struct PhysicsBodyDesc {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Quat rotation{};
    bool sphere = false;
    Vec3 half_extents{0.5f, 0.5f, 0.5f};  // box
    float radius = 0.5f;                 // sphere
    float mass = 1.0f;
    float restitution = 0.15f;
    float friction = 0.5f;
    bool is_static = false;
};

// The interface every physics backend implements.
class PhysicsBackend {
public:
    virtual ~PhysicsBackend() = default;

    virtual uint32_t create_body(const PhysicsBodyDesc& desc) = 0;
    virtual void destroy_body(uint32_t body) = 0;
    virtual bool valid(uint32_t body) const = 0;

    virtual void set_transform(uint32_t body, const Vec3& position, const Quat& rotation) = 0;
    virtual void set_velocity(uint32_t body, const Vec3& velocity) = 0;
    virtual void apply_impulse(uint32_t body, const Vec3& impulse) = 0;

    virtual Vec3 position(uint32_t body) const = 0;
    virtual Quat rotation(uint32_t body) const = 0;
    virtual Vec3 velocity(uint32_t body) const = 0;

    virtual void step(float delta_seconds) = 0;
    virtual size_t body_count() const = 0;
    virtual const char* name() const = 0;
};

// Creates the best available backend: Jolt when it was compiled in, otherwise
// the small built-in solver.
std::unique_ptr<PhysicsBackend> create_physics_backend(bool prefer_jolt = true);

// True when the engine was built with the Jolt backend.
bool jolt_available();

} // namespace bge
