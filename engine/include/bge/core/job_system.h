// BGE Revived - lightweight job system.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace bge {

// A small fork-join thread pool. parallel_for is intended for data-parallel work
// (transform updates, culling, animation sampling). Calls may not be nested.
class JobSystem {
public:
    explicit JobSystem(uint32_t worker_count = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    uint32_t worker_count() const;
    uint32_t thread_count() const;

    void parallel_for(uint32_t begin, uint32_t end, uint32_t grain_size,
                      const std::function<void(uint32_t, uint32_t)>& fn);

    void submit(std::function<void()> job);
    void wait_idle();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bge
