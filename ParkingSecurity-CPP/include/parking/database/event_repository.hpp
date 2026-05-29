#pragma once

#include "parking/core/types.hpp"
#include "parking/database/db_connection.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace parking::database {

/// Persistence for detection events.
class EventRepository {
public:
    explicit EventRepository(ConnectionPool& pool);

    struct NewEvent {
        std::string                camera_id;
        EventType                  event_type;
        /// UUID of the matched person. Only written to events.employee_id
        /// when person_type == Employee; otherwise stored NULL and the name
        /// comes from person_name (for students / unknowns).
        std::optional<std::string> person_id;
        std::optional<PersonType>  person_type;
        std::optional<std::string> person_name;
        float                      confidence{0.0f};
        std::string                bbox_json;     ///< "[x1,y1,x2,y2]"
        std::vector<uint8_t>       face_jpeg;     ///< Cropped face, JPEG-encoded (optional)
        std::string                frame_path;    ///< Relative path to full frame on disk (optional)
        Timestamp                  captured_at;
    };

    /// Insert a new event. Returns the generated event UUID.
    std::string insert(const NewEvent& event);

    /// Mark an event as having had its alert delivered.
    void mark_alert_sent(const std::string& event_id);

    struct Query {
        std::optional<EventType>   type;
        std::optional<std::string> camera_id;
        std::chrono::hours         lookback{24};
        int                        limit{200};
    };

    struct StoredEvent {
        std::string                event_id;
        std::string                camera_id;
        EventType                  event_type;
        std::optional<std::string> person_id;
        std::optional<std::string> person_name;
        float                      confidence;
        bool                       alert_sent;
        Timestamp                  captured_at;
    };

    std::vector<StoredEvent> list(const Query& query);

    /// Delete events matching the query; returns number deleted.
    int clear(const Query& query);

private:
    ConnectionPool& pool_;
};

}  // namespace parking::database
