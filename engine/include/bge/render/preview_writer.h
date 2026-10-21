#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bge {

// Writes the rendered frame to disk so an external tool (the Blender add-on)
// can display exactly what the engine produces.
//
// The frame is written to a temporary file and then renamed, so a reader never
// sees a half written image.
class PreviewWriter {
public:
    bool open(const std::string& folder, int width, int height, bool write_files = true);
    void close();

    // Captures the current framebuffer into memory. Cheap enough to run every
    // frame; write_frame() then publishes it as a PNG.
    void capture();

    // Writes the last captured frame as frame.png.
    bool write_frame();

    const std::string& frame_path() const { return frame_path_; }
    uint64_t frames_written() const { return frames_written_; }

    // Upper bound on the frame rate, so a preview does not saturate the CPU.
    void set_max_fps(float fps) { max_fps_ = fps; }
    // True when enough time passed for another frame.
    bool should_write(double now_seconds) const;
    // Records the time of the last written frame.
    void mark_written(double now_seconds) { last_write_ = now_seconds; }

    int width() const { return width_; }
    int height() const { return height_; }

    // The most recent frame as raw RGBA bytes (bottom-up), for the network
    // preview server.
    const std::vector<uint8_t>& pixels() const { return pixels_; }

private:
    std::string folder_;
    std::string frame_path_;
    int width_ = 0;
    int height_ = 0;
    uint64_t frames_written_ = 0;
    double last_write_ = -1.0;
    float max_fps_ = 12.0f;
    std::vector<uint8_t> pixels_;
};

} // namespace bge
