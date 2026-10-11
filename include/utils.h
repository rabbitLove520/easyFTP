#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace easyftp {

// 去除首尾空白
std::string trim(const std::string& s);

// 按分隔符切分（忽略连续分隔符）
std::vector<std::string> split(const std::string& s, char delim);

// 拼接目录与文件名
std::string join_path(const std::string& dir, const std::string& name);

// 取路径最后一段
std::string basename_of(const std::string& path);

// 简单通配符匹配（支持 * 和 ?）
bool wildcard_match(const std::string& pattern, const std::string& name);

// 展开本地目录中匹配模式的文件（mput 用），pattern 不含通配符时原样返回
std::vector<std::string> expand_local_pattern(const std::string& pattern);

// 解析 "host" 或 "host:port"，默认端口 21
bool parse_host_port(const std::string& input, std::string& host, uint16_t& port);

// 人类可读的文件大小（进度显示用）
std::string human_size(uint64_t bytes);

} // namespace easyftp
