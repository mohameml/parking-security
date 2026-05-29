#pragma once

#include "parking/core/types.hpp"
#include "parking/database/db_connection.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace parking::schedule {

/// Checks whether a given student is authorized to be present right now
/// based on their exam schedule. Results are cached in-memory with a TTL
/// to avoid hammering the database on every frame (a single person may
/// be detected 30 times/second).
class ScheduleChecker {
public:
    struct Config {
        std::chrono::seconds cache_ttl{600};   ///< 10 minutes
    };

    struct TimeSlot {
        std::string code;            ///< "H1" or "H2"
        std::string name;            ///< "Matin" or "Après-midi"
        std::chrono::seconds start;  ///< Seconds past midnight
        std::chrono::seconds end;
    };

    struct Result {
        EventType                   event_type;   ///< Authorized / WrongTime / WrongDay
        std::string                 message;
        std::optional<std::string>  subject;
        std::optional<std::string>  room;
        std::optional<std::string>  time_slot;
    };

    explicit ScheduleChecker(database::ConnectionPool& pool);
    ScheduleChecker(database::ConnectionPool& pool, Config config);
    ~ScheduleChecker();

    /// Check if a student is authorized to be here right now. Employees (non-students)
    /// always return Authorized; no schedule check is performed.
    Result check(PersonType type, const std::string& person_id);

    /// Invalidate cache for a specific student (called after schedule import).
    void invalidate(const std::string& person_id);

    /// Invalidate the entire cache.
    void invalidate_all();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace parking::schedule
