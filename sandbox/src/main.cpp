// BGE Revived sandbox. The engine does the work; this file composes a scene and
// owns the frame loop, exactly as a game built on the runtime would.
#include "bge/core/log.h"
#include "bge/core/time.h"
#include "bge/ecs/components.h"
#include "bge/physics/physics_backend.h"
#include "bge/physics/physics_binding.h"
#include "bge/physics/physics_world.h"
#include "bge/platform/window.h"
#include "bge/render/renderer.h"
#include "bge/scene/scene.h"
#include "bge/scene/scene_loader.h"
#include "bge/scene/scene_saver.h"
#include "bge/script/python_host.h"

#include <glad/gl.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace bge;
namespace fs = std::filesystem;

struct Options {
    int width = 1600;
    int height = 900;
    bool headless = false;
    int frames = -1;
    std::string screenshot = "screenshot.png";
    std::string assets;
    std::string scene;
    std::string save_scene;
    std::string script;
    bool physics = false;
    bool builtin_physics = false;
    bool watch = false;
    bool vsync = true;
    bool free_camera = false;
};

static Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                BGE_ERROR("Missing value for %s", name);
                return "";
            }
            return argv[++i];
        };
        if (a == "--width") o.width = std::atoi(next("--width"));
        else if (a == "--height") o.height = std::atoi(next("--height"));
        else if (a == "--headless") o.headless = true;
        else if (a == "--frames") o.frames = std::atoi(next("--frames"));
        else if (a == "--screenshot") o.screenshot = next("--screenshot");
        else if (a == "--assets") o.assets = next("--assets");
        else if (a == "--scene") o.scene = next("--scene");
        else if (a == "--save-scene") o.save_scene = next("--save-scene");
        else if (a == "--script") o.script = next("--script");
        else if (a == "--physics") o.physics = true;
        else if (a == "--builtin-physics") o.builtin_physics = true;
        else if (a == "--watch") o.watch = true;
        else if (a == "--free-camera") o.free_camera = true;
        else if (a == "--no-vsync") o.vsync = false;
        else BGE_WARN("Unknown argument: %s", a.c_str());
    }
    if (o.headless && o.frames < 0) o.frames = 60;
    return o;
}

static std::string find_asset_root(const std::string& explicit_root, const char* argv0) {
    std::vector<fs::path> candidates;
    if (!explicit_root.empty()) candidates.push_back(explicit_root);
    fs::path exe = fs::absolute(argv0).parent_path();
    candidates.push_back(exe / "assets");
    candidates.push_back(exe / ".." / "assets");
    candidates.push_back(exe / ".." / ".." / "assets");
    fs::path cwd = fs::current_path();
    candidates.push_back(cwd / "assets");
    candidates.push_back(cwd / ".." / "assets");
    for (const fs::path& candidate : candidates) {
        if (fs::exists(candidate / "shaders" / "pbr.vert")) {
            return fs::weakly_canonical(candidate).string();
        }
    }
    return "assets";
}

static void save_screenshot(int width, int height, const std::string& path) {
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    std::vector<unsigned char> flipped(pixels.size());
    size_t row = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
        std::memcpy(flipped.data() + static_cast<size_t>(y) * row,
                    pixels.data() + static_cast<size_t>(height - 1 - y) * row, row);
    }
    if (stbi_write_png(path.c_str(), width, height, 4, flipped.data(), static_cast<int>(row))) {
        BGE_INFO("Screenshot saved: %s (%dx%d)", path.c_str(), width, height);
    } else {
        BGE_ERROR("Failed to write screenshot: %s", path.c_str());
    }
}

struct OrbitCamera {
    Vec3 target{0.0f, 2.4f, 0.0f};
    float yaw = 0.76f;
    float pitch = 0.22f;
    float radius = 30.0f;
    bool auto_rotate = true;

