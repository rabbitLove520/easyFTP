#pragma once

#include "ftp_client.h"

#include <string>
#include <vector>

namespace easyftp {

// 单文件传输（内部处理 PASV、TYPE、RETR/STOR、进度显示）
bool download_file(FtpClient& client, const std::string& remote_path,
                   const std::string& local_path);
bool upload_file(FtpClient& client, const std::string& local_path,
                 const std::string& remote_path);

// 多文件传输：files 为已展开的文件名列表，返回成功个数
int download_files(FtpClient& client, const std::vector<std::string>& files);
int upload_files(FtpClient& client, const std::vector<std::string>& files);

} // namespace easyftp
