// 入口装配（B4）：只做"解析参数 → 构造服务 → init/start → 等待退出信号 → stop"。
// HTTP 服务封装见 services/http_server（白名单与请求处理器由 HttpServer 统一管理）。

// GCC 16 起标准头不再透传提供 <cstdint>，而 cxxopts 2.2.0 自身未包含该头，
// 其接口直接使用 uint8_t 等类型，故须先于 cxxopts 引入（F2 顺序的已登记例外，
// 缺陷与替代方案见 docs/06 附录 A"C++17 工具链适配登记"）。
#include <cstdint>

#include <cxxopts.hpp>

#include <iostream>
#include <string>

#include "common/app_config.h"
#include "common/logger.h"
#include "common/shutdown.h"
#include "common/util.h"
#include "csv_store/csv_store.h"
#include "http_server/http_server.h"

int main(int argc, char** argv) {
    collector::AppOptions opts;

    // ---- 命令行解析（cxxopts，硬性约束 13）：三级优先级中最高 ----
    try {
        cxxopts::Options options("tagledger",
                                 "Industrial tag ledger (point table) management HTTP service");
        options.add_options()
            ("port", "HTTP listen port (default 8080)", cxxopts::value<int>())
            ("data-dir", "data directory (default data)",
             cxxopts::value<std::string>())
            ("web-dir", "web assets directory (default web)",
             cxxopts::value<std::string>())
            ("config-dir", "config directory (default config)",
             cxxopts::value<std::string>())
            ("help", "print this help");
        options.allow_unrecognised_options();
        auto result = options.parse(argc, argv);
        if (result.count("help")) {
            std::cout << options.help();
            return 0;
        }
        if (result.count("port")) {
            opts.port = result["port"].as<int>();
            opts.cli_port = true;
        }
        if (result.count("data-dir")) {
            opts.data_dir = result["data-dir"].as<std::string>();
            opts.cli_data_dir = true;
        }
        if (result.count("web-dir")) {
            opts.web_dir = result["web-dir"].as<std::string>();
            opts.cli_web_dir = true;
        }
        if (result.count("config-dir")) {
            opts.config_dir = result["config-dir"].as<std::string>();
            opts.cli_config_dir = true;
        }
        // 未识别参数提示（cxxopts 2.2 对未识别选项静默跳过；仅审计告警，不参与解析）
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a.rfind("--", 0) != 0) continue;
            std::string name = a.substr(2, a.find('=') - 2);
            if (name != "port" && name != "data-dir" && name != "web-dir" &&
                name != "config-dir" && name != "help")
                collector::log_warning("unknown argument ignored: " + a);
        }
    } catch (const std::exception& e) {
        collector::log_error(std::string("invalid command line: ") + e.what());
        return 1;
    }

    // ---- 应用配置文件（toml++，硬性约束 13）：优先级介于命令行与内置默认之间 ----
    // 引导依赖：config_dir 由命令行或内置默认决定，不可经 app.toml 设置。
    // 说明：load_app_config 内部的 WARN/INFO 在 logger 就绪前进入缓冲，init_logger 回放。
    collector::load_app_config(collector::path_join(opts.config_dir, "app.toml"),
                               &opts);

    // ---- 日志系统初始化（spdlog，白名单第 7 库）：等级/异步队列/轮转策略来自 [log] 段 ----
    if (!collector::init_logger(opts.log)) {
        // 初始化失败降级：本进程日志丢弃（不影响服务可用性），仅 stderr 提示一次
        std::cerr << "[ERROR] logger init failed; logging disabled" << std::endl;
    }

    collector::log_info("tagledger v1.1.0 starting (C++17), port=" +
                        std::to_string(opts.port));

    // ---- 构造与装配（HttpServer 内部管理白名单与请求处理器）----
    collector::PointStore store(opts.data_dir);
    collector::ShutdownController shutdown_ctl;
    collector::HttpServer http(opts, store, shutdown_ctl);

    // ---- 信号安装（组件就绪之后、监听之前；见 docs/05 第 5.1 节）----
    // 回调在 monitor/请求线程执行，仅记录日志（清理统一在 stop()）。
    shutdown_ctl.set_on_stop([]() {
        collector::log_info("stop requested, beginning graceful shutdown");
    });
    shutdown_ctl.install_handlers();

    // ---- 服务启动：init 装配 + start 监听 ----
    if (!http.init()) {
        collector::log_error("http server init failed");
        return 1;
    }
    http.start();  // 绑定失败时内部置 listen_failed 并触发 request_stop

    // ---- 等待退出信号 ----
    shutdown_ctl.wait();

    // ---- 优雅退出：stop 幂等（停接新连接 → 排空在途请求 → join 监听线程）----
    // 存储层无后台资源：CSV 写入（临时文件+原子替换）已在 stop 前完成；
    // PointStore/HttpServer 按栈序自然析构；shutdown_logger 最后 flush 并停后台线程池。
    http.stop();
    const int sig = shutdown_ctl.last_signal();
    collector::log_info("graceful shutdown complete (signal=" +
                        std::to_string(sig) + ")");
    collector::shutdown_logger();  // 退出顺序最后一步（docs/05 第 5.4 节）
    return http.listen_failed() ? 1 : 0;
}
