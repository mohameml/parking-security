#include "parking/alerts/telegram_alerter.hpp"

#include "parking/core/logger.hpp"

// Scaffolding stub. Real implementation will use libcurl to POST a multipart
// form to https://api.telegram.org/bot<TOKEN>/sendPhoto with:
//   - chat_id = configured chat
//   - caption = formatted Markdown string (person name, camera, time, score)
//   - photo   = the face crop JPEG

namespace parking::alerts {

TelegramAlerter::TelegramAlerter(Config config) : config_(std::move(config)) {}
TelegramAlerter::~TelegramAlerter() = default;

bool TelegramAlerter::send(const RecognitionResult& /*result*/,
                            const std::vector<uint8_t>& /*face_jpeg*/) {
    if (config_.bot_token.empty() || config_.chat_id.empty()) {
        PLOG_DEBUG(telegram, "Not configured — skipping");
        return false;
    }
    // TODO: libcurl multipart POST
    return false;
}

}  // namespace parking::alerts
