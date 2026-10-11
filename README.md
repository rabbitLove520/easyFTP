# easyFTP

纯命令行的 FTP 客户端，Linux 平台，C++17 实现，除 GNU readline 外无其他第三方依赖。

## 功能特性

- 完整 FTP 命令交互：连接登录、目录浏览/管理、文件上传下载、批量传输
- 被动模式（PASV）数据连接，支持 IPv4 / IPv6
- ASCII / BINARY 传输模式切换（ASCII 模式自动做 CRLF ↔ LF 行尾转换）
- 通配符批量传输：`mget *.txt`、`mput *.log`
- 实时进度显示（单行刷新）+ 传输速率统计
- Socket 保活稳定：TCP KeepAlive、非阻塞连接超时、收发超时、SIGPIPE 防护
- 行编辑与命令历史：上/下箭头翻阅历史命令，左右箭头/Home/End 行内编辑，Ctrl+R 搜索历史（基于 GNU readline；管道输入时自动退化）

## 编译

依赖：CMake ≥ 3.10、支持 C++17 的 g++ / clang++、libreadline-dev（Debian/Ubuntu：`sudo apt install libreadline-dev`）

```bash
cmake -S . -B build
cmake --build build
```

生成的可执行文件为 `build/easyftp`。

## 运行

```bash
# 方式一：启动后用 open 命令连接
./build/easyftp

# 方式二：启动时直接指定服务器（可选端口，默认 21）
./build/easyftp ftp.example.com
./build/easyftp 192.168.1.10 2121
```

启动后进入交互式提示符 `ftp>`，连接成功后变为 `ftp(服务器地址)>`。

## 命令列表

| 命令 | 用法 | 说明 |
|---|---|---|
| `open` | `open <host> [port]` | 连接并登录 FTP 服务器 |
| `help` / `?` | `help` | 显示帮助信息 |
| `pwd` | `pwd` | 显示远程工作目录 |
| `lpwd` | `lpwd` | 显示本地工作目录 |
| `cd` | `cd <远程目录>` | 切换远程工作目录 |
| `lcd` | `lcd <本地目录>` | 切换本地工作目录 |
| `ls` | `ls [远程路径]` | 列出远程目录内容 |
| `lls` | `lls [通配符]` | 列出本地目录内容 |
| `mkdir` | `mkdir <远程目录>` | 在远程创建目录 |
| `rmdir` | `rmdir <远程目录>` | 删除远程目录 |
| `delete` | `delete <远程文件>` | 删除远程文件 |
| `get` | `get <远程文件> [本地名]` | 下载文件 |
| `mget` | `mget <通配符...>` | 批量下载远程文件 |
| `put` | `put <本地文件> [远程名]` | 上传文件 |
| `mput` | `mput <通配符...>` | 批量上传本地文件 |
| `ascii` | `ascii` | 传输模式切换为 ASCII |
| `binary` | `binary` | 传输模式切换为 BINARY（默认） |
| `exit` / `quit` | `exit` | 断开并退出程序（Ctrl+D 亦可） |

`open` 连接后依次提示输入用户名（直接回车默认 `anonymous`）和密码（输入不回显）。

## 使用示例

```
$ ./build/easyftp 127.0.0.1 22121
easyFTP v1.0 - 命令行 FTP 客户端（输入 help 查看帮助）
220 mini ftpd ready
用户名 (默认 anonymous): anonymous
密码:
230 logged in
已登录 127.0.0.1
ftp(127.0.0.1)> pwd
257 "/"
/
ftp(127.0.0.1)> binary
200 type ok
传输模式: BINARY
ftp(127.0.0.1)> put big.bin
227 Entering Passive Mode (127,0,0,1,220,117)
150 ready to receive
226 transfer complete
完成 big.bin: 3.00 MB, 平均 73.71 MB/s
ftp(127.0.0.1)> mget *.txt
227 Entering Passive Mode (127,0,0,1,218,89)
150 opening data connection
226 transfer complete
完成 remote.txt: 25 B, 平均 597 B/s
mget 完成: 1/1
ftp(127.0.0.1)> exit
221 bye
再见
```

## 项目结构

```
easyFTP/
├── CMakeLists.txt
├── include/
│   ├── ftp_socket.h      # Socket 层：连接、保活、超时收发
│   ├── ftp_client.h      # 协议层：命令发送、响应解析、登录、PASV
│   ├── ftp_transfer.h    # 传输层：上传下载、进度、ASCII 转换
│   ├── ftp_command.h     # 命令层：命令表与会话状态
│   ├── line_editor.h     # 行编辑：readline 集成与历史记录
│   └── utils.h           # 工具：路径、通配符、字符串处理
└── src/
    ├── main.cpp          # 入口：信号处理、REPL 循环
    ├── ftp_socket.cpp
    ├── ftp_client.cpp
    ├── ftp_transfer.cpp
    ├── ftp_command.cpp
    ├── line_editor.cpp
    └── utils.cpp
```

## 稳定性设计

- **TCP 保活**：`SO_KEEPALIVE` + 空闲 30s 开始探测、每 10s 一次、3 次无响应判定断链
- **连接超时**：非阻塞 connect + poll，15 秒超时，避免网络不通时卡死
- **分级超时**：控制连接收发 120s，数据连接 300s
- **SIGPIPE 防护**：`MSG_NOSIGNAL` 发送 + 忽略 SIGPIPE 信号双保险，对端异常关闭不会导致进程崩溃
- **完整协议处理**：多行响应（`xxx-` 前缀）解析、PASV 地址端口解析、传输后 226 确认
