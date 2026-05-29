#include "event_model.hpp"

#include <QString>

namespace parking::ui {

EventModel::EventModel(EventBus& bus, QObject* parent)
    : QAbstractListModel(parent), bus_(bus) {
    sub_ = bus.subscribe([this](const RecognitionResult& r) {
        // TODO: marshal onto Qt thread via QMetaObject::invokeMethod
        (void)r;
    });
}

EventModel::~EventModel() { bus_.unsubscribe(sub_); }

int EventModel::rowCount(const QModelIndex&) const {
    return static_cast<int>(rows_.size());
}

QVariant EventModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(rows_.size())) return {};
    const Row& r = rows_[static_cast<size_t>(index.row())];
    switch (role) {
        case TypeRole:       return static_cast<int>(r.type);
        case PersonNameRole: return r.person_name;
        case ConfidenceRole: return static_cast<double>(r.confidence);
        default:             return {};
    }
}

QHash<int, QByteArray> EventModel::roleNames() const {
    return {
        {TypeRole,        "eventType"},
        {PersonNameRole,  "personName"},
        {ConfidenceRole,  "confidence"},
        {TimestampRole,   "timestamp"},
        {FaceImageRole,   "faceImage"},
    };
}

}  // namespace parking::ui