    void update(float dt, Window& window, CameraState& out) {
        const Input& input = window.input();

        if (input.mouse_pressed(0)) window.set_cursor_captured(true);
        if (input.mouse_released(0)) window.set_cursor_captured(false);

        if (window.cursor_captured()) {
            Vec2 delta = input.mouse_delta();
            yaw -= delta.x * 0.0045f;
            pitch = clampf(pitch + delta.y * 0.0045f, -0.12f, 1.15f);
        }

        if (input.key_pressed(Key::Space)) auto_rotate = !auto_rotate;
        if (auto_rotate && !window.cursor_captured()) yaw += 0.055f * dt;

        float wheel = input.scroll_delta();
        if (std::fabs(wheel) > 0.001f) radius = clampf(radius - wheel * 2.0f, 7.0f, 65.0f);

        float cp = std::cos(pitch);
        Vec3 offset{radius * cp * std::sin(yaw), radius * std::sin(pitch), radius * cp * std::cos(yaw)};
        out.position = target + offset;
        out.forward = normalize(target - out.position);
        out.up = {0.0f, 1.0f, 0.0f};
        out.fov_y = radians(50.0f);
        out.near_plane = 0.1f;
        out.far_plane = 600.0f;
    }
};

static Vec3 srgb(float r, float g, float b) {
    return {std::pow(r, 2.2f), std::pow(g, 2.2f), std::pow(b, 2.2f)};
}

