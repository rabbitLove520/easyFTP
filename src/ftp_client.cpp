#include "ftp_client.h"

#include <cstdlib>
#include <iostream>

#include "utils.h"

namespace easyftp {

namespace {

// 回显服务器响应
void print_reply(const FtpReply& r) {
    for (const auto& l : r.lines) std::cout << l << "\n";
    std::cout << std::flush;
}

// 从数据连接读取全部内容直到对端关闭
void collect_data(FtpSocket& s, std::string& out) {
    char tmp[8192];
    for (;;) {
        ssize_t n = s.recv_raw(tmp, sizeof(tmp));
        if (n <= 0) break;
        out.append(tmp, (size_t)n);
    }
}

} // namespace

FtpClient::~FtpClient() {
    disconnect();
}

bool FtpClient::connect(const std::string& host, uint16_t port) {
    disconnect();
    if (!ctrl_.connect(host, port, 15)) return false;
    ctrl_.set_recv_timeout(120);    // 控制连接读响应超时，防止永久挂死

    host_ = host;
    port_ = port;

    FtpReply welcome = read_reply();
    print_reply(welcome);
    if (welcome.code != 220) {    // 没拿到欢迎语（含连接已死/超时），不留僵尸连接
        ctrl_.close();
        return false;
    }
    return true;
}

void FtpClient::disconnect() {
    if (!ctrl_.is_valid()) return;
    ctrl_.send_string("QUIT\r\n");
    ctrl_.set_recv_timeout(3);      // 服务器不回 221 也不至于卡住
    std::string line;
    while (ctrl_.recv_line(line)) std::cout << line << "\n";
    ctrl_.close();
}

void FtpClient::note_ctrl_down() {
    // 服务器端空闲超时、手机休眠断网等都会走到这里
    std::cout << "与服务器的连接已断开（服务器可能因空闲超时关闭了连接），请重新 open\n"
              << std::flush;
    ctrl_.close();
}

FtpReply FtpClient::send_command(const std::string& cmd) {
    FtpReply reply;
    if (!ctrl_.is_valid()) return reply;
    if (!ctrl_.send_string(cmd + "\r\n")) {
        note_ctrl_down();
        return reply;
    }
    reply = read_reply();
    if (reply.code == 0) {        // 发出去但没读到任何响应：断开或超时
        note_ctrl_down();
        return FtpReply{};
    }
    print_reply(reply);
    if (reply.code == 421)        // RFC 959：服务器声明关闭控制连接
        note_ctrl_down();
    return reply;
}

FtpReply FtpClient::read_reply() {
    FtpReply reply;
    std::string line;
    if (!ctrl_.recv_line(line) || line.size() < 3) return reply;

    reply.code = std::atoi(line.substr(0, 3).c_str());
    if (reply.code <= 0) return reply;
    reply.lines.push_back(line);

    // 首行 "xxx-" 表示多行响应，直到以 "xxx " 开头的行结束
    bool multiline = (line.size() > 3 && line[3] == '-');
    const std::string code_str = line.substr(0, 3);
    while (multiline) {
        if (!ctrl_.recv_line(line)) break;
        reply.lines.push_back(line);
        if (line.size() >= 3 && line.compare(0, 3, code_str) == 0 &&
            (line.size() == 3 || line[3] == ' '))
            multiline = false;
    }
    return reply;
}

bool FtpClient::login(const std::string& user, const std::string& pass) {
    FtpReply r = send_command("USER " + user);
    if (r.code == 230 || r.code == 202) return true;    // 无需密码
    if (r.code != 331) return false;

    r = send_command("PASS " + pass);
    return r.code == 230 || r.code == 202;
}

std::string FtpClient::pwd() {
    FtpReply r = send_command("PWD");
    if (!r.is_positive() || r.lines.empty()) return "";
    const std::string& l = r.lines.back();
    size_t b = l.find('"');
    size_t e = l.rfind('"');
    if (b == std::string::npos || e <= b) return "";
    return l.substr(b + 1, e - b - 1);
}

bool FtpClient::cd(const std::string& path) {
    return send_command("CWD " + path).is_positive();
}

bool FtpClient::mkdir(const std::string& dir) {
    return send_command("MKD " + dir).is_positive();
}

bool FtpClient::rmdir(const std::string& dir) {
    return send_command("RMD " + dir).is_positive();
}

bool FtpClient::remove_file(const std::string& file) {
    return send_command("DELE " + file).is_positive();
}

bool FtpClient::list(std::vector<std::string>& out, const std::string& path) {
    return collect_listing(path.empty() ? "LIST" : "LIST " + path, out);
}

bool FtpClient::name_list(std::vector<std::string>& out) {
    return collect_listing("NLST", out);
}

bool FtpClient::collect_listing(const std::string& cmd, std::vector<std::string>& out) {
    out.clear();
    FtpSocket data;
    if (!open_data_connection(data)) return false;

    FtpReply r = send_command(cmd);
    if (!r.is_preliminary()) {          // 期望 125/150
        data.close();
        return false;
    }

    std::string buf;
    collect_data(data, buf);
    data.close();
    finish_transfer();                  // 读 226

    for (const std::string& l : split(buf, '\n')) {
        std::string t = trim(l);
        if (!t.empty()) out.push_back(t);
    }
    return true;
}

bool FtpClient::raw(const std::string& cmd) {
    return send_command(cmd).is_positive();
}

bool FtpClient::open_data_connection(FtpSocket& data_sock) {
    if (!apply_transfer_mode()) return false;    // 失败原因已由 send_command 报告

    FtpReply r = send_command("PASV");
    if (r.code == 0) return false;               // 连接已断开，已报告
    if (r.code != 227 || r.lines.empty()) {
        std::cout << "服务器拒绝被动模式 (PASV " << r.code << ")\n";
        return false;
    }

    // 解析 "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)"
    const std::string& l = r.lines.back();
    size_t b = l.find('(');
    size_t e = l.find(')', b == std::string::npos ? 0 : b);
    if (b == std::string::npos || e == std::string::npos) return false;

    std::vector<std::string> parts = split(l.substr(b + 1, e - b - 1), ',');
    if (parts.size() != 6) return false;

    std::string host = parts[0] + "." + parts[1] + "." + parts[2] + "." + parts[3];
    int p1 = std::atoi(parts[4].c_str());
    int p2 = std::atoi(parts[5].c_str());
    uint16_t port = (uint16_t)(p1 * 256 + p2);

    if (!data_sock.connect(host, port, 15)) {
        std::cout << "连接数据端口 " << host << ":" << port << " 失败\n";
        return false;
    }
    data_sock.set_recv_timeout(300);    // 传输期间 5 分钟无数据则判定异常
    return true;
}

bool FtpClient::finish_transfer() {
    FtpReply r = read_reply();
    if (r.code == 0) {            // 226 一直没来：连接断开或超时
        note_ctrl_down();
        return false;
    }
    print_reply(r);
    return r.is_positive();
}

bool FtpClient::apply_transfer_mode() {
    FtpReply r = send_command(mode_ == TransferMode::BINARY ? "TYPE I" : "TYPE A");
    return r.is_positive();
}

void FtpClient::set_transfer_mode(TransferMode m) {
    mode_ = m;
    if (is_connected()) apply_transfer_mode();
}

} // namespace easyftp
