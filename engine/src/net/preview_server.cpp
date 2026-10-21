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

void append_u32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
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
    in_buffer_.clear();
    out_buffer_.clear();
    has_pending_scene_ = false;
}

void PreviewServer::set_frame(int width, int height, const std::vector<uint8_t>& rgba) {
    frame_width_ = width;
    frame_height_ = height;
    frame_pixels_ = rgba;
    ++frame_serial_;
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

    if (command == "hello") {
        queue("{\"cmd\":\"hello\",\"ok\":true,\"version\":1,\"width\":" + std::to_string(frame_width_) +
              ",\"height\":" + std::to_string(frame_height_) + "}");
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
            return;
        }
        if (frame_pixels_.empty()) {
            queue("{\"cmd\":\"frame\",\"ok\":false}");
            return;
        }
        char header[128];
        std::snprintf(header, sizeof(header),
                      "{\"cmd\":\"frame\",\"ok\":true,\"width\":%d,\"height\":%d,\"bytes\":%zu}",
                      frame_width_, frame_height_, frame_pixels_.size());
        queue(header, &frame_pixels_);
        ++frames_served_;
    } else if (command == "resize") {
        const std::string width = json_field(json, "width");
        const std::string height = json_field(json, "height");
        resize_width_ = std::atoi(width.c_str());
        resize_height_ = std::atoi(height.c_str());
        resize_pending_ = resize_width_ > 0 && resize_height_ > 0;
        queue("{\"cmd\":\"resize\",\"ok\":true}");
    }
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
        break;  // would block or an error: try again later
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
            client_ = static_cast<uintptr_t>(accepted);
            in_buffer_.clear();
            out_buffer_.clear();
            BGE_INFO("Preview server: client connected");
        }
    }

    handle_input();
    flush_output();

    // A render request is answered only after a *new* frame was produced, so a
    // resize or a scene push is always reflected in the answer.
    if (render_pending_ && !frame_pixels_.empty() && frame_serial_ > render_serial_) {
        char header[128];
        std::snprintf(header, sizeof(header),
                      "{\"cmd\":\"frame\",\"ok\":true,\"width\":%d,\"height\":%d,\"bytes\":%zu}",
                      frame_width_, frame_height_, frame_pixels_.size());
        queue(header, &frame_pixels_);
        render_pending_ = false;
        ++frames_served_;
        flush_output();
    }
}

} // namespace bge
