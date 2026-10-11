#pragma once

#include <string>
#include <cstdint>
#include <cstddef>

namespace easyftp {

// TCP Socket 封装：负责连接、保活（keepalive）、收发与超时控制
class FtpSocket {
public:
    FtpSocket() = default;
    ~FtpSocket();

    FtpSocket(const FtpSocket&) = delete;
    FtpSocket& operator=(const FtpSocket&) = delete;

    // 连接到 host:port，超时 timeout_sec 秒；成功后自动启用 TCP 保活
    bool connect(const std::string& host, uint16_t port, int timeout_sec = 15);
    void close();

    bool is_valid() const { return fd_ >= 0; }
    int fd() const { return fd_; }

    // 发送完整数据（循环 send 直到全部发出）
    bool send_all(const void* buf, size_t len);
    bool send_string(const std::string& data);

    // 原始接收：>0 实际字节数；0 对端关闭；-1 错误
    ssize_t recv_raw(void* buf, size_t len);

    // 读取一行（以 \r\n 或 \n 结尾）
    bool recv_line(std::string& line);

    // 读取固定长度（二进制传输用）
    bool recv_exact(void* buf, size_t len);

    // 启用 TCP keepalive（空闲探测，防止 NAT/防火墙断链）
    void enable_keepalive();

    // 设置接收超时（秒），用于非阻塞式读取
    void set_recv_timeout(int seconds);

private:
    int fd_ = -1;
    std::string in_buf_;    // 接收缓冲（recv_line 跨调用保留剩余数据）
};

} // namespace easyftp
