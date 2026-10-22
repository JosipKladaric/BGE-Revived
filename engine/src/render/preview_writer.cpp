#include "bge/render/preview_writer.h"

#include "bge/core/log.h"

#include <glad/gl.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

namespace bge {
namespace {

namespace fs = std::filesystem;

void write_png(const std::string& path, int width, int height, const unsigned char* pixels) {
    stbi_write_png(path.c_str(), width, height, 4, pixels, width * 4);
}

} // namespace

bool PreviewWriter::open(const std::string& folder, int width, int height, bool write_files) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) {
        BGE_ERROR("Preview: cannot create '%s': %s", folder.c_str(), ec.message().c_str());
        return false;
    }
    folder_ = folder;
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    frame_path_ = write_files ? (fs::path(folder_) / "frame.png").string() : std::string();
    if (write_files) {
        BGE_INFO("Preview: writing frames to %s (%dx%d)", frame_path_.c_str(), width_, height_);
    }
    return true;
}

void PreviewWriter::close() {
    folder_.clear();
    frame_path_.clear();
    frames_written_ = 0;
    last_write_ = -1.0;
}

bool PreviewWriter::should_write(double now_seconds) const {
    if (max_fps_ <= 0.0f) return true;
    if (last_write_ < 0.0) return true;
    return (now_seconds - last_write_) >= (1.0 / max_fps_);
}

void PreviewWriter::set_size(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
}

void PreviewWriter::capture() {
    if (width_ <= 0 || height_ <= 0) return;

    std::vector<unsigned char> pixels(static_cast<size_t>(width_) * height_ * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // OpenGL reads bottom-up; flip into image order.
    std::vector<unsigned char> flipped(pixels.size());
    const size_t stride = static_cast<size_t>(width_) * 4;
    for (int y = 0; y < height_; ++y) {
        std::memcpy(flipped.data() + static_cast<size_t>(height_ - 1 - y) * stride,
                    pixels.data() + static_cast<size_t>(y) * stride, stride);
    }
    pixels_ = std::move(flipped);
}

bool PreviewWriter::write_frame() {
    if (frame_path_.empty() || width_ <= 0 || height_ <= 0) return false;
    if (pixels_.empty()) capture();
    if (pixels_.empty()) return false;

    const std::vector<unsigned char>& flipped = pixels_;

    // Write to a temporary file and rename it, so a reader never sees a
    // partially written image.
    const fs::path target(frame_path_);
    const fs::path temporary = target.parent_path() / "frame_tmp.png";
    write_png(temporary.string(), width_, height_, flipped.data());

    std::error_code ec;
    fs::rename(temporary, target, ec);
    if (ec) {
        fs::remove(target, ec);
        ec.clear();
        fs::rename(temporary, target, ec);
        if (ec) {
            BGE_WARN("Preview: cannot publish the frame: %s", ec.message().c_str());
            return false;
        }
    }
    ++frames_written_;
    return true;
}

} // namespace bge
