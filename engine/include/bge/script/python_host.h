#pragma once

#include <string>
#include <vector>

namespace bge {

class Scene;

// Embedded CPython host exposing a Blender-style `bge` module.
//
// The host is optional: when the engine is built without CPython every call
// becomes a no-op and `available()` returns false. Scripts are plain Python and
// talk to the engine through a small native layer (`bge._native`), with the
// user-facing API implemented in Python on top of it.
class PythonHost {
public:
    PythonHost();
    ~PythonHost();

    PythonHost(const PythonHost&) = delete;
    PythonHost& operator=(const PythonHost&) = delete;

    struct Desc {
        bool headless = false;
        Scene* scene = nullptr;        // bound before the startup script runs
        std::string startup_script;   // optional file executed after the module is registered
        std::string startup_source;   // optional source executed after the module is registered
    };

    bool init(const Desc& desc);
    void shutdown();
    bool available() const;

    // Bind the scene the scripts operate on.
    void set_scene(Scene* scene);

    // Execute Python code. Returns false and records the error on failure.
    bool run_string(const std::string& source, const std::string& name = "<bge>");
    bool run_file(const std::string& path);

    // Executes the script of every controller attached to a scene object.
    // While a controller runs, bge.logic.getCurrentController() returns it.
    int run_controllers();

    // Called once per frame: dispatches the registered handlers.
    void tick(float delta_seconds);

    // Frame counter and last error text.
    uint64_t frame() const { return frame_; }
    const std::string& last_error() const { return last_error_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    uint64_t frame_ = 0;
    std::string last_error_;
};

} // namespace bge
