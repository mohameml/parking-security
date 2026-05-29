#include "parking/database/event_publisher.hpp"

#include "parking/core/logger.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <string>

namespace parking::database {

namespace {

// How long to buffer the first detection of a visit before firing the event.
// During this window, any better sample (higher score, or Authorized over
// Unknown) replaces the pending one — so the event matches what the overlay
// settled on once the face stabilised.
constexpr std::chrono::milliseconds kAggregationWindow{1500};

// Cosine threshold above which two unknown embeddings are treated as the
// same stranger for deduplication purposes. Also used to recognise a
// previously-unknown pending as "actually this known person" when we get a
// confident identification.
constexpr float kUnknownMatchThreshold = 0.65f;

std::string to_iso8601_utc(Timestamp ts) {
    using namespace std::chrono;
    const auto sys_tp = time_point_cast<system_clock::duration>(ts);
    const auto tt     = system_clock::to_time_t(sys_tp);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[40];
    std::snprintf(buf, sizeof(buf),
                  "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

std::size_t curl_discard_cb(void*, std::size_t size, std::size_t nmemb, void*) {
    return size * nmemb;
}

std::string base64_encode(const uint8_t* data, std::size_t len) {
    static constexpr char kAlpha[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (std::size_t i = 0; i < len; i += 3) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) v |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) v |= static_cast<uint32_t>(data[i + 2]);
        out.push_back(kAlpha[(v >> 18) & 0x3F]);
        out.push_back(kAlpha[(v >> 12) & 0x3F]);
        out.push_back(i + 1 < len ? kAlpha[(v >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < len ? kAlpha[ v        & 0x3F] : '=');
    }
    return out;
}

std::vector<uint8_t> crop_face_jpeg(const std::vector<uint8_t>& full_jpeg,
                                     const BBox& bbox) {
    if (full_jpeg.empty()) return {};
    cv::Mat frame = cv::imdecode(
        cv::Mat(1, static_cast<int>(full_jpeg.size()), CV_8UC1,
                const_cast<uint8_t*>(full_jpeg.data())),
        cv::IMREAD_COLOR);
    if (frame.empty()) return {};

    const int pad = 20;
    const int x1 = std::max(0, bbox.x1 - pad);
    const int y1 = std::max(0, bbox.y1 - pad);
    const int x2 = std::min(frame.cols, bbox.x2 + pad);
    const int y2 = std::min(frame.rows, bbox.y2 + pad);
    if (x2 <= x1 || y2 <= y1) return {};

    cv::Mat face = frame(cv::Rect(x1, y1, x2 - x1, y2 - y1));
    std::vector<uint8_t> out;
    const std::vector<int> params{cv::IMWRITE_JPEG_QUALITY, 85};
    if (!cv::imencode(".jpg", face, out, params)) return {};
    return out;
}

float cosine(const Embedding& a, const Embedding& b) {
    double da = 0.0, db = 0.0, dot = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        da  += a[i] * a[i];
        db  += b[i] * b[i];
        dot += a[i] * b[i];
    }
    if (da <= 0.0 || db <= 0.0) return 0.0f;
    return static_cast<float>(dot / (std::sqrt(da) * std::sqrt(db)));
}

/// Rank two RecognitionResult samples. Higher-scoring wins; a known match
/// always beats an unknown one, regardless of score.
bool is_better(const RecognitionResult& a, const RecognitionResult& b) {
    const bool a_known = a.event_type != EventType::Unknown;
    const bool b_known = b.event_type != EventType::Unknown;
    if (a_known != b_known) return a_known;
    return a.similarity_score > b.similarity_score;
}

}  // namespace

EventPublisher::EventPublisher(EventRepository&           repo,
                               schedule::ScheduleChecker& sched,
                               Config                     config)
    : repo_(repo), sched_(sched), config_(std::move(config)) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (!config_.backend_url.empty()) {
        PLOG_INFO(events, "EventPublisher posting to backend at {}", config_.backend_url);
    } else {
        PLOG_INFO(events, "EventPublisher using direct DB insert (no backend URL)");
    }
    worker_ = std::thread(&EventPublisher::worker_loop, this);
}

EventPublisher::~EventPublisher() {
    running_.store(false);
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    curl_global_cleanup();
}

void EventPublisher::worker_loop() {
    using namespace std::chrono;
    while (running_.load()) {
        // Collect snapshots of pending entries whose aggregation window has
        // elapsed. We copy the RecognitionResult out under the lock, update
        // last_fired_ + best_score_, drop the entry from pending_, and then
        // release the lock before doing slow work (HTTP POST, OpenCV crop).
        struct Task {
            std::string          key;
            RecognitionResult    rec;
            float                effective_score;
            std::vector<uint8_t> frame_jpeg;
        };
        std::vector<Task> tasks;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait_for(lock, milliseconds(200),
                         [this] { return !running_.load(); });
            if (!running_.load()) return;

            const auto now = steady_clock::now();
            for (auto it = pending_.begin(); it != pending_.end();) {
                if (now - it->second.first_seen < kAggregationWindow) {
                    ++it;
                    continue;
                }
                Task t;
                t.key        = it->first;
                t.rec        = it->second.best;
                t.frame_jpeg = std::move(it->second.best_frame_jpeg);
                t.effective_score = t.rec.similarity_score;
                const auto prev = best_score_.find(t.key);
                if (prev != best_score_.end() && prev->second > t.effective_score) {
                    t.effective_score = prev->second;
                }
                last_fired_[t.key] = now;
                best_score_[t.key] = t.effective_score;
                it = pending_.erase(it);
                tasks.push_back(std::move(t));
            }

            // GC stale cooldown entries while we have the lock.
            const auto stale_limit = std::chrono::minutes(5);
            for (auto it2 = last_fired_.begin(); it2 != last_fired_.end();) {
                if (now - it2->second > stale_limit) {
                    best_score_.erase(it2->first);
                    it2 = last_fired_.erase(it2);
                } else {
                    ++it2;
                }
            }
        }
        // Lock released — do the I/O heavy work here.
        for (auto& t : tasks) {
            (void)t.key;
            fire_event_nolock(t.rec, t.effective_score, t.frame_jpeg);
        }
    }
}

