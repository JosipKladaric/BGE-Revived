#include "bge/net/preview_server.h"

#include "bge/core/log.h"

#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace bge {
namespace {

// The server speaks a small JSON subset. Extracting the few fields we need by
// hand keeps the engine free of a JSON dependency on the network path.
std::string json_field(const std::string& json, const char* name) {
    const std::string key = std::string("\"") + name + "\"";
    size_t position = json.find(key);
    if (position == std::string::npos) return {};
    position = json.find(':', position + key.size());
    if (position == std::string::npos) return {};
    ++position;
    while (position < json.size() && (json[position] == ' ' || json[position] == '\t')) ++position;
    if (position >= json.size()) return {};

    if (json[position] == '"') {
        ++position;
        std::string value;
        while (position < json.size() && json[position] != '"') {
            if (json[position] == '\\' && position + 1 < json.size()) {
                ++position;
                switch (json[position]) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    default: value += json[position]; break;
                }
            } else {
                value += json[position];
            }
            ++position;
        }
        return value;
    }

    std::string value;
    while (position < json.size() && json[position] != ',' && json[position] != '}') {
        value += json[position];
        ++position;
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\n')) value.pop_back();
    return value;
}

// The booleans we care about ("shm": true). Python's json.dumps writes a space
// after the colon, so this cannot be a substring test.
bool json_bool(const std::string& json, const char* name) {
    const std::string key = std::string("\"") + name + "\"";
    size_t position = json.find(key);
    if (position == std::string::npos) return false;
    position = json.find(':', position + key.size());
    if (position == std::string::npos) return false;
    ++position;
    while (position < json.size() && (json[position] == ' ' || json[position] == '\t')) ++position;
    return json.compare(position, 4, "true") == 0;
}

void append_u32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

// The pixel region is sized once, when the server starts, so both processes map
// the same amount of memory. 64 MiB covers 4096x4096 RGBA with room to spare and
// only the pages that are actually written are committed.
constexpr size_t kSharedFrameCapacity = 64u << 20;

std::string frame_region_name(uint16_t port) {
#if defined(_WIN32)
    // The Local\ namespace keeps the region private to the logon session.
    return "Local\\bge_frame_" + std::to_string(port);
#else
    return "/bge_frame_" + std::to_string(port);
#endif
}

// The shared memory name goes into the hello reply, so backslashes have to be
// escaped.
std::string json_escape(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size() + 8);
    for (char character : text) {
        switch (character) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += character; break;
        }
    }
    return escaped;
}

#if defined(_WIN32)
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
int close_socket(socket_t handle) { return closesocket(handle); }
bool would_block() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
int close_socket(socket_t handle) { return close(handle); }
bool would_block() { return errno == EWOULDBLOCK; }
#endif

} // namespace

PreviewServer::PreviewServer() = default;

PreviewServer::~PreviewServer() {
    stop();
}

bool PreviewServer::start(uint16_t port) {
    stop();

#if defined(_WIN32)
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        BGE_ERROR("Preview server: WSAStartup failed");
        return false;
    }
#endif

    socket_t handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle == kInvalidSocket) {
        BGE_ERROR("Preview server: cannot create the socket");
        return false;
    }

    int reuse = 1;
    setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        BGE_ERROR("Preview server: cannot bind port %u", port);
        close_socket(handle);
        return false;
    }
    if (listen(handle, 2) != 0) {
        BGE_ERROR("Preview server: cannot listen on port %u", port);
        close_socket(handle);
        return false;
    }

    listening_ = static_cast<uintptr_t>(handle);
    port_ = port;

    // Frames travel through shared memory when the platform allows it; the
    // socket then only carries the small control messages.
    shared_frame_.create(frame_region_name(port), kSharedFrameCapacity);
    if (shared_frame_.is_open()) {
        BGE_INFO("Preview server: shared frame buffer '%s' (%zu MiB)", shared_frame_.name().c_str(),
                 shared_frame_.capacity() >> 20);
    } else {
        BGE_WARN("Preview server: no shared frame buffer, frames travel over the socket");
    }

    BGE_INFO("Preview server listening on 127.0.0.1:%u", port);
    return true;
}

void PreviewServer::stop() {
    if (client_ != 0) {
        close_socket(static_cast<socket_t>(client_));
        client_ = 0;
    }
    if (listening_ != 0) {
        close_socket(static_cast<socket_t>(listening_));
        listening_ = 0;
#if defined(_WIN32)
        WSACleanup();
#endif
    }
    shared_frame_.close();
    client_shared_ = false;
    frame_in_shared_ = false;
    in_buffer_.clear();
    out_buffer_.clear();
    has_pending_scene_ = false;
}

