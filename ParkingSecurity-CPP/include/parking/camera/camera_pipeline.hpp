#pragma once

#include "parking/core/types.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace parking::inference { class EmbeddingIndex; }

namespace parking::camera {

/// Owns the GStreamer (optionally DeepStream) pipeline that pulls frames
/// from the physical camera, pushes them through the face detector +
/// recognizer, and emits RecognitionResults.
///
/// The pipeline runs on its own thread; callers interact with it via
/// start()/stop() and subscribe callbacks.
class CameraPipeline {
public:
    /// Callback fired for every detected face in every processed frame.
    using DetectionCallback = std::function<void(const Detection&)>;

    /// Callback fired once per Detection when an EmbeddingIndex is attached
    /// and the face matches (or fails to match) an enrolled person.
    using RecognitionCallback = std::function<void(const RecognitionResult&)>;

    /// Callback fired each time the annotated frame is ready for display.
    /// The frame pointer is only valid for the duration of the callback —
    /// copy it if you need it later.
    using FrameCallback = std::function<void(const Frame&)>;

    /// Callback fired on fatal pipeline errors (before shutdown).
    using ErrorCallback = std::function<void(const std::string&)>;

    explicit CameraPipeline(CameraConfig config);
    ~CameraPipeline();

    CameraPipeline(const CameraPipeline&) = delete;
    CameraPipeline& operator=(const CameraPipeline&) = delete;

    void set_detection_callback(DetectionCallback cb) noexcept;
    void set_recognition_callback(RecognitionCallback cb) noexcept;
    void set_frame_callback(FrameCallback cb) noexcept;
    void set_error_callback(ErrorCallback cb) noexcept;

    /// Attach an in-memory embedding index. The pipeline calls query() on it
    /// for each live embedding; if the top match exceeds `threshold`, fires
    /// the recognition callback with the match; otherwise with event_type=
    /// Unknown. Pointer must outlive the pipeline.
    void set_embedding_index(const inference::EmbeddingIndex* index,
                             float recognition_threshold = 0.5f) noexcept;

    /// Start the pipeline on a background thread. Returns false if construction failed.
    bool start();

    /// Stop and join the pipeline thread.
    void stop();

    bool is_running() const noexcept { return running_.load(); }

    /// Capture a single snapshot (JPEG bytes) from the most recent frame.
    /// Useful for event persistence.
    std::vector<uint8_t> capture_snapshot(int quality = 85);

    /// Return a copy of the most recently encoded JPEG frame (same frames
    /// the MJPEG server streams — i.e. with OSD bboxes+labels drawn).
    /// Empty vector if the jpeg appsink isn't running yet. Thread-safe.
    std::vector<uint8_t> latest_frame_jpeg() const;

    /// Current performance counters.
    struct Stats {
        uint64_t frames_captured{0};
        uint64_t frames_processed{0};
        uint64_t detections{0};
        double   avg_latency_ms{0.0};
        double   current_fps{0.0};
    };
    Stats stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool>     running_{false};
};

}  // namespace parking::camera
