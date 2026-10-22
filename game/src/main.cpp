// BGE Revived game template.
//
// This is what a packaged game runs: it loads the scene next to the
// executable, starts the optional Python script, and runs the normal engine
// loop. There is no demo content in here on purpose - the game is whatever
// the Blender author exported.
//
//   bge_game.exe                       uses ./game.bgescene next to the exe
//   bge_game.exe --scene other.bgescene
//   bge_game.exe --headless --frames 60 --screenshot out.png
#include "bge/core/log.h"
#include "bge/core/time.h"
#include "bge/ecs/components.h"
#include "bge/physics/physics_backend.h"
#include "bge/physics/physics_binding.h"
#include "bge/physics/physics_world.h"
#include "bge/platform/window.h"
#include "bge/net/preview_server.h"
#include "bge/render/preview_writer.h"
#include "bge/render/renderer.h"
#include "bge/scene/scene.h"
#include "bge/scene/scene_loader.h"
#include "bge/script/python_host.h"

#include <glad/gl.h>
#include <stb_image_write.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace bge;
namespace fs = std::filesystem;

namespace {

struct Options {
    int width = 1280;
    int height = 720;
    bool headless = false;
    int frames = -1;
    std::string screenshot;
    std::string scene;
    std::string script;
    std::string assets;
    bool vsync = true;
    bool watch = false;
    // Preview mode: render into a folder the Blender add-on can watch.
    std::string preview;
    int preview_width = 960;
    int preview_height = 540;
    float preview_fps = 12.0f;
    int preview_port = 0;
    bool builtin_physics = false;
};

// The directory that holds the executable: a packaged game keeps its scene,
// textures and assets there.
fs::path executable_directory() {
#ifdef _WIN32
    char buffer[MAX_PATH] = {0};
    DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return fs::path(buffer).parent_path();
    }
#endif
    std::error_code ec;
    return fs::current_path(ec);
}

Options parse_args(int argc, char** argv) {
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
        else if (a == "--scene") o.scene = next("--scene");
        else if (a == "--script") o.script = next("--script");
        else if (a == "--assets") o.assets = next("--assets");
        else if (a == "--watch") o.watch = true;
        else if (a == "--builtin-physics") o.builtin_physics = true;
        else if (a == "--preview") o.preview = next("--preview");
        else if (a == "--preview-size") {
            std::string value = next("--preview-size");
            size_t separator = value.find_first_of("xX");
            if (separator != std::string::npos) {
                o.preview_width = std::atoi(value.substr(0, separator).c_str());
                o.preview_height = std::atoi(value.substr(separator + 1).c_str());
            }
        }
        else if (a == "--preview-fps") o.preview_fps = static_cast<float>(std::atof(next("--preview-fps")));
        else if (a == "--preview-port") o.preview_port = std::atoi(next("--preview-port"));
        else if (a == "--no-vsync") o.vsync = false;
        else BGE_WARN("Unknown argument: %s", a.c_str());
    }
    if (o.headless && o.frames < 0) o.frames = 60;
    return o;
}

void save_screenshot(int width, int height, const std::string& path) {
    if (path.empty()) return;
    int channels = 4;
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * channels);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // OpenGL reads bottom-up; flip into image order.
    std::vector<unsigned char> flipped(pixels.size());
    const size_t stride = static_cast<size_t>(width) * channels;
    for (int y = 0; y < height; ++y) {
        std::memcpy(flipped.data() + static_cast<size_t>(height - 1 - y) * stride,
                    pixels.data() + static_cast<size_t>(y) * stride, stride);
    }
    if (stbi_write_png(path.c_str(), width, height, channels, flipped.data(), stride)) {
        BGE_INFO("Screenshot saved: %s (%dx%d)", path.c_str(), width, height);
    } else {
        BGE_ERROR("Failed to write screenshot: %s", path.c_str());
    }
}

} // namespace

