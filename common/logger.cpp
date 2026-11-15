#include "common/logger.h"

// 复用自 third_party/spdlog（白名单第 7 库，选型依据见 docs/06 附录 A）：
// 分级/异步/轮转能力开箱即用，替代自研日志实现（L3 检索后人工确认引入）。
#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace collector {

namespace {

std::mutex g_mutex;                       // 保护 g_logger / g_early / g_init_failed
std::shared_ptr<spdlog::logger> g_logger; // 唯一日志句柄（双 sink：rotating 文件 + stdout）
bool g_init_failed = false;               // 初始化失败后降级：日志直接丢弃，不再缓冲

// 初始化前缓冲（配置加载在 logger 就绪之前，须保留其 WARN/INFO）。
struct EarlyEntry {
    spdlog::level::level_enum level;
    std::string msg;
};
constexpr size_t kEarlyCap = 100;         // 缓冲上限，超出丢弃（防未初始化场景无限增长）
std::vector<EarlyEntry> g_early;

spdlog::level::level_enum parse_level(const std::string& s) {
    if (s == "trace") return spdlog::level::trace;
    if (s == "debug") return spdlog::level::debug;
    if (s == "info") return spdlog::level::info;
    if (s == "warning" || s == "warn") return spdlog::level::warn;
    if (s == "error") return spdlog::level::err;
    if (s == "critical") return spdlog::level::critical;
    if (s == "off") return spdlog::level::off;
    return spdlog::level::info;  // 未知等级按 info（与 app.toml 其他键的宽容语义一致）
}

void dispatch(spdlog::level::level_enum lv, const std::string& msg) {
    std::shared_ptr<spdlog::logger> l;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_logger) {
            l = g_logger;  // 拷贝句柄后在锁外输出，避免日志调用与 shutdown 长互斥
        } else if (!g_init_failed && g_early.size() < kEarlyCap) {
            g_early.push_back({lv, msg});
        }
    }
    if (l) l->log(lv, msg);
}

}  // namespace

bool init_logger(const LogConfig& cfg) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_logger) return false;  // 重复初始化拒绝（全生命周期一次）

    try {
        // rotating_file_sink 打开失败（目录不可建/无权限）时抛异常 → 统一出口。
        // spdlog 的 max_files 计"轮转旧文件数"（当前文件另计）；需求语义为
        // "含当前文件最多保留 max_files 个"，故传 max_files - 1。
        const int rotated_keep = cfg.max_files > 0 ? cfg.max_files - 1 : 0;
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            cfg.file_path, static_cast<size_t>(cfg.max_file_mb) * 1024 * 1024,
            rotated_keep);
        std::vector<spdlog::sink_ptr> sinks{std::move(file_sink)};
        if (cfg.console) sinks.push_back(std::make_shared<spdlog::sinks::stdout_sink_mt>());

        if (cfg.async) {
            spdlog::init_thread_pool(static_cast<size_t>(cfg.queue_size),
                                     static_cast<size_t>(cfg.worker_threads));
            g_logger = std::make_shared<spdlog::async_logger>(
                "tagledger", sinks.begin(), sinks.end(), spdlog::thread_pool(),
                spdlog::async_overflow_policy::block);
        } else {
            g_logger = std::make_shared<spdlog::logger>("tagledger", sinks.begin(),
                                                        sinks.end());
        }
        g_logger->set_level(parse_level(cfg.level));
        // 与旧 util::log_line 格式对齐：[时间][级别] 消息（docs/05 §5.4 巡检标志依赖）
        g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l] %v");

        // 回放初始化前的缓冲日志
        for (const EarlyEntry& e : g_early) g_logger->log(e.level, e.msg);
        g_early.clear();
        return true;
    } catch (const std::exception&) {
        g_init_failed = true;  // 降级：后续日志丢弃（初始化早期无法用自身日志记录原因）
        g_early.clear();
        spdlog::shutdown();    // 回滚已建线程池等资源（幂等）
        return false;
    }
}

void shutdown_logger() noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_logger) return;
    g_logger->flush();     // 收尾日志落盘（C7）
    g_logger.reset();
    spdlog::shutdown();    // 排空异步队列并停线程池（幂等）
    g_early.clear();
}

void log_debug(const std::string& msg) { dispatch(spdlog::level::debug, msg); }
void log_info(const std::string& msg) { dispatch(spdlog::level::info, msg); }
void log_warning(const std::string& msg) { dispatch(spdlog::level::warn, msg); }
void log_error(const std::string& msg) { dispatch(spdlog::level::err, msg); }

}  // namespace collector
