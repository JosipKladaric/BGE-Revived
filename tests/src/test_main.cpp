// BGE Revived - foundation tests. No framework, just deterministic checks.
#include "bge/core/job_system.h"
#include "bge/core/time.h"
#include "bge/ecs/components.h"
#include "bge/ecs/world.h"
#include "bge/math/math.h"
#include "bge/net/shared_frame.h"
#include "bge/physics/physics_backend.h"
#include "bge/physics/physics_world.h"
#include "bge/scene/scene.h"
#include "bge/script/python_host.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace bge;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
    ++g_checks; \
    if (!(cond)) { \
        ++g_failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
static bool near(const Vec3& a, const Vec3& b, float eps = 1e-4f) { return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps); }

static void test_math() {
    Mat4 t = Mat4::translation({2, 3, 4});
    Vec3 p = t.transform_point({1, 0, 0});
    CHECK(near(p, {3, 3, 4}));

    Quat q = Quat::from_axis_angle({0, 1, 0}, HALF_PI);
    Vec3 r = q.rotate({1, 0, 0});
    CHECK(near(r, {0, 0, -1}, 1e-3f));

    Mat4 m = Mat4::trs({1, 2, 3}, q, {1, 1, 1});
    Vec3 rp = m.transform_point({1, 0, 0});
    CHECK(near(rp, {1, 2, 2}, 1e-3f));

    Mat4 inv = m.inverse();
    CHECK(near(inv.transform_point(rp), {1, 0, 0}, 1e-3f));

    Mat4 view = Mat4::look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    CHECK(near(view.transform_point({0, 0, 0}), {0, 0, -5}));

    AABB box{{-1, -1, -1}, {1, 1, 1}};
    Frustum f = Frustum::from_matrix(Mat4::perspective(radians(60), 1.0f, 0.1f, 100.0f) * view);
    CHECK(f.intersects(box));
    AABB far_box{{100, 100, 100}, {101, 101, 101}};
    CHECK(!f.intersects(far_box));

    CHECK(near(length(normalize(Vec3{3, 4, 0})), 1.0f));
    CHECK(near(degrees(radians(90.0f)), 90.0f));
}

struct Position { float x = 0, y = 0; };
struct Velocity { float x = 0, y = 0; };
struct Tag { int value = 0; };

static void test_ecs() {
    World w;
    Entity a = w.create();
    Entity b = w.create();
    CHECK(a.valid() && b.valid());
    CHECK(a != b);
    CHECK(w.alive_count() == 2);

    w.add(a, Position{1, 2});
    w.add(a, Velocity{3, 4});
    w.add(b, Position{5, 6});
    CHECK(w.has<Position>(a));
    CHECK(w.has<Velocity>(a));
    CHECK(!w.has<Velocity>(b));

    int visited = 0;
    w.each<Position, Velocity>([&](Entity, Position& p, Velocity& v) {
        p.x += v.x;
        p.y += v.y;
        ++visited;
    });
    CHECK(visited == 1);
    CHECK(near(w.get<Position>(a)->x, 4.0f));

    Entity c = w.create();
    w.add(c, Position{9, 9});
    w.destroy(a);
    CHECK(!w.valid(a));
    CHECK(w.alive_count() == 2);
    CHECK(w.get<Position>(a) == nullptr);

    Entity reused = w.create();
    CHECK(reused.index == a.index);
    CHECK(reused.generation != a.generation);
    CHECK(w.valid(reused));

    visited = 0;
    w.each<Position>([&](Entity, Position&) { ++visited; });
    CHECK(visited == 2);

    w.clear();
    CHECK(w.alive_count() == 0);
}

static void test_jobs() {
    JobSystem jobs(4);
    std::atomic<int> sum{0};
    jobs.parallel_for(0, 10000, 64, [&](uint32_t begin, uint32_t end) {
        int local = 0;
        for (uint32_t i = begin; i < end; ++i) local += static_cast<int>(i);
        sum.fetch_add(local, std::memory_order_relaxed);
    });
    CHECK(sum.load() == 10000 * 9999 / 2);

    std::atomic<int> counter{0};
    for (int i = 0; i < 100; ++i) jobs.submit([&] { counter.fetch_add(1, std::memory_order_relaxed); });
    jobs.wait_idle();
    CHECK(counter.load() == 100);
}

