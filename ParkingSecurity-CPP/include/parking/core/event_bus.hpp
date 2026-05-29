#pragma once

#include "parking/core/types.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace parking {

/// In-process pub/sub for recognition results.
///
/// Services (DB writer, alert service, UI, WebSocket broadcaster) subscribe to
/// the bus and react to each RecognitionResult independently. The event bus is
/// thread-safe; publishers and subscribers can live on different threads.
class EventBus {
public:
    using Handler        = std::function<void(const RecognitionResult&)>;
    using SubscriptionId = uint64_t;

    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    /// Subscribe a handler. Returns a subscription ID that can be used to unsubscribe.
    SubscriptionId subscribe(Handler handler);

    /// Remove a previously-registered subscription.
    void unsubscribe(SubscriptionId id);

    /// Publish an event to all subscribers. Exceptions thrown by a handler are
    /// logged and swallowed; one misbehaving subscriber does not affect the others.
    void publish(const RecognitionResult& result);

private:
    struct Entry {
        SubscriptionId id;
        Handler        handler;
    };

    std::mutex         mutex_;
    std::vector<Entry> subscribers_;
    SubscriptionId     next_id_{1};
};

}  // namespace parking
