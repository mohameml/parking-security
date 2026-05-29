#include "parking/alerts/alert_service.hpp"

#include "parking/core/logger.hpp"

#include <future>
#include <mutex>
#include <vector>

namespace parking::alerts {

class AlertService::Impl {
public:
    std::mutex                             mutex;
    std::vector<std::unique_ptr<Alerter>>  alerters;
    std::vector<std::future<void>>         in_flight;
};

AlertService::AlertService()  : impl_(std::make_unique<Impl>()) {}
AlertService::~AlertService() { flush(); }

void AlertService::add_alerter(std::unique_ptr<Alerter> alerter) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    PLOG_INFO(alerts, "Registered alerter: {}", alerter->name());
    impl_->alerters.push_back(std::move(alerter));
}

void AlertService::dispatch(const RecognitionResult& result,
                             const std::vector<uint8_t>& face_jpeg) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    // Clean up completed futures to bound memory
    impl_->in_flight.erase(
        std::remove_if(impl_->in_flight.begin(), impl_->in_flight.end(),
                       [](std::future<void>& f) {
                           return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                       }),
        impl_->in_flight.end());

    for (auto& alerter : impl_->alerters) {
        auto* ptr = alerter.get();
        impl_->in_flight.push_back(std::async(std::launch::async, [ptr, result, face_jpeg]() {
            try {
                bool ok = ptr->send(result, face_jpeg);
                if (!ok) {
                    PLOG_WARN(alerts, "{} returned failure", ptr->name());
                }
            } catch (const std::exception& e) {
                PLOG_ERROR(alerts, "{} threw: {}", ptr->name(), e.what());
            }
        }));
    }
}

void AlertService::flush() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto& f : impl_->in_flight) {
        if (f.valid()) f.wait();
    }
    impl_->in_flight.clear();
}

}  // namespace parking::alerts