bool EventPublisher::post_to_backend(const std::string& body) const {
    CURL* h = curl_easy_init();
    if (!h) return false;

    const std::string url = config_.backend_url + "/api/events";
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!config_.backend_api_key.empty()) {
        const std::string h_api = "X-Api-Key: " + config_.backend_api_key;
        headers = curl_slist_append(headers, h_api.c_str());
    }

    curl_easy_setopt(h, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(h, CURLOPT_POST,           1L);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS,     body.c_str());
    curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(h, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION,  curl_discard_cb);
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS,     2000L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, 500L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL,       1L);

    const CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(h);

    if (rc != CURLE_OK) {
        PLOG_WARN(events, "POST /api/events failed: {} ({})",
                  curl_easy_strerror(rc), static_cast<int>(rc));
        return false;
    }
    if (status / 100 != 2) {
        PLOG_WARN(events, "POST /api/events returned HTTP {}", status);
        return false;
    }
    return true;
}

void EventPublisher::handle(const RecognitionResult& rec) {
    const bool is_known = rec.event_type != EventType::Unknown && rec.person_id;
    const bool has_embedding =
        std::any_of(rec.detection.embedding.begin(),
                    rec.detection.embedding.end(),
                    [](float v) { return v != 0.0f; });

    // Default cooldown key.
    std::string cooldown_key = is_known
        ? "person:" + *rec.person_id
        : "unknown:track:" + std::to_string(rec.detection.track_id);

    const auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(mu_);

    // ── Unknown dedup: collapse "same stranger, new track" into one key ──
    if (!is_known && has_embedding) {
        // Expire stale unknowns (>2 minutes untouched).
        const auto stale = std::chrono::minutes(2);
        recent_unknowns_.erase(
            std::remove_if(recent_unknowns_.begin(), recent_unknowns_.end(),
                           [&](const UnknownSample& u) { return now - u.last_seen > stale; }),
            recent_unknowns_.end());

        int   best_idx = -1;
        float best_sim = 0.0f;
        for (std::size_t i = 0; i < recent_unknowns_.size(); ++i) {
            const float s = cosine(recent_unknowns_[i].embedding, rec.detection.embedding);
            if (s > best_sim) { best_sim = s; best_idx = static_cast<int>(i); }
        }
        if (best_idx >= 0 && best_sim >= kUnknownMatchThreshold) {
            recent_unknowns_[best_idx].last_seen = now;
            cooldown_key = "unknown:emb:" + std::to_string(best_idx);
        } else {
            UnknownSample s{};
            s.embedding = rec.detection.embedding;
            s.last_seen = now;
            recent_unknowns_.push_back(s);
            cooldown_key = "unknown:emb:" + std::to_string(recent_unknowns_.size() - 1);
        }
    }

    // ── If we're still in cooldown for this key, just update best_score ──
    const auto fired_it = last_fired_.find(cooldown_key);
    if (fired_it != last_fired_.end() && now - fired_it->second < config_.cooldown) {
        auto& best = best_score_[cooldown_key];
        if (rec.similarity_score > best) best = rec.similarity_score;
        return;
    }

    // ── If an Authorized match arrives and we have a pending Unknown for the
    //    same physical face (matched by embedding), cancel the Unknown.
    //    Prevents the "unknown-then-authorized" double event. ──
    if (is_known && has_embedding) {
        for (auto it = pending_.begin(); it != pending_.end();) {
            const auto& other = it->second.best;
            const bool other_is_unknown = other.event_type == EventType::Unknown;
            const bool other_has_emb =
                std::any_of(other.detection.embedding.begin(),
                            other.detection.embedding.end(),
                            [](float v) { return v != 0.0f; });
            if (other_is_unknown && other_has_emb &&
                cosine(other.detection.embedding, rec.detection.embedding)
                    >= kUnknownMatchThreshold) {
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // ── Add or upgrade the pending entry for this key ──
    // Snapshot the full frame now: the bbox in `rec` only maps onto THIS
    // frame, not whatever frame is live when the worker flushes 1.5 s later.
    std::vector<uint8_t> snapshot;
    if (config_.frame_provider) snapshot = config_.frame_provider();

    auto p_it = pending_.find(cooldown_key);
    if (p_it == pending_.end()) {
        Pending p{};
        p.best            = rec;
        p.first_seen      = now;
        p.best_frame_jpeg = std::move(snapshot);
        pending_.emplace(cooldown_key, std::move(p));
    } else if (is_better(rec, p_it->second.best)) {
        p_it->second.best            = rec;
        p_it->second.best_frame_jpeg = std::move(snapshot);
    }
    cv_.notify_one();  // wake worker to check if any pending is now ready
}

void EventPublisher::fire_event_nolock(const RecognitionResult& rec,
                                        float effective_score,
                                        const std::vector<uint8_t>& frame_jpeg) {
    const bool is_known = rec.event_type != EventType::Unknown && rec.person_id;
    if (!is_known && !config_.write_unknowns) return;

    EventType final_type = rec.event_type;
    std::optional<std::string> schedule_note;
    if (is_known && rec.person_type == PersonType::Student) {
        const auto sched_result = sched_.check(PersonType::Student, *rec.person_id);
        final_type = sched_result.event_type;
        if (!sched_result.message.empty()) schedule_note = sched_result.message;
    }

    std::ostringstream bbox_os;
    bbox_os << '[' << rec.detection.bbox.x1 << ',' << rec.detection.bbox.y1
            << ',' << rec.detection.bbox.x2 << ',' << rec.detection.bbox.y2 << ']';
    const std::string bbox_json = bbox_os.str();

    bool posted = false;
    if (!config_.backend_url.empty()) {
        std::string face_b64, frame_b64;
        // Use the frame captured at detection time, not a freshly-fetched one.
        // The bbox in `rec` is only valid against that specific frame; by now
        // (1.5s later) the face may have moved out of the box.
        if (!frame_jpeg.empty()) {
            frame_b64 = base64_encode(frame_jpeg.data(), frame_jpeg.size());
            const auto face_crop = crop_face_jpeg(frame_jpeg, rec.detection.bbox);
            if (!face_crop.empty()) {
                face_b64 = base64_encode(face_crop.data(), face_crop.size());
            }
        }

        nlohmann::json j;
        j["camera_id"]     = config_.camera_id;
        j["event_type"]    = std::string(to_string(final_type));
        if (final_type != EventType::Unknown &&
            rec.person_type == PersonType::Employee &&
            rec.person_id) {
            j["employee_id"] = *rec.person_id;
        } else {
            j["employee_id"] = nullptr;
        }
        j["employee_name"] = rec.display_name.value_or(std::string{});
        j["confidence"]    = effective_score;
        j["bbox"]          = bbox_json;
        j["face_image"]    = face_b64.empty()  ? nlohmann::json(nullptr) : nlohmann::json(face_b64);
        j["frame_image"]   = frame_b64.empty() ? nlohmann::json(nullptr) : nlohmann::json(frame_b64);
        j["timestamp"]     = to_iso8601_utc(rec.detection.captured_at);

        posted = post_to_backend(j.dump());
        if (posted) {
            PLOG_INFO(events,
                      "POSTed event (flushed): type={} name='{}' conf={:.2f} {}",
                      to_string(final_type),
                      rec.display_name.value_or(std::string{"Unknown"}),
                      effective_score,
                      schedule_note.value_or(std::string{}));
        }
    }

    if (posted) return;

    EventRepository::NewEvent ev{};
    ev.camera_id   = config_.camera_id;
    ev.event_type  = final_type;
    ev.person_id   = rec.person_id;
    ev.person_type = rec.person_type;
    ev.person_name = rec.display_name;
    ev.confidence  = effective_score;
    ev.bbox_json   = bbox_json;
    ev.captured_at = rec.detection.captured_at;
    try {
        const auto id = repo_.insert(ev);
        PLOG_INFO(events,
                  "DB-inserted (fallback) id={} type={} name='{}' conf={:.2f}",
                  id, to_string(final_type),
                  rec.display_name.value_or(std::string{"Unknown"}),
                  effective_score);
    } catch (const std::exception& e) {
        PLOG_ERROR(events, "Fallback DB insert failed: {}", e.what());
    }
}

}  // namespace parking::database
