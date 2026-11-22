// Embedded POSIX HTTP server for the dashboard.
#include "dev_atlas/server.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <thread>
#include <unistd.h>
#include <netinet/in.h>
#include <signal.h>

namespace dev_atlas {

namespace {

std::atomic<bool> g_stop{false};
int g_listen_fd = -1;

void on_signal(int) {
    g_stop = true;
    if (g_listen_fd >= 0) {
        ::close(g_listen_fd);
        g_listen_fd = -1;
    }
}

std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void send_all(int fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, data + sent, len - sent, 0);
        if (n <= 0) return;
        sent += static_cast<size_t>(n);
    }
}

struct Route { std::string body; std::string ctype; int status; const char* stext; };

Route route(const std::string& path, const std::filesystem::path& html,
            const std::filesystem::path& json) {
    if (path == "/" || path == "/index.html") {
        auto b = read_file(html);
        if (!b.empty()) return {b, "text/html", 200, "OK"};
    } else if (path == "/api/scan.json") {
        auto b = read_file(json);
        if (!b.empty()) return {b, "application/json", 200, "OK"};
    }
    return {"not found", "text/plain", 404, "Not Found"};
}

void handle_conn(int fd, const std::filesystem::path& html, const std::filesystem::path& json) {
    char buf[8192];
    ssize_t total = 0;
    while (total < static_cast<ssize_t>(sizeof buf) - 1) {
        ssize_t n = ::read(fd, buf + total, sizeof buf - 1 - static_cast<size_t>(total));
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';
        if (std::strstr(buf, "\r\n\r\n")) break;
    }
    std::string req(buf, static_cast<size_t>(total));
    auto eol = req.find("\r\n");
    std::string first = eol != std::string::npos ? req.substr(0, eol) : req;
    std::string path;
    if (first.size() > 4 && first.substr(0, 4) == "GET ") {
        auto sp = first.find(' ', 4);
        path = sp != std::string::npos ? first.substr(4, sp - 4) : first.substr(4);
    }
    auto r = route(path, html, json);
    char hdr[512];
    int hl = std::snprintf(hdr, sizeof hdr,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        r.status, r.stext, r.ctype.c_str(), r.body.size());
    send_all(fd, hdr, static_cast<size_t>(hl));
    if (!r.body.empty()) send_all(fd, r.body.data(), r.body.size());
    ::close(fd);
}

}  // namespace

int serve(const std::filesystem::path& html_path, const std::filesystem::path& json_path, int port,
          const std::string& host) {
    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = ::inet_addr(host.c_str());
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof addr) < 0) {
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 16) < 0) {
        ::close(fd);
        return -1;
    }
    g_listen_fd = fd;
    while (!g_stop) {
        int cfd = ::accept(fd, nullptr, nullptr);
        if (cfd < 0) {
            if (g_stop) break;
            continue;
        }
        std::thread(handle_conn, cfd, html_path, json_path).detach();
    }
    ::close(fd);
    g_listen_fd = -1;
    return 0;
}

}  // namespace dev_atlas
