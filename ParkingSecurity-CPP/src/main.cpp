// parking-security — production daemon entry point.
//
// Run with:
//   parking-security [path/to/parking.ini]
//
// Defaults to `config/parking.ini` relative to the working directory. All
// knobs are settable from the INI file (see config/parking.ini for the full
// list); a handful of them accept env-var overrides prefixed PARKING_*.
//
// The binary is designed to run as a systemd service on the Jetson. It runs
// forever until SIGINT/SIGTERM, which triggers a graceful shutdown of the
// GStreamer pipeline, MJPEG server, and DB pool.

#include "parking/camera/camera_pipeline.hpp"
#include "parking/core/config.hpp"
#include "parking/core/logger.hpp"
#include "parking/database/db_connection.hpp"
#include "parking/database/event_publisher.hpp"
#include "parking/database/event_repository.hpp"
#include "parking/inference/embedding_index.hpp"
#include "parking/schedule/schedule_checker.hpp"
#include "parking/version.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>
#include <unistd.h>

namespace {

std::atomic<bool> g_shutdown{false};

void handle_signal(int /*sig*/) { g_shutdown.store(true); }

}  // namespace

int main(int argc, char** argv) {
    using namespace parking;

    // ─── Config ─────────────────────────────────────────────
    std::filesystem::path config_file = (argc > 1) ? argv[1] : "config/parking.ini";

    Config cfg;
    try {
        cfg = Config::load(config_file);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << std::endl;
        return 1;
    }

    // ─── Logger ─────────────────────────────────────────────
    logger::init(cfg.logging.console, cfg.logging.log_file, cfg.logging.level);
    PLOG_INFO(main, "parking-security {} starting (pid={}, config={})",
              version_string, static_cast<int>(::getpid()), config_file.string());

    try {
        cfg.validate();
    } catch (const std::exception& e) {
        PLOG_CRIT(main, "Config validation failed: {}", e.what());
        return 2;
    }

    // ─── Signals ────────────────────────────────────────────
    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    // ─── Optional DB persistence + schedule + publisher ─────
    std::unique_ptr<database::ConnectionPool>   db_pool;
    std::unique_ptr<database::EventRepository>  event_repo;
    std::unique_ptr<schedule::ScheduleChecker>  scheduler;
    std::unique_ptr<database::EventPublisher>   publisher;
    if (!cfg.database.connection_string.empty()) {
        try {
            db_pool    = std::make_unique<database::ConnectionPool>(
                cfg.database.connection_string, cfg.database.pool_size);
            event_repo = std::make_unique<database::EventRepository>(*db_pool);
            scheduler  = std::make_unique<schedule::ScheduleChecker>(*db_pool);

            database::EventPublisher::Config pcfg{};
            pcfg.camera_id       = cfg.camera.camera_id;
            pcfg.cooldown        = cfg.recognition.detection_cooldown;
            pcfg.backend_url     = cfg.backend.url;
            pcfg.backend_api_key = cfg.backend.api_key;
            publisher = std::make_unique<database::EventPublisher>(
                *event_repo, *scheduler, pcfg);

            PLOG_INFO(main, "Persistence ready (pool={}, camera_id={}, cooldown={}s)",
                      cfg.database.pool_size, cfg.camera.camera_id,
                      cfg.recognition.detection_cooldown.count());
        } catch (const std::exception& e) {
            PLOG_ERROR(main, "Persistence init failed — will run without DB: {}",
                       e.what());
            db_pool.reset();
            event_repo.reset();
            scheduler.reset();
            publisher.reset();
        }
    }

    // ─── Embedding index (optional) ─────────────────────────
    inference::EmbeddingIndex index;
    bool have_index = false;
    if (!cfg.recognition.embedding_index_path.empty() &&
        std::filesystem::exists(cfg.recognition.embedding_index_path)) {
        try {
            index.load_from_json(cfg.recognition.embedding_index_path);
            have_index = true;
            PLOG_INFO(main, "Loaded {} enrolled embeddings from {}",
                      index.size(),
                      cfg.recognition.embedding_index_path.string());
        } catch (const std::exception& e) {
            PLOG_ERROR(main, "Failed to load embedding index: {} — continuing "
                              "without it (all faces will be Unknown).",
                       e.what());
        }
    }

    // ─── Camera pipeline config (pulled from Config) ────────
    CameraConfig cam_cfg        = cfg.camera;
    cam_cfg.enable_detector     = true;
    cam_cfg.detector_config_path = cfg.models.detector_config.string();
    cam_cfg.enable_tracker      = cfg.recognition.enable_tracker;
    cam_cfg.enable_recognizer   = cfg.recognition.enable_recognizer;
    cam_cfg.recognizer_config_path = cfg.models.recognizer_config.string();
    cam_cfg.enable_stream       = cfg.stream.enabled;
    cam_cfg.stream_port         = cfg.stream.port;
    cam_cfg.stream_jpeg_quality = cfg.stream.jpeg_quality;

    // Pipelines can die on USB-camera hiccups or DeepStream CUDA
    // illegal-address events. Rather than try to rebuild in-process (which
    // races with half-released v4l2/GStreamer resources), we exit non-zero
    // and let systemd's Restart=on-failure do a clean process restart.
    std::atomic<bool> pipeline_dead{false};
    camera::CameraPipeline* live_pipeline = nullptr;

    auto build_pipeline = [&]() -> std::unique_ptr<camera::CameraPipeline> {
        auto p = std::make_unique<camera::CameraPipeline>(cam_cfg);
        if (have_index) {
            p->set_embedding_index(&index, cfg.recognition.similarity_threshold);
        }
        p->set_recognition_callback([&](const RecognitionResult& r) {
            if (publisher) publisher->handle(r);
        });
        p->set_error_callback([&](const std::string& msg) {
            PLOG_ERROR(main, "Pipeline error: {}", msg);
            pipeline_dead.store(true);
        });
        return p;
    };

    auto pipeline = build_pipeline();
    live_pipeline = pipeline.get();

    // Now that the pipeline exists, give the publisher a frame provider so
    // each persisted event carries a face-thumbnail + full-frame image.
    if (publisher) {
        // A tiny wrapper so we can re-point live_pipeline across restarts.
        // publisher keeps the lambda for its lifetime.
        auto fp = [&live_pipeline]() -> std::vector<uint8_t> {
            if (!live_pipeline) return {};
            return live_pipeline->latest_frame_jpeg();
        };
        // EventPublisher::Config is only read once at construction — rebuild
        // it so the frame_provider is honoured. This is a bit awkward but
        // keeps EventPublisher's API simple.
        database::EventPublisher::Config pcfg{};
        pcfg.camera_id        = cfg.camera.camera_id;
        pcfg.cooldown         = cfg.recognition.detection_cooldown;
        pcfg.backend_url      = cfg.backend.url;
        pcfg.backend_api_key  = cfg.backend.api_key;
        pcfg.frame_provider   = fp;
        publisher = std::make_unique<database::EventPublisher>(
            *event_repo, *scheduler, pcfg);
    }
    if (!pipeline->start()) {
        PLOG_CRIT(main, "Failed to start camera pipeline");
        return 3;
    }

    PLOG_INFO(main, "All systems nominal. Running until SIGINT/SIGTERM.");
    if (cfg.stream.enabled) {
        PLOG_INFO(main, "Live view: http://<jetson-ip>:{}/", cfg.stream.port);
    }

    // ─── Run until signal or pipeline death ─────────────────
    // One-minute stats + heartbeat check. Any pipeline error OR a full
    // minute without new frames causes us to exit non-zero so systemd
    // restarts the process cleanly. Never try to rebuild in-process —
    // half-released v4l2/GStreamer resources cause cascading failures.
    auto next_tick       = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    uint64_t last_frames = 0;
    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        if (pipeline_dead.load()) {
            PLOG_CRIT(main, "Pipeline died — exiting so systemd restarts "
                            "the process with a fresh GStreamer context.");
            pipeline->stop();
            return 4;
        }

        if (std::chrono::steady_clock::now() >= next_tick) {
            const auto s = pipeline->stats();
            PLOG_INFO(main, "stats: frames={} fps={:.1f} detections={}",
                      s.frames_captured, s.current_fps, s.detections);
            if (s.frames_captured == last_frames) {
                PLOG_CRIT(main, "No new frames in the last minute — exiting "
                                "so systemd restarts the process.");
                pipeline->stop();
                return 5;
            }
            last_frames = s.frames_captured;
            next_tick  += std::chrono::minutes(1);
        }
    }

    // ─── Shutdown ───────────────────────────────────────────
    PLOG_INFO(main, "Shutting down");
    if (pipeline) pipeline->stop();
    // publisher / scheduler / event_repo / db_pool tear down in reverse order
    // via RAII — explicitly ordered here so the DB pool outlives everything
    // that uses it.
    publisher.reset();
    scheduler.reset();
    event_repo.reset();
    db_pool.reset();

    PLOG_INFO(main, "Goodbye");
    return 0;
}
