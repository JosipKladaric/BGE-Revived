#include "bge/render/texture.h"

#include "bge/core/log.h"

#include <glad/gl.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace bge {
namespace {

void set_common_params(uint32_t id, bool srgb) {
    (void)srgb;
    glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(id, GL_TEXTURE_BASE_LEVEL, 0);
    glTextureParameteri(id, GL_TEXTURE_MAX_LEVEL, 1000);

#if defined(GL_MAX_TEXTURE_MAX_ANISOTROPY) && defined(GL_TEXTURE_MAX_ANISOTROPY)
    if (GLAD_GL_EXT_texture_filter_anisotropic) {
        float max_aniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &max_aniso);
        if (max_aniso > 1.0f) glTextureParameterf(id, GL_TEXTURE_MAX_ANISOTROPY, std::min(max_aniso, 8.0f));
    }
#endif
}

uint32_t create_texture_2d(int width, int height, bool srgb, const void* pixels) {
    uint32_t id = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &id);
    int levels = 1 + static_cast<int>(std::floor(std::log2(static_cast<double>(std::max(width, height)))));
    glTextureStorage2D(id, levels, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height);
    if (pixels) {
        glTextureSubImage2D(id, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
    glGenerateTextureMipmap(id);
    set_common_params(id, srgb);
    return id;
}

uint32_t create_texture_2d_float(int width, int height, const float* pixels) {
    uint32_t id = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &id);
    int levels = 1 + static_cast<int>(std::floor(std::log2(static_cast<double>(std::max(width, height)))));
    glTextureStorage2D(id, levels, GL_RGBA16F, width, height);
    if (pixels) {
        glTextureSubImage2D(id, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels);
    }
    glGenerateTextureMipmap(id);
    set_common_params(id, false);
    return id;
}

} // namespace

TextureGPU load_texture(const std::string& path, bool srgb) {
    TextureGPU out;
    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
        BGE_ERROR("Failed to load texture '%s': %s", path.c_str(), stbi_failure_reason());
        return out;
    }

    out.id = create_texture_2d(width, height, srgb, data);
    out.width = width;
    out.height = height;
    stbi_image_free(data);
    BGE_DEBUG("Texture loaded: %s (%dx%d, %d channels)", path.c_str(), width, height, channels);
    return out;
}

TextureGPU load_texture_equirect(const std::string& path) {
    TextureGPU out;
    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load(true);

    if (stbi_is_hdr(path.c_str())) {
        float* data = stbi_loadf(path.c_str(), &width, &height, &channels, 4);
        if (!data) {
            BGE_ERROR("Failed to load HDR environment '%s': %s", path.c_str(), stbi_failure_reason());
            return out;
        }
        out.id = create_texture_2d_float(width, height, data);
        out.width = width;
        out.height = height;
        stbi_image_free(data);
        BGE_INFO("HDR environment loaded: %s (%dx%d)", path.c_str(), width, height);
    } else {
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (!data) {
            BGE_ERROR("Failed to load environment '%s': %s", path.c_str(), stbi_failure_reason());
            return out;
        }
        out.id = create_texture_2d(width, height, true, data);
        out.width = width;
        out.height = height;
        stbi_image_free(data);
        BGE_INFO("Environment loaded: %s (%dx%d)", path.c_str(), width, height);
    }

    if (out.id) {
        glTextureParameteri(out.id, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(out.id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    return out;
}

TextureGPU make_checker_texture(int size, const Vec3& color_a, const Vec3& color_b, int cells) {    size = std::max(size, 2);
    cells = std::max(cells, 1);

    std::vector<unsigned char> pixels(static_cast<size_t>(size) * size * 4);
    const float cell_size = static_cast<float>(size) / static_cast<float>(cells);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int cx = static_cast<int>(x / cell_size);
            int cy = static_cast<int>(y / cell_size);
            float checker = ((cx + cy) & 1) ? 1.0f : 0.0f;

            // Thin grid lines give the ground plane readable scale.
            float fx = (x % static_cast<int>(cell_size)) / cell_size;
            float fy = (y % static_cast<int>(cell_size)) / cell_size;
            bool line = fx < 0.018f || fy < 0.018f;

            Vec3 c = lerp(color_a, color_b, checker);
            if (line) c = c * 0.55f;
            size_t i = (static_cast<size_t>(y) * size + x) * 4;
            pixels[i + 0] = static_cast<unsigned char>(saturate(c.x) * 255.0f);
            pixels[i + 1] = static_cast<unsigned char>(saturate(c.y) * 255.0f);
            pixels[i + 2] = static_cast<unsigned char>(saturate(c.z) * 255.0f);
            pixels[i + 3] = 255;
        }
    }

    TextureGPU out;
    out.id = create_texture_2d(size, size, true, pixels.data());
    out.width = size;
    out.height = size;
    BGE_INFO("Checker texture generated: %dx%d, %d cells", size, size, cells);
    return out;
}

TextureGPU make_white_texture() {
    unsigned char pixel[4] = {255, 255, 255, 255};
    TextureGPU out;
    out.id = create_texture_2d(1, 1, false, pixel);
    out.width = 1;
    out.height = 1;
    return out;
}

} // namespace bge
