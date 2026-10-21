#include "bge/physics/physics_world.h"

#include "bge/core/log.h"

#include <algorithm>
#include <cmath>

namespace bge {
namespace {

constexpr float kSleepLinear = 0.05f;
constexpr float kSleepAngular = 0.05f;
constexpr float kSleepTime = 0.6f;

float randomish(uint32_t index) {
    // A cheap deterministic value used to break symmetry between resting boxes.
    uint32_t x = index * 1664525u + 1013904223u;
    x ^= x >> 16;
    return (static_cast<float>(x & 0xFFFF) / 65535.0f) * 2.0f - 1.0f;
}

} // namespace

PhysicsWorld::PhysicsWorld() = default;
PhysicsWorld::~PhysicsWorld() = default;

uint32_t PhysicsWorld::create_body(const PhysicsBodyDesc& desc) {
    Body body;
    body.desc = desc;
    body.position = desc.position;
    body.rotation = desc.rotation;
    bodies_.push_back(body);
    return static_cast<uint32_t>(bodies_.size() - 1);
}

void PhysicsWorld::destroy_body(uint32_t body) {
    if (!valid(body)) return;
    bodies_[body].removed = true;
}

void PhysicsWorld::set_transform(uint32_t body, const Vec3& position, const Quat& rotation) {
    if (!valid(body)) return;
    bodies_[body].position = position;
    bodies_[body].rotation = rotation.normalized();
    bodies_[body].velocity = Vec3{0, 0, 0};
    bodies_[body].sleep_timer = 0.0f;
}

void PhysicsWorld::set_velocity(uint32_t body, const Vec3& velocity) {
    if (!valid(body)) return;
    bodies_[body].velocity = velocity;
    bodies_[body].sleep_timer = 0.0f;
}

void PhysicsWorld::apply_impulse(uint32_t body, const Vec3& impulse) {
    if (!valid(body)) return;
    Body& b = bodies_[body];
    if (b.desc.is_static || b.desc.mass <= 0.0f) return;
    b.velocity = b.velocity + impulse / b.desc.mass;
    b.sleep_timer = 0.0f;
}

void PhysicsWorld::apply_force(uint32_t body, const Vec3& force) {
    if (!valid(body)) return;
    bodies_[body].force = bodies_[body].force + force;
    bodies_[body].sleep_timer = 0.0f;
}

Vec3 PhysicsWorld::position(uint32_t body) const {
    if (!valid(body)) return Vec3{0, 0, 0};
    return bodies_[body].position;
}

Quat PhysicsWorld::rotation(uint32_t body) const {
    if (!valid(body)) return Quat::identity();
    return bodies_[body].rotation;
}

Vec3 PhysicsWorld::velocity(uint32_t body) const {
    if (!valid(body)) return Vec3{0, 0, 0};
    return bodies_[body].velocity;
}

bool PhysicsWorld::is_sleeping(uint32_t body) const {
    if (!valid(body)) return false;
    return bodies_[body].sleeping;
}

Vec3 PhysicsWorld::support(const Body& body, const Vec3& direction) const {
    if (body.desc.shape == ShapeType::Sphere) {
        Vec3 d = normalize(direction);
        return body.position + d * body.desc.radius;
    }
    // Box: the corner furthest along the direction, in world space.
    Quat q = body.rotation;
    Vec3 local = {
        direction.x >= 0.0f ? body.desc.half_extents.x : -body.desc.half_extents.x,
        direction.y >= 0.0f ? body.desc.half_extents.y : -body.desc.half_extents.y,
        direction.z >= 0.0f ? body.desc.half_extents.z : -body.desc.half_extents.z,
    };
    return body.position + q.rotate(local);
}

void PhysicsWorld::integrate(Body& body, float dt) {
    if (body.desc.is_static) {
        body.force = Vec3{0, 0, 0};
        return;
    }
    const float inverse_mass = body.desc.mass > 0.0f ? 1.0f / body.desc.mass : 0.0f;
    Vec3 acceleration = gravity_ + body.force * inverse_mass;
    body.velocity = body.velocity + acceleration * dt;
    body.position = body.position + body.velocity * dt;
    body.force = Vec3{0, 0, 0};

    if (length_sq(body.velocity) < kSleepLinear * kSleepLinear &&
        length_sq(body.angular_velocity) < kSleepAngular * kSleepAngular) {
        body.sleep_timer += dt;
        if (body.sleep_timer > kSleepTime) {
            body.sleeping = true;
            body.velocity = Vec3{0, 0, 0};
        }
    } else {
        body.sleep_timer = 0.0f;
    }
}

void PhysicsWorld::apply_ground(Body& body, float dt) const {
    if (!ground_enabled_ || body.desc.is_static) return;

    float lowest = 0.0f;
    if (body.desc.shape == ShapeType::Sphere) {
        lowest = body.position.y - body.desc.radius;
    } else {
        // The lowest corner of the oriented box.
        float lowest_local = 0.0f;
        for (int i = 0; i < 8; ++i) {
            Vec3 corner{(i & 1) ? body.desc.half_extents.x : -body.desc.half_extents.x,
                        (i & 2) ? body.desc.half_extents.y : -body.desc.half_extents.y,
                        (i & 4) ? body.desc.half_extents.z : -body.desc.half_extents.z};
            Vec3 world = body.position + body.rotation.rotate(corner);
            lowest_local = std::min(lowest_local, world.y);
        }
        lowest = lowest_local;
    }

    const float penetration = ground_height_ - lowest;
    if (penetration <= 0.0f) return;

    // Push the body out and cancel the downward velocity.
    body.position.y += penetration;
    if (body.velocity.y < 0.0f) {
        body.velocity.y = -body.velocity.y * body.desc.restitution;
        if (std::fabs(body.velocity.y) < 0.2f) body.velocity.y = 0.0f;
    }
    // Ground friction.
    body.velocity.x *= (1.0f - std::min(1.0f, body.desc.friction * dt * 12.0f));
    body.velocity.z *= (1.0f - std::min(1.0f, body.desc.friction * dt * 12.0f));
}

void PhysicsWorld::collect_contacts(std::vector<Contact>& out) const {
    out.clear();
    for (size_t i = 0; i < bodies_.size(); ++i) {
        const Body& a = bodies_[i];
        if (a.removed || a.sleeping) continue;
        for (size_t j = i + 1; j < bodies_.size(); ++j) {
            const Body& b = bodies_[j];
            if (b.removed || b.sleeping) continue;
            if (a.desc.is_static && b.desc.is_static) continue;

            Vec3 delta = b.position - a.position;
            const float distance = length(delta);
            const float radius_a = (a.desc.shape == ShapeType::Sphere) ? a.desc.radius
                                                                        : length(a.desc.half_extents);
            const float radius_b = (b.desc.shape == ShapeType::Sphere) ? b.desc.radius
                                                                        : length(b.desc.half_extents);

            if (distance > radius_a + radius_b) continue;

            Contact contact;
            contact.a = static_cast<uint32_t>(i);
            contact.b = static_cast<uint32_t>(j);
            contact.normal = distance > 1e-5f ? delta / distance : Vec3{0, 1, 0};

            if (a.desc.shape == ShapeType::Sphere && b.desc.shape == ShapeType::Sphere) {
                contact.penetration = (a.desc.radius + b.desc.radius) - distance;
                contact.point = a.position + contact.normal * a.desc.radius;
            } else if (a.desc.shape == ShapeType::Box && b.desc.shape == ShapeType::Box) {
                // Axis aligned overlap of the rotated boxes, approximated with the
                // world aligned extents of the rotated box.
                Vec3 extent_a{0, 0, 0};
                Vec3 extent_b{0, 0, 0};
                for (int axis = 0; axis < 3; ++axis) {
                    float sum_a = 0.0f;
                    float sum_b = 0.0f;
                    for (int k = 0; k < 3; ++k) {
                        float c = std::fabs(a.rotation.rotate(Vec3{k == 0 ? 1.0f : 0.0f,
                                                                 k == 1 ? 1.0f : 0.0f,
                                                                 k == 2 ? 1.0f : 0.0f})[axis]);
                        sum_a += c * a.desc.half_extents[k];
                        sum_b += c * b.desc.half_extents[k];
                    }
                    extent_a[axis] = sum_a;
                    extent_b[axis] = sum_b;
                }
                Vec3 overlap{extent_a.x + extent_b.x - std::fabs(delta.x),
                             extent_a.y + extent_b.y - std::fabs(delta.y),
                             extent_a.z + extent_b.z - std::fabs(delta.z)};
                if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f) continue;
                if (overlap.y <= overlap.x && overlap.y <= overlap.z) {
                    contact.normal = Vec3{0, delta.y >= 0.0f ? 1.0f : -1.0f, 0};
                    contact.penetration = overlap.y;
                } else if (overlap.x <= overlap.z) {
                    contact.normal = Vec3{delta.x >= 0.0f ? 1.0f : -1.0f, 0, 0};
                    contact.penetration = overlap.x;
                } else {
                    contact.normal = Vec3{0, 0, delta.z >= 0.0f ? 1.0f : -1.0f};
                    contact.penetration = overlap.z;
                }
                contact.point = (a.position + b.position) * 0.5f;
            } else {
                // Sphere against box: use the closest point on the box.
                const Body& sphere = (a.desc.shape == ShapeType::Sphere) ? a : b;
                const Body& box = (a.desc.shape == ShapeType::Sphere) ? b : a;
                Vec3 local = box.rotation.rotate(box.position - sphere.position);
                Vec3 closest{clampf(local.x, -box.desc.half_extents.x, box.desc.half_extents.x),
                             clampf(local.y, -box.desc.half_extents.y, box.desc.half_extents.y),
                             clampf(local.z, -box.desc.half_extents.z, box.desc.half_extents.z)};
                Vec3 difference = local - closest;
                float gap = length(difference);
                if (gap > sphere.desc.radius) continue;
                Vec3 normal_local = gap > 1e-5f ? difference / gap : Vec3{0, 1, 0};
                contact.normal = box.rotation.rotate(normal_local);
                if (a.desc.shape != ShapeType::Sphere) contact.normal = -contact.normal;
                contact.penetration = sphere.desc.radius - gap;
                contact.point = sphere.position - normal_local * sphere.desc.radius;
            }

            if (contact.penetration > 0.0f) out.push_back(contact);
        }
    }
}

