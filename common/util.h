#pragma once
#include <string>
#include <vector>

namespace collector {

// 请求路径/文件名过滤：仅 [A-Za-z0-9_-]，1~64 位。
bool is_safe_name(const std::string& s);

// ---------- 文件操作层（std::filesystem 适配） ----------
// 说明：历史原因（MinGW GCC 8.x libstdc++ 的 std::filesystem 编译期缺陷）曾以
// Win32/POSIX 原生 API 实现；2026-11-15 工具链升级至 GCC 16.2 后缺陷消除，
// 内部实现已按人工确认的方案 A 迁移为 std::filesystem（error_code 重载，不抛异常）。
// 函数签名保持不变，作为全项目文件操作的统一入口。

// 用 '/' 拼接目录与文件名
std::string path_join(const std::string& dir, const std::string& name);

bool file_exists(const std::string& path);
bool remove_file(const std::string& path);
bool make_dirs(const std::string& path);  // 递归建目录
// 原子替换式重命名：目标存在则覆盖；失败返回 false
bool rename_replace(const std::string& from, const std::string& to);
// 列出目录下扩展名为 .csv 的普通文件（仅文件名，不含路径）
std::vector<std::string> list_csv_files(const std::string& dir);

}  // namespace collector
