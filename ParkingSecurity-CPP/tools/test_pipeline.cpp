// Milestone 1/2/3/4/5 smoke test: start pipeline + name-match + write events.
// Flags:
//   [detector-config]        enable SCRFD detector
//   --track                  enable in-process IoU tracker (needs detector)
//   --recognize <cfg>        enable ArcFace secondary GIE (needs detector)
//   --index <json>           load enrolled embeddings, print name matches
//   --threshold <f>          cosine-sim threshold for a valid match (default 0.5)
//   --persist <pgconn>       libpq connection string — writes events to DB
//   --camera-id <id>         DB camera_id for persisted events (default cam-entrance-01)
//   --stream <port>          MJPEG HTTP server on this port (default 8090 if used)
//
//   Examples:
//     test_pipeline /dev/video0 ../../config/face_detection.txt --track \
//                   --recognize ../../config/face_recognition.txt \
//                   --index ../../data/embeddings.json --threshold 0.45 \
//                   --persist "host=localhost port=5432 user=parking password=changeme dbname=parking_security"

#include "parking/camera/camera_pipeline.hpp"
#include "parking/core/logger.hpp"
#include "parking/database/db_connection.hpp"
#include "parking/database/event_publisher.hpp"
#include "parking/database/event_repository.hpp"
#include "parking/inference/embedding_index.hpp"
#include "parking/schedule/schedule_checker.hpp"

#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace {
std::atomic<bool> g_running{true};
void signal_handler(int) { g_running.store(false); }
}  // namespace

