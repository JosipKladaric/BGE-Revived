#pragma once

#include <cstdint>
#include <string>

#include "bge/math/math.h"

namespace bge {

struct TextureGPU {
    uint32_t id = 0;
    int width = 0;
    int height = 0;
    std::string path;  // source file, kept so scenes can be saved with their textures
};

TextureGPU load_texture(const std::string& path, bool srgb = true);
// Equirectangular world/environment texture. HDR images are kept in RGBA16F;
// LDR images are decoded from sRGB. Wrapping is repeat horizontally and clamp
// vertically so the lat-long map does not bleed across the poles.
TextureGPU load_texture_equirect(const std::string& path);
TextureGPU make_checker_texture(int size, const Vec3& color_a, const Vec3& color_b, int cells);
TextureGPU make_white_texture();

} // namespace bge
