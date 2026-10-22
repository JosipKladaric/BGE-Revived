#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bge {

// A frame buffer shared between two processes.
//
// The engine renders into a named shared memory region and an authoring tool
// (the Blender add-on) reads from the same region. Only the frame *pixels* move
// this way; the small JSON control messages stay on the preview socket, so the
// socket never has to carry several megabytes per frame.
//
// Layout: a 4 KiB header followed by the pixel area.
//
//     offset  size  field
//     0       4     magic    "BGE1"
//     4       4     version
//     8       4     header size (4096)
//     12      4     capacity, in bytes of pixel storage
//     16      4     width
//     20      4     height
//     24      4     stride (width * 4)
//     28      4     flags (bit 0: a frame has been published)
//     32      8     serial
//     40      8     frames published
//     48      4     writer process id
//     52      4     reserved
//
// The writer publishes with the classic seqlock protocol, so a reader never
// observes a half written frame:
//
//     writer: serial = odd, write the pixels, serial = even
//     reader: s1 = serial (even), copy the pixels, s2 = serial, s1 == s2
//
// tools/preview_client.py mirrors this layout; keep the two in sync.
class SharedFrame {
public:
    static constexpr uint32_t kMagic = 0x31454742u;  // "BGE1", little endian
    static constexpr uint32_t kVersion = 1;
    static constexpr uint32_t kHeaderSize = 4096;

    SharedFrame();
    ~SharedFrame();

    SharedFrame(const SharedFrame&) = delete;
    SharedFrame& operator=(const SharedFrame&) = delete;

    // Creates the region, or attaches to it when another process already
    // created one with this name. The region always has the same size, so both
    // sides agree on the layout.
    bool create(const std::string& name, size_t capacity);
    void close();

    bool is_open() const { return view_ != nullptr; }
    const std::string& name() const { return name_; }
    size_t capacity() const { return capacity_; }

    // Writer: publishes one frame. Returns false when the frame does not fit or
    // the region is closed; the caller then falls back to the socket.
    bool publish(int width, int height, const void* rgba, size_t bytes);

    // Reader: copies the newest frame into `out`. Returns false when no
    // complete frame is available, or when the frame is still the one named by
    // `skip_serial` (pass 0 to always read).
    bool read(int& width, int& height, std::vector<uint8_t>& out, uint64_t* serial = nullptr,
              uint64_t skip_serial = 0);

    // The serial of the last published frame (even once the write finished).
    uint64_t serial() const;
    uint64_t frames() const;

private:
    std::string name_;
    size_t capacity_ = 0;
    void* view_ = nullptr;
#if defined(_WIN32)
    void* mapping_ = nullptr;
#else
    int fd_ = -1;
#endif
};

} // namespace bge
