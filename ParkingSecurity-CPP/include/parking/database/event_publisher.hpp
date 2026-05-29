#pragma once

#include "parking/core/types.hpp"
#include "parking/database/event_repository.hpp"
#include "parking/schedule/schedule_checker.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace parking::database {

/// Turns a stream of RecognitionResult callbacks from CameraPipeline into
/// durable events in the Postgres `events` table, applying:
///   - schedule check for students (employee always authorized)
///   - per-person cooldown to avoid one row per frame
///   - optional UNKNOWN suppression so tiny-face junk doesn't flood the log
///
/// Thread-safety: handle() is safe to call from the pipeline's probe thread.
class EventPublisher {
public:
    struct Config {
        std::string          camera_id{"cam-entrance-01"};
        std::chrono::seconds cooldown{10};
        bool                 write_unknowns{true};

        /// If non-empty, events are POSTed here as JSON (Python backend API),
        /// and the backend handles the Postgres insert + WebSocket broadcast
        /// to the dashboard. If empty, we fall back to direct DB insert via
        /// the repository — dashboard won't see events live without a refresh.
        std::string backend_url;
        std::string backend_api_key;

        /// If set, called to get the most recent full-frame JPEG. The publisher
        /// base64-encodes the result for the event's `frame_image` field and
        /// crops the face bbox for `face_image` — so the dashboard can show
        /// thumbnails without us maintaining an on-disk snapshots cache.
        std::function<std::vector<uint8_t>()> frame_provider;
    };

    EventPublisher(EventRepository&            repo,
                   schedule::ScheduleChecker&  sched,
                   Config                      config);
    ~EventPublisher();

    /// Plug into CameraPipeline::set_recognition_callback(...).
    void handle(const RecognitionResult& rec);

private:
    /// Try to POST the event to the Python backend. Returns true on HTTP 2xx.
    bool post_to_backend(const std::string& body) const;

    /// Fire a single event (schedule check + JSON build + POST or DB insert).
    /// MUST be called without holding `mu_` — it does slow I/O (HTTP,
    /// OpenCV encode) and shouldn't block concurrent handle() calls.
    /// `frame_jpeg` is the full-frame capture taken at the moment `rec` was
    /// observed (may be empty if no frame_provider was wired).
    void fire_event_nolock(const RecognitionResult& rec,
                            float effective_score,
                            const std::vector<uint8_t>& frame_jpeg);

    /// Background worker that periodically drains pending entries whose
    /// aggregation window has elapsed, then fires their events lock-free.
    void worker_loop();

    EventRepository&           repo_;
    schedule::ScheduleChecker& sched_;
    Config                     config_;

    // One pending entry per cooldown key. An event only fires AFTER the
    // aggregation window (~1.5 s) so we can pick whichever detection during
    // that window had the best classification.
    //
    // We also snapshot the full-frame JPEG at the moment the best sample was
    // chosen: the bbox in `best` is valid only against that frame, so we
    // mustn't re-grab the latest frame at flush time (the face may have
    // moved out of the bbox by then).
    struct Pending {
        RecognitionResult                      best;
        std::chrono::steady_clock::time_point  first_seen;
        std::vector<uint8_t>                   best_frame_jpeg;
    };

    // Rolling buffer of recent unknowns, so "same stranger, new track id"
    // collapses into one event via cosine-similarity matching.
    struct UnknownSample {
        Embedding                              embedding;
        std::chrono::steady_clock::time_point  last_seen;
    };

    std::mutex                                                             mu_;
    std::condition_variable                                                cv_;
    std::atomic<bool>                                                      running_{true};
    std::thread                                                            worker_;

    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_fired_;
    std::unordered_map<std::string, float>                                 best_score_;
    std::unordered_map<std::string, Pending>                               pending_;
    std::vector<UnknownSample>                                             recent_unknowns_;
};

}  // namespace parking::database