// Applies an incremental scene update to the running scene.
void apply_preview_command(Scene& scene, Renderer& renderer, const PreviewCommand& update,
                            std::unordered_map<int32_t, uint32_t>& mesh_handles) {
    switch (update.type) {
        case PreviewCommand::Type::Transform: {
            for (size_t i = 0; i < update.names.size(); ++i) {
                Entity e = scene.find(update.names[i]);
                Transform* t = e ? scene.world().get<Transform>(e) : nullptr;
                if (!t) continue;
                Mat4 matrix = Mat4::identity();
                for (int k = 0; k < 16; ++k) matrix.m[k] = update.matrices[i][k];
                decompose_trs(matrix, t->position, t->rotation, t->scale);
            }
            scene.update_transforms();
            break;
        }
        case PreviewCommand::Type::Object: {
            for (size_t i = 0; i < update.names.size(); ++i) {
                Entity e = scene.find(update.names[i]);
                if (!e) {
                    e = scene.create_entity(update.names[i]);
                    if (!e) continue;
                }
                Transform* t = scene.world().get<Transform>(e);
                if (t) {
                    Mat4 matrix = Mat4::identity();
                    for (int k = 0; k < 16; ++k) matrix.m[k] = update.matrices[i][k];
                    decompose_trs(matrix, t->position, t->rotation, t->scale);
                }
                MeshRenderer* mesh = scene.world().get<MeshRenderer>(e);
                if (!mesh) {
                    mesh = &scene.world().add(e, MeshRenderer{});
                }
                const int32_t mesh_id = update.mesh_ids[i];
                auto found = mesh_handles.find(mesh_id);
                if (found != mesh_handles.end()) mesh->mesh = found->second;
                mesh->visible = update.visible_flags[i] != 0;
            }
            scene.update_transforms();
            break;
        }
        case PreviewCommand::Type::Remove: {
            for (const std::string& name : update.names) {
                Entity e = scene.find(name);
                if (e) scene.world().destroy(e);
            }
            break;
        }
        case PreviewCommand::Type::Mesh: {
            // The payload holds the vertices (position, normal, uv) and the indices.
            const size_t vertex_bytes = static_cast<size_t>(update.vertex_count) * 8 * sizeof(float);
            const size_t index_bytes = static_cast<size_t>(update.index_count) * sizeof(uint32_t);
            if (update.payload.size() < vertex_bytes + index_bytes) break;

            MeshData data;
            data.vertices.resize(update.vertex_count);
            for (uint32_t i = 0; i < update.vertex_count; ++i) {
                float values[8];
                std::memcpy(values, update.payload.data() + static_cast<size_t>(i) * 8 * sizeof(float),
                            sizeof(values));
                data.vertices[i] = {{values[0], values[1], values[2]},
                                    {values[3], values[4], values[5]},
                                    {values[6], values[7]}};
            }
            data.indices.resize(update.index_count);
            std::memcpy(data.indices.data(),
                        update.payload.data() + vertex_bytes,
                        static_cast<size_t>(update.index_count) * sizeof(uint32_t));
            data.compute_bounds();

            const uint32_t handle = renderer.create_mesh(data);
            if (handle) {
                mesh_handles[update.id] = handle;
                BGE_INFO("Live link: uploaded mesh %d (%u vertices)", update.id, update.vertex_count);
            }
            break;
        }
        default:
            break;
    }
}

