#pragma once

#include "parking/core/types.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace parking::inference {

/// In-memory index of enrolled embeddings for fast nearest-neighbor search.
///
/// All stored embeddings are pre-normalized to unit length. A single
/// cosine-similarity lookup is implemented as one matrix-vector multiply
/// (`matrix @ query`), completing in ~0.5 ms for 10k embeddings on the
/// Jetson's NEON/SIMD units.
///
/// Future: swap the backend for FAISS-GPU + HNSW to go O(log N) for
/// very large databases.
///
/// Thread-safety: the index is internally locked. Reads and writes can
/// happen concurrently from multiple threads.
class EmbeddingIndex {
public:
    struct Entry {
        std::string person_id;        ///< Employee UUID or student NODOS
        std::string display_name;
        PersonType  person_type;
        Embedding   embedding;        ///< Already L2-normalized
    };

    struct QueryResult {
        std::string person_id;
        std::string display_name;
        PersonType  person_type;
        float       score;            ///< Cosine similarity, [-1, 1]
    };

    EmbeddingIndex();
    ~EmbeddingIndex();

    /// Replace the entire index atomically. The rebuild happens off to the side
    /// and the pointer is swapped in when complete, so readers never see a
    /// partial state.
    void rebuild(std::vector<Entry> entries);

    /// Load embeddings from the JSON file produced by
    /// `scripts/export_embeddings.sh`. Throws on I/O or parse errors.
    void load_from_json(const std::filesystem::path& file);

    /// Add a single embedding (used by the progressive learning pipeline).
    void add(Entry entry);

    /// Remove all embeddings for a person (used on employee deactivation).
    void remove_person(const std::string& person_id);

    /// Find the single best match. Returns nullopt if score < threshold
    /// or the index is empty.
    std::optional<QueryResult> query(const Embedding& query_embedding,
                                      float threshold = 0.5f) const;

    /// Find the top-K matches.
    std::vector<QueryResult> query_top_k(const Embedding& query_embedding,
                                          int k = 5) const;

    /// Current size.
    size_t size() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace parking::inference
