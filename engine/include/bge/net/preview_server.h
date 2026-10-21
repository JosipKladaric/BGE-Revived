#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bge {

// A tiny local TCP server that lets an authoring tool push scenes and pull
// rendered frames.
//
// The protocol is length prefixed, so a JSON header can contain any text:
//
//     [4 byte json length][4 byte payload length][json][payload]
//
// Requests:
//     {"cmd":"hello"}                  -> {"cmd":"hello","ok":true,...}
//     {"cmd":"scene","json":"...","bin":N} + N bytes -> {"cmd":"scene","ok":true}
//     {"cmd":"frame"}                   -> {"cmd":"frame","width":W,"height":H,"bytes":N} + N bytes
//     {"cmd":"ping"}                    -> {"cmd":"pong"}
//
// Everything is polled from the render thread, so there is no locking between
// the simulation and the network.
class PreviewServer {
public:
    PreviewServer();
    ~PreviewServer();

    bool start(uint16_t port);
    void stop();
    bool running() const { return listening_ != 0; }
    uint16_t port() const { return port_; }
    bool connected() const { return client_ != 0; }

    // Call once per frame: accepts connections and moves data in and out.
    void poll();

    // A scene pushed by the client, waiting to be applied by the game.
    bool take_pending_scene(std::string& json, std::vector<uint8_t>& binary);

    // A resize requested by the client (for example the Blender render
    // resolution).
    bool take_resize_request(int& width, int& height);

    // The latest frame, ready to be served to the client.
    void set_frame(int width, int height, const std::vector<uint8_t>& rgba);

    // Number of frames served, for diagnostics.
    uint64_t frames_served() const { return frames_served_; }

private:
    bool handle_input();
    void queue(const std::string& json, const std::vector<uint8_t>* payload = nullptr);
    void flush_output();
    void handle_message(const std::string& json, const std::vector<uint8_t>& payload);

    uintptr_t listening_ = 0;
    uintptr_t client_ = 0;
    uint16_t port_ = 0;
    std::vector<uint8_t> in_buffer_;
    std::vector<uint8_t> out_buffer_;
    std::string pending_scene_json_;
    std::vector<uint8_t> pending_scene_binary_;
    bool has_pending_scene_ = false;
    int frame_width_ = 0;
    int frame_height_ = 0;
    std::vector<uint8_t> frame_pixels_;
    bool render_pending_ = false;
    uint64_t render_serial_ = 0;
    uint64_t frame_serial_ = 0;
    bool resize_pending_ = false;
    int resize_width_ = 0;
    int resize_height_ = 0;
    uint64_t frames_served_ = 0;
};

} // namespace bge
