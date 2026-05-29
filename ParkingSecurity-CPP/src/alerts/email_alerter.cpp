#include "parking/alerts/email_alerter.hpp"

#include "parking/core/logger.hpp"

// Scaffolding stub. Real implementation will use libcurl's SMTP support with
// STARTTLS to send an email containing the face crop as an inline attachment.

namespace parking::alerts {

EmailAlerter::EmailAlerter(Config config) : config_(std::move(config)) {}
EmailAlerter::~EmailAlerter() = default;

bool EmailAlerter::send(const RecognitionResult& /*result*/,
                         const std::vector<uint8_t>& /*face_jpeg*/) {
    if (config_.smtp_host.empty() || config_.user.empty() || config_.to.empty()) {
        PLOG_DEBUG(email, "Not configured — skipping");
        return false;
    }
    // TODO: libcurl SMTP
    return false;
}

}  // namespace parking::alerts
