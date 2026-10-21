#include "bge/physics/physics_binding.h"

#include "bge/ecs/components.h"
#include "bge/physics/physics_world.h"
#include "bge/scene/scene.h"

namespace bge {

void create_physics_bodies(Scene& scene, PhysicsWorld& physics) {
    scene.world().each<Transform, RigidBody>([&](Entity, Transform& transform, RigidBody& rigid) {
        PhysicsBodyDesc desc;
        desc.position = transform.position;
        desc.rotation = transform.rotation;
        desc.shape = rigid.sphere ? ShapeType::Sphere : ShapeType::Box;
        desc.half_extents = rigid.half_extents;
        desc.radius = rigid.radius;
        desc.mass = rigid.mass;
        desc.restitution = rigid.restitution;
        desc.friction = rigid.friction;
        desc.is_static = rigid.is_static;
        rigid.body = physics.create_body(desc);
    });
}

void sync_physics_transforms(Scene& scene, PhysicsWorld& physics) {
    scene.world().each<Transform, RigidBody>([&](Entity, Transform& transform, RigidBody& rigid) {
        if (!physics.valid(rigid.body)) return;
        transform.position = physics.position(rigid.body);
        transform.rotation = physics.rotation(rigid.body);
    });
}

} // namespace bge
