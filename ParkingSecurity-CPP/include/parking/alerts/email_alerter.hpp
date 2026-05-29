#pragma once

#include "parking/alerts/alert_service.hpp"

#include <cstdint>
#include <string>

namespace parking::alerts {

/// Sends an alert as an email with the face crop attached.
/// Uses SMTP (libcurl under the hood).
class EmailAlerter final : public Alerter {
public:
    struct Config {
        std::string smtp_host;
        uint16_t    smtp_port{587};
        std::string user;
        std::string password;
        std::string to;
        std::string from;           ///< Defaults to `user` if empty
        bool        use_starttls{true};
    };

    explicit EmailAlerter(Config config);
    ~EmailAlerter() override;

    bool send(const RecognitionResult& result,
              const std::vector<uint8_t>& face_jpeg) override;

    std::string_view name() const noexcept override { return "email"; }

private:
    Config config_;
};

}  // namespace parking::alerts
