#pragma once

#include "parking/alerts/alert_service.hpp"

#include <string>

namespace parking::alerts {

/// Sends an alert via Telegram Bot API (sendPhoto with caption).
/// Requires a bot token (from @BotFather) and a target chat ID.
class TelegramAlerter final : public Alerter {
public:
    struct Config {
        std::string bot_token;
        std::string chat_id;
        std::string api_base = "https://api.telegram.org";
    };

    explicit TelegramAlerter(Config config);
    ~TelegramAlerter() override;

    bool send(const RecognitionResult& result,
              const std::vector<uint8_t>& face_jpeg) override;

    std::string_view name() const noexcept override { return "telegram"; }

private:
    Config config_;
};

}  // namespace parking::alerts
