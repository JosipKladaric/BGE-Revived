#pragma once

#include <string>

namespace bge {

class Scene;
class Renderer;

// Writes a scene in the same .bgescene format the loader reads, so a running
// game can save its level and load it back later.
//
// Meshes and materials are taken from the renderer, which keeps a CPU side copy
// of everything it uploaded.
bool save_bge_scene(Scene& scene, const Renderer& renderer, const std::string& path,
                    std::string* error = nullptr);

} // namespace bge
