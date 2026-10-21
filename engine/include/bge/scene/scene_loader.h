#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bge {

class Scene;
class Renderer;

// Loads the compact .bgescene format written by tools/bge_export.py.
// Returns false and fills `error` on malformed input or missing assets.
bool load_bge_scene(Scene& scene, Renderer& renderer, const std::string& path,
                    std::string* error = nullptr);

// Loads a scene that is already in memory: the manifest text and the packed
// geometry buffer. `base` resolves the relative texture paths.
bool load_bge_scene_data(Scene& scene, Renderer& renderer, const std::string& manifest,
                          const std::vector<uint8_t>& buffer, const std::string& base,
                          std::string* error = nullptr);

} // namespace bge
