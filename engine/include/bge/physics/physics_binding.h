#pragma once

namespace bge {

class Scene;
class PhysicsWorld;

// Creates a physics body for every entity that carries a RigidBody component.
void create_physics_bodies(Scene& scene, PhysicsWorld& physics);

// Copies the simulated transforms back into the scene entities.
void sync_physics_transforms(Scene& scene, PhysicsWorld& physics);

} // namespace bge
