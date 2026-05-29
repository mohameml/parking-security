#pragma once

#include <QAbstractListModel>
#include <QString>
#include <vector>

namespace parking::database { class ConnectionPool; }

namespace parking::ui {

/// Paginated list of enrolled people (employees + students) for the People page.
class PeopleModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int     page       READ page       WRITE setPage       NOTIFY pageChanged)
    Q_PROPERTY(int     pageSize   READ pageSize   CONSTANT)
    Q_PROPERTY(int     totalCount READ totalCount NOTIFY totalChanged)
    Q_PROPERTY(QString search     READ search     WRITE setSearch     NOTIFY searchChanged)
    Q_PROPERTY(QString typeFilter READ typeFilter WRITE setTypeFilter NOTIFY typeFilterChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TypeRole,
        NameRole,
        ExamCountRole,
        EmbeddingCountRole,
        PhotoPathRole,
    };

    explicit PeopleModel(database::ConnectionPool& db_pool, QObject* parent = nullptr);
    ~PeopleModel() override;

    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int     page() const noexcept       { return page_; }
    int     pageSize() const noexcept   { return kPageSize; }
    int     totalCount() const noexcept { return total_; }
    QString search() const              { return search_; }
    QString typeFilter() const          { return type_filter_; }

    void setPage(int page);
    void setSearch(const QString& q);
    void setTypeFilter(const QString& t);

signals:
    void pageChanged();
    void totalChanged();
    void searchChanged();
    void typeFilterChanged();

private:
    struct Row { QString id, type, name, photo_path; int exam_count, embedding_count; };

    void refresh();

    database::ConnectionPool& db_pool_;
    std::vector<Row>          rows_;
    int                       page_{1};
    int                       total_{0};
    QString                   search_;
    QString                   type_filter_{"all"};
    static constexpr int      kPageSize = 25;
};

}  // namespace parking::ui
