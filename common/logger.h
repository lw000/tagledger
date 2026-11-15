#pragma once
#include <string>

namespace collector {

// 日志配置（来源：app.toml [log] 段，缺省键用内置默认值）。
struct LogConfig {
    std::string level = "info";      // trace/debug/info/warning/error/critical/off
    bool async = true;               // 异步模式（后台线程写盘，队列满时阻塞）
    int queue_size = 8192;           // 异步队列长度（条）
    int worker_threads = 1;          // 异步后台线程数
    std::string file_path = "logs/tagledger.log";  // 日志文件（轮转基名）
    int max_file_mb = 10;            // 单文件上限（MB，超出轮转）
    int max_files = 100;             // 最多保留的日志文件数（含当前文件）
    bool console = true;             // 是否同步输出到 stdout（重定向/后台运行时可关）
};

// 初始化日志系统：创建轮转文件 sink（10M×100 保留策略，见 LogConfig）+ stdout sink，
// 异步模式时建立后台线程池。成功后回放初始化前的缓冲日志（配置加载阶段产生）。
// 可能失败（建目录/打开文件）：失败返回 false 且本进程日志降级为丢弃（不抛异常，C6）。
// 全生命周期仅允许成功一次：重复调用返回 false。
bool init_logger(const LogConfig& cfg);

// 停止日志系统：flush 落盘并释放后台线程池；幂等（未初始化/重复调用无副作用，noexcept）。
// 退出顺序中置于最后一步（docs/05 第 5.4 节）。
void shutdown_logger() noexcept;

// 分级日志入口（spdlog/fmt 语法由实现内部支持；未初始化时进入缓冲或丢弃）。
void log_debug(const std::string& msg);
void log_info(const std::string& msg);
void log_warning(const std::string& msg);
void log_error(const std::string& msg);

}  // namespace collector
