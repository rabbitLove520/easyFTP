#include "utils.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace easyftp {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string join_path(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    if (dir.back() == '/') return dir + name;
    return dir + "/" + name;
}

std::string basename_of(const std::string& path) {
    size_t p = path.rfind('/');
    if (p == std::string::npos) return path;
    return path.substr(p + 1);
}

bool wildcard_match(const std::string& pattern, const std::string& name) {
    size_t p = 0, n = 0;
    size_t star = std::string::npos, mark = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[n])) {
            ++p; ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = n;
        } else if (star != std::string::npos) {
            p = star + 1;
            n = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

static bool has_wildcard(const std::string& s) {
    return s.find('*') != std::string::npos || s.find('?') != std::string::npos;
}

std::vector<std::string> expand_local_pattern(const std::string& pattern) {
    std::vector<std::string> out;
    if (pattern.empty()) return out;

    // 拆出目录部分与文件名部分
    std::string dir = ".", file = pattern;
    size_t slash = pattern.rfind('/');
    if (slash != std::string::npos) {
        dir = pattern.substr(0, slash);
        if (dir.empty()) dir = "/";
        file = pattern.substr(slash + 1);
    }

    if (!has_wildcard(file)) {
        // 无通配符：直接检查存在性
        struct stat st;
        std::string full = (dir == ".") ? file : join_path(dir, file);
        if (stat(full.c_str(), &st) == 0) out.push_back(full);
        return out;
    }

    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        if (wildcard_match(file, name))
            out.push_back((dir == ".") ? name : join_path(dir, name));
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool parse_host_port(const std::string& input, std::string& host, uint16_t& port) {
    if (input.empty()) return false;
    port = 21;
    size_t c = input.rfind(':');
    if (c == std::string::npos) {
        host = input;
        return true;
    }
    int p = std::atoi(input.substr(c + 1).c_str());
    if (p <= 0 || p > 65535) return false;
    host = input.substr(0, c);
    port = (uint16_t)p;
    return !host.empty();
}

std::string human_size(uint64_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    char buf[64];
    if (u == 0)
        std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    else
        std::snprintf(buf, sizeof(buf), "%.2f %s", v, units[u]);
    return buf;
}

} // namespace easyftp