void PreviewServer::set_frame(int width, int height, const std::vector<uint8_t>& rgba) {
    frame_width_ = width;
    frame_height_ = height;
    ++frame_serial_;

    // Publish into the shared region and keep no second copy: the socket path
    // reads the pixels back only when a client that cannot use shared memory
    // asks for a frame.
    frame_in_shared_ = shared_frame_.is_open() &&
                       shared_frame_.publish(width, height, rgba.data(), rgba.size());
    if (frame_in_shared_) {
        frame_pixels_.clear();
    } else {
        frame_pixels_ = rgba;
    }
}

void PreviewServer::send_frame() {
    if (frame_serial_ == 0) {
        queue("{\"cmd\":\"frame\",\"ok\":false}");
        return;
    }

    char header[160];
    if (client_shared_ && frame_in_shared_) {
        std::snprintf(header, sizeof(header),
                      "{\"cmd\":\"frame\",\"ok\":true,\"shm\":true,\"width\":%d,\"height\":%d,"
                      "\"bytes\":%zu,\"serial\":%llu}",
                      frame_width_, frame_height_,
                      static_cast<size_t>(frame_width_) * static_cast<size_t>(frame_height_) * 4,
                      static_cast<unsigned long long>(shared_frame_.serial()));
        queue(header);
        ++frames_served_;
        return;
    }

    if (frame_pixels_.empty() && frame_in_shared_) {
        // A client without shared memory support: copy the frame back out of
        // the region, once per request.
        int width = 0;
        int height = 0;
        if (!shared_frame_.read(width, height, frame_pixels_)) {
            queue("{\"cmd\":\"frame\",\"ok\":false}");
            return;
        }
    }
    if (frame_pixels_.empty()) {
        queue("{\"cmd\":\"frame\",\"ok\":false}");
        return;
    }
    std::snprintf(header, sizeof(header),
                  "{\"cmd\":\"frame\",\"ok\":true,\"width\":%d,\"height\":%d,\"bytes\":%zu}",
                  frame_width_, frame_height_, frame_pixels_.size());
    queue(header, &frame_pixels_);
    ++frames_served_;
}

bool PreviewServer::take_pending_scene(std::string& json, std::vector<uint8_t>& binary) {
    if (!has_pending_scene_) return false;
    json = pending_scene_json_;
    binary = pending_scene_binary_;
    pending_scene_json_.clear();
    pending_scene_binary_.clear();
    has_pending_scene_ = false;
    return true;
}

bool PreviewServer::take_pending_command(PreviewCommand& command) {
    if (!has_pending_command_) return false;
    command = std::move(pending_command_);
    pending_command_ = PreviewCommand{};
    has_pending_command_ = false;
    return true;
}

bool PreviewServer::take_resize_request(int& width, int& height) {
    if (!resize_pending_) return false;
    width = resize_width_;
    height = resize_height_;
    resize_pending_ = false;
    return true;
}

void PreviewServer::queue(const std::string& json, const std::vector<uint8_t>* payload) {
    if (client_ == 0) return;
    const size_t payload_size = payload ? payload->size() : 0;
    std::vector<uint8_t> message;
    message.reserve(8 + json.size() + payload_size);
    append_u32(message, static_cast<uint32_t>(json.size()));
    append_u32(message, static_cast<uint32_t>(payload_size));
    message.insert(message.end(), json.begin(), json.end());
    if (payload) message.insert(message.end(), payload->begin(), payload->end());
    out_buffer_.insert(out_buffer_.end(), message.begin(), message.end());
}

