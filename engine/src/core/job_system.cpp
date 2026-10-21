#include "bge/core/job_system.h"

#include "bge/core/log.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace bge {

struct JobSystem::Impl {
    std::vector<std::thread> workers;
    std::deque<std::function<void()>> queue;
    mutable std::mutex mutex;
    std::condition_variable work_cv;
    std::condition_variable done_cv;
    std::atomic<int> pending{0};
    std::atomic<bool> stop{false};
    uint32_t worker_count = 0;

    void worker_loop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                work_cv.wait(lock, [this] { return stop.load(std::memory_order_acquire) || !queue.empty(); });
                if (stop.load(std::memory_order_acquire) && queue.empty()) return;
                job = std::move(queue.front());
                queue.pop_front();
            }
            job();
            if (pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard<std::mutex> lock(mutex);
                done_cv.notify_all();
            }
        }
    }

    // Execute one queued job on the calling thread, or return false if empty.
    bool try_run_one() {
        std::function<void()> job;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (queue.empty()) return false;
            job = std::move(queue.front());
            queue.pop_front();
        }
        job();
        if (pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> lock(mutex);
            done_cv.notify_all();
        }
        return true;
    }

    void push(std::function<void()> job) {
        pending.fetch_add(1, std::memory_order_acq_rel);
        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.push_back(std::move(job));
        }
        work_cv.notify_one();
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        done_cv.wait(lock, [this] { return pending.load(std::memory_order_acquire) == 0; });
    }
};

JobSystem::JobSystem(uint32_t worker_count) : impl_(std::make_unique<Impl>()) {
    if (worker_count == 0) {
        uint32_t hw = std::thread::hardware_concurrency();
        worker_count = hw > 1 ? hw - 1 : 0;
    }
    impl_->worker_count = worker_count;
    impl_->workers.reserve(worker_count);
    for (uint32_t i = 0; i < worker_count; ++i) {
        impl_->workers.emplace_back([this] { impl_->worker_loop(); });
    }
    BGE_INFO("Job system: %u worker threads (%u total)", worker_count, worker_count + 1);
}

JobSystem::~JobSystem() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stop.store(true, std::memory_order_release);
    }
    impl_->work_cv.notify_all();
    for (auto& t : impl_->workers) {
        if (t.joinable()) t.join();
    }
}

uint32_t JobSystem::worker_count() const { return impl_->worker_count; }
uint32_t JobSystem::thread_count() const { return impl_->worker_count + 1; }

void JobSystem::parallel_for(uint32_t begin, uint32_t end, uint32_t grain_size,
                             const std::function<void(uint32_t, uint32_t)>& fn) {
    if (begin >= end) return;
    uint32_t count = end - begin;
    if (impl_->worker_count == 0 || count <= grain_size) {
        fn(begin, end);
        return;
    }

    uint32_t chunk = std::max<uint32_t>(grain_size, 1);
    uint32_t chunks = (count + chunk - 1) / chunk;
    chunks = std::min<uint32_t>(chunks, impl_->worker_count * 8);
    chunk = (count + chunks - 1) / chunks;

    for (uint32_t start = begin; start < end; start += chunk) {
        uint32_t stop = std::min(start + chunk, end);
        impl_->push([&fn, start, stop] { fn(start, stop); });
    }

    // The calling thread participates so the main thread is not idle.
    while (impl_->try_run_one()) {
    }
    impl_->wait();
}

void JobSystem::submit(std::function<void()> job) {
    impl_->push(std::move(job));
}

void JobSystem::wait_idle() { impl_->wait(); }

} // namespace bge
