#include "bge/net/shared_frame.h"

#include "bge/core/log.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace bge {
namespace {

// The on disk layout. Both processes cast the mapping to this struct, so the
// field order and the offsets are part of the protocol.
struct SharedHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t capacity;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t flags;
    uint64_t serial;
    uint64_t frames;
    uint32_t writer_pid;
    uint32_t reserved;
};

static_assert(sizeof(SharedHeader) <= SharedFrame::kHeaderSize, "the header does not fit");
static_assert(offsetof(SharedHeader, magic) == 0, "layout drift");
static_assert(offsetof(SharedHeader, version) == 4, "layout drift");
static_assert(offsetof(SharedHeader, header_size) == 8, "layout drift");
static_assert(offsetof(SharedHeader, capacity) == 12, "layout drift");
static_assert(offsetof(SharedHeader, width) == 16, "layout drift");
static_assert(offsetof(SharedHeader, height) == 20, "layout drift");
static_assert(offsetof(SharedHeader, stride) == 24, "layout drift");
static_assert(offsetof(SharedHeader, flags) == 28, "layout drift");
static_assert(offsetof(SharedHeader, serial) == 32, "layout drift");
static_assert(offsetof(SharedHeader, frames) == 40, "layout drift");
static_assert(offsetof(SharedHeader, writer_pid) == 48, "layout drift");

uint32_t process_id() {
#if defined(_WIN32)
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(getpid());
#endif
}

#if defined(_WIN32)
std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
    wide.resize(static_cast<size_t>(length - 1));
    return wide;
}
#endif

} // namespace

SharedFrame::SharedFrame() = default;

SharedFrame::~SharedFrame() {
    close();
}

bool SharedFrame::create(const std::string& name, size_t capacity) {
    close();
    if (name.empty() || capacity == 0) return false;

    const size_t total = kHeaderSize + capacity;
    bool created = true;

#if defined(_WIN32)
    const std::wstring wide = widen(name);
    if (wide.empty()) return false;
    SetLastError(0);
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                        static_cast<DWORD>(total >> 32),
                                        static_cast<DWORD>(total & 0xFFFFFFFFu), wide.c_str());
    if (mapping == nullptr) {
        BGE_WARN("Shared frame: cannot create '%s' (error %lu)", name.c_str(), GetLastError());
        return false;
    }
    created = GetLastError() != ERROR_ALREADY_EXISTS;
    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, total);
    if (view == nullptr) {
        BGE_WARN("Shared frame: cannot map '%s' (error %lu)", name.c_str(), GetLastError());
        CloseHandle(mapping);
        return false;
    }
    mapping_ = mapping;
    view_ = view;
#else
    const int fd = shm_open(name.c_str(), O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd < 0) {
        // Already there: attach to the region the other process created.
        const int existing = shm_open(name.c_str(), O_RDWR, 0600);
        if (existing < 0) {
            BGE_WARN("Shared frame: cannot open '%s'", name.c_str());
            return false;
        }
        created = false;
        fd_ = existing;
    } else {
        fd_ = fd;
    }
    if (ftruncate(fd_, static_cast<off_t>(total)) != 0) {
        BGE_WARN("Shared frame: cannot size '%s'", name.c_str());
        close();
        return false;
    }
    void* view = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (view == MAP_FAILED) {
        BGE_WARN("Shared frame: cannot map '%s'", name.c_str());
        close();
        return false;
    }
    view_ = view;
#endif

    name_ = name;
    capacity_ = capacity;

    // A fresh region starts empty; an existing one keeps the frame the other
    // process published.
    auto* header = static_cast<SharedHeader*>(view_);
    if (created || header->magic != kMagic || header->version != kVersion) {
        std::memset(view_, 0, kHeaderSize);
        header->magic = kMagic;
        header->version = kVersion;
        header->header_size = kHeaderSize;
        header->capacity = static_cast<uint32_t>(capacity > 0xFFFFFFFFull ? 0xFFFFFFFFull : capacity);
        header->writer_pid = process_id();
    }
    return true;
}

