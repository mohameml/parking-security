#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

// libpqxx's `connection` is a type alias, not a class, so we can't forward-declare
// it cleanly. Just include it — downstream database code needs pqxx anyway.
#include <pqxx/connection>

namespace parking::database {

/// Small connection pool wrapping libpqxx. Threads check out a connection,
/// run a transaction, and return it. Pool size is set at construction.
class ConnectionPool {
public:
    /// A RAII handle returning a connection to the pool on destruction.
    class Handle {
    public:
        Handle(ConnectionPool* pool, std::unique_ptr<pqxx::connection> conn);
        ~Handle();

        Handle(Handle&&) noexcept;
        Handle& operator=(Handle&&) noexcept;
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;

        pqxx::connection& operator*() noexcept { return *conn_; }
        pqxx::connection* operator->() noexcept { return conn_.get(); }
        explicit operator bool() const noexcept { return conn_ != nullptr; }

    private:
        ConnectionPool*                   pool_;
        std::unique_ptr<pqxx::connection> conn_;
    };

    ConnectionPool(const std::string& connection_string, int pool_size);
    ~ConnectionPool();

    /// Acquire a connection (blocks if all are in use, up to `timeout`).
    Handle acquire();

private:
    void release(std::unique_ptr<pqxx::connection> conn);
    friend class Handle;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace parking::database
