#include "parking/core/logger.hpp"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace parking::logger {

namespace {

std::mutex                                        g_mutex;
std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> g_loggers;
std::vector<spdlog::sink_ptr>                     g_sinks;
spdlog::level::level_enum                         g_level = spdlog::level::info;
bool                                              g_initialized = false;

spdlog::level::level_enum parse_level(const std::string& s) {
    if (s == "trace")    return spdlog::level::trace;
    if (s == "debug")    return spdlog::level::debug;
    if (s == "info")     return spdlog::level::info;
    if (s == "warn")     return spdlog::level::warn;
    if (s == "error")    return spdlog::level::err;
    if (s == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

}  // namespace

void init(bool console, const std::string& log_file, const std::string& level) {
    std::lock_guard<std::mutex> lock(g_mutex);

    g_level = parse_level(level);
    g_sinks.clear();

    if (console) {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
        g_sinks.push_back(sink);
    }

    if (!log_file.empty()) {
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file, 10 * 1024 * 1024 /* 10 MB */, 5 /* rotations */);
        sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v");
        g_sinks.push_back(sink);
    }

    g_initialized = true;
}

std::shared_ptr<spdlog::logger> get(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_initialized) {
        // Default: console only, info level.
        g_sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        g_initialized = true;
    }

    if (auto it = g_loggers.find(name); it != g_loggers.end()) {
        return it->second;
    }

    auto logger = std::make_shared<spdlog::logger>(name, g_sinks.begin(), g_sinks.end());
    logger->set_level(g_level);
    logger->flush_on(spdlog::level::warn);
    g_loggers[name] = logger;
    return logger;
}

}  // namespace parking::logger
