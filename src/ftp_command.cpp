#include "ftp_command.h"

#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>

#include "ftp_transfer.h"
#include "utils.h"

namespace easyftp {

namespace {

using Args = const std::vector<std::string>&;

// 密码输入（终端时关闭回显）
std::string read_password(const char* prompt) {
    std::cout << prompt << std::flush;
    bool tty = isatty(STDIN_FILENO);
    termios oldt{};
    if (tty) {
        tcgetattr(STDIN_FILENO, &oldt);
        termios newt = oldt;
        newt.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    }
    std::string pass;
    std::getline(std::cin, pass);
    if (tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        std::cout << "\n";
    }
    return pass;
}

bool has_wildcard(const std::string& s) {
    return s.find('*') != std::string::npos || s.find('?') != std::string::npos;
}

} // namespace

CommandDispatcher::CommandDispatcher(FtpClient& client, Session& session)
    : client_(client), session_(session) {
    register_commands();
}

bool CommandDispatcher::dispatch(const std::string& line) {
    std::string t = trim(line);
    if (t.empty()) return true;

    std::vector<std::string> parts = split(t, ' ');
    std::string cmd = parts[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
    parts.erase(parts.begin());

    auto it = commands_.find(cmd);
    if (it == commands_.end()) {
        std::cout << "未知命令: " << cmd << "（输入 help 查看帮助）\n";
        return true;
    }
    return it->second(parts);
}

void CommandDispatcher::register_commands() {
    // 未连接时远程命令给出友好提示
    auto need_conn = [this]() {
        if (!client_.is_connected()) {
            std::cout << "尚未连接服务器，请先执行 open <host> [port]\n";
            return false;
        }
        return true;
    };
    auto usage = [](const std::string& cmd, const std::string& tip) {
        std::cout << "用法: " << cmd << " " << tip << "\n";
    };

    // ---- open ----
    commands_["open"] = [this, usage](Args a) {
        if (a.empty()) { usage("open", "<host> [port]"); return true; }
        if (client_.is_connected()) {
            std::cout << "已连接 " << client_.host() << "，先断开...\n";
            client_.disconnect();
        }
        std::string host;
        uint16_t port = 21;
        if (!parse_host_port(a[0], host, port)) {
            std::cout << "无效的地址: " << a[0] << "\n";
            return true;
        }
        if (a.size() >= 2) {
            int p = std::atoi(a[1].c_str());
            if (p > 0 && p <= 65535) port = (uint16_t)p;
        }
        if (!client_.connect(host, port)) {
            std::cout << "连接 " << host << ":" << port << " 失败\n";
            return true;
        }

        std::string user, pass;
        std::cout << "用户名 (默认 anonymous): " << std::flush;
        if (!std::getline(std::cin, user)) return false;
        user = trim(user);
        if (user.empty()) user = "anonymous";
        pass = read_password("密码: ");
        if (pass.empty() && user == "anonymous") pass = "anonymous@";

        if (!client_.login(user, pass)) {
            std::cout << "登录失败\n";
            client_.disconnect();
        } else {
            std::cout << "已登录 " << host << "\n";
        }
        return true;
    };

    // ---- help / ? ----
    auto help_fn = [this](Args) {
        print_help();
        return true;
    };
    commands_["help"] = help_fn;
    commands_["?"] = help_fn;

    // ---- exit / quit ----
    auto exit_fn = [this](Args) {
        client_.disconnect();
        std::cout << "再见\n";
        return false;
    };
    commands_["exit"] = exit_fn;
    commands_["quit"] = exit_fn;

    // ---- cd / lcd ----
    commands_["cd"] = [this, need_conn, usage](Args a) {
        if (!need_conn()) return true;
        if (a.empty()) { usage("cd", "<远程目录>"); return true; }
        client_.cd(a[0]);    // 响应由 send_command 回显
        return true;
    };
    commands_["lcd"] = [this, usage](Args a) {
        if (a.empty()) { usage("lcd", "<本地目录>"); return true; }
        if (chdir(a[0].c_str()) != 0) {
            std::cout << "lcd: 无法切换到 " << a[0] << "\n";
            return true;
        }
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd))) {
            session_.local_dir = cwd;
            std::cout << "本地目录: " << cwd << "\n";
        }
        return true;
    };

    // ---- ls / lls ----
    commands_["ls"] = [this, need_conn](Args a) {
        if (!need_conn()) return true;
        std::vector<std::string> lines;
        if (client_.list(lines, a.empty() ? "" : a[0])) {
            for (const auto& l : lines) std::cout << l << "\n";
        }
        return true;
    };
    commands_["lls"] = [this](Args a) {
        auto entries = expand_local_pattern(a.empty() ? "*" : a[0]);
        for (const auto& e : entries) {
            struct stat st;
            std::cout << e;
            if (stat(e.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) std::cout << "/";
            std::cout << "\n";
        }
        if (entries.empty()) std::cout << "(空)\n";
        return true;
    };

    // ---- pwd / lpwd ----
    commands_["pwd"] = [this, need_conn](Args) {
        if (!need_conn()) return true;
        std::string p = client_.pwd();
        if (!p.empty()) std::cout << p << "\n";
        return true;
    };
    commands_["lpwd"] = [this](Args) {
        std::cout << session_.local_dir << "\n";
        return true;
    };

    // ---- mkdir / rmdir / delete ----
    commands_["mkdir"] = [this, need_conn, usage](Args a) {
        if (!need_conn()) return true;
        if (a.empty()) { usage("mkdir", "<远程目录>"); return true; }
        client_.mkdir(a[0]);
        return true;
    };
    commands_["rmdir"] = [this, need_conn, usage](Args a) {
        if (!need_conn()) return true;
        if (a.empty()) { usage("rmdir", "<远程目录>"); return true; }
        client_.rmdir(a[0]);
        return true;
    };
    commands_["delete"] = [this, need_conn, usage](Args a) {
        if (!need_conn()) return true;
        if (a.empty()) { usage("delete", "<远程文件>"); return true; }
        client_.remove_file(a[0]);
        return true;
    };

    // ---- get / put ----
    commands_["get"] = [this, need_conn, usage](Args a) {
        if (!need_conn()) return true;
        if (a.empty()) { usage("get", "<远程文件> [本地文件名]"); return true; }
        download_file(client_, a[0], a.size() > 1 ? a[1] : "");
        return true;
    };
    commands_["put"] = [this, need_conn, usage](Args a) {
        if (!need_conn()) return true;
        if (a.empty()) { usage("put", "<本地文件> [远程文件名]"); return true; }
        upload_file(client_, a[0], a.size() > 1 ? a[1] : "");
        return true;
    };

    // ---- mget / mput ----
    commands_["mget"] = [this, need_conn, usage](Args a) {
        if (!need_conn()) return true;
        if (a.empty()) { usage("mget", "<通配符...>"); return true; }
        std::vector<std::string> names;
        if (!client_.name_list(names)) return true;

        // 展开通配符并去重
        std::vector<std::string> targets;
        for (const auto& pat : a) {
            if (!has_wildcard(pat)) {
                targets.push_back(pat);
                continue;
            }
            for (const auto& n : names)
                if (wildcard_match(pat, n)) targets.push_back(n);
        }
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());

        if (targets.empty()) {
            std::cout << "没有匹配的远程文件\n";
            return true;
        }
        int ok = download_files(client_, targets);
        std::cout << "mget 完成: " << ok << "/" << targets.size() << "\n";
        return true;
    };
    commands_["mput"] = [this, need_conn, usage](Args a) {
        if (!need_conn()) return true;
        if (a.empty()) { usage("mput", "<通配符...>"); return true; }

        std::vector<std::string> files;
        for (const auto& pat : a) {
            auto matched = expand_local_pattern(pat);
            if (matched.empty()) {
                std::cout << "本地没有匹配: " << pat << "\n";
                continue;
            }
            for (const auto& m : matched) {
                struct stat st;
                if (stat(m.c_str(), &st) == 0 && S_ISREG(st.st_mode))
                    files.push_back(m);
            }
        }
        if (files.empty()) return true;
        int ok = upload_files(client_, files);
        std::cout << "mput 完成: " << ok << "/" << files.size() << "\n";
        return true;
    };

    // ---- ascii / binary（未连接时也可设置，连接后生效）----
    commands_["ascii"] = [this](Args) {
        client_.set_transfer_mode(TransferMode::ASCII);
        std::cout << "传输模式: ASCII\n";
        return true;
    };
    commands_["binary"] = [this](Args) {
        client_.set_transfer_mode(TransferMode::BINARY);
        std::cout << "传输模式: BINARY\n";
        return true;
    };

    // ---- 帮助文本 ----
    help_texts_["?"]          = "?                             显示帮助信息";
    help_texts_["ascii"]      = "ascii                         传输模式切换为 ASCII";
    help_texts_["binary"]     = "binary                        传输模式切换为 BINARY（默认）";
    help_texts_["cd"]         = "cd <远程目录>                 切换远程工作目录";
    help_texts_["delete"]     = "delete <远程文件>             删除远程文件";
    help_texts_["exit"]       = "exit | quit                   断开并退出程序";
    help_texts_["get"]        = "get <远程文件> [本地名]       下载文件";
    help_texts_["help"]       = "help                          显示帮助信息";
    help_texts_["lcd"]        = "lcd <本地目录>                切换本地工作目录";
    help_texts_["lls"]        = "lls [通配符]                  列出本地目录内容";
    help_texts_["lpwd"]       = "lpwd                          显示本地工作目录";
    help_texts_["ls"]         = "ls [远程路径]                 列出远程目录内容";
    help_texts_["mget"]       = "mget <通配符...>              批量下载远程文件";
    help_texts_["mkdir"]      = "mkdir <远程目录>              在远程创建目录";
    help_texts_["mput"]       = "mput <通配符...>              批量上传本地文件";
    help_texts_["open"]       = "open <host> [port]            连接并登录 FTP 服务器";
    help_texts_["put"]        = "put <本地文件> [远程名]       上传文件";
    help_texts_["pwd"]        = "pwd                           显示远程工作目录";
    help_texts_["quit"]       = "quit                          断开并退出程序";
    help_texts_["rmdir"]      = "rmdir <远程目录>              删除远程目录";
}

void CommandDispatcher::print_help() {
    std::cout << "可用命令:\n";
    for (const auto& kv : help_texts_)
        std::cout << "  " << kv.second << "\n";
    std::cout << std::flush;
}

} // namespace easyftp