void PreviewServer::handle_message(const std::string& json, const std::vector<uint8_t>& payload) {
    const std::string command = json_field(json, "cmd");
    BGE_INFO("Preview server: message '%s'", command.c_str());

    if (command == "hello") {
        // A client that understands shared memory asks for it here; the pixel
        // bytes then never touch the socket.
        client_shared_ = json_bool(json, "shm");

        std::string reply = "{\"cmd\":\"hello\",\"ok\":true,\"version\":2,\"width\":" +
                            std::to_string(frame_width_) + ",\"height\":" +
                            std::to_string(frame_height_);
        if (shared_frame_.is_open()) {
            reply += ",\"shm\":\"" + json_escape(shared_frame_.name()) +
                     "\",\"shm_capacity\":" + std::to_string(shared_frame_.capacity());
        }
        reply += "}";
        queue(reply);
        BGE_INFO("Preview server: client hello (shared memory: %s)",
                 client_shared_ && shared_frame_.is_open() ? "yes" : "no");
    } else if (command == "ping") {
        queue("{\"cmd\":\"pong\"}");
    } else if (command == "scene") {
        const std::string manifest = json_field(json, "json");
        pending_scene_json_ = manifest;
        pending_scene_binary_.assign(payload.begin(), payload.end());
        has_pending_scene_ = true;
        BGE_INFO("Preview server: received a scene (%zu bytes of geometry)", payload.size());
        queue("{\"cmd\":\"scene\",\"ok\":true}");
    } else if (command == "frame" || command == "render") {
        if (command == "render") {
            // The answer is sent after the next rendered frame, so the client
            // always gets an image that matches the current scene.
            render_pending_ = true;
            render_serial_ = frame_serial_;
            BGE_INFO("Preview server: render requested (frame %llu)",
                     static_cast<unsigned long long>(frame_serial_));
            return;
        }
        send_frame();
        BGE_INFO("Preview server: served frame %llu (%dx%d, shared memory: %s)",
                 static_cast<unsigned long long>(frame_serial_), frame_width_, frame_height_,
                 client_shared_ && frame_in_shared_ ? "yes" : "no");
    } else if (command == "resize") {
        const std::string width = json_field(json, "width");
        const std::string height = json_field(json, "height");
        resize_width_ = std::atoi(width.c_str());
        resize_height_ = std::atoi(height.c_str());
        resize_pending_ = resize_width_ > 0 && resize_height_ > 0;
        queue("{\"cmd\":\"resize\",\"ok\":true}");
    } else if (command == "transform" || command == "object" || command == "remove" ||
               command == "mesh" || command == "material") {
        parse_scene_command(command, json, payload);
    }
}

namespace {

bool read_u16(const std::vector<uint8_t>& data, size_t& offset, uint16_t& value) {
    if (offset + 2 > data.size()) return false;
    value = static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
    offset += 2;
    return true;
}

bool read_i32(const std::vector<uint8_t>& data, size_t& offset, int32_t& value) {
    if (offset + 4 > data.size()) return false;
    uint32_t raw = static_cast<uint32_t>(data[offset]) |
                   (static_cast<uint32_t>(data[offset + 1]) << 8) |
                   (static_cast<uint32_t>(data[offset + 2]) << 16) |
                   (static_cast<uint32_t>(data[offset + 3]) << 24);
    value = static_cast<int32_t>(raw);
    offset += 4;
    return true;
}

bool read_floats(const std::vector<uint8_t>& data, size_t& offset, float* values, size_t count) {
    const size_t bytes = count * sizeof(float);
    if (offset + bytes > data.size()) return false;
    std::memcpy(values, data.data() + offset, bytes);
    offset += bytes;
    return true;
}

} // namespace

void PreviewServer::parse_scene_command(const std::string& command, const std::string& json,
                                        const std::vector<uint8_t>& payload) {
    PreviewCommand update;
    size_t offset = 0;

    if (command == "transform" || command == "object") {
        const uint32_t count = static_cast<uint32_t>(std::atoi(json_field(json, "count").c_str()));
        for (uint32_t i = 0; i < count; ++i) {
            uint16_t name_length = 0;
            if (!read_u16(payload, offset, name_length)) break;
            if (offset + name_length > payload.size()) break;
            std::string name(reinterpret_cast<const char*>(payload.data() + offset), name_length);
            offset += name_length;

            int32_t mesh_id = -1;
            int32_t material_id = -1;
            uint8_t visible = 1;
            if (command == "object") {
                if (!read_i32(payload, offset, mesh_id)) break;
                if (!read_i32(payload, offset, material_id)) break;
                if (offset >= payload.size()) break;
                visible = payload[offset++];
            }
            std::array<float, 16> matrix{};
            if (!read_floats(payload, offset, matrix.data(), 16)) break;

            update.names.push_back(name);
            update.matrices.push_back(matrix);
            update.mesh_ids.push_back(mesh_id);
            update.material_ids.push_back(material_id);
            update.visible_flags.push_back(visible);
        }
        update.type = (command == "transform") ? PreviewCommand::Type::Transform
                                                : PreviewCommand::Type::Object;
    } else if (command == "remove") {
        const uint32_t count = static_cast<uint32_t>(std::atoi(json_field(json, "count").c_str()));
        for (uint32_t i = 0; i < count; ++i) {
            uint16_t name_length = 0;
            if (!read_u16(payload, offset, name_length)) break;
            if (offset + name_length > payload.size()) break;
            update.names.emplace_back(reinterpret_cast<const char*>(payload.data() + offset), name_length);
            offset += name_length;
        }
        update.type = PreviewCommand::Type::Remove;
    } else if (command == "mesh") {
        update.type = PreviewCommand::Type::Mesh;
        update.id = std::atoi(json_field(json, "id").c_str());
        update.vertex_count = static_cast<uint32_t>(std::atoi(json_field(json, "vertex_count").c_str()));
        update.index_count = static_cast<uint32_t>(std::atoi(json_field(json, "index_count").c_str()));
        update.payload.assign(payload.begin(), payload.end());
    } else if (command == "material") {
        update.type = PreviewCommand::Type::Material;
        update.json = json_field(json, "material");
    }

    pending_command_ = std::move(update);
    has_pending_command_ = true;
    queue("{\"cmd\":\"ok\",\"for\":\"" + command + "\"}");
}