void SharedFrame::close() {
    if (view_ != nullptr) {
#if defined(_WIN32)
        UnmapViewOfFile(view_);
        if (mapping_ != nullptr) CloseHandle(static_cast<HANDLE>(mapping_));
        mapping_ = nullptr;
#else
        munmap(view_, kHeaderSize + capacity_);
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
#endif
        view_ = nullptr;
    }
    capacity_ = 0;
    name_.clear();
}

bool SharedFrame::publish(int width, int height, const void* rgba, size_t bytes) {
    if (view_ == nullptr || width <= 0 || height <= 0 || rgba == nullptr) return false;

    const size_t stride = static_cast<size_t>(width) * 4;
    const size_t needed = stride * static_cast<size_t>(height);
    if (needed > capacity_ || bytes < needed) return false;

    auto* header = static_cast<SharedHeader*>(view_);
    auto* pixels = static_cast<uint8_t*>(view_) + kHeaderSize;
    std::atomic_ref<uint64_t> serial(header->serial);

    const uint64_t start = serial.load(std::memory_order_relaxed);
    // Odd serial: a reader that overlaps this write will retry.
    serial.store(start + 1, std::memory_order_seq_cst);
    std::atomic_thread_fence(std::memory_order_release);

    header->width = static_cast<uint32_t>(width);
    header->height = static_cast<uint32_t>(height);
    header->stride = static_cast<uint32_t>(stride);
    header->flags = 1;
    std::memcpy(pixels, rgba, needed);

    // Even serial: the frame is complete and consistent.
    serial.store(start + 2, std::memory_order_seq_cst);
    std::atomic_ref<uint64_t>(header->frames).fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool SharedFrame::read(int& width, int& height, std::vector<uint8_t>& out, uint64_t* serial_out,
                       uint64_t skip_serial) {
    if (view_ == nullptr) return false;

    auto* header = static_cast<SharedHeader*>(view_);
    const auto* pixels = static_cast<const uint8_t*>(view_) + kHeaderSize;
    std::atomic_ref<uint64_t> serial(header->serial);

    // The writer only takes a few milliseconds, so a handful of attempts is
    // plenty; giving up is better than stalling the caller. The short sleep
    // matters: a tight retry loop would burn through every attempt while the
    // writer is still copying the pixels.
    for (int attempt = 0; attempt < 50; ++attempt) {
        const uint64_t first = serial.load(std::memory_order_acquire);
        if ((first & 1u) != 0) {  // a write is in progress
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            continue;
        }
        if (first == 0 || (header->flags & 1u) == 0) return false;
        if (skip_serial != 0 && first == skip_serial) return false;

        const uint32_t frame_width = header->width;
        const uint32_t frame_height = header->height;
        const uint32_t frame_stride = header->stride;
        if (frame_width == 0 || frame_height == 0 || frame_stride < frame_width * 4) return false;

        const size_t needed = static_cast<size_t>(frame_stride) * frame_height;
        if (needed > capacity_) return false;

        out.resize(needed);
        std::memcpy(out.data(), pixels, needed);

        if (serial.load(std::memory_order_acquire) != first) continue;  // torn read

        width = static_cast<int>(frame_width);
        height = static_cast<int>(frame_height);
        if (serial_out != nullptr) *serial_out = first;
        return true;
    }
    return false;
}

uint64_t SharedFrame::serial() const {
    if (view_ == nullptr) return 0;
    auto* header = static_cast<SharedHeader*>(view_);
    return std::atomic_ref<uint64_t>(header->serial).load(std::memory_order_acquire);
}

uint64_t SharedFrame::frames() const {
    if (view_ == nullptr) return 0;
    auto* header = static_cast<SharedHeader*>(view_);
    return std::atomic_ref<uint64_t>(header->frames).load(std::memory_order_relaxed);
}

} // namespace bge