static void test_time() {
    FixedTimestep fixed(120.0);
    int steps = 0;
    int produced = fixed.consume(1.0 / 60.0, [&](double) { ++steps; });
    CHECK(produced == 2);
    CHECK(steps == 2);
    CHECK(near(static_cast<float>(fixed.alpha()), 0.0f, 1e-3f));

    FixedTimestep clamp(120.0);
    int clamped = 0;
    int clamped_steps = clamp.consume(10.0, [&](double) { ++clamped; });
    CHECK(clamped_steps <= 8);
    CHECK(clamped == clamped_steps);
}

static void test_python_host() {
    Scene scene;
    Entity e = scene.create_entity("Cube");
    Transform* t = scene.world().get<Transform>(e);
    CHECK(t != nullptr);
    if (t) t->position = Vec3{1.0f, 2.0f, 3.0f};

    PythonHost host;
    PythonHost::Desc desc;
    desc.scene = &scene;
    desc.startup_source = R"(
import bge
from bge.types import Vector

cube = bge.objects["Cube"]
cube.location = Vector(4.0, 5.0, 6.0)
cube.rotation_euler = (0.0, 0.0, 1.5707963)
_cube = cube
_length = Vector(3.0, 4.0, 0.0).length
_sum = Vector(1.0, 2.0, 3.0) + Vector(1.0, 1.0, 1.0)
_cross = Vector(1.0, 0.0, 0.0).cross(Vector(0.0, 1.0, 0.0))
_count = len(bge.objects)
_has = "Cube" in bge.objects
_version = bge.__version__
_spawned = bge.logic.scene.addObject("Spawned").name
)";

    if (!host.init(desc)) {
        std::printf("FAIL python host did not start: %s\n", host.last_error().c_str());
        ++g_failures;
        return;
    }
    ++g_checks;

    // The script ran against the live scene. (Component storage is a sparse
    // set, so re-fetch the transform instead of trusting the old pointer.)
    Transform* moved = scene.world().get<Transform>(e);
    CHECK(moved != nullptr);
    if (moved) {
        CHECK(near(moved->position, Vec3{4.0f, 5.0f, 6.0f}));
        Vec3 euler = moved->rotation.to_euler();
        CHECK(near(euler.z, HALF_PI, 1e-3f));
    }

    // Run a second script that returns its results through the scene.
    host.run_string(R"(
from bge.types import Vector
bge.logic.scene.addObject("Length").location = (Vector(3.0, 4.0, 0.0).length, 0.0, 0.0)
bge.logic.scene.addObject("Cross").location = Vector(1.0, 0.0, 0.0).cross(Vector(0.0, 1.0, 0.0))
bge.logic.scene.addObject("Count").location = (float(len(bge.objects)), 0.0, 0.0)
)");
    Entity length_entity = scene.find("Length");
    Entity cross_entity = scene.find("Cross");
    Entity count_entity = scene.find("Count");
    CHECK(length_entity.valid());
    CHECK(cross_entity.valid());
    CHECK(count_entity.valid());
    if (length_entity.valid()) {
        const Transform* lt = scene.world().get<Transform>(length_entity);
        CHECK(lt && near(lt->position.x, 5.0f));
    }
    if (cross_entity.valid()) {
        const Transform* ct = scene.world().get<Transform>(cross_entity);
        CHECK(ct && near(ct->position, Vec3{0, 0, 1}));
    }
    if (count_entity.valid()) {
        const Transform* nt = scene.world().get<Transform>(count_entity);
        CHECK(nt && nt->position.x >= 3.0f);
    }

    // Handlers run on tick.
    host.run_string("bge.app.handlers.post_update.append(lambda: bge.logic.scene.addObject('Tick'))");
    host.tick(1.0f / 60.0f);
    CHECK(scene.find("Tick").valid());

    // Custom properties round trip through the scene.
    host.run_string(R"(
bge.objects["Cube"]["health"] = 75
bge.objects["Cube"]["label"] = "hero"
bge.objects["Cube"]["alive"] = True
)");
    Entity cube_entity = scene.find("Cube");
    const Properties* properties = scene.world().get<Properties>(cube_entity);
    CHECK(properties != nullptr);
    if (properties) {
        auto health = properties->values.find("health");
        CHECK(health != properties->values.end());
        if (health != properties->values.end()) {
            CHECK(health->second.kind == Property::Kind::Number);
            CHECK(near(static_cast<float>(health->second.number), 75.0f));
        }
        auto label = properties->values.find("label");
        CHECK(label != properties->values.end());
        if (label != properties->values.end()) CHECK(label->second.text == "hero");
        auto alive = properties->values.find("alive");
        CHECK(alive != properties->values.end());
        if (alive != properties->values.end()) CHECK(alive->second.kind == Property::Kind::Flag);
    }

    // Controllers run their script with themselves as the current controller.
    Entity controlled = scene.create_entity("Controlled");
    Controller controller;
    controller.name = "TestController";
    controller.script =
        "import bge\n"
        "current = bge.logic.getCurrentController()\n"
        "bge.logic.scene.addObject('ControllerRan').location = (1.0, 2.0, 3.0)\n"
        "bge.logic.scene.addObject('ControllerOwner').location = (0.0, 0.0, 0.0)\n";
    scene.world().add(controlled, controller);
    int executed = host.run_controllers();
    CHECK(executed == 1);
    CHECK(scene.find("ControllerRan").valid());
    CHECK(scene.find("ControllerOwner").valid());

    // Timers fire on tick.
    host.run_string(R"(
bge.app.timers.new(0.05, lambda: bge.logic.scene.addObject('Timer'))
)");
    host.tick(1.0f / 60.0f);
    host.tick(1.0f / 60.0f);
    host.tick(1.0f / 60.0f);
    CHECK(scene.find("Timer").valid());

    host.shutdown();
}