void PhysicsWorld::resolve_contact(Contact& contact, float dt) {
    Body& a = bodies_[contact.a];
    Body& b = bodies_[contact.b];

    const float inv_a = (a.desc.is_static || a.desc.mass <= 0.0f) ? 0.0f : 1.0f / a.desc.mass;
    const float inv_b = (b.desc.is_static || b.desc.mass <= 0.0f) ? 0.0f : 1.0f / b.desc.mass;
    const float inv_sum = inv_a + inv_b;
    if (inv_sum <= 0.0f) return;

    Vec3 relative = b.velocity - a.velocity;
    float separating = dot(relative, contact.normal);
    if (separating > 0.0f) return;  // already moving apart

    const float restitution = std::min(a.desc.restitution, b.desc.restitution);
    float impulse = -(1.0f + restitution) * separating / inv_sum;

    // Positional correction keeps resting stacks from sinking into each other.
    const float correction = std::max(0.0f, contact.penetration - 0.001f) * 0.4f / inv_sum;

    Vec3 impulse_vector = contact.normal * impulse;
    a.velocity = a.velocity - impulse_vector * inv_a;
    b.velocity = b.velocity + impulse_vector * inv_b;
    a.position = a.position - contact.normal * correction * inv_a;
    b.position = b.position + contact.normal * correction * inv_b;

    // Friction along the tangential component of the relative velocity.
    Vec3 tangent = relative - contact.normal * separating;
    float tangent_length = length(tangent);
    if (tangent_length > 1e-4f) {
        tangent = tangent / tangent_length;
        float friction = std::sqrt(a.desc.friction * b.desc.friction);
        float friction_impulse = -tangent_length / inv_sum;
        friction_impulse = clampf(friction_impulse, -impulse * friction, impulse * friction);
        Vec3 friction_vector = tangent * friction_impulse;
        a.velocity = a.velocity - friction_vector * inv_a;
        b.velocity = b.velocity + friction_vector * inv_b;
    }

    a.sleep_timer = 0.0f;
    b.sleep_timer = 0.0f;
    a.sleeping = false;
    b.sleeping = false;
    (void)dt;
}

void PhysicsWorld::step(float delta_seconds) {
    if (delta_seconds <= 0.0f) return;
    ++steps_;

    for (Body& body : bodies_) {
        if (body.removed) continue;
        if (body.sleeping) continue;
        integrate(body, delta_seconds);
    }

    std::vector<Contact> contacts;
    collect_contacts(contacts);
    for (Contact& contact : contacts) resolve_contact(contact, delta_seconds);

    for (Body& body : bodies_) {
        if (body.removed || body.desc.is_static) continue;
        apply_ground(body, delta_seconds);
    }
}

} // namespace bge
