#pragma once

#include "bge/net/shared_frame.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bge {

// A scene change pushed by the authoring tool. The server parses the binary
// payloads; the game applies them to the running scene.
struct PreviewCommand {
    enum class Type {
        None,
        Scene,      // a full scene replace
        Transform,  // a moved object
        Object,     // a new or changed object
        Remove,     // a deleted object
        Mesh,       // new geometry
        Material,   // a material description
    };

    Type type = Type::None;
    std::string name;                  // the object name
    int32_t id = -1;                   // the mesh id
    int32_t material_index = -1;       // the material slot
    bool visible = true;
    float matrix[16] = {0};           // the world matrix, column major
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    std::string json;                  // the scene or material description
    std::vector<uint8_t> payload;      // the geometry
    std::vector<std::string> names;    // the objects of a bulk command
    std::vector<std::array<float, 16>> matrices;
    std::vector<int32_t> mesh_ids;
    std::vector<int32_t> material_ids;
    std::vector<uint8_t> visible_flags;
};

// A tiny local TCP server that lets an authoring tool push scenes and pull
// rendered frames.
//
// The protocol is length prefixed, so a JSON header can contain any text:
//
//     [4 byte json length][4 byte payload length][json][payload]
//
// The pixels of a frame normally travel through a named shared memory region,
// not through the socket: `hello` advertises the region name and capacity, and
// a `frame` reply then only carries `{"cmd":"frame","ok":true,"shm":true,...}`.
// A client that does not ask for shared memory (`{"cmd":"hello","shm":true}`)
// still receives the pixels over the socket.
//
// Requests:
//     {"cmd":"hello"}                  -> {"cmd":"hello","ok":true,...,
//                                          "shm":"Local\\bge_frame_8765","shm_capacity":N}
//     {"cmd":"scene","json":"...","bin":N} + N bytes -> {"cmd":"scene","ok":true}
//     {"cmd":"frame"}                   -> {"cmd":"frame","width":W,"height":H,"bytes":N}
//                                          (pixels follow, unless "shm":true)
//     {"cmd":"ping"}                    -> {"cmd":"pong"}
//
// Incremental scene updates carry binary payloads:
//     {"cmd":"transform","count":N}  + N * (u16 name length, name, 16 floats)
//     {"cmd":"object","count":N,...} + N * (u16 name length, name, i32 mesh, i32 material,
//                                           u8 visible, 16 floats)
//     {"cmd":"mesh","id":M,...}      + the vertex and index data
//     {"cmd":"remove","count":N}     + N * (u16 name length, name)
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

    // An incremental scene change, waiting to be applied by the game.
    bool take_pending_command(PreviewCommand& command);

    // A resize requested by the client (for example the Blender render
    // resolution).
    bool take_resize_request(int& width, int& height);

    // The latest frame, ready to be served to the client.
    void set_frame(int width, int height, const std::vector<uint8_t>& rgba);

    // The shared memory region pixels travel through, when the platform gave us
    // one. The authoring tool learns its name from `hello`.
    bool shared_frame_available() const { return shared_frame_.is_open(); }
    const std::string& shared_frame_name() const { return shared_frame_.name(); }
    size_t shared_frame_capacity() const { return shared_frame_.capacity(); }

    // Number of frames served, for diagnostics.
    uint64_t frames_served() const { return frames_served_; }

private:
    bool handle_input();
    void queue(const std::string& json, const std::vector<uint8_t>* payload = nullptr);
    void flush_output();
    void handle_message(const std::string& json, const std::vector<uint8_t>& payload);
    void parse_scene_command(const std::string& command, const std::string& json,
                             const std::vector<uint8_t>& payload);
    // Queues the current frame for the connected client, using shared memory
    // when the client asked for it.
    void send_frame();

    uintptr_t listening_ = 0;
    uintptr_t client_ = 0;
    uint16_t port_ = 0;
    std::vector<uint8_t> in_buffer_;
    std::vector<uint8_t> out_buffer_;
    std::string pending_scene_json_;
    std::vector<uint8_t> pending_scene_binary_;
    bool has_pending_scene_ = false;
    PreviewCommand pending_command_;
    bool has_pending_command_ = false;
    int frame_width_ = 0;
    int frame_height_ = 0;
    std::vector<uint8_t> frame_pixels_;
    bool frame_in_shared_ = false;
    bool client_shared_ = false;
    SharedFrame shared_frame_;
    bool render_pending_ = false;
    uint64_t render_serial_ = 0;
    uint64_t frame_serial_ = 0;
    bool resize_pending_ = false;
    int resize_width_ = 0;
    int resize_height_ = 0;
    uint64_t frames_served_ = 0;
};

} // namespace bge