static void test_physics() {
    PhysicsWorld physics;
    physics.set_gravity(Vec3{0.0f, -10.0f, 0.0f});

    // A free body accelerates with gravity.
    PhysicsBodyDesc falling;
    falling.sphere = true;
    falling.radius = 0.5f;
    falling.position = Vec3{0.0f, 10.0f, 0.0f};
    uint32_t body = physics.create_body(falling);
    for (int i = 0; i < 10; ++i) physics.step(1.0f / 60.0f);
    float expected = 10.0f - 0.5f * 10.0f * (10.0f / 60.0f) * (10.0f / 60.0f);
    CHECK(near(physics.position(body).y, expected, 0.05f));
    CHECK(physics.velocity(body).y < 0.0f);

    // A body that reaches the ground comes to rest on it.
    PhysicsBodyDesc resting;
    resting.sphere = true;
    resting.radius = 0.5f;
    resting.position = Vec3{0.0f, 1.0f, 0.0f};
    uint32_t rest_body = physics.create_body(resting);
    for (int i = 0; i < 240; ++i) physics.step(1.0f / 60.0f);
    CHECK(near(physics.position(rest_body).y, 0.5f, 0.05f));
    CHECK(std::fabs(physics.velocity(rest_body).y) < 0.3f);

    // An impulse changes the velocity by impulse / mass.
    PhysicsBodyDesc pushed;
    pushed.sphere = false;
    pushed.mass = 2.0f;
    pushed.position = Vec3{0.0f, 0.5f, 0.0f};
    uint32_t push_body = physics.create_body(pushed);
    physics.apply_impulse(push_body, Vec3{4.0f, 0.0f, 0.0f});
    CHECK(near(physics.velocity(push_body).x, 2.0f, 1e-3f));

    // Two spheres pushed towards each other separate instead of overlapping.
    PhysicsBodyDesc left;
    left.sphere = true;
    left.radius = 0.5f;
    left.position = Vec3{-1.0f, 0.5f, 0.0f};
    uint32_t left_body = physics.create_body(left);
    PhysicsBodyDesc right;
    right.sphere = true;
    right.radius = 0.5f;
    right.position = Vec3{1.0f, 0.5f, 0.0f};
    uint32_t right_body = physics.create_body(right);
    physics.set_velocity(left_body, Vec3{2.0f, 0.0f, 0.0f});
    physics.set_velocity(right_body, Vec3{-2.0f, 0.0f, 0.0f});
    for (int i = 0; i < 60; ++i) physics.step(1.0f / 60.0f);
    float separation = physics.position(right_body).x - physics.position(left_body).x;
    CHECK(separation > 0.9f);

    // Static bodies never move.
    PhysicsBodyDesc wall;
    wall.sphere = false;
    wall.is_static = true;
    wall.position = Vec3{0.0f, 5.0f, 0.0f};
    uint32_t wall_body = physics.create_body(wall);
    for (int i = 0; i < 60; ++i) physics.step(1.0f / 60.0f);
    CHECK(near(physics.position(wall_body).y, 5.0f));
}

