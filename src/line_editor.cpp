#include "line_editor.h"

#include <readline/history.h>
#include <readline/readline.h>

#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace easyftp {

bool read_line(const std::string& prompt, std::string& out) {
    // 交互式终端 → readline 提供行编辑 + 历史回溯
    if (isatty(STDIN_FILENO)) {
        char* line = readline(prompt.c_str());
        if (!line) return false;  // EOF（Ctrl+D）
        out.assign(line);
        free(line);
        return true;
    }

    // 退化路径：管道/重定向
    return static_cast<bool>(std::getline(std::cin, out));
}

void add_history(const std::string& line) {
    // 仅非空行进入历史，避免空回车刷屏
    if (!line.empty()) {
        ::add_history(line.c_str());
    }
}

}  // namespace easyftp
