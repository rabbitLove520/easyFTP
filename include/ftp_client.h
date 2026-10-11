#pragma once

#include "ftp_socket.h"

#include <string>
#include <vector>

namespace easyftp {

// 传输模式
enum class TransferMode {
    ASCII,
    BINARY
};

// FTP 服务器响应（支持多行）
struct FtpReply {
    int code = 0;                          // 3 位响应码
    std::vector<std::string> lines;        // 响应文本

    bool ok() const { return code > 0; }

    // 2xx 正向完成
    bool is_positive() const { return code >= 200 && code < 300; }
    // 1xx预备 / 3xx 需要后续
    bool is_preliminary() const { return code >= 100 && code < 200; }
};

// FTP 协议核心：控制连接管理、命令收发、PASV 数据连接
class FtpClient {
public:
    ~FtpClient();

    bool is_connected() const { return ctrl_.is_valid(); }

    // 连接服务器并完成欢迎语读取；登录单独进行
    bool connect(const std::string& host, uint16_t port);
    void disconnect();

    // 发送 FTP 命令并读取响应（命令自动追加 \r\n）
    FtpReply send_command(const std::string& cmd);
    // 仅读取响应（用于登录等分步交互）
    FtpReply read_reply();

    // ---- 常用命令封装 ----
    bool login(const std::string& user, const std::string& pass);
    std::string pwd();                                   // 返回远程工作目录
    bool cd(const std::string& path);
    bool mkdir(const std::string& dir);
    bool rmdir(const std::string& dir);
    bool remove_file(const std::string& file);

    // LIST 结果（通过数据连接获取，长格式，每行一项）
    bool list(std::vector<std::string>& out, const std::string& path = "");
    // NLST：仅文件名列表（mget 通配符匹配用）
    bool name_list(std::vector<std::string>& out);

    // 命令透传（TYPE A / TYPE I 等）
    bool raw(const std::string& cmd);

    // PASV 模式：解析服务器返回的地址端口并建立数据连接
    bool open_data_connection(FtpSocket& data_sock);

    // 通知服务器传输已结束（读取 226）
    bool finish_transfer();

    void set_transfer_mode(TransferMode m);
    TransferMode transfer_mode() const { return mode_; }

    const std::string& host() const { return host_; }
    uint16_t port() const { return port_; }

private:
    // 发送 LIST/NLST 并通过数据连接收集结果（按行拆分）
    bool collect_listing(const std::string& cmd, std::vector<std::string>& out);
    // 发送 TYPE A / TYPE I 使模式生效
    bool apply_transfer_mode();
    // 控制连接已死：打印原因并关闭，使 is_connected() 恢复为 false
    void note_ctrl_down();

    FtpSocket ctrl_;                  // 控制连接
    std::string host_;
    uint16_t port_ = 21;
    TransferMode mode_ = TransferMode::BINARY;
};

} // namespace easyftp