bool PreviewServer::handle_input() {
    if (client_ == 0) return false;

    uint8_t chunk[65536];
    bool received = false;
    for (;;) {
        int count = recv(static_cast<socket_t>(client_), reinterpret_cast<char*>(chunk), sizeof(chunk), 0);
        if (count > 0) {
            in_buffer_.insert(in_buffer_.end(), chunk, chunk + count);
            received = true;
            if (static_cast<size_t>(count) < sizeof(chunk)) break;
            continue;
        }
        if (count == 0) {
            // The client closed the connection.
            close_socket(static_cast<socket_t>(client_));
            client_ = 0;
            in_buffer_.clear();
            return received;
        }
        if (would_block()) break;  // no data yet
        break;  // a real error: drop the connection
    }

    // Parse as many complete messages as we have.
    for (;;) {
        if (in_buffer_.size() < 8) break;
        const uint32_t json_length = static_cast<uint32_t>(in_buffer_[0]) |
                                    (static_cast<uint32_t>(in_buffer_[1]) << 8) |
                                    (static_cast<uint32_t>(in_buffer_[2]) << 16) |
                                    (static_cast<uint32_t>(in_buffer_[3]) << 24);
        const uint32_t payload_length = static_cast<uint32_t>(in_buffer_[4]) |
                                        (static_cast<uint32_t>(in_buffer_[5]) << 8) |
                                        (static_cast<uint32_t>(in_buffer_[6]) << 16) |
                                        (static_cast<uint32_t>(in_buffer_[7]) << 24);
        if (in_buffer_.size() < 8 + json_length + payload_length) break;

        std::string json(reinterpret_cast<const char*>(in_buffer_.data() + 8), json_length);
        std::vector<uint8_t> payload(in_buffer_.begin() + 8 + json_length,
                                     in_buffer_.begin() + 8 + json_length + payload_length);
        in_buffer_.erase(in_buffer_.begin(), in_buffer_.begin() + 8 + json_length + payload_length);
        handle_message(json, payload);
    }
    return received;
}

void PreviewServer::flush_output() {
    if (client_ == 0 || out_buffer_.empty()) return;
    for (;;) {
        const int sent = send(static_cast<socket_t>(client_),
                              reinterpret_cast<const char*>(out_buffer_.data()),
                              static_cast<int>(out_buffer_.size()), 0);
        if (sent > 0) {
            out_buffer_.erase(out_buffer_.begin(), out_buffer_.begin() + sent);
            if (out_buffer_.empty()) return;
            continue;
        }
        if (sent < 0 && would_block()) return;  // try again later
        BGE_WARN("Preview server: the client stopped reading (send failed)");
        close_socket(static_cast<socket_t>(client_));
        client_ = 0;
        out_buffer_.clear();
        return;
    }
}

void PreviewServer::poll() {
    if (listening_ == 0) return;

    if (client_ == 0) {
        const socket_t accepted = accept(static_cast<socket_t>(listening_), nullptr, nullptr);
        if (accepted != kInvalidSocket) {
            // Non blocking, so the render thread never stalls on a client that
            // is only listening.
#if defined(_WIN32)
            u_long mode = 1;
            ioctlsocket(accepted, FIONBIO, &mode);
#else
            fcntl(accepted, F_SETFL, fcntl(accepted, F_GETFL, 0) | O_NONBLOCK);
#endif
            client_ = static_cast<uintptr_t>(accepted);
            in_buffer_.clear();
            out_buffer_.clear();
            client_shared_ = false;
            BGE_INFO("Preview server: client connected");
        }
    }

    handle_input();
    flush_output();

    // A render request is answered only after a *new* frame was produced, so a
    // resize or a scene push is always reflected in the answer.
    if (render_pending_ && frame_serial_ > render_serial_) {
        render_pending_ = false;
        send_frame();
        BGE_INFO("Preview server: sending frame %llu",
                 static_cast<unsigned long long>(frame_serial_));
        flush_output();
    }
}

} // namespace bge