static float hash01(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return static_cast<float>(x & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

static void build_physics_scene(Scene& scene, Renderer& renderer) {
    TextureHandle checker = renderer.create_checker_texture(512, {0.14f, 0.15f, 0.17f}, {0.22f, 0.23f, 0.26f}, 8);

    MaterialDesc ground_mat;
    ground_mat.base_color = {1, 1, 1, 1};
    ground_mat.roughness = 0.85f;
    ground_mat.albedo = checker;
    ground_mat.use_albedo = true;
    ground_mat.uv_scale = 24.0f;
    MaterialHandle ground_material = renderer.create_material(ground_mat);

    MaterialDesc box_mat;
    box_mat.base_color = Vec4{srgb(0.86f, 0.42f, 0.16f), 1};
    box_mat.roughness = 0.38f;
    box_mat.metallic = 0.08f;
    MaterialHandle box_material = renderer.create_material(box_mat);

    MaterialDesc ball_mat;
    ball_mat.base_color = Vec4{srgb(0.18f, 0.55f, 0.92f), 1};
    ball_mat.roughness = 0.22f;
    ball_mat.metallic = 0.35f;
    MaterialHandle ball_material = renderer.create_material(ball_mat);

    MeshHandle plane = renderer.create_mesh(make_plane(60.0f, 1.0f));
    MeshHandle cube = renderer.create_mesh(make_cube(1.0f));
    MeshHandle sphere = renderer.create_mesh(make_sphere(1.0f, 48, 32));

    Entity ground = scene.create_mesh_entity("Ground", plane, ground_material);
    {
        // A static floor body: Jolt has no implicit ground plane, so the demo
        // needs something for the pile to land on.
        RigidBody floor_body;
        floor_body.is_static = true;
        floor_body.half_extents = {60.0f, 0.1f, 60.0f};
        scene.world().add(ground, floor_body);
    }

    // A loose grid of boxes that topples into a pile.
    for (int i = 0; i < 9; ++i) {
        int column = i % 3;
        int row = i / 3;
        Entity e = scene.create_mesh_entity("Box" + std::to_string(i), cube, box_material);
        Transform* t = scene.world().get<Transform>(e);
        t->position = {(column - 1) * 1.08f, 4.0f + row * 1.05f, (row - 1) * 1.08f};
        t->rotation = Quat::from_axis_angle({0.3f, 1.0f, 0.1f}, i * 0.41f);
        RigidBody body;
        body.mass = 1.0f;
        body.half_extents = {0.5f, 0.5f, 0.5f};
        body.restitution = 0.05f;
        body.friction = 0.6f;
        scene.world().add(e, body);
    }

    // A few spheres that roll into the pile.
    for (int i = 0; i < 4; ++i) {
        Entity e = scene.create_mesh_entity("Ball" + std::to_string(i), sphere, ball_material);
        Transform* t = scene.world().get<Transform>(e);
        t->position = {-2.6f + i * 1.7f, 4.5f + i * 0.7f, -1.2f};
        t->scale = {0.8f, 0.8f, 0.8f};
        RigidBody body;
        body.sphere = true;
        body.radius = 0.4f;
        body.mass = 0.8f;
        body.restitution = 0.35f;
        body.friction = 0.35f;
        scene.world().add(e, body);
    }

    Entity camera = scene.create_entity("Camera");
    scene.world().add(camera, Camera{radians(45.0f), 0.1f, 200.0f, true});
    Transform* ct = scene.world().get<Transform>(camera);
    ct->position = {3.1f, 2.0f, 4.0f};
    ct->rotation = Quat::from_euler(Vec3{radians(-11.0f), radians(-38.0f), 0.0f});
    scene.camera_entity = camera;

    Entity sun = scene.create_entity("Sun");
    DirectionalLight light;
    light.direction = normalize(Vec3{-0.45f, -0.8f, -0.4f});
    light.color = srgb(1.0f, 0.9f, 0.76f);
    light.intensity = 4.0f;
    scene.world().add(sun, light);
    scene.sun_entity = sun;

    scene.sky_color = srgb(0.10f, 0.16f, 0.28f);
    scene.horizon_color = srgb(0.42f, 0.52f, 0.68f);
    scene.ground_color = srgb(0.10f, 0.12f, 0.16f);
    scene.fog_color = scene.horizon_color;
    scene.fog_density = 0.004f;
    scene.ambient_intensity = 0.7f;
    scene.update_transforms();
}

static void build_scene(Scene& scene, Renderer& renderer) {
    TextureHandle checker = renderer.create_checker_texture(512, {0.16f, 0.17f, 0.19f}, {0.26f, 0.27f, 0.30f}, 4);
    TextureHandle white = 0;

    MaterialDesc ground_mat;
    ground_mat.base_color = {1, 1, 1, 1};
    ground_mat.roughness = 0.84f;
    ground_mat.metallic = 0.0f;
    ground_mat.albedo = checker;
    ground_mat.use_albedo = true;
    ground_mat.uv_scale = 80.0f;
    MaterialHandle ground_material = renderer.create_material(ground_mat);

    MaterialDesc cube_mat;
    cube_mat.base_color = {1, 1, 1, 1};
    cube_mat.roughness = 0.46f;
    cube_mat.metallic = 0.04f;
    MaterialHandle cube_material = renderer.create_material(cube_mat);

    MaterialDesc gloss_mat;
    gloss_mat.base_color = {0.95f, 0.95f, 0.95f, 1};
    gloss_mat.roughness = 0.18f;
    gloss_mat.metallic = 0.55f;
    MaterialHandle gloss_material = renderer.create_material(gloss_mat);

    MaterialDesc gold_mat;
    gold_mat.base_color = Vec4{srgb(1.0f, 0.72f, 0.22f), 1};
    gold_mat.metallic = 0.82f;
    gold_mat.roughness = 0.32f;
    gold_mat.emissive = srgb(0.15f, 0.08f, 0.02f);
    MaterialHandle gold_material = renderer.create_material(gold_mat);

    MaterialDesc chrome_mat;
    chrome_mat.base_color = Vec4{srgb(0.90f, 0.93f, 0.97f), 1};
    chrome_mat.metallic = 1.0f;
    chrome_mat.roughness = 0.06f;
    MaterialHandle chrome_material = renderer.create_material(chrome_mat);

    MaterialDesc copper_mat;
    copper_mat.base_color = Vec4{srgb(0.96f, 0.55f, 0.38f), 1};
    copper_mat.metallic = 1.0f;
    copper_mat.roughness = 0.28f;
    MaterialHandle copper_material = renderer.create_material(copper_mat);

    MaterialDesc blue_mat;
    blue_mat.base_color = Vec4{srgb(0.18f, 0.42f, 0.95f), 1};
    blue_mat.metallic = 0.12f;
    blue_mat.roughness = 0.18f;
    MaterialHandle blue_material = renderer.create_material(blue_mat);

    MaterialDesc red_mat;
    red_mat.base_color = Vec4{srgb(0.92f, 0.16f, 0.12f), 1};
    red_mat.metallic = 0.05f;
    red_mat.roughness = 0.32f;
    MaterialHandle red_material = renderer.create_material(red_mat);

    MaterialDesc green_mat;
    green_mat.base_color = Vec4{srgb(0.12f, 0.75f, 0.38f), 1};
    green_mat.metallic = 0.22f;
    green_mat.roughness = 0.18f;
    MaterialHandle green_material = renderer.create_material(green_mat);

    MeshHandle plane = renderer.create_mesh(make_plane(1200.0f, 1.0f));
    MeshHandle cube = renderer.create_mesh(make_cube(1.0f));
    MeshHandle sphere = renderer.create_mesh(make_sphere(1.0f, 64, 40));
    MeshHandle knot = renderer.create_mesh(make_torus_knot(1.0f, 0.16f, 320, 28));

    Entity ground = scene.create_mesh_entity("Ground", plane, ground_material);
    (void)ground;

    // A field of instanced cubes. One mesh, one material, per-instance tint:
    // this is the BGE's per-object draw-call problem solved by batching.
    const float step = 2.9f;
    for (int z = -6; z <= 6; ++z) {
        for (int x = -6; x <= 6; ++x) {
            Vec3 p{static_cast<float>(x) * step, 0.0f, static_cast<float>(z) * step};
            float dist = length(p);
            if (dist < 7.2f) continue;

            static const Vec3 palette[8] = {
                srgb(0.88f, 0.84f, 0.76f), // bone
                srgb(0.74f, 0.54f, 0.36f), // sand
                srgb(0.66f, 0.30f, 0.22f), // terracotta
                srgb(0.16f, 0.45f, 0.46f), // teal
                srgb(0.20f, 0.28f, 0.46f), // navy
                srgb(0.78f, 0.58f, 0.16f), // ochre
                srgb(0.36f, 0.50f, 0.34f), // sage
                srgb(0.42f, 0.20f, 0.38f), // plum
            };
            uint32_t seed = static_cast<uint32_t>((x + 17) * 73856093 ^ (z + 17) * 19349663);
            float h = 0.5f + hash01(seed) * 2.7f;
            float hue = hash01(seed ^ 0x9e3779b9u);
            uint32_t color_index = static_cast<uint32_t>(hue * 8.0f) & 7u;
            Vec3 color = palette[color_index] * (0.84f + hash01(seed ^ 0xabcdefu) * 0.16f);
            float width = 0.72f + hash01(seed ^ 0x314159u) * 0.46f;

            Entity e = scene.create_mesh_entity("Cube", cube, (hue > 0.88f) ? gloss_material : cube_material);
            Transform* t = scene.world().get<Transform>(e);
            t->position = {p.x, h * 0.5f, p.z};
            t->scale = {width, h, width};
            t->rotation = Quat::from_axis_angle({0, 1, 0}, hash01(seed ^ 0x5151u) * 0.35f);
            MeshRenderer* r = scene.world().get<MeshRenderer>(e);
            r->tint = Vec4{color, 1.0f};

            if (h > 2.9f) {
                scene.world().add(e, Bob{0.03f, 0.4f + hash01(seed ^ 0x77u) * 0.5f, hash01(seed ^ 0x99u) * TWO_PI, t->position.y});
            }
        }
    }

    // PBR material showcase.
    Entity knot_entity = scene.create_mesh_entity("Knot", knot, gold_material);
    Transform* kt = scene.world().get<Transform>(knot_entity);
    kt->position = {0.0f, 3.3f, 0.0f};
    kt->scale = {1.55f, 1.55f, 1.55f};
    scene.world().add(knot_entity, Rotator{Vec3{0, 1, 0}, 0.55f});

    const MaterialHandle sphere_materials[] = {gold_material, chrome_material, copper_material, blue_material, red_material, green_material};
    const char* sphere_names[] = {"Gold", "Chrome", "Copper", "Blue", "Red", "Green"};
    for (int i = 0; i < 6; ++i) {
        float angle = static_cast<float>(i) / 6.0f * TWO_PI;
        Entity e = scene.create_mesh_entity(sphere_names[i], sphere, sphere_materials[i]);
        Transform* t = scene.world().get<Transform>(e);
        t->position = {std::cos(angle) * 6.0f, 1.5f, std::sin(angle) * 6.0f};
        t->scale = {1.3f, 1.3f, 1.3f};
        scene.world().add(e, Bob{0.12f, 0.9f + i * 0.08f, angle, 1.5f});
        scene.world().add(e, Rotator{Vec3{0, 1, 0}, 0.25f + i * 0.05f});
    }

    // One tall marker to give the scene a vertical anchor.
    Entity marker = scene.create_mesh_entity("Marker", cube, gold_material);
    Transform* mt = scene.world().get<Transform>(marker);
    mt->position = {-11.0f, 4.0f, -11.0f};
    mt->scale = {0.6f, 8.0f, 0.6f};
    scene.world().add(marker, Rotator{Vec3{0, 1, 0}, 0.15f});

    // A controller: a script attached to an object, the classic BGE building
    // block. It is run by the Python host when the sandbox starts.
    Controller controller;
    controller.name = "MarkerPulse";
    controller.script =
        "import bge\n"
        "owner = bge.logic.getCurrentController().owner\n"
        "owner['pulse'] = 0.0\n"
        "owner['hits'] = 0\n"
        "def pulse():\n"
        "    owner['pulse'] = owner['pulse'] + 0.05\n"
        "    owner['hits'] = owner['hits'] + 1\n"
        "bge.app.timers.new(0.05, pulse)\n";
    scene.world().add(marker, controller);

    // Camera and sun are entities so the scene is data, not hard-coded renderer state.
    Entity camera = scene.create_entity("Camera");
    scene.world().add(camera, Camera{radians(50.0f), 0.1f, 600.0f, true});
    scene.camera_entity = camera;

    Entity sun = scene.create_entity("Sun");
    DirectionalLight light;
    light.direction = normalize(Vec3{-0.55f, -0.72f, -0.45f});
    light.color = srgb(1.0f, 0.86f, 0.68f);
    light.intensity = 5.0f;
    light.cast_shadow = true;
    scene.world().add(sun, light);
    scene.sun_entity = sun;

    scene.sky_color = srgb(0.06f, 0.15f, 0.40f);
    scene.horizon_color = srgb(0.38f, 0.56f, 0.84f);
    scene.ground_color = srgb(0.07f, 0.08f, 0.10f);
    scene.fog_color = srgb(0.38f, 0.50f, 0.68f);
    scene.fog_density = 0.0050f;
    scene.exposure = 1.0f;
    scene.ambient_intensity = 0.60f;

    (void)white;
    scene.update_transforms();
}

int main(int argc, char** argv) {
    log_init("bge_sandbox");
    Options opt = parse_args(argc, argv);
    std::string asset_root = find_asset_root(opt.assets, argv[0]);
    BGE_INFO("Asset root: %s", asset_root.c_str());

    WindowDesc desc;
    desc.title = "BGE Revived · Sandbox";
    desc.width = opt.width;
    desc.height = opt.height;
    desc.vsync = opt.vsync;
    desc.hidden = opt.headless;

    Window window;
    if (!window.create(desc)) {
        BGE_FATAL("Failed to create window");
        return 1;
    }

    Renderer renderer;
    RendererDesc renderer_desc;
    renderer_desc.asset_root = asset_root;
    renderer_desc.shadow_size = 2048;
    renderer_desc.shadows = true;
    renderer_desc.fxaa = true;
    if (!renderer.init(window, renderer_desc)) {
        BGE_FATAL("Failed to initialise renderer");
        return 1;
    }

    Scene scene;
    bool imported_scene = false;
    if (!opt.scene.empty()) {
        std::string error;
        if (!load_bge_scene(scene, renderer, opt.scene, &error)) {
            BGE_FATAL("Could not load scene '%s': %s", opt.scene.c_str(), error.c_str());
            renderer.shutdown();
            return 1;
        }
        imported_scene = true;
        if (opt.headless) scene.apply_camera_component();
    } else if (opt.physics) {
        build_physics_scene(scene, renderer);
    } else {
        build_scene(scene, renderer);
    }

    // Physics: create a body for every rigid body in the scene and step the
    // simulation on the fixed timestep. Jolt is used when it was built in.
    std::unique_ptr<PhysicsBackend> physics = create_physics_backend(!opt.builtin_physics);
    if (!physics) physics = std::make_unique<PhysicsWorld>();
    BGE_INFO("Physics: %s backend", physics->name());
    create_physics_bodies(scene, *physics);
    if (physics->body_count() > 0) {
        BGE_INFO("Physics: %zu bodies, gravity %.2f m/s^2", physics->body_count(), -9.81f);
    }

    OrbitCamera orbit;
    if (opt.headless) orbit.auto_rotate = false;
    bool use_scene_camera = imported_scene && scene.camera_entity.valid() && !opt.free_camera;
    if (imported_scene && !opt.free_camera) {
        orbit.auto_rotate = false;
    }

    // Optional Python host. A script can drive the scene through the `bge`
    // module: bge.objects["Name"].location, bge.app.handlers.post_update, ...
    PythonHost python;
    if (!opt.script.empty()) {
        PythonHost::Desc python_desc;
        python_desc.headless = opt.headless;
        python_desc.scene = &scene;
        python_desc.startup_script = opt.script;
        if (python.init(python_desc)) {
            python.set_scene(&scene);
            int controllers = python.run_controllers();
            if (controllers > 0) BGE_INFO("Ran %d controller script(s)", controllers);
        } else {
            BGE_WARN("Python host unavailable: %s", python.last_error().c_str());
        }
    }

    Clock clock;
    FixedTimestep fixed(120.0);
    int frame = 0;
    double fps_timer = 0.0;
    int fps_frames = 0;
    double simulated = 0.0;
    bool screenshot_done = false;

    // Live link: watch the scene file and reload it in place when it changes.
    // The Blender addon re-exports the scene whenever the user edits it.
    bool watch_scene = opt.watch && !opt.scene.empty();
    std::filesystem::file_time_type last_stamp{};
    if (watch_scene) {
        std::error_code ec;
        last_stamp = std::filesystem::last_write_time(opt.scene, ec);
        BGE_INFO("Live link: watching '%s'", opt.scene.c_str());
    }
    double watch_timer = 0.0;

    while (!window.should_close()) {
        window.poll_events();
        double raw_dt = clock.tick();
        double dt = opt.headless ? (1.0 / 60.0) : raw_dt;
        if (dt > 0.25) dt = 0.25;

        if (window.input().key_pressed(Key::Escape)) {
            if (window.cursor_captured()) window.set_cursor_captured(false);
            else window.request_close();
        }

        if (window.resized()) {
            renderer.resize(window.width(), window.height());
            window.clear_resized();
        }

        fixed.consume(dt, [&](double step) {
            simulated += step;
            scene.fixed_update(static_cast<float>(step));
            if (physics->body_count() > 0) {
                physics->step(static_cast<float>(step));
                sync_physics_transforms(scene, *physics);
            }
        });

        if (watch_scene) {
            watch_timer += dt;
            if (watch_timer > 0.2) {
                watch_timer = 0.0;
                std::error_code ec;
                std::filesystem::file_time_type stamp = std::filesystem::last_write_time(opt.scene, ec);
                if (ec) {
                    BGE_WARN("Live link: cannot stat '%s': %s", opt.scene.c_str(), ec.message().c_str());
                } else if (stamp != last_stamp) {
                    last_stamp = stamp;
                    scene.clear();
                    std::string error;
                    if (load_bge_scene(scene, renderer, opt.scene, &error)) {
                        scene.update_transforms();
                        if (use_scene_camera) scene.apply_camera_component();
                        create_physics_bodies(scene, *physics);
                        BGE_INFO("Live link: reloaded '%s'", opt.scene.c_str());
                    } else {
                        BGE_ERROR("Live link: reload failed: %s", error.c_str());
                    }
                }
            }
        }

        if (python.available()) python.tick(static_cast<float>(dt));

        scene.update_transforms();
        if (use_scene_camera) {
            scene.apply_camera_component();
        } else {
            orbit.update(static_cast<float>(dt), window, scene.camera);
        }

        renderer.begin_frame();
        renderer.render(scene, scene.camera);

        if (!opt.headless) window.swap_buffers();

        ++frame;
        ++fps_frames;
        fps_timer += raw_dt;
        if (!opt.headless && fps_timer >= 0.5) {
            const RenderStats& s = renderer.stats();
            char title[220];
            std::snprintf(title, sizeof(title),
                          "BGE Revived · %.0f FPS · %u draws · %u instances · %u tris · %u culled",
                          fps_frames / fps_timer, s.draw_calls, s.instances, s.triangles, s.culled);
            window.set_title(title);
            fps_timer = 0.0;
            fps_frames = 0;
        }

        if (opt.frames > 0 && frame >= opt.frames) {
            if (opt.headless && !screenshot_done) {
                glFinish();
                save_screenshot(window.width(), window.height(), opt.screenshot);
                screenshot_done = true;
            }
            break;
        }
    }

    const RenderStats& stats = renderer.stats();
    BGE_INFO("Sandbox exiting: %d frames, %.2fs simulated, %u draws, %u instances, %u tris, %.2f ms CPU",
             frame, simulated, stats.draw_calls, stats.instances, stats.triangles, stats.cpu_ms);

    if (!opt.save_scene.empty()) {
        std::string error;
        if (!save_bge_scene(scene, renderer, opt.save_scene, &error)) {
            BGE_ERROR("Could not save the scene: %s", error.c_str());
        }
    }

    python.shutdown();
    renderer.shutdown();
    return 0;
}
