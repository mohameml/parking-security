#include "people_model.hpp"

#include "parking/database/db_connection.hpp"

namespace parking::ui {

PeopleModel::PeopleModel(database::ConnectionPool& db_pool, QObject* parent)
    : QAbstractListModel(parent), db_pool_(db_pool) {
    refresh();
}

PeopleModel::~PeopleModel() = default;

int PeopleModel::rowCount(const QModelIndex&) const {
    return static_cast<int>(rows_.size());
}

QVariant PeopleModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(rows_.size())) return {};
    const Row& r = rows_[static_cast<size_t>(index.row())];
    switch (role) {
        case IdRole:              return r.id;
        case TypeRole:            return r.type;
        case NameRole:            return r.name;
        case ExamCountRole:       return r.exam_count;
        case EmbeddingCountRole:  return r.embedding_count;
        case PhotoPathRole:       return r.photo_path;
        default:                  return {};
    }
}

QHash<int, QByteArray> PeopleModel::roleNames() const {
    return {
        {IdRole,              "personId"},
        {TypeRole,            "personType"},
        {NameRole,            "personName"},
        {ExamCountRole,       "examCount"},
        {EmbeddingCountRole,  "embeddingCount"},
        {PhotoPathRole,       "photoPath"},
    };
}

void PeopleModel::setPage(int page) {
    if (page == page_) return;
    page_ = page;
    emit pageChanged();
    refresh();
}

void PeopleModel::setSearch(const QString& q) {
    if (q == search_) return;
    search_ = q;
    page_ = 1;
    emit searchChanged();
    emit pageChanged();
    refresh();
}

void PeopleModel::setTypeFilter(const QString& t) {
    if (t == type_filter_) return;
    type_filter_ = t;
    page_ = 1;
    emit typeFilterChanged();
    emit pageChanged();
    refresh();
}

void PeopleModel::refresh() {
    // TODO: query DB with pagination + search + filter, update rows_ and total_.
    beginResetModel();
    rows_.clear();
    endResetModel();
    total_ = 0;
    emit totalChanged();
}

}  // namespace parking::ui
