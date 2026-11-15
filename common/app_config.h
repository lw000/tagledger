#pragma once
// 应用启动配置：命令行（cxxopts）→ app.toml（toml++）→ 内置默认，三级优先级。
// 本头文件不引入 toml++/cxxopts（重量级单头库只出现在对应实现文件中）。
#include <string>

#include "common/logger.h"

namespace collector {

struct AppOptions {
    int port = 8080;
    std::string data_dir = "data";
    std::string web_dir = "web";
    std::string config_dir = "config";
    LogConfig log;  // [log] 段：日志等级/异步队列/保留策略（键缺省用 LogConfig 内置默认）

    // 命令行显式设置的键：优先级最高，app.toml 不覆盖。
    // 引导依赖：config_dir 用于定位 app.toml，不可经配置文件设置（仅命令行/默认值）。
    bool cli_port = false;
    bool cli_data_dir = false;
    bool cli_web_dir = false;
    bool cli_config_dir = false;
};

// 读取 app.toml 并合并进 opts：文件不存在返回 false（保持现有值，零配置可启动）；
// 仅覆盖未被命令行显式设置的键；解析失败记 WARN 并忽略（不视为致命错误）。
// 合法键：port(int)、data_dir/web_dir(string)、[log] 段（level(string)、async(bool)、
// queue_size/max_file_mb/max_files(int)、file_path(string)）。
bool load_app_config(const std::string& toml_path, AppOptions* out);

}  // namespace collector
