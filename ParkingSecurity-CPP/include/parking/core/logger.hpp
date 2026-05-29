#pragma once

#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace parking::logger {

/// Initialize the global logger. Call once at program startup.
/// - `console`: whether to log to stdout
/// - `log_file`: path to a rotating log file, or empty for none
/// - `level`: "trace", "debug", "info", "warn", "error", "critical"
void init(bool console = true,
          const std::string& log_file = "",
          const std::string& level = "info");

/// Returns the named logger (creating it on first use).
std::shared_ptr<spdlog::logger> get(const std::string& name);

}  // namespace parking::logger

// Convenience macros: `PLOG_INFO(camera, "opened {}x{}", w, h);`
#define PLOG_TRACE(name, ...)    ::parking::logger::get(#name)->trace(__VA_ARGS__)
#define PLOG_DEBUG(name, ...)    ::parking::logger::get(#name)->debug(__VA_ARGS__)
#define PLOG_INFO(name,  ...)    ::parking::logger::get(#name)->info(__VA_ARGS__)
#define PLOG_WARN(name,  ...)    ::parking::logger::get(#name)->warn(__VA_ARGS__)
#define PLOG_ERROR(name, ...)    ::parking::logger::get(#name)->error(__VA_ARGS__)
#define PLOG_CRIT(name,  ...)    ::parking::logger::get(#name)->critical(__VA_ARGS__)
