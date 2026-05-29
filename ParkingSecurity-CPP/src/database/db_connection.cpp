#include "parking/database/db_connection.hpp"

#include "parking/core/logger.hpp"

#include <pqxx/pqxx>

#include <condition_variable>
#include <mutex>
#include <queue>

namespace parking::database {

class ConnectionPool::Impl {
public:
    std::string                                     conn_string;
    int                                             size{5};
    std::mutex                                      mutex;
    std::condition_variable                         cv;
    std::queue<std::unique_ptr<pqxx::connection>>   available;
    int                                             in_use{0};
};

ConnectionPool::ConnectionPool(const std::string& connection_string, int pool_size)
    : impl_(std::make_unique<Impl>()) {
    impl_->conn_string = connection_string;
    impl_->size = pool_size;

    for (int i = 0; i < pool_size; ++i) {
        try {
            impl_->available.push(std::make_unique<pqxx::connection>(connection_string));
        } catch (const std::exception& e) {
            PLOG_ERROR(database, "Failed to create connection {}: {}", i, e.what());
            throw;
        }
    }
    PLOG_INFO(database, "Connection pool created ({} connections)", pool_size);
}

ConnectionPool::~ConnectionPool() = default;

ConnectionPool::Handle ConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->cv.wait(lock, [this]() { return !impl_->available.empty(); });
    auto conn = std::move(impl_->available.front());
    impl_->available.pop();
    ++impl_->in_use;
    return Handle{this, std::move(conn)};
}

void ConnectionPool::release(std::unique_ptr<pqxx::connection> conn) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->available.push(std::move(conn));
    --impl_->in_use;
    impl_->cv.notify_one();
}

// ─── Handle ──────────────────────────────────────────────────────

ConnectionPool::Handle::Handle(ConnectionPool* pool, std::unique_ptr<pqxx::connection> conn)
    : pool_(pool), conn_(std::move(conn)) {}

ConnectionPool::Handle::Handle(Handle&& other) noexcept
    : pool_(other.pool_), conn_(std::move(other.conn_)) {
    other.pool_ = nullptr;
}

ConnectionPool::Handle& ConnectionPool::Handle::operator=(Handle&& other) noexcept {
    if (this != &other) {
        if (conn_ && pool_) pool_->release(std::move(conn_));
        pool_       = other.pool_;
        conn_       = std::move(other.conn_);
        other.pool_ = nullptr;
    }
    return *this;
}

ConnectionPool::Handle::~Handle() {
    if (conn_ && pool_) pool_->release(std::move(conn_));
}

}  // namespace parking::database
