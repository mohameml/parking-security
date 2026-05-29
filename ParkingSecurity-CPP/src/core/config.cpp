#include "parking/core/config.hpp"

#include "parking/core/logger.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace parking {

namespace {

std::string trim(std::string s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    const auto end   = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    return s.substr(start, end - start + 1);
}

/// Minimal INI parser. Returns map of "section.key" -> value. Comment lines
/// start with `#` or `;`. Inline `#`/`;` comments are NOT stripped (libpq-
/// style connection strings contain `;` and `#` in places we don't want
/// chopped).
std::unordered_map<std::string, std::string> parse_ini(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) {
        throw std::runtime_error("Cannot open config file: " + file.string());
    }

    std::unordered_map<std::string, std::string> values;
    std::string                                   section;
    std::string                                   line;

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const auto key   = trim(line.substr(0, eq));
        const auto value = trim(line.substr(eq + 1));
        values[section.empty() ? key : section + "." + key] = value;
    }
    return values;
}

bool parse_bool(std::string_view s, bool fallback) {
    if (s == "1" || s == "true"  || s == "yes" || s == "on")  return true;
    if (s == "0" || s == "false" || s == "no"  || s == "off") return false;
    return fallback;
}

template <typename T>
T parse_numeric(const std::string& s, T fallback) {
    try {
        if constexpr (std::is_same_v<T, float>)    return std::stof(s);
        else if constexpr (std::is_same_v<T, double>) return std::stod(s);
        else                                        return static_cast<T>(std::stol(s));
    } catch (...) { return fallback; }
}

}  // namespace

Config Config::load(const std::filesystem::path& file) {
    Config cfg;

    if (!std::filesystem::exists(file)) {
        PLOG_WARN(config, "Config file not found, using defaults: {}", file.string());
        cfg.apply_env_overrides();
        return cfg;
    }

    std::unordered_map<std::string, std::string> values;
    try {
        values = parse_ini(file);
    } catch (const std::exception& e) {
        PLOG_WARN(config, "Failed to parse {}: {} — using defaults",
                  file.string(), e.what());
        cfg.apply_env_overrides();
        return cfg;
    }

    auto get_str = [&](const std::string& k, std::string& dst) {
        auto it = values.find(k);
        if (it != values.end()) dst = it->second;
    };
    auto get_path = [&](const std::string& k, std::filesystem::path& dst) {
        auto it = values.find(k);
        if (it != values.end()) dst = it->second;
    };
    auto get_int = [&]<typename T>(const std::string& k, T& dst) {
        auto it = values.find(k);
        if (it != values.end()) dst = parse_numeric<T>(it->second, dst);
    };
    auto get_float = [&](const std::string& k, float& dst) {
        auto it = values.find(k);
        if (it != values.end()) dst = parse_numeric<float>(it->second, dst);
    };
    auto get_bool = [&](const std::string& k, bool& dst) {
        auto it = values.find(k);
        if (it != values.end()) dst = parse_bool(it->second, dst);
    };

    // ── camera ──
    get_str ("camera.source_uri",   cfg.camera.source_uri);
    get_str ("camera.camera_id",    cfg.camera.camera_id);
    get_int ("camera.width",        cfg.camera.width);
    get_int ("camera.height",       cfg.camera.height);
    get_int ("camera.target_fps",   cfg.camera.target_fps);
    get_bool("camera.mjpeg_input",  cfg.camera.mjpeg_input);

    // ── models ──
    get_path("models.detector_config",     cfg.models.detector_config);
    get_path("models.recognizer_config",   cfg.models.recognizer_config);
    get_path("models.detector_engine",     cfg.models.detector_engine);
    get_path("models.recognizer_engine",   cfg.models.recognizer_engine);
    get_int ("models.detector_input_width",  cfg.models.detector_input_width);
    get_int ("models.detector_input_height", cfg.models.detector_input_height);
    get_int ("models.recognizer_input_size", cfg.models.recognizer_input_size);
    get_float("models.detection_threshold",  cfg.models.detection_threshold);

    // ── recognition ──
    get_path ("recognition.embedding_index_path",  cfg.recognition.embedding_index_path);
    get_float("recognition.similarity_threshold",  cfg.recognition.similarity_threshold);
    get_float("recognition.tta_margin",            cfg.recognition.tta_margin);
    get_int  ("recognition.max_embeddings_per_person",  cfg.recognition.max_embeddings_per_person);
    get_float("recognition.progressive_learn_threshold", cfg.recognition.progressive_learn_threshold);
    if (auto it = values.find("recognition.detection_cooldown_sec"); it != values.end()) {
        cfg.recognition.detection_cooldown =
            std::chrono::seconds(parse_numeric<int>(it->second, 10));
    }
    get_bool("recognition.enable_tracker",    cfg.recognition.enable_tracker);
    get_bool("recognition.enable_recognizer", cfg.recognition.enable_recognizer);

    // ── stream ──
    get_bool("stream.enabled",      cfg.stream.enabled);
    get_int ("stream.port",         cfg.stream.port);
    get_int ("stream.jpeg_quality", cfg.stream.jpeg_quality);

    // ── database ──
    get_str("database.connection_string", cfg.database.connection_string);
    get_int("database.pool_size",         cfg.database.pool_size);

    // ── backend API ──
    get_str("backend.url",     cfg.backend.url);
    get_str("backend.api_key", cfg.backend.api_key);

    // ── alerts ──
    get_str("alerts.telegram_bot_token", cfg.alerts.telegram_bot_token);
    get_str("alerts.telegram_chat_id",   cfg.alerts.telegram_chat_id);
    get_str("alerts.smtp_host",          cfg.alerts.smtp_host);
    get_int("alerts.smtp_port",          cfg.alerts.smtp_port);
    get_str("alerts.smtp_user",          cfg.alerts.smtp_user);
    get_str("alerts.smtp_password",      cfg.alerts.smtp_password);
    get_str("alerts.alert_email_to",     cfg.alerts.alert_email_to);

    // ── api ──
    get_str ("api.bind_host", cfg.api.bind_host);
    get_int ("api.bind_port", cfg.api.bind_port);
    get_bool("api.enabled",   cfg.api.enabled);

    // ── logging ──
    get_str ("logging.level",    cfg.logging.level);
    get_str ("logging.log_file", cfg.logging.log_file);
    get_bool("logging.console",  cfg.logging.console);

    cfg.apply_env_overrides();
    return cfg;
}

