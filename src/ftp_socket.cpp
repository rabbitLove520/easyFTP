#include "ftp_socket.h"

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

namespace easyftp {

FtpSocket::~FtpSocket() {
    close();
}

bool FtpSocket::connect(const std::string& host, uint16_t port, int timeout_sec) {
    close();

    char port_str[16];
    std::snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;        // IPv4/IPv6 均可
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), port_str, &hints, &res) != 0 || !res)
        return false;

    for (struct addrinfo* p = res; p; p = p->ai_next) {
        int fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        // 非阻塞连接 + poll 超时
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int r = ::connect(fd, p->ai_addr, p->ai_addrlen);
        if (r != 0) {
            if (errno != EINPROGRESS) {
                ::close(fd);
                continue;
            }
            struct pollfd pfd{fd, POLLOUT, 0};
            if (poll(&pfd, 1, timeout_sec * 1000) <= 0 || !(pfd.revents & POLLOUT)) {
                ::close(fd);
                continue;
            }
            int err = 0;
            socklen_t len = sizeof(err);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
                ::close(fd);
                continue;
            }
        }
        // 恢复阻塞模式
        fcntl(fd, F_SETFL, flags);

        fd_ = fd;
        freeaddrinfo(res);
        enable_keepalive();
        return true;
    }
    freeaddrinfo(res);
    return false;
}

void FtpSocket::close() {
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
    in_buf_.clear();
}

bool FtpSocket::send_all(const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    size_t sent = 0;
    while (sent < len) {
        // MSG_NOSIGNAL: 对端已关闭时不触发 SIGPIPE，返回 EPIPE 错误
        ssize_t n = ::send(fd_, p + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        sent += (size_t)n;
    }
    return true;
}

bool FtpSocket::send_string(const std::string& data) {
    return send_all(data.data(), data.size());
}

ssize_t FtpSocket::recv_raw(void* buf, size_t len) {
    for (;;) {
        ssize_t n = ::recv(fd_, buf, len, 0);
        if (n < 0 && errno == EINTR) continue;
        return n;
    }
}

bool FtpSocket::recv_line(std::string& line) {
    line.clear();
    for (;;) {
        size_t pos = in_buf_.find('\n');
        if (pos != std::string::npos) {
            line.assign(in_buf_, 0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            in_buf_.erase(0, pos + 1);
            return true;
        }
        char tmp[4096];
        ssize_t n = recv_raw(tmp, sizeof(tmp));
        if (n <= 0) return false;    // EOF / 错误 / 超时
        in_buf_.append(tmp, (size_t)n);
    }
}

bool FtpSocket::recv_exact(void* buf, size_t len) {
    char* p = static_cast<char*>(buf);
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv_raw(p + got, len - got);
        if (n <= 0) return false;
        got += (size_t)n;
    }
    return true;
}

void FtpSocket::enable_keepalive() {
    if (fd_ < 0) return;
    int on = 1;
    setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
#ifdef TCP_KEEPIDLE
    // 空闲 30s 后开始探测，每 10s 一次，3 次无响应判定断链
    int idle = 30, intvl = 10, cnt = 3;
    setsockopt(fd_, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(fd_, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(fd_, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif
}

void FtpSocket::set_recv_timeout(int seconds) {
    if (fd_ < 0) return;
    struct timeval tv{seconds, 0};
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

} // namespace easyftp
