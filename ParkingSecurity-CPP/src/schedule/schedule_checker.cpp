#include "parking/schedule/schedule_checker.hpp"

#include "parking/core/logger.hpp"

#include <pqxx/pqxx>

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Mirrors the Python backend's /api/schedule/check/{student_id} logic:
//   1. Find all exam_schedules rows the student is assigned to.
//   2. If none are scheduled for today → WrongDay.
//   3. If any today-exam has now ∈ [start_time, end_time] → Authorized.
//   4. Otherwise → WrongTime.

namespace parking::schedule {

namespace {

struct ExamSlot {
    std::string subject;
    std::string room;
    std::string time_slot;
    std::chrono::system_clock::time_point start_utc;
    std::chrono::system_clock::time_point end_utc;
};

struct CacheEntry {
    ScheduleChecker::Result               result;
    std::chrono::steady_clock::time_point cached_at;
};

}  // namespace

class ScheduleChecker::Impl {
public:
    database::ConnectionPool& pool;
    Config                    config;
    std::mutex                mutex;
    std::unordered_map<std::string, CacheEntry> cache;

    Impl(database::ConnectionPool& p, Config c) : pool(p), config(c) {}
};

ScheduleChecker::ScheduleChecker(database::ConnectionPool& pool)
    : impl_(std::make_unique<Impl>(pool, Config{})) {}

ScheduleChecker::ScheduleChecker(database::ConnectionPool& pool, Config config)
    : impl_(std::make_unique<Impl>(pool, config)) {}

ScheduleChecker::~ScheduleChecker() = default;

ScheduleChecker::Result ScheduleChecker::check(PersonType type,
                                                const std::string& person_id) {
    // Employees bypass the schedule check entirely; they're always on.
    if (type != PersonType::Student) {
        Result r{};
        r.event_type = EventType::Authorized;
        r.message    = "Employee — always authorized";
        return r;
    }

    const auto now_steady = std::chrono::steady_clock::now();

    // Cache lookup (short TTL to keep exam slot decisions near real-time).
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto it = impl_->cache.find(person_id);
        if (it != impl_->cache.end() &&
            now_steady - it->second.cached_at < impl_->config.cache_ttl) {
            return it->second.result;
        }
    }

    // DB lookup: collect every (exam_date, start_time, end_time, subject, room,
    // time_slot) that this student is enrolled in. We do the today/time_slot
    // comparison in C++ so the query can stay simple and index-friendly.
    Result result{};
    try {
        auto handle = impl_->pool.acquire();
        pqxx::nontransaction txn(*handle);

        const auto rows = txn.exec_params(
            "SELECT es.exam_date, es.start_time, es.end_time, "
            "       es.subject, es.room, es.time_slot "
            "FROM exam_schedules es "
            "JOIN student_exams se ON se.exam_id = es.id "
            "WHERE se.student_id = $1::uuid "
            "ORDER BY es.exam_date, es.start_time",
            person_id);

        if (rows.empty()) {
            result.event_type = EventType::WrongDay;
            result.message     = "No scheduled exams for this student";
        } else {
            const auto now_sys = std::chrono::system_clock::now();
            const auto tt      = std::chrono::system_clock::to_time_t(now_sys);
            std::tm tm_utc{};
#ifdef _WIN32
            gmtime_s(&tm_utc, &tt);
#else
            gmtime_r(&tt, &tm_utc);
#endif
            const int today_year  = tm_utc.tm_year + 1900;
            const int today_mon   = tm_utc.tm_mon + 1;
            const int today_day   = tm_utc.tm_mday;
            const int now_seconds =
                tm_utc.tm_hour * 3600 + tm_utc.tm_min * 60 + tm_utc.tm_sec;

            bool has_today = false;
            bool matched   = false;
            for (const auto& row : rows) {
                const std::string exam_date_s = row[0].as<std::string>();  // "YYYY-MM-DD"
                int y = 0, m = 0, d = 0;
                if (std::sscanf(exam_date_s.c_str(), "%d-%d-%d", &y, &m, &d) != 3)
                    continue;
                if (!(y == today_year && m == today_mon && d == today_day))
                    continue;

                has_today = true;
                const std::string start_s = row[1].as<std::string>();  // "HH:MM:SS"
                const std::string end_s   = row[2].as<std::string>();
                int sh = 0, sm = 0, ss = 0, eh = 0, em = 0, es = 0;
                std::sscanf(start_s.c_str(), "%d:%d:%d", &sh, &sm, &ss);
                std::sscanf(end_s.c_str(),   "%d:%d:%d", &eh, &em, &es);
                const int start_secs = sh * 3600 + sm * 60 + ss;
                const int end_secs   = eh * 3600 + em * 60 + es;

                if (now_seconds >= start_secs && now_seconds <= end_secs) {
                    result.event_type = EventType::Authorized;
                    result.subject    = row[3].as<std::string>();
                    result.room       = row[4].as<std::string>();
                    result.time_slot  = row[5].as<std::string>();
                    result.message    = "Authorized for " + *result.subject +
                                        " in " + *result.room;
                    matched           = true;
                    break;
                }
            }

            if (!matched) {
                if (has_today) {
                    result.event_type = EventType::WrongTime;
                    result.message    = "Has exam today but outside time slot";
                } else {
                    result.event_type = EventType::WrongDay;
                    result.message    = "No exam scheduled for today";
                }
            }
        }
    } catch (const std::exception& e) {
        PLOG_ERROR(schedule, "DB error checking schedule for {}: {}",
                   person_id, e.what());
        // On DB failure, fail-closed to Unknown (we'd rather not wave someone
        // through based on a timed-out query).
        result.event_type = EventType::Unknown;
        result.message     = std::string("schedule DB error: ") + e.what();
    }

    // Cache the decision.
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->cache[person_id] = {result, now_steady};
    }
    return result;
}

void ScheduleChecker::invalidate(const std::string& person_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->cache.erase(person_id);
}

void ScheduleChecker::invalidate_all() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->cache.clear();
}

}  // namespace parking::schedule
