#include "common/util.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace collector {

bool is_safe_name(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

std::string path_join(const std::string& dir, const std::string& name) {
    // 保持字符串拼接与 '/' 分隔符输出（docs/05：路径分隔符统一 '/'）；
    // 若改用 fs::path::append，Windows 下会输出 '\'，改变既有行为。
    if (dir.empty()) return name;
    if (dir.back() == '/' || dir.back() == '\\') return dir + name;
    return dir + "/" + name;
}

bool file_exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec);  // 查询失败按不存在处理，与旧 GetFileAttributesA 语义一致
}

bool remove_file(const std::string& path) {
    std::error_code ec;
    return fs::remove(path, ec);
}

bool make_dirs(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    fs::create_directories(path, ec);  // 已存在为目录时不报错（返回 false 也视为成功）
    return !ec;
}

bool rename_replace(const std::string& from, const std::string& to) {
    // fs::rename 在 POSIX 为原子替换；Windows 下按 REPLACE_EXISTING 语义覆盖。
    // 失败时回退为删除目标后重命名（保留既有兜底）。
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) return true;
    remove_file(to);
    fs::rename(from, to, ec);
    return !ec;
}

std::vector<std::string> list_csv_files(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (ec) return out;
    for (const fs::directory_entry& e : it) {
        std::error_code entry_ec;
        if (!e.is_regular_file(entry_ec) || entry_ec) continue;
        const std::string ext = e.path().extension().string();
        // 大小写不敏感比较 ".csv"，保持旧 _findfirst("*.csv") 的 Windows 语义
        const bool is_csv = ext.size() == 4 && ext[0] == '.' &&
                            (ext[1] == 'c' || ext[1] == 'C') &&
                            (ext[2] == 's' || ext[2] == 'S') &&
                            (ext[3] == 'v' || ext[3] == 'V');
        if (is_csv) out.push_back(e.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace collector
