#include "parking/core/event_bus.hpp"

#include "parking/core/logger.hpp"

#include <algorithm>

namespace parking {

EventBus::SubscriptionId EventBus::subscribe(Handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto id = next_id_++;
    subscribers_.push_back({id, std::move(handler)});
    return id;
}

void EventBus::unsubscribe(SubscriptionId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.erase(
        std::remove_if(subscribers_.begin(), subscribers_.end(),
                       [id](const Entry& e) { return e.id == id; }),
        subscribers_.end());
}

void EventBus::publish(const RecognitionResult& result) {
    // Copy subscriber list under lock, then invoke handlers without holding
    // the lock (so handlers that publish back don't deadlock).
    std::vector<Entry> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = subscribers_;
    }

    for (const auto& entry : snapshot) {
        try {
            entry.handler(result);
        } catch (const std::exception& e) {
            PLOG_ERROR(event_bus, "Subscriber {} threw: {}", entry.id, e.what());
        } catch (...) {
            PLOG_ERROR(event_bus, "Subscriber {} threw unknown exception", entry.id);
        }
    }
}

}  // namespace parking
