#include "parking/database/event_repository.hpp"

#include "parking/core/logger.hpp"

#include <pqxx/pqxx>

#include <cstdio>
#include <ctime>
#include <stdexcept>

namespace parking::database {

namespace {

/// Format a Timestamp (microsecond-precision system_clock time_point) as an
/// ISO-8601 string in UTC that libpq accepts for `timestamp with time zone`.
/// Example: "2026-04-19T02:59:41.123456+00:00"
std::string to_iso8601_utc(Timestamp ts) {
    using namespace std::chrono;
    const auto sys_tp = time_point_cast<system_clock::duration>(ts);
    const auto tt     = system_clock::to_time_t(sys_tp);
    const auto frac   = duration_cast<microseconds>(
                            ts.time_since_epoch()).count() % 1000000;

    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[40];
    std::snprintf(buf, sizeof(buf),
                  "%04d-%02d-%02dT%02d:%02d:%02d.%06ld+00:00",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<long>(frac));
    return buf;
}

EventType event_type_from_string(std::string_view s) noexcept {
    if (s == "authorized") return EventType::Authorized;
    if (s == "wrong_time") return EventType::WrongTime;
    if (s == "wrong_day")  return EventType::WrongDay;
    return EventType::Unknown;
}

}  // namespace

EventRepository::EventRepository(ConnectionPool& pool) : pool_(pool) {}

std::string EventRepository::insert(const NewEvent& event) {
    auto handle = pool_.acquire();
    pqxx::work txn(*handle);

    // Only populate events.employee_id when we actually matched an employee.
    // For students and unknowns the column stays NULL (the FK is to employees
    // only), and the name travels in employee_name for display purposes.
    std::optional<std::string> employee_id;
    if (event.person_type == PersonType::Employee && event.person_id) {
        employee_id = *event.person_id;
    }

    const std::string ts = to_iso8601_utc(event.captured_at);
    const std::string et = std::string(to_string(event.event_type));

    const pqxx::result r = txn.exec_params(
        "INSERT INTO events "
        "  (id, camera_id, event_type, employee_id, employee_name, "
        "   confidence, bbox, frame_image, alert_sent, timestamp) "
        "VALUES "
        "  (gen_random_uuid(), $1, $2, $3, $4, $5, $6, $7, false, $8) "
        "RETURNING id::text",
        event.camera_id,
        et,
        employee_id,
        event.person_name,
        event.confidence,
        event.bbox_json,
        event.frame_path.empty() ? std::optional<std::string>{} : std::optional{event.frame_path},
        ts);
    txn.commit();

    const auto event_id = r[0][0].as<std::string>();
    PLOG_DEBUG(events, "Inserted {} event for camera={} person={} (id={})",
               et, event.camera_id,
               event.person_name.value_or("<none>"), event_id);
    return event_id;
}

void EventRepository::mark_alert_sent(const std::string& event_id) {
    auto handle = pool_.acquire();
    pqxx::work txn(*handle);
    txn.exec_params("UPDATE events SET alert_sent = true WHERE id = $1::uuid",
                    event_id);
    txn.commit();
}

std::vector<EventRepository::StoredEvent> EventRepository::list(const Query& query) {
    // Use COALESCE tricks so we can always pass 3 params (libpqxx 6.4's
    // exec_params wants a fixed arg pack at compile time).
    auto handle = pool_.acquire();
    pqxx::nontransaction txn(*handle);

    const std::string type_filter =
        query.type ? std::string(to_string(*query.type)) : std::string{};
    const std::string cam_filter  = query.camera_id.value_or(std::string{});
    const long lookback_secs =
        std::chrono::duration_cast<std::chrono::seconds>(query.lookback).count();

    const std::string sql =
        "SELECT id::text, camera_id, event_type, employee_id::text, "
        "       employee_name, confidence, alert_sent "
        "FROM events "
        "WHERE timestamp >= NOW() - make_interval(secs => $1) "
        "  AND ($2 = '' OR event_type = $2) "
        "  AND ($3 = '' OR camera_id  = $3) "
        "ORDER BY timestamp DESC "
        "LIMIT " + std::to_string(query.limit);

    const pqxx::result rows =
        txn.exec_params(sql, lookback_secs, type_filter, cam_filter);

    std::vector<StoredEvent> out;
    out.reserve(rows.size());
    for (auto row = rows.begin(); row != rows.end(); ++row) {
        StoredEvent e{};
        e.event_id   = (*row)[0].as<std::string>();
        e.camera_id  = (*row)[1].as<std::string>();
        e.event_type = event_type_from_string((*row)[2].as<std::string>());
        if (!(*row)[3].is_null()) e.person_id   = (*row)[3].as<std::string>();
        if (!(*row)[4].is_null()) e.person_name = (*row)[4].as<std::string>();
        e.confidence = (*row)[5].is_null() ? 0.0f : (*row)[5].as<float>();
        e.alert_sent = !(*row)[6].is_null() && (*row)[6].as<bool>();
        out.push_back(std::move(e));
    }
    return out;
}

int EventRepository::clear(const Query& query) {
    auto handle = pool_.acquire();
    pqxx::work txn(*handle);

    const std::string type_filter =
        query.type ? std::string(to_string(*query.type)) : std::string{};
    const std::string cam_filter  = query.camera_id.value_or(std::string{});
    const long lookback_secs =
        std::chrono::duration_cast<std::chrono::seconds>(query.lookback).count();

    const pqxx::result r = txn.exec_params(
        "DELETE FROM events "
        "WHERE timestamp >= NOW() - make_interval(secs => $1) "
        "  AND ($2 = '' OR event_type = $2) "
        "  AND ($3 = '' OR camera_id  = $3)",
        lookback_secs, type_filter, cam_filter);
    txn.commit();
    return static_cast<int>(r.affected_rows());
}

}  // namespace parking::database