int main(int argc, char** argv) {
    log_init("bge_game");
    Options opt = parse_args(argc, argv);
    const fs::path base = executable_directory();

    std::string scene_path = opt.scene;
    if (!opt.preview.empty()) {
        // Preview mode: the add-on writes the scene into the preview folder.
        const fs::path preview_scene = fs::path(opt.preview) / "scene.bgescene";
        if (fs::exists(preview_scene)) scene_path = preview_scene.string();
    }
    if (scene_path.empty()) {
        for (const char* candidate : {"game.bgescene", "scene.bgescene", "level.bgescene"}) {
            if (fs::exists(base / candidate)) {
                scene_path = (base / candidate).string();
                break;
            }
        }
    }
    if (scene_path.empty() && (opt.preview_port > 0 || !opt.preview.empty())) {
        // In preview mode the scene arrives over the socket, so the engine can
        // start without one.
        BGE_INFO("Preview mode: waiting for a scene");
    } else if (scene_path.empty()) {
        BGE_FATAL("No scene found next to the executable (expected game.bgescene).");
        return 1;
    }

    std::string asset_root = opt.assets;
    if (asset_root.empty()) {
        asset_root = (base / "assets").string();
        if (!fs::exists(asset_root)) asset_root = (base / "assets" / "shaders").parent_path().string();
    }

    WindowDesc desc;
    desc.title = "BGE Revived Game";
    desc.width = opt.preview.empty() ? opt.width : opt.preview_width;
    desc.height = opt.preview.empty() ? opt.height : opt.preview_height;
    desc.vsync = opt.vsync && opt.preview.empty();
    desc.hidden = opt.headless || !opt.preview.empty();

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
    std::string error;
    if (!scene_path.empty() && !load_bge_scene(scene, renderer, scene_path, &error)) {
        BGE_FATAL("Could not load scene '%s': %s", scene_path.c_str(), error.c_str());
        renderer.shutdown();
        return 1;
    }
    if (!scene_path.empty()) scene.apply_camera_component();

    std::unique_ptr<PhysicsBackend> physics = create_physics_backend(!opt.builtin_physics);
    if (!physics) physics = std::make_unique<PhysicsWorld>();
    BGE_INFO("Physics: %s backend", physics->name());
    create_physics_bodies(scene, *physics);

    PreviewWriter preview;
    if (!opt.preview.empty() || opt.preview_port > 0) {
        const std::string folder = opt.preview.empty() ? (base / "preview").string() : opt.preview;
        preview.open(folder, window.width(), window.height(), !opt.preview.empty());
        preview.set_max_fps(opt.preview_fps);
    }

    // Network preview: an authoring tool pushes scenes and pulls frames.
    PreviewServer server;
    std::unordered_map<int32_t, uint32_t> mesh_handles;
    if (opt.preview_port > 0) {
        server.start(static_cast<uint16_t>(opt.preview_port));
    }
    const fs::path preview_base = opt.preview.empty() ? base : fs::path(opt.preview);

    PythonHost python;
    std::string script_path = opt.script;
    if (script_path.empty() && fs::exists(base / "startup.py")) {
        script_path = (base / "startup.py").string();
    }
    if (!script_path.empty()) {
        PythonHost::Desc python_desc;
        python_desc.headless = opt.headless;
        python_desc.scene = &scene;
        python_desc.startup_script = script_path;
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
    double simulated = 0.0;
    double real_time = 0.0;
    bool screenshot_done = false;

    // Live link: reload the scene in place when the authoring tool rewrites it.
    bool watch_scene = opt.watch || !opt.preview.empty();
    std::filesystem::file_time_type last_stamp{};
    if (watch_scene) {
        std::error_code ec;
        last_stamp = std::filesystem::last_write_time(scene_path, ec);
        BGE_INFO("Live link: watching '%s'", scene_path.c_str());
    }
    double watch_timer = 0.0;

    while (!window.should_close()) {
        window.poll_events();
        double raw_dt = clock.tick();
        double dt = opt.headless ? (1.0 / 60.0) : raw_dt;
        if (dt > 0.25) dt = 0.25;

        if (window.input().key_pressed(Key::Escape)) window.request_close();

        fixed.consume(dt, [&](double step) {
            simulated += step;
            scene.fixed_update(static_cast<float>(step));
            if (physics->body_count() > 0) {
                physics->step(static_cast<float>(step));
                sync_physics_transforms(scene, *physics);
            }
        });

        if (python.available()) python.tick(static_cast<float>(dt));

        if (server.running()) {
            server.poll();

            // A scene pushed over the network replaces the current one.
            std::string pushed_json;
            std::vector<uint8_t> pushed_binary;
            if (server.take_pending_scene(pushed_json, pushed_binary)) {
                scene.clear();
                std::string reload_error;
                if (load_bge_scene_data(scene, renderer, pushed_json, pushed_binary,
                                        preview_base.string(), &reload_error)) {
                    scene.update_transforms();
                    scene.apply_camera_component();
                    create_physics_bodies(scene, *physics);
                    BGE_INFO("Live link: applied the pushed scene");
                } else {
                    BGE_ERROR("Live link: the pushed scene failed to load: %s", reload_error.c_str());
                }
            }

            // Incremental updates: only what changed is transferred.
            PreviewCommand update;
            while (server.take_pending_command(update)) {
                apply_preview_command(scene, renderer, update, mesh_handles);
            }

            int requested_width = 0;
            int requested_height = 0;
            if (server.take_resize_request(requested_width, requested_height)) {
                if (window.resize(requested_width, requested_height)) {
                    renderer.resize(requested_width, requested_height);
                    preview.set_size(requested_width, requested_height);
                    BGE_INFO("Live link: resized to %dx%d", requested_width, requested_height);
                }
            }
        }

        if (watch_scene) {
            watch_timer += dt;
            if (watch_timer > 0.2) {
                watch_timer = 0.0;
                std::error_code ec;
                std::filesystem::file_time_type stamp = std::filesystem::last_write_time(scene_path, ec);
                if (ec) {
                    BGE_WARN("Live link: cannot stat '%s': %s", scene_path.c_str(), ec.message().c_str());
                } else if (stamp != last_stamp) {
                    last_stamp = stamp;
                    scene.clear();
                    std::string reload_error;
                    if (load_bge_scene(scene, renderer, scene_path, &reload_error)) {
                        scene.update_transforms();
                        scene.apply_camera_component();
                        create_physics_bodies(scene, *physics);
                        BGE_INFO("Live link: reloaded '%s'", scene_path.c_str());
                    } else {
                        BGE_ERROR("Live link: reload failed: %s", reload_error.c_str());
                    }
                }
            }
        }

        scene.update_transforms();
        scene.apply_camera_component();

        renderer.begin_frame();
        renderer.render(scene, scene.camera);

        if (!preview.frame_path().empty() || server.running()) {
            // Capture the frame so the file preview and the network preview
            // both serve the real engine output.
            preview.capture();
        }

        if (!preview.frame_path().empty()) {
            // The preview mirrors the real engine output for the Blender side.
            real_time += raw_dt;
            if (preview.should_write(real_time)) {
                preview.write_frame();
                preview.mark_written(real_time);
            }
        }

        if (server.running()) {
            // Publish the fresh frame so a waiting client gets the current image.
            if (!preview.pixels().empty()) {
                server.set_frame(preview.width(), preview.height(), preview.pixels());
            }
            server.poll();
        }

        if (!opt.headless) window.swap_buffers();

        ++frame;
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
    BGE_INFO("Game exiting: %d frames, %.2fs simulated, %u draws, %u instances, %u tris, %.2f ms CPU",
             frame, simulated, stats.draw_calls, stats.instances, stats.triangles, stats.cpu_ms);
    python.shutdown();
    renderer.shutdown();
    return 0;
}
