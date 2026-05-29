#include "parking/inference/embedding_index.hpp"

#include "parking/core/logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <vector>

namespace parking::inference {

namespace {

void l2_normalize(Embedding& v) {
    float sum = 0.0f;
    for (float x : v) sum += x * x;
    float norm = std::sqrt(sum);
    if (norm > 0.0f) {
        float inv = 1.0f / norm;
        for (float& x : v) x *= inv;
    }
}

float dot(const Embedding& a, const Embedding& b) {
    float s = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
    return s;
}

}  // namespace

class EmbeddingIndex::Impl {
public:
    mutable std::shared_mutex mutex;
    std::vector<Entry>        entries;   // All embeddings pre-normalized
};

EmbeddingIndex::EmbeddingIndex()  : impl_(std::make_unique<Impl>()) {}
EmbeddingIndex::~EmbeddingIndex() = default;

void EmbeddingIndex::rebuild(std::vector<Entry> entries) {
    // Normalize while building (off the critical path).
    for (auto& e : entries) l2_normalize(e.embedding);

    {
        std::unique_lock lock(impl_->mutex);
        impl_->entries = std::move(entries);
    }
    PLOG_INFO(index, "Index rebuilt: {} embeddings", size());
}

void EmbeddingIndex::load_from_json(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) {
        throw std::runtime_error("embedding_index: cannot open " + file.string());
    }

    nlohmann::json doc;
    in >> doc;

    std::vector<Entry> entries;
    const auto& people = doc.at("people");
    entries.reserve(people.size() * 4);  // rough guess at avg embeddings/person

    int skipped_bad_dim = 0;
    for (const auto& person : people) {
        const std::string pid  = person.value("person_id", "");
        const std::string name = person.value("display_name", "");
        const std::string type = person.value("person_type", "student");
        const PersonType  pt   = (type == "employee") ? PersonType::Employee
                                                      : PersonType::Student;

        if (!person.contains("embeddings")) continue;
        for (const auto& emb : person.at("embeddings")) {
            if (!emb.is_array() || emb.size() != std::tuple_size_v<Embedding>) {
                ++skipped_bad_dim;
                continue;
            }
            Entry e{};
            e.person_id    = pid;
            e.display_name = name;
            e.person_type  = pt;
            for (std::size_t i = 0; i < e.embedding.size(); ++i) {
                e.embedding[i] = emb[i].get<float>();
            }
            entries.push_back(std::move(e));
        }
    }

    PLOG_INFO(index, "Loaded {} embeddings from {} ({} people, {} skipped)",
              entries.size(), file.string(), people.size(), skipped_bad_dim);
    rebuild(std::move(entries));
}

void EmbeddingIndex::add(Entry entry) {
    l2_normalize(entry.embedding);
    std::unique_lock lock(impl_->mutex);
    impl_->entries.push_back(std::move(entry));
}

void EmbeddingIndex::remove_person(const std::string& person_id) {
    std::unique_lock lock(impl_->mutex);
    impl_->entries.erase(
        std::remove_if(impl_->entries.begin(), impl_->entries.end(),
                       [&](const Entry& e) { return e.person_id == person_id; }),
        impl_->entries.end());
}

std::optional<EmbeddingIndex::QueryResult> EmbeddingIndex::query(
    const Embedding& query_embedding, float threshold) const {
    // Pre-normalize query
    Embedding q = query_embedding;
    l2_normalize(q);

    std::shared_lock lock(impl_->mutex);
    if (impl_->entries.empty()) return std::nullopt;

    const Entry* best = nullptr;
    float        best_score = -2.0f;

    for (const auto& entry : impl_->entries) {
        float score = dot(q, entry.embedding);
        if (score > best_score) {
            best_score = score;
            best       = &entry;
        }
    }

    if (best_score < threshold) return std::nullopt;
    return QueryResult{best->person_id, best->display_name, best->person_type, best_score};
}

std::vector<EmbeddingIndex::QueryResult> EmbeddingIndex::query_top_k(
    const Embedding& query_embedding, int k) const {
    Embedding q = query_embedding;
    l2_normalize(q);

    std::shared_lock lock(impl_->mutex);
    std::vector<QueryResult> results;
    results.reserve(impl_->entries.size());
    for (const auto& entry : impl_->entries) {
        results.push_back({entry.person_id, entry.display_name, entry.person_type,
                           dot(q, entry.embedding)});
    }

    std::partial_sort(results.begin(),
                      results.begin() + std::min(static_cast<size_t>(k), results.size()),
                      results.end(),
                      [](const QueryResult& a, const QueryResult& b) { return a.score > b.score; });

    if (static_cast<int>(results.size()) > k) results.resize(k);
    return results;
}

size_t EmbeddingIndex::size() const {
    std::shared_lock lock(impl_->mutex);
    return impl_->entries.size();
}

}  // namespace parking::inference
