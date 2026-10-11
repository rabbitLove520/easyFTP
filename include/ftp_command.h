#pragma once

#include "ftp_client.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace easyftp {

// 本地会话状态
struct Session {
    std::string local_dir;    // 本地工作目录（lcd/lpwd 使用）
};

// 用户命令解析与分发
class CommandDispatcher {
public:
    // 返回 true 继续循环，false 表示用户要求退出
    using Handler = std::function<bool(const std::vector<std::string>& args)>;

    CommandDispatcher(FtpClient& client, Session& session);

    // 解析并执行一行用户输入（支持 ? 作为 help 别名）
    bool dispatch(const std::string& line);

private:
    void register_commands();
    void print_help();

    FtpClient& client_;
    Session& session_;

    std::map<std::string, Handler> commands_;      // 命令名 -> 处理函数
    std::map<std::string, std::string> help_texts_; // 命令名 -> 用法说明
};

} // namespace easyftp
