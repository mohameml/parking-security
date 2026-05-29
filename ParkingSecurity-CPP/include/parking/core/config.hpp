#pragma once

#include "parking/core/types.hpp"

#include <chrono>
#include <filesystem>
#include <string>

namespace parking {

/// Aggregated runtime configuration (loaded from config.ini + environment).
struct Config {
    // ── Camera ──────────────────────────────────────────
    CameraConfig camera{
        .source_uri = "v4l2:///dev/video0",
        .camera_id  = "cam-entrance-01",
        .width      = 1920,
        .height     = 1080,
        .target_fps = 30,
        .mjpeg_input = true,
    };

    // ── Models ──────────────────────────────────────────
    struct Models {
        // nvinfer config files (the things the pipeline actually passes to
        // DeepStream). The engines they reference must exist on disk too.
        std::filesystem::path detector_config   = "config/face_detection.txt";
        std::filesystem::path recognizer_config = "config/face_recognition.txt";
        std::filesystem::path detector_engine   = "models/det_10g.engine";
        std::filesystem::path recognizer_engine = "models/w600k_r50.engine";
        int32_t detector_input_width  = 640;
        int32_t detector_input_height = 640;
        int32_t recognizer_input_size = 112;
        float   detection_threshold   = 0.5f;
    } models;

    // ── Recognition ─────────────────────────────────────
    struct Recognition {
        /// Path to the enrolled-embeddings JSON produced by
        /// `scripts/export_embeddings.sh`. Empty = skip recognition.
        std::filesystem::path embedding_index_path = "data/embeddings.json";
        float similarity_threshold     = 0.45f;
        float tta_margin               = 0.10f;
        int   max_embeddings_per_person = 20;
        float progressive_learn_threshold = 0.70f;
        std::chrono::seconds detection_cooldown{10};
        bool  enable_tracker    = true;
        bool  enable_recognizer = true;
    } recognition;

    // ── Live HTTP stream ────────────────────────────────
    struct Stream {
        bool    enabled = true;
        int32_t port    = 8090;
        int32_t jpeg_quality = 70;
    } stream;

    // ── Database ────────────────────────────────────────
    struct Database {
        std::string connection_string =
            "host=localhost port=5432 user=parking password=changeme dbname=parking_security";
        int pool_size = 5;
    } database;

    // ── Backend API (for live dashboard updates) ────────
    // When backend.url is set, events are POSTed to that API instead of
    // directly inserted into Postgres. The backend then handles the DB
    // insert AND the WebSocket broadcast to the React dashboard — which is
    // what lets alerts / new events show up instantly without a page refresh.
    // Leave backend.url empty to fall back to direct DB insert.
    struct Backend {
        std::string url     = "http://localhost:8000";
        std::string api_key;
    } backend;

    // ── Alerts ──────────────────────────────────────────
    struct Alerts {
        std::string telegram_bot_token;
        std::string telegram_chat_id;
        std::string smtp_host = "smtp.gmail.com";
        uint16_t    smtp_port = 587;
        std::string smtp_user;
        std::string smtp_password;
        std::string alert_email_to;
    } alerts;

    // ── HTTP API (optional, for external integrations) ──
    struct Api {
        std::string bind_host = "0.0.0.0";
        uint16_t    bind_port = 8000;
        bool        enabled   = false;
    } api;

    // ── UI ──────────────────────────────────────────────
    struct Ui {
        bool fullscreen    = false;
        bool use_drm_sink  = false;   ///< Bypass X11, render directly to HDMI
        int  window_width  = 1280;
        int  window_height = 720;
    } ui;

    // ── Logging ─────────────────────────────────────────
    struct Logging {
        std::string level     = "info";
        std::string log_file  = "/var/log/parking-security/parking.log";
        bool        console   = true;
    } logging;

    /// Load config from a file (INI format). Missing keys use defaults.
    /// Environment variables override file values.
    static Config load(const std::filesystem::path& file);

    /// Resolve env var `PARKING_*` overrides after file load.
    void apply_env_overrides();

    /// Validate required fields; throws std::invalid_argument on failure.
    void validate() const;
};

}  // namespace parking