void Config::apply_env_overrides() {
    auto env = [](const char* name) -> std::optional<std::string> {
        if (const char* v = std::getenv(name); v != nullptr) return std::string{v};
        return std::nullopt;
    };

    if (auto v = env("PARKING_CAMERA_SOURCE"))    camera.source_uri          = *v;
    if (auto v = env("PARKING_CAMERA_ID"))        camera.camera_id           = *v;
    if (auto v = env("PARKING_DB_URL"))           database.connection_string = *v;
    if (auto v = env("PARKING_INDEX_PATH"))       recognition.embedding_index_path = *v;
    if (auto v = env("PARKING_STREAM_PORT"))      stream.port                = std::stoi(*v);
    if (auto v = env("PARKING_BACKEND_URL"))      backend.url                = *v;
    if (auto v = env("PARKING_CAMERA_API_KEY"))   backend.api_key            = *v;
    if (auto v = env("PARKING_TELEGRAM_TOKEN"))   alerts.telegram_bot_token  = *v;
    if (auto v = env("PARKING_TELEGRAM_CHAT_ID")) alerts.telegram_chat_id    = *v;
    if (auto v = env("PARKING_LOG_LEVEL"))        logging.level              = *v;
    if (auto v = env("PARKING_LOG_FILE"))         logging.log_file           = *v;
}

void Config::validate() const {
    if (camera.source_uri.empty()) {
        throw std::invalid_argument("camera.source_uri is required");
    }
    if (recognition.similarity_threshold < 0.0f || recognition.similarity_threshold > 1.0f) {
        throw std::invalid_argument("recognition.similarity_threshold must be in [0, 1]");
    }

    // Model engine files must exist only if the corresponding stage is on.
    if (!std::filesystem::exists(models.detector_engine)) {
        throw std::invalid_argument(
            "detector engine not found: " + models.detector_engine.string());
    }
    if (!std::filesystem::exists(models.detector_config)) {
        throw std::invalid_argument(
            "detector nvinfer config not found: " + models.detector_config.string());
    }
    if (recognition.enable_recognizer) {
        if (!std::filesystem::exists(models.recognizer_engine)) {
            throw std::invalid_argument(
                "recognizer engine not found: " + models.recognizer_engine.string());
        }
        if (!std::filesystem::exists(models.recognizer_config)) {
            throw std::invalid_argument(
                "recognizer nvinfer config not found: " + models.recognizer_config.string());
        }
    }
    if (!recognition.embedding_index_path.empty() &&
        !std::filesystem::exists(recognition.embedding_index_path)) {
        // Warning only: the daemon will still run and emit Unknown events.
        PLOG_WARN(config,
                  "Embedding index not found: {}. Recognition will treat all "
                  "faces as Unknown.", recognition.embedding_index_path.string());
    }
}

std::string_view to_string(EventType type) noexcept {
    switch (type) {
        case EventType::Authorized: return "authorized";
        case EventType::WrongTime:  return "wrong_time";
        case EventType::WrongDay:   return "wrong_day";
        case EventType::Unknown:    return "unknown";
    }
    return "invalid";
}

}  // namespace parking
