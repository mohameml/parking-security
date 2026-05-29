#pragma once

// Tiny single-file MJPEG HTTP server. Clients get a multipart/x-mixed-replace
// stream of JPEG frames — works directly in Chrome/Firefox/VLC by opening the
// URL, no extra viewer required. Zero external dependencies (POSIX sockets).
//
// Usage:
//   MjpegServer srv;
//   srv.start(8090);            // listen on 0.0.0.0:8090
//   // ... whenever you have a fresh JPEG frame ...
//   srv.publish(jpeg_bytes);
//   // on shutdown:
//   srv.stop();
//
// Thread model:
//   - One accept thread per server.
//   - One worker thread per connected client (detached; self-terminates when
//     the client disconnects or the server stops).
//   - publish() is safe to call from any thread; fans out to all clients.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace parking::camera {

class MjpegServer {
public:
    MjpegServer() = default;
    ~MjpegServer() { stop(); }

    MjpegServer(const MjpegServer&)            = delete;
    MjpegServer& operator=(const MjpegServer&) = delete;

    /// Begin listening on 0.0.0.0:port. Returns false if bind/listen fails.
    bool start(int port);

    /// Stop the server; joins the accept thread and notifies all clients.
    void stop();

    /// Publish a new JPEG frame. Copies the bytes; caller-owned buffer can be
    /// reused immediately. No-op if the server isn't running.
    void publish(const uint8_t* data, std::size_t size);

    bool is_running() const noexcept { return running_.load(); }
    int  port() const noexcept { return port_; }

private:
    void accept_loop();
    void serve_client(int client_fd);

    std::atomic<bool>       running_{false};
    int                     listen_fd_{-1};
    int                     port_{0};
    std::thread             accept_thread_;

    // Latest frame shared with client threads.
    mutable std::mutex      frame_mu_;
    std::condition_variable frame_cv_;
    std::vector<uint8_t>    latest_jpeg_;
    uint64_t                frame_seq_{0};
};

}  // namespace parking::camera