int main(int argc, char** argv) {
    using namespace parking;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <source-uri> [detector-config] [--track] [--recognize <cfg>]\n";
        return 1;
    }

    logger::init(/*console=*/true, /*log_file=*/"", /*level=*/"info");

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    CameraConfig cfg{
        .source_uri  = argv[1],
        .camera_id   = "test",
        .width       = 1920,
        .height      = 1080,
        .target_fps  = 30,
        .mjpeg_input = true,
    };

    std::string source = argv[1];
    if (source.find(".mp4")  != std::string::npos ||
        source.find(".mkv")  != std::string::npos ||
        source.find(".h264") != std::string::npos ||
        source.find("rtsp:") != std::string::npos) {
        cfg.mjpeg_input = false;
    }

    if (argc >= 3) {
        cfg.enable_detector      = true;
        cfg.detector_config_path = argv[2];
        std::cout << "[test] Detector enabled (" << cfg.detector_config_path << ")\n";
    }
    std::string index_path;
    std::string pg_conn;
    std::string camera_id    = "cam-entrance-01";
    float       rec_threshold = 0.5f;
    // Default: run forever (until SIGINT/SIGTERM). Override with --duration N.
    int         duration_sec = -1;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--track") {
            cfg.enable_tracker = true;
            std::cout << "[test] Tracker enabled  (in-process IoU tracker)\n";
        } else if (a == "--recognize" && i + 1 < argc) {
            cfg.enable_recognizer      = true;
            cfg.recognizer_config_path = argv[++i];
            std::cout << "[test] Recognizer enabled (" << cfg.recognizer_config_path << ")\n";
        } else if (a == "--index" && i + 1 < argc) {
            index_path = argv[++i];
        } else if (a == "--threshold" && i + 1 < argc) {
            rec_threshold = std::stof(argv[++i]);
        } else if (a == "--persist" && i + 1 < argc) {
            pg_conn = argv[++i];
        } else if (a == "--camera-id" && i + 1 < argc) {
            camera_id = argv[++i];
        } else if (a == "--stream" && i + 1 < argc) {
            cfg.enable_stream = true;
            cfg.stream_port   = std::stoi(argv[++i]);
            std::cout << "[test] MJPEG stream enabled on port " << cfg.stream_port
                      << " (open http://<jetson>:" << cfg.stream_port << "/)\n";
        } else if (a == "--duration" && i + 1 < argc) {
            duration_sec = std::stoi(argv[++i]);
        }
    }

    // Load embedding index (if provided) BEFORE creating the pipeline so its
    // pointer is valid for the whole pipeline lifetime.
    inference::EmbeddingIndex index;
    if (!index_path.empty()) {
        try {
            std::cout << "[test] Loading embedding index from " << index_path << "...\n";
            index.load_from_json(index_path);
            std::cout << "[test] Index ready: " << index.size()
                      << " enrolled embeddings (threshold=" << rec_threshold << ")\n";
        } catch (const std::exception& e) {
            std::cerr << "[test] Failed to load index: " << e.what() << "\n";
            return 4;
        }
    }

    camera::CameraPipeline pipeline(cfg);
    if (!index_path.empty()) {
        pipeline.set_embedding_index(&index, rec_threshold);
    }

    // Optional persistence: open a pool, construct repo + scheduler + publisher,
    // and attach its handle() as the recognition callback. Keep them at this
    // scope so they outlive the pipeline's run.
    std::unique_ptr<database::ConnectionPool>    pool;
    std::unique_ptr<database::EventRepository>   event_repo;
    std::unique_ptr<schedule::ScheduleChecker>   scheduler;
    std::unique_ptr<database::EventPublisher>    publisher;
    if (!pg_conn.empty()) {
        try {
            pool       = std::make_unique<database::ConnectionPool>(pg_conn, 2);
            event_repo = std::make_unique<database::EventRepository>(*pool);
            scheduler  = std::make_unique<schedule::ScheduleChecker>(*pool);
            database::EventPublisher::Config pcfg{};
            pcfg.camera_id = camera_id;
            publisher = std::make_unique<database::EventPublisher>(
                *event_repo, *scheduler, pcfg);
            std::cout << "[test] Persistence enabled (camera_id=" << camera_id
                      << ", cooldown=" << pcfg.cooldown.count() << "s)\n";
        } catch (const std::exception& e) {
            std::cerr << "[test] Persistence init failed: " << e.what() << "\n";
            return 5;
        }
    }

    std::atomic<uint64_t> frames_seen{0};
    pipeline.set_frame_callback([&](const Frame& f) {
        if (frames_seen.fetch_add(1) == 0) {
            std::cout << "[test] First frame received: "
                      << f.width << "x" << f.height << "\n";
        }
    });

    std::atomic<uint64_t> det_count{0};
    std::unordered_set<uint64_t> tracks_seen;
    std::mutex                   tracks_mu;
    pipeline.set_detection_callback([&](const Detection& d) {
        const auto n = det_count.fetch_add(1) + 1;
        bool is_new_track = false;
        if (d.track_id != 0) {
            std::lock_guard<std::mutex> l(tracks_mu);
            is_new_track = tracks_seen.insert(d.track_id).second;
        }
        if (n <= 10 || n % 30 == 0 || is_new_track) {
            // Quick sanity stats on the embedding so we can see that ArcFace
            // actually produced a 512-d vector (norm should be ~20-30 for a
            // valid face, first values should vary frame-to-frame).
            float norm_sq = 0.0f, emb_max = -1e9f, emb_min = 1e9f;
            int   nonzero = 0;
            for (float v : d.embedding) {
                norm_sq += v * v;
                if (v != 0.0f) ++nonzero;
                if (v > emb_max) emb_max = v;
                if (v < emb_min) emb_min = v;
            }
            const float norm = std::sqrt(norm_sq);

            std::cout << "[det #" << n << "] "
                      << "track=" << d.track_id << " "
                      << "bbox=[" << d.bbox.x1 << "," << d.bbox.y1 << "->"
                                   << d.bbox.x2 << "," << d.bbox.y2 << "] "
                      << "score=" << d.detection_score;
            if (nonzero > 0) {
                std::cout << "  emb_norm=" << norm
                          << " range=[" << emb_min << "," << emb_max << "]"
                          << " nz=" << nonzero << "/512";
            }
            if (is_new_track) std::cout << "  (new track)";
            std::cout << "\n";
        }
    });

    // Print one line per new (track, person) match so we don't spam the log
    // when a known face sits in view for a hundred frames.
    std::unordered_map<uint64_t, std::string> track_to_name;  // track_id → display_name
    std::mutex                                 track_name_mu;
    std::atomic<uint64_t>                      rec_total{0};
    std::atomic<uint64_t>                      rec_known{0};
    pipeline.set_recognition_callback([&](const RecognitionResult& r) {
        rec_total.fetch_add(1);
        const std::string name = r.display_name.value_or("Unknown");
        if (r.event_type != EventType::Unknown) rec_known.fetch_add(1);

        // Forward to the DB publisher if persistence was enabled.
        if (publisher) publisher->handle(r);

        bool first_for_this_track = false;
        if (r.detection.track_id != 0) {
            std::lock_guard<std::mutex> l(track_name_mu);
            auto it = track_to_name.find(r.detection.track_id);
            if (it == track_to_name.end() || it->second != name) {
                track_to_name[r.detection.track_id] = name;
                first_for_this_track = true;
            }
        } else {
            first_for_this_track = true;  // no track ID → always print
        }

        if (first_for_this_track) {
            std::cout << "[recognized] track=" << r.detection.track_id
                      << "  name=\"" << name << "\""
                      << "  similarity=" << r.similarity_score
                      << "  (" << (r.event_type == EventType::Unknown ? "UNKNOWN" : "known")
                      << ")\n";
        }
    });

    pipeline.set_error_callback([](const std::string& msg) {
        std::cerr << "[test] ERROR: " << msg << "\n";
        g_running.store(false);
    });

    if (!pipeline.start()) {
        std::cerr << "[test] Failed to start pipeline\n";
        return 2;
    }

    auto start   = std::chrono::steady_clock::now();
    int  seconds = 0;
    while (g_running.load() && (duration_sec < 0 || seconds < duration_sec)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ++seconds;
        const auto s = pipeline.stats();
        std::size_t unique_tracks = 0;
        {
            std::lock_guard<std::mutex> l(tracks_mu);
            unique_tracks = tracks_seen.size();
        }
        std::cout << "[test] " << seconds << "s: frames=" << s.frames_captured
                  << "  fps=" << s.current_fps
                  << "  detections=" << s.detections
                  << "  unique_tracks=" << unique_tracks
                  << "  matched=" << rec_known.load() << "/" << rec_total.load()
                  << "\n";
    }

    pipeline.stop();

    const auto total =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::size_t unique_tracks = 0;
    {
        std::lock_guard<std::mutex> l(tracks_mu);
        unique_tracks = tracks_seen.size();
    }
    std::cout << "[test] Total: " << frames_seen.load()
              << " frames in " << total << "s  (avg "
              << (frames_seen.load() / total) << " fps, "
              << det_count.load() << " detections, "
              << unique_tracks << " unique tracks)\n";

    return (frames_seen.load() > 0) ? 0 : 3;
}
