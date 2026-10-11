#include "ftp_transfer.h"

#include <sys/stat.h>

#include <chrono>
#include <cstdio>
#include <iostream>

#include "utils.h"

namespace easyftp {

namespace {

using Clock = std::chrono::steady_clock;

// 单行进度显示（\r 覆盖），每 100ms 刷新一次
class Progress {
public:
    Progress(const char* tag, std::string name)
        : tag_(tag), name_(std::move(name)), t0_(Clock::now()), last_(t0_) {}

    void update(uint64_t bytes) {
        auto now = Clock::now();
        if (now - last_ < std::chrono::milliseconds(100)) return;
        last_ = now;
        std::cout << "\r" << tag_ << " " << name_ << ": " << human_size(bytes)
                  << "          " << std::flush;
    }

    // 传输结束：换行并打印总计与平均速率
    void finish(uint64_t bytes, bool ok) {
        double secs = std::chrono::duration<double>(Clock::now() - t0_).count();
        std::cout << "\r" << (ok ? "完成" : "失败") << " " << name_ << ": "
                  << human_size(bytes);
        if (secs > 0.001)
            std::cout << ", 平均 " << human_size((uint64_t)(bytes / secs)) << "/s";
        std::cout << "          \n" << std::flush;
    }

private:
    const char* tag_;
    std::string name_;
    Clock::time_point t0_, last_;
};

// ASCII 模式下载方向：CRLF -> LF（处理跨块的孤立 \r）
struct AsciiDecoder {
    bool pending_cr = false;
    std::string out;

    const std::string& feed(const char* buf, size_t len) {
        out.clear();
        out.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            char c = buf[i];
            if (pending_cr) {
                if (c != '\n') out += '\r';
                pending_cr = false;
            }
            if (c == '\r') pending_cr = true;
            else out += c;
        }
        return out;
    }

    // 上一次 feed() 的 out 已随循环写入文件，这里必须先清空，
    // 否则结尾无孤立 \r 时会把上一块内容重复写一遍
    void flush_eof() {
        out.clear();
        if (pending_cr) {
            out += '\r';
            pending_cr = false;
        }
    }
};

// ASCII 模式上传方向：LF -> CRLF（有状态：跳过已是 CRLF 的行尾，
// 并正确处理 \r 落在上一块末尾、\n 落在下一块开头的跨块情况）
struct AsciiEncoder {
    bool last_cr = false;

    std::string encode(const char* buf, size_t len) {
        std::string out;
        out.reserve(len + len / 8);
        for (size_t i = 0; i < len; ++i) {
            char c = buf[i];
            if (c == '\n' && !last_cr) out += '\r';
            out += c;
            last_cr = (c == '\r');
        }
        return out;
    }
};

} // namespace

bool download_file(FtpClient& client, const std::string& remote_path,
                   const std::string& local_path) {
    std::string local = local_path.empty() ? basename_of(remote_path) : local_path;

    FtpSocket data;
    if (!client.open_data_connection(data)) return false;    // 原因已在内部打印

    FtpReply r = client.send_command("RETR " + remote_path);
    if (!r.is_preliminary()) {    // 期望 125/150
        data.close();
        return false;
    }

    FILE* f = std::fopen(local.c_str(), "wb");
    if (!f) {
        std::cout << "本地: 无法创建文件 " << local << "\n";
        data.close();
        return false;
    }

    Progress prog("下载", local);
    uint64_t total = 0;
    bool ok = true;
    char buf[8192];
    AsciiDecoder dec;
    const bool ascii = (client.transfer_mode() == TransferMode::ASCII);

    for (;;) {
        ssize_t n = data.recv_raw(buf, sizeof(buf));
        if (n < 0) {
            ok = false;
            break;
        }
        if (n == 0) break;    // 对端关闭，传输结束

        if (ascii) {
            const std::string& s = dec.feed(buf, (size_t)n);
            if (!s.empty() && std::fwrite(s.data(), 1, s.size(), f) != s.size()) {
                ok = false;
                break;
            }
        } else {
            if (std::fwrite(buf, 1, (size_t)n, f) != (size_t)n) {
                ok = false;
                break;
            }
        }
        total += (uint64_t)n;
        prog.update(total);
    }
    if (ok && ascii) {
        dec.flush_eof();
        if (!dec.out.empty() && std::fwrite(dec.out.data(), 1, dec.out.size(), f) != dec.out.size())
            ok = false;
    }

    std::fclose(f);
    data.close();
    // 读取 226 传输完成响应
    if (!client.finish_transfer()) ok = false;

    prog.finish(total, ok);
    return ok;
}

bool upload_file(FtpClient& client, const std::string& local_path,
                 const std::string& remote_path) {
    struct stat st;
    if (stat(local_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        std::cout << "本地: 文件不存在 " << local_path << "\n";
        return false;
    }
    std::string remote = remote_path.empty() ? basename_of(local_path) : remote_path;

    FtpSocket data;
    if (!client.open_data_connection(data)) return false;    // 原因已在内部打印

    FtpReply r = client.send_command("STOR " + remote);
    if (!r.is_preliminary()) {
        data.close();
        return false;
    }

    FILE* f = std::fopen(local_path.c_str(), "rb");
    if (!f) {
        std::cout << "本地: 无法读取文件 " << local_path << "\n";
        data.close();
        return false;
    }

    Progress prog("上传", local_path);
    uint64_t total = 0;
    bool ok = true;
    char buf[8192];
    const bool ascii = (client.transfer_mode() == TransferMode::ASCII);
    AsciiEncoder enc;

    for (;;) {
        size_t n = std::fread(buf, 1, sizeof(buf), f);
        if (n == 0) break;

        if (ascii) {
            std::string s = enc.encode(buf, n);
            if (!s.empty() && !data.send_all(s.data(), s.size())) {
                ok = false;
                break;
            }
        } else {
            if (!data.send_all(buf, n)) {
                ok = false;
                break;
            }
        }
        total += (uint64_t)n;
        prog.update(total);
    }
    if (std::ferror(f)) ok = false;
    std::fclose(f);
    data.close();
    if (!client.finish_transfer()) ok = false;

    prog.finish(total, ok);
    return ok;
}

int download_files(FtpClient& client, const std::vector<std::string>& files) {
    int ok = 0;
    for (const auto& f : files) {
        if (download_file(client, f, "")) ++ok;
    }
    return ok;
}

int upload_files(FtpClient& client, const std::vector<std::string>& files) {
    int ok = 0;
    for (const auto& f : files) {
        if (upload_file(client, f, "")) ++ok;
    }
    return ok;
}

} // namespace easyftp
