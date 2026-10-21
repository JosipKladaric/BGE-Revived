// BGE Revived - frame timing / fixed timestep helpers.
#pragma once

#include <chrono>
#include <cstdint>

namespace bge {

class Clock {
public:
    Clock() : start_(std::chrono::steady_clock::now()), last_(start_) {}

    double elapsed() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

    double tick() {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last_).count();
        last_ = now;
        return dt;
    }

    void reset() { start_ = last_ = std::chrono::steady_clock::now(); }

private:
    std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::time_point last_;
};

// Converts variable frame deltas into a fixed-rate simulation. Rendering runs
// as fast as possible; gameplay/physics always see a constant dt.
class FixedTimestep {
public:
    explicit FixedTimestep(double hz = 120.0) : step_(1.0 / hz) {}

    void set_rate(double hz) { step_ = 1.0 / hz; }
    double step() const { return step_; }

    template <typename Fn>
    int consume(double frame_dt, Fn&& fixed_update) {
        // Avoid the spiral of death after a breakpoint or window drag.
        if (frame_dt > 0.25) frame_dt = 0.25;
        accumulator_ += frame_dt;
        int steps = 0;
        while (accumulator_ >= step_ && steps < max_steps_) {
            fixed_update(step_);
            accumulator_ -= step_;
            ++steps;
        }
        if (steps == max_steps_) accumulator_ = 0.0;
        return steps;
    }

    double alpha() const { return accumulator_ / step_; }

private:
    double step_ = 1.0 / 120.0;
    double accumulator_ = 0.0;
    int max_steps_ = 8;
};

} // namespace bge
