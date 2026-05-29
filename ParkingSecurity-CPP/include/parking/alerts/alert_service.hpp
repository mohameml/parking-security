#pragma once

#include "parking/core/types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace parking::alerts {

/// Abstract interface for a single alert channel (Telegram, Email, Firebase...).
class Alerter {
public:
    virtual ~Alerter() = default;

    /// Send a single alert. Returns true on success. Implementations should
    /// not throw; network errors should be logged and return false.
    virtual bool send(const RecognitionResult& result,
                      const std::vector<uint8_t>& face_jpeg) = 0;

    virtual std::string_view name() const noexcept = 0;
};

/// Fans out a single alert to all configured channels in parallel.
/// If one channel fails, others still deliver.
class AlertService {
public:
    AlertService();
    ~AlertService();

    void add_alerter(std::unique_ptr<Alerter> alerter);

    /// Fire the alert on all channels. Non-blocking: dispatches work to a
    /// thread pool and returns immediately. Callers can optionally wait
    /// via the returned future.
    void dispatch(const RecognitionResult& result,
                  const std::vector<uint8_t>& face_jpeg);

    /// Wait for all in-flight alerts to complete.
    void flush();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace parking::alerts
