#pragma once

#include "parking/core/types.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>

namespace parking::camera {

/// Single-producer, multi-consumer latest-frame buffer.
///
/// The camera thread calls `push()` with every new annotated frame. Readers
/// (UI, stream server, snapshot service) call `latest()` to get the most
/// recent frame without blocking the producer. Old frames are dropped.
///
/// This is a "triple-buffer-lite": writers never block, readers get the
/// latest complete frame, and there's no queueing.
class FrameBuffer {
public:
    FrameBuffer();
    ~FrameBuffer();

    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;

    /// Publish a new frame. Thread-safe. Never blocks.
    void push(Frame frame);

    /// Get the latest frame (copy). Returns nullopt if no frame has been pushed yet.
    std::optional<Frame> latest() const;

    /// Wait for a new frame to arrive. Returns false if timeout expires.
    bool wait_for_new(std::chrono::milliseconds timeout);

    /// Drop the stored frame (e.g., when source disconnects).
    void clear();

private:
    mutable std::mutex      mutex_;
    std::condition_variable cv_;
    std::optional<Frame>    frame_;
    uint64_t                last_sequence_{0};
};

}  // namespace parking::camera
