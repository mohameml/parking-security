#include "parking/core/event_bus.hpp"

#include <gtest/gtest.h>

#include <atomic>

using namespace parking;

TEST(EventBus, PublishesToMultipleSubscribers) {
    EventBus bus;
    std::atomic<int> count{0};

    bus.subscribe([&count](const RecognitionResult&) { ++count; });
    bus.subscribe([&count](const RecognitionResult&) { ++count; });

    bus.publish(RecognitionResult{});
    EXPECT_EQ(count.load(), 2);
}

TEST(EventBus, UnsubscribeStopsDelivery) {
    EventBus bus;
    std::atomic<int> count{0};

    auto id = bus.subscribe([&count](const RecognitionResult&) { ++count; });
    bus.publish(RecognitionResult{});
    bus.unsubscribe(id);
    bus.publish(RecognitionResult{});

    EXPECT_EQ(count.load(), 1);
}

TEST(EventBus, ExceptionsInHandlersDoNotAffectOthers) {
    EventBus bus;
    std::atomic<int> count{0};

    bus.subscribe([](const RecognitionResult&) { throw std::runtime_error("boom"); });
    bus.subscribe([&count](const RecognitionResult&) { ++count; });

    bus.publish(RecognitionResult{});
    EXPECT_EQ(count.load(), 1);  // Second handler still ran
}
