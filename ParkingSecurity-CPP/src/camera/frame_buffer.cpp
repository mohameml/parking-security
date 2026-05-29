#include "parking/camera/frame_buffer.hpp"

namespace parking::camera {

FrameBuffer::FrameBuffer()  = default;
FrameBuffer::~FrameBuffer() = default;

void FrameBuffer::push(Frame frame) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_           = std::move(frame);
        last_sequence_   = frame_->sequence;
    }
    cv_.notify_all();
}

std::optional<Frame> FrameBuffer::latest() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return frame_;
}

bool FrameBuffer::wait_for_new(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto current = last_sequence_;
    return cv_.wait_for(lock, timeout, [&]() { return last_sequence_ != current; });
}

void FrameBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    frame_.reset();
}

}  // namespace parking::camera
