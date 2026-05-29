#pragma once

#include "parking/core/types.hpp"

#include <memory>
#include <vector>

namespace parking::tracking {

/// Assigns persistent track IDs to detections across frames using a Kalman
/// filter + Hungarian algorithm for association (SORT-style tracking).
///
/// Benefits over raw per-frame detection:
/// - Each person keeps the same track_id as they move through the frame
/// - Missed frames don't cause duplicate events (cooldown is per-track)
/// - Smoother bounding boxes via motion prediction
class FaceTracker {
public:
    struct Config {
        /// Min IoU for a detection to associate with an existing track.
        float iou_threshold = 0.3f;
        /// How many frames to keep a lost track alive before deleting it.
        int   max_age = 30;
        /// Min consecutive detections before a track is confirmed.
        int   min_hits = 3;
    };

    struct Input {
        BBox  bbox;
        float detection_score;
    };

    struct Track {
        uint64_t track_id;
        BBox     bbox;
        int      age;            ///< Frames since last detection
        int      hit_streak;     ///< Consecutive hits
        bool     confirmed;      ///< True once hit_streak >= min_hits
    };

    FaceTracker();
    explicit FaceTracker(const Config& config);
    ~FaceTracker();

    /// Step the tracker forward with a new set of detections. Returns the
    /// current set of active tracks (both matched and predicted).
    std::vector<Track> update(const std::vector<Input>& detections);

    /// Reset all tracks (e.g., camera reconnect).
    void reset();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace parking::tracking
