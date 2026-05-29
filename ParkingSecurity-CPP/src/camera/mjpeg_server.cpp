#include "parking/camera/mjpeg_server.hpp"

#include "parking/core/logger.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

namespace parking::camera {

namespace {

bool send_all(int fd, const void* data, std::size_t size) {
    const auto* p = static_cast<const char*>(data);
    std::size_t left = size;
    while (left > 0) {
        const ssize_t n = ::send(fd, p, left, MSG_NOSIGNAL);
        if (n <= 0) return false;
        p    += n;
        left -= static_cast<std::size_t>(n);
    }
    return true;
}

// Landing page makes the default URL render a browser-viewable stream even
// when the user types the bare host:port into the address bar. When the
// daemon restarts (our supervisor rebuilds the pipeline on CUDA glitches),
// the multipart stream connection drops; the JS below auto-reconnects by
// re-setting the img.src every 2s until the img fires load events again.
constexpr const char* kIndexHtml =
    "<!doctype html>\n"
    "<html><head><title>Parking Security \u2014 live</title>"
    "<style>body{margin:0;background:#111;display:flex;align-items:center;"
    "justify-content:center;height:100vh;color:#888;font-family:sans-serif}"
    "img{max-width:100%;max-height:100vh}</style></head><body>"
    "<img id=\"s\" src=\"/stream\"/>"
    "<script>"
    "(function(){var img=document.getElementById('s');"
    "var lastLoad=Date.now();"
    "img.addEventListener('load',function(){lastLoad=Date.now();});"
    "img.addEventListener('error',function(){setTimeout(reconnect,1000);});"
    "function reconnect(){img.src='/stream?t='+Date.now();}"
    "setInterval(function(){"
    "  if(Date.now()-lastLoad>4000){reconnect();lastLoad=Date.now();}"
    "},2000);"
    "})();"
    "</script></body></html>\n";

}  // namespace

bool MjpegServer::start(int port) {
    if (running_.exchange(true)) return true;

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        PLOG_ERROR(stream, "socket() failed: {}", std::strerror(errno));
        running_.store(false);
        return false;
    }

    const int yes = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(static_cast<uint16_t>(port));
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        PLOG_ERROR(stream, "bind({}) failed: {}", port, std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_.store(false);
        return false;
    }
    if (::listen(listen_fd_, 4) < 0) {
        PLOG_ERROR(stream, "listen() failed: {}", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_.store(false);
        return false;
    }

    port_           = port;
    accept_thread_  = std::thread(&MjpegServer::accept_loop, this);
    PLOG_INFO(stream, "MJPEG server listening on 0.0.0.0:{}", port);
    return true;
}

void MjpegServer::stop() {
    if (!running_.exchange(false)) return;

    // Close the listener to unblock accept().
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) accept_thread_.join();

    // Wake up per-client workers so they exit their wait loop.
    frame_cv_.notify_all();
    PLOG_INFO(stream, "MJPEG server stopped");
}

void MjpegServer::publish(const uint8_t* data, std::size_t size) {
    if (!running_.load()) return;
    {
        std::lock_guard<std::mutex> lock(frame_mu_);
        latest_jpeg_.assign(data, data + size);
        ++frame_seq_;
    }
    frame_cv_.notify_all();
}

void MjpegServer::accept_loop() {
    while (running_.load()) {
        sockaddr_in peer{};
        socklen_t   peer_len = sizeof(peer);
        const int   client_fd = ::accept(listen_fd_,
                                         reinterpret_cast<sockaddr*>(&peer),
                                         &peer_len);
        if (client_fd < 0) {
            if (!running_.load()) break;
            if (errno == EINTR)  continue;
            PLOG_WARN(stream, "accept() failed: {}", std::strerror(errno));
            continue;
        }
        PLOG_INFO(stream, "client connected: {}:{}",
                  ::inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
        std::thread(&MjpegServer::serve_client, this, client_fd).detach();
    }
}

void MjpegServer::serve_client(int client_fd) {
    // Read up to 2 KB of request headers. We only care about the first line
    // to decide whether to serve the index page or the MJPEG stream.
    char  req_buf[2048];
    const ssize_t nr = ::recv(client_fd, req_buf, sizeof(req_buf) - 1, 0);
    if (nr <= 0) { ::close(client_fd); return; }
    req_buf[nr] = '\0';

    const bool wants_stream =
        std::strstr(req_buf, "GET /stream") != nullptr;

    if (!wants_stream) {
        // Serve the tiny HTML landing page (so the user can just type the
        // bare host:port URL and see the stream).
        char hdr[256];
        const int hdr_len = std::snprintf(
            hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n",
            std::strlen(kIndexHtml));
        send_all(client_fd, hdr, static_cast<std::size_t>(hdr_len));
        send_all(client_fd, kIndexHtml, std::strlen(kIndexHtml));
        ::close(client_fd);
        return;
    }

    // MJPEG stream response.
    static constexpr const char* kStreamHdr =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=--frame\r\n"
        "Cache-Control: no-cache, private\r\n"
        "Pragma: no-cache\r\n"
        "Connection: close\r\n\r\n";
    if (!send_all(client_fd, kStreamHdr, std::strlen(kStreamHdr))) {
        ::close(client_fd);
        return;
    }

    uint64_t last_seq = 0;
    while (running_.load()) {
        std::vector<uint8_t> frame;
        {
            std::unique_lock<std::mutex> lock(frame_mu_);
            frame_cv_.wait_for(lock, std::chrono::milliseconds(500),
                [&]{ return frame_seq_ != last_seq || !running_.load(); });
            if (!running_.load()) break;
            if (frame_seq_ == last_seq) continue;  // spurious wake / timeout
            frame    = latest_jpeg_;
            last_seq = frame_seq_;
        }

        char part_hdr[128];
        const int part_len = std::snprintf(
            part_hdr, sizeof(part_hdr),
            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
            frame.size());
        if (!send_all(client_fd, part_hdr, static_cast<std::size_t>(part_len))) break;
        if (!send_all(client_fd, frame.data(), frame.size()))                     break;
        if (!send_all(client_fd, "\r\n", 2))                                      break;
    }
    ::close(client_fd);
}

}  // namespace parking::camera
