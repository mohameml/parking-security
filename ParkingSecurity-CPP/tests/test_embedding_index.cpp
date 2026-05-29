#include "parking/inference/embedding_index.hpp"

#include <gtest/gtest.h>

#include <random>

using namespace parking;
using namespace parking::inference;

namespace {

Embedding random_embedding(unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> dist(0.0f, 1.0f);
    Embedding e{};
    for (auto& v : e) v = dist(rng);
    return e;
}

}  // namespace

TEST(EmbeddingIndex, EmptyReturnsNullopt) {
    EmbeddingIndex idx;
    EXPECT_EQ(idx.size(), 0u);
    EXPECT_FALSE(idx.query(random_embedding(1)).has_value());
}

TEST(EmbeddingIndex, SelfMatchScoresOne) {
    EmbeddingIndex idx;
    Embedding e = random_embedding(42);

    idx.rebuild({
        {"p1", "Alice", PersonType::Employee, e},
    });

    auto result = idx.query(e, /*threshold=*/0.9f);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->person_id, "p1");
    EXPECT_NEAR(result->score, 1.0f, 1e-4f);
}

TEST(EmbeddingIndex, BelowThresholdNotMatched) {
    EmbeddingIndex idx;
    idx.rebuild({
        {"p1", "Alice", PersonType::Employee, random_embedding(1)},
    });

    // Query a totally different random embedding
    auto result = idx.query(random_embedding(999), /*threshold=*/0.9f);
    EXPECT_FALSE(result.has_value());
}

TEST(EmbeddingIndex, RemovePerson) {
    EmbeddingIndex idx;
    idx.rebuild({
        {"p1", "Alice", PersonType::Employee, random_embedding(1)},
        {"p1", "Alice", PersonType::Employee, random_embedding(2)},
        {"p2", "Bob",   PersonType::Employee, random_embedding(3)},
    });
    EXPECT_EQ(idx.size(), 3u);
    idx.remove_person("p1");
    EXPECT_EQ(idx.size(), 1u);
}
