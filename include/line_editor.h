#pragma once
// 行编辑模块：基于 GNU readline，提供命令历史与行内编辑。
// 标准输入非终端（管道/重定向）时，退化为普通 getline。

#include <string>

namespace easyftp {

// 读取一行输入。
// 返回 false 表示 EOF（Ctrl+D 或输入流结束）。
// prompt 仅在交互式终端模式下作为行首提示符显示。
bool read_line(const std::string& prompt, std::string& out);

// 将非空行加入命令历史（上/下箭头可回溯）。
// 库未加载或非交互模式下静默忽略。
void add_history(const std::string& line);

}  // namespace easyftp
