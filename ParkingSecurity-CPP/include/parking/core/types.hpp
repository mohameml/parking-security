#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace parking {

/// A 512-dimensional face embedding vector.
using Embedding = std::array<float, 512>;

/// High-resolution timestamp (system_clock, microsecond precision).
using Timestamp = std::chrono::time_point<std::chrono::system_clock, std::chrono::microseconds>;

/// Bounding box in pixel coordinates.
struct BBox {
    int32_t x1{0};
    int32_t y1{0};
    int32_t x2{0};
    int32_t y2{0};

    constexpr int32_t width() const noexcept  { return x2 - x1; }
    constexpr int32_t height() const noexcept { return y2 - y1; }
    constexpr int32_t area() const noexcept   { return width() * height(); }
};

/// Types of detection events emitted by the camera pipeline.
enum class EventType : uint8_t {
    Authorized,    ///< Known person with valid access at this time
    WrongTime,     ///< Known person but not scheduled for this time slot
    WrongDay,      ///< Known person but not scheduled for today
    Unknown,       ///< Face not in the enrolled database
};

std::string_view to_string(EventType type) noexcept;

/// Which kind of enrolled person matched.
enum class PersonType : uint8_t {
    Employee,
    Student,
};

/// A single face detection from one frame.
struct Detection {
    uint64_t   track_id{0};       ///< Persistent track across frames
    BBox       bbox{};
    float      detection_score{0.0f};  ///< SCRFD confidence (0..1)
    Embedding  embedding{};       ///< ArcFace 512-dim, unnormalized
    Timestamp  captured_at{};
};

/// A recognition result, produced by matching a Detection against the database.
struct RecognitionResult {
    Detection                   detection;
    EventType                   event_type{EventType::Unknown};
    std::optional<std::string>  person_id;         ///< UUID / NODOS of the matched person
    std::optional<std::string>  display_name;
    std::optional<PersonType>   person_type;
    float                       similarity_score{0.0f};  ///< Cosine similarity, [-1, 1]
    std::optional<std::string>  schedule_info;     ///< Human-readable schedule detail
};

/// A camera frame held on the GPU (via DeepStream NvBufSurface) or CPU.
struct Frame {
    uint64_t   sequence{0};
    Timestamp  captured_at{};
    int32_t    width{0};
    int32_t    height{0};
    /// Opaque handle: if zero-copy GPU frame, this points to an NvBufSurface;
    /// otherwise it owns a CPU buffer managed by the pipeline.
    void*      buffer_handle{nullptr};
};

/// Configuration passed to long-running services at startup.
struct CameraConfig {
    std::string  source_uri;       ///< v4l2:///dev/video0 or rtsp://...
    std::string  camera_id;        ///< Identifier stored on events
    int32_t      width{1920};
    int32_t      height{1080};
    int32_t      target_fps{30};
    bool         mjpeg_input{true};

    /// If true, insert a DeepStream `nvinfer` SCRFD detector into the pipeline
    /// and fire DetectionCallback for every face found. Requires
    /// detector_config_path to point at a valid nvinfer config file.
    bool         enable_detector{false};
    std::string  detector_config_path;  ///< Path to face_detection.txt

    /// If true, enable the in-process IoU tracker so each face gets a
    /// persistent track_id across frames. (We bypass DeepStream's nvtracker.)
    bool         enable_tracker{false};
    /// Unused in the C++ IoU tracker path; kept for eventual nvtracker revisit.
    std::string  tracker_config_path;
    std::string  tracker_ll_lib_path{
        "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so"};
    int32_t      tracker_width{640};
    int32_t      tracker_height{384};

    /// If true, insert a secondary `nvinfer` (ArcFace) after the detector so
    /// each detected face gets a 512-dim embedding attached as user meta.
    bool         enable_recognizer{false};
    std::string  recognizer_config_path;  ///< Path to face_recognition.txt

    /// If true, add `nvdsosd + tee + jpegenc` so bounding boxes are drawn on
    /// a parallel branch of the pipeline and JPEG frames are pushed to the
    /// MjpegServer listening on `stream_port`.
    bool         enable_stream{false};
    int32_t      stream_port{8090};
    int32_t      stream_jpeg_quality{70};
};

}  // namespace parking
