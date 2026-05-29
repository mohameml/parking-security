#pragma once

#include "parking/core/event_bus.hpp"

#include <QAbstractListModel>
#include <deque>

namespace parking::ui {

/// Exposes a rolling list of the last N events to QML.
/// Subscribes to the EventBus and emits model signals on the Qt thread.
class EventModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        TypeRole        = Qt::UserRole + 1,
        PersonNameRole,
        ConfidenceRole,
        TimestampRole,
        FaceImageRole,    ///< Data URL to embedded JPEG thumbnail
    };

    explicit EventModel(EventBus& bus, QObject* parent = nullptr);
    ~EventModel() override;

    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    /// Emitted on the UI thread whenever a new event arrives (for sound + toast).
    void newEvent(int type, const QString& name, double confidence);

private:
    struct Row {
        EventType   type;
        QString     person_name;
        float       confidence;
        Timestamp   captured_at;
    };

    EventBus&                      bus_;
    EventBus::SubscriptionId       sub_{0};
    std::deque<Row>                rows_;
    static constexpr size_t        kMaxRows = 100;
};

}  // namespace parking::ui
