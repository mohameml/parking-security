// Minimal IoU-based multi-face tracker. Greedy matching between the newest
// batch of detections and the open tracks; no Kalman filter. Sufficient for
// faces because they don't move across more than a few bbox widths per frame
// at typical camera FPS.
//
// Lifecycle per track:
//   - New detection with no good IoU match → create a new Track.
//   - Match (IoU ≥ iou_threshold) → update bbox, reset age, hit_streak++.
//   - No match this frame → age++. Delete when age > max_age.
//   - A track is "confirmed" once hit_streak ≥ min_hits. Unconfirmed tracks
//     still get returned so callers can decide whether to surface them.

#include "parking/tracking/face_tracker.hpp"

#include <algorithm>
#include <vector>

namespace parking::tracking {

namespace {

struct Internal {
    uint64_t id{0};
    BBox     bbox{};
    int      age{0};
    int      hit_streak{0};
    bool     confirmed{false};
};

float iou(const BBox& a, const BBox& b) {
    const int ix1 = std::max(a.x1, b.x1);
    const int iy1 = std::max(a.y1, b.y1);
    const int ix2 = std::min(a.x2, b.x2);
    const int iy2 = std::min(a.y2, b.y2);
    const int iw  = std::max(0, ix2 - ix1);
    const int ih  = std::max(0, iy2 - iy1);
    const int inter = iw * ih;
    const int area_a = std::max(0, a.x2 - a.x1) * std::max(0, a.y2 - a.y1);
    const int area_b = std::max(0, b.x2 - b.x1) * std::max(0, b.y2 - b.y1);
    const int un    = area_a + area_b - inter;
    return un > 0 ? static_cast<float>(inter) / static_cast<float>(un) : 0.0f;
}

}  // namespace

class FaceTracker::Impl {
public:
    Config                config;
    uint64_t              next_track_id{1};
    std::vector<Internal> tracks;
};

FaceTracker::FaceTracker() : impl_(std::make_unique<Impl>()) {}

FaceTracker::FaceTracker(const Config& config) : impl_(std::make_unique<Impl>()) {
    impl_->config = config;
}

FaceTracker::~FaceTracker() = default;

std::vector<FaceTracker::Track> FaceTracker::update(const std::vector<Input>& detections) {
    auto& T = impl_->tracks;
    const auto& cfg = impl_->config;

    // Age every existing track; reset to 0 on a match below.
    for (auto& t : T) t.age++;

    // Build every (track_i, det_j) IoU pair above the threshold.
    struct Pair { int ti; int di; float score; };
    std::vector<Pair> pairs;
    pairs.reserve(T.size() * detections.size());
    for (std::size_t i = 0; i < T.size(); ++i) {
        for (std::size_t j = 0; j < detections.size(); ++j) {
            const float s = iou(T[i].bbox, detections[j].bbox);
            if (s >= cfg.iou_threshold) {
                pairs.push_back({static_cast<int>(i), static_cast<int>(j), s});
            }
        }
    }

    // Greedy: repeatedly take the highest-IoU unassigned pair.
    std::sort(pairs.begin(), pairs.end(),
              [](const Pair& a, const Pair& b) { return a.score > b.score; });
    std::vector<bool> track_used(T.size(), false);
    std::vector<bool> det_used(detections.size(), false);
    for (const auto& p : pairs) {
        if (track_used[p.ti] || det_used[p.di]) continue;
        auto& t = T[p.ti];
        t.bbox       = detections[p.di].bbox;
        t.age        = 0;
        t.hit_streak = t.hit_streak + 1;
        if (t.hit_streak >= cfg.min_hits) t.confirmed = true;
        track_used[p.ti] = true;
        det_used[p.di]   = true;
    }

    // Unmatched detections become new tracks.
    for (std::size_t j = 0; j < detections.size(); ++j) {
        if (det_used[j]) continue;
        Internal n{};
        n.id         = impl_->next_track_id++;
        n.bbox       = detections[j].bbox;
        n.age        = 0;
        n.hit_streak = 1;
        n.confirmed  = (cfg.min_hits <= 1);
        T.push_back(n);
    }

    // Drop tracks that have been lost for too long.
    T.erase(std::remove_if(T.begin(), T.end(),
                           [&](const Internal& t) { return t.age > cfg.max_age; }),
            T.end());

    // Return all currently active tracks.
    std::vector<Track> out;
    out.reserve(T.size());
    for (const auto& t : T) {
        out.push_back(Track{t.id, t.bbox, t.age, t.hit_streak, t.confirmed});
    }
    return out;
}

void FaceTracker::reset() {
    impl_->tracks.clear();
    impl_->next_track_id = 1;
}

}  // namespace parking::tracking
