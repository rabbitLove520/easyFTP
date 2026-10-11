#include <unistd.h>

#include <csignal>
#include <climits>
#include <iostream>
#include <string>

#include "ftp_client.h"
#include "ftp_command.h"
#include "line_editor.h"

namespace {

// 提示符：未连接 "ftp> "，已连接 "ftp(host)> "
std::string make_prompt(const easyftp::FtpClient& client) {
    if (client.is_connected()) return "ftp(" + client.host() + ")> ";
    return "ftp> ";
}

} // namespace

int main(int argc, char* argv[]) {
    // 对端异常关闭时 send 返回 EPIPE 而不是杀死进程
    std::signal(SIGPIPE, SIG_IGN);

    easyftp::FtpClient client;
    easyftp::Session session;

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) session.local_dir = cwd;

    easyftp::CommandDispatcher dispatcher(client, session);

    std::cout << "easyFTP " << "v1.0 - 命令行 FTP 客户端（输入 help 查看帮助）\n";

    // 命令行参数: easyftp [host [port]]，相当于启动后自动 open
    if (argc >= 2) {
        std::string cmd = std::string("open ") + argv[1];
        if (argc >= 3) cmd += std::string(" ") + argv[2];
        dispatcher.dispatch(cmd);
    }

    for (;;) {
        std::string line;
        // 终端下由 readline 提供行编辑与历史（上/下箭头），非终端自动退化
        if (!easyftp::read_line(make_prompt(client), line)) {    // Ctrl+D
            std::cout << "\n";
            break;
        }
        easyftp::add_history(line);
        if (!dispatcher.dispatch(line)) break;
    }

    client.disconnect();
    return 0;
}