// The Jolt backend, when it was compiled in, must simulate the same basics.
static void test_jolt() {
    std::unique_ptr<PhysicsBackend> backend = create_physics_backend(true);
    if (!backend) {
        std::printf("  [skip] Jolt is not compiled in\n");
        return;
    }
    CHECK(std::string(backend->name()) == "jolt");

    PhysicsBodyDesc floor_desc;
    floor_desc.is_static = true;
    floor_desc.half_extents = Vec3{10.0f, 0.5f, 10.0f};
    floor_desc.position = Vec3{0.0f, -0.5f, 0.0f};
    uint32_t floor = backend->create_body(floor_desc);
    CHECK(backend->valid(floor));

    PhysicsBodyDesc box_desc;
    box_desc.half_extents = Vec3{0.5f, 0.5f, 0.5f};
    box_desc.position = Vec3{0.0f, 4.0f, 0.0f};
    box_desc.mass = 1.0f;
    uint32_t box = backend->create_body(box_desc);
    CHECK(backend->valid(box));

    for (int i = 0; i < 240; ++i) backend->step(1.0f / 60.0f);

    // The box comes to rest on the floor: the floor top is y=0, so the center
    // settles around y=0.5.
    CHECK(near(backend->position(box).y, 0.5f, 0.1f));
    CHECK(std::fabs(backend->velocity(box).y) < 0.2f);
    // The floor did not move.
    CHECK(near(backend->position(floor).y, -0.5f, 1e-3f));

    // An impulse moves a dynamic body, and a static body stays put.
    backend->apply_impulse(box, Vec3{0.0f, 2.0f, 0.0f});
    backend->set_velocity(box, Vec3{2.0f, 0.0f, 0.0f});
    backend->step(1.0f / 60.0f);
    CHECK(backend->velocity(box).x > 1.0f);

    backend->destroy_body(box);
    CHECK(!backend->valid(box));
}

static void test_animation() {
    Scene scene;
    Entity e = scene.create_entity("Animated");
    Transform* transform = scene.world().get<Transform>(e);

    Animation animation;
    animation.fps = 30.0f;
    animation.times = {0.0f, 1.0f};
    std::array<float, 16> start{};
    std::array<float, 16> end{};
    start[0] = start[5] = start[10] = start[15] = 1.0f;
    end[0] = end[5] = end[10] = end[15] = 1.0f;
    end[12] = 10.0f;  // the translation lives in the last column
    animation.matrices = {start, end};
    scene.world().add(e, animation);

    scene.fixed_update(0.5f);
    CHECK(transform != nullptr);
    if (transform) CHECK(near(transform->position.x, 5.0f, 0.01f));

    scene.fixed_update(0.5f);
    if (transform) CHECK(near(transform->position.x, 10.0f, 0.01f));
}

static void test_shared_frame() {
#if defined(_WIN32)
    const std::string name = "Local\\bge_test_frame";
#else
    const std::string name = "/bge_test_frame";
#endif

    SharedFrame writer;
    CHECK(writer.create(name, 1u << 20));
    if (!writer.is_open()) return;

    // A second handle is what the authoring tool does: it attaches to the
    // region the engine created.
    SharedFrame reader;
    CHECK(reader.create(name, 1u << 20));
    if (!reader.is_open()) {
        writer.close();
        return;
    }

    std::vector<uint8_t> frame(64 * 32 * 4);
    for (size_t i = 0; i < frame.size(); ++i) frame[i] = static_cast<uint8_t>((i * 7) & 0xFF);

    CHECK(writer.publish(64, 32, frame.data(), frame.size()));
    int width = 0;
    int height = 0;
    uint64_t serial = 0;
    std::vector<uint8_t> out;
    CHECK(reader.read(width, height, out, &serial));
    CHECK(width == 64 && height == 32);
    CHECK(out == frame);
    CHECK(serial > 0);

    // An unchanged frame is skipped: the preview uses this to avoid uploading
    // the same image again.
    CHECK(!reader.read(width, height, out, &serial, serial));

    // The next frame is picked up.
    frame[0] = 0xAB;
    CHECK(writer.publish(64, 32, frame.data(), frame.size()));
    CHECK(reader.read(width, height, out, &serial, 0));
    CHECK(!out.empty() && out[0] == 0xAB);

    // A frame larger than the region is rejected instead of overrunning it.
    CHECK(!writer.publish(1024, 1024, frame.data(), frame.size()));

    writer.close();
    reader.close();
}

int main() {
    std::printf("BGE Revived foundation tests\n");
    test_math();
    test_ecs();
    test_jobs();
    test_time();
    test_python_host();
    test_physics();
    test_jolt();
    test_animation();
    test_shared_frame();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
