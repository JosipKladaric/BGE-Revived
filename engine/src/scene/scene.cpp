#include "bge/scene/scene.h"

#include "bge/core/log.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace bge {

Mat4 CameraState::projection(float aspect) const {
    if (aspect <= 1e-6f) aspect = 1.0f;
    if (orthographic) {
        float half_w = std::max(ortho_width, 1e-4f) * 0.5f;
        float half_h = std::max(ortho_height, 1e-4f) * 0.5f;
        float cx = shift_x * ortho_width;
        float cy = shift_y * ortho_height;
        return Mat4::ortho(-half_w + cx, half_w + cx, -half_h + cy, half_h + cy,
                           near_plane, far_plane);
    }

    // Off-centre perspective, equivalent to Blender's camera shift.
    Mat4 r = Mat4::zero();
    const float f = 1.0f / std::tan(fov_y * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[8] = 2.0f * shift_x;
    r.m[9] = 2.0f * shift_y;
    r.m[10] = (far_plane + near_plane) / (near_plane - far_plane);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * far_plane * near_plane) / (near_plane - far_plane);
    return r;
}

Scene::Scene() = default;

Entity Scene::create_entity(std::string name, Entity parent) {
    Entity e = world_.create();
    world_.add(e, Transform{});
    world_.add(e, Name{std::move(name)});
    if (parent) set_parent(e, parent);
    return e;
}

Entity Scene::create_mesh_entity(std::string name, uint32_t mesh, uint32_t material, Entity parent) {
    Entity e = create_entity(std::move(name), parent);
    world_.add(e, MeshRenderer{mesh, material, true, true, Vec4{1, 1, 1, 1}});
    return e;
}

void Scene::clear() {
    // Collect first: destroying while iterating the pools is not safe.
    std::vector<Entity> entities;
    world_.each<Name>([&](Entity e, Name&) { entities.push_back(e); });
    for (Entity e : entities) world_.destroy(e);
    camera_entity = Entity{};
    sun_entity = Entity{};
}

Entity Scene::find(std::string_view name) {
    Entity result{};
    world_.each<Name>([&](Entity e, Name& n) {
        if (!result.valid() && n.value == name) result = e;
    });
    return result;
}

void Scene::set_parent(Entity child, Entity parent) {
    if (!world_.valid(child) || child == parent) return;
    if (parent && !world_.valid(parent)) return;

    // Reject cycles by walking up from the prospective parent.
    Entity cursor = parent;
    int guard = 0;
    while (cursor && guard++ < 100000) {
        if (cursor == child) {
            BGE_WARN("set_parent: rejecting cycle for entity %u", child.index);
            return;
        }
        Parent* p = world_.get<Parent>(cursor);
        cursor = p ? p->value : Entity{};
    }

    if (Parent* old = world_.get<Parent>(child)) {
        if (Children* siblings = world_.get<Children>(old->value)) {
            auto& list = siblings->value;
            list.erase(std::remove(list.begin(), list.end(), child), list.end());
        }
    }

    if (!parent) {
        world_.remove<Parent>(child);
        return;
    }

    if (Parent* existing = world_.get<Parent>(child)) existing->value = parent;
    else world_.add(child, Parent{parent});

    if (!world_.has<Children>(parent)) world_.add(parent, Children{});
    Children* children = world_.get<Children>(parent);
    if (children && std::find(children->value.begin(), children->value.end(), child) == children->value.end()) {
        children->value.push_back(child);
    }
}

void update_world_transform(World& world, Entity entity, const Mat4& parent_world) {
    Transform* t = world.get<Transform>(entity);
    if (!t) return;
    t->local = Mat4::trs(t->position, t->rotation, t->scale);
    t->world = parent_world * t->local;
    t->normal = t->world.normal_matrix();

    Children* children = world.get<Children>(entity);
    if (!children) return;
    for (Entity child : children->value) {
        if (world.valid(child)) update_world_transform(world, child, t->world);
    }
}

void Scene::update_transforms() {
    world_.each<Transform>([&](Entity e, Transform& t) {
        if (!world_.has<Parent>(e)) update_world_transform(world_, e, Mat4::identity());
    });
}

void Scene::fixed_update(float dt) {
    world_.each<Transform, Rotator>([&](Entity, Transform& t, Rotator& r) {
        Quat delta = Quat::from_axis_angle(r.axis, r.speed * dt);
        t.rotation = (delta * t.rotation).normalized();
    });

    world_.each<Transform, Bob>([&](Entity, Transform& t, Bob& b) {
        b.phase += b.frequency * dt;
        t.position.y = b.base_y + std::sin(b.phase) * b.amplitude;
    });
}

void Scene::apply_camera_component() {
    if (!camera_entity) return;
    Transform* t = world_.get<Transform>(camera_entity);
    Camera* c = world_.get<Camera>(camera_entity);
    if (!t || !c) return;
    camera.position = t->position;
    camera.forward = normalize(t->rotation.rotate({0.0f, 0.0f, -1.0f}));
    camera.up = normalize(t->rotation.rotate({0.0f, 1.0f, 0.0f}));
    camera.fov_y = c->fov_y;
    camera.near_plane = c->near_plane;
    camera.far_plane = c->far_plane;
    camera.orthographic = c->orthographic;
    camera.ortho_width = c->ortho_width;
    camera.ortho_height = c->ortho_height;
    camera.shift_x = c->shift_x;
    camera.shift_y = c->shift_y;
}

} // namespace bge
