#include "parking/core/config.hpp"

#include <gtest/gtest.h>

using namespace parking;

TEST(Config, DefaultsAreSane) {
    Config cfg;
    EXPECT_FALSE(cfg.camera.source_uri.empty());
    EXPECT_GT(cfg.camera.target_fps, 0);
    EXPECT_GE(cfg.recognition.similarity_threshold, 0.0f);
    EXPECT_LE(cfg.recognition.similarity_threshold, 1.0f);
    EXPECT_GT(cfg.database.pool_size, 0);
}

TEST(Config, EventTypeStringsMatch) {
    EXPECT_EQ(to_string(EventType::Authorized), "authorized");
    EXPECT_EQ(to_string(EventType::WrongTime),  "wrong_time");
    EXPECT_EQ(to_string(EventType::WrongDay),   "wrong_day");
    EXPECT_EQ(to_string(EventType::Unknown),    "unknown");
}
