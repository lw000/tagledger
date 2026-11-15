#include "common/logger.h"

#include "common/util.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>

using collector::init_logger;
using collector::LogConfig;
using collector::log_info;
using collector::log_warning;
using collector::make_dirs;
using collector::path_join;
using collector::remove_file;
using collector::shutdown_logger;

namespace {
// 每个用例独立临时目录；析构兜底 shutdown（用例内 REQUIRE 提前退出也不污染后续用例）。
struct LogFixture {
    std::string dir;
    std::string file;

    explicit LogFixture(std::string name)
        : dir(path_join("build_test_data", name)),
          file(path_join(dir, "test.log")) {
        make_dirs(dir);
        shutdown_logger();
        // 清理可能的轮转残留（spdlog 命名：test.N.log，索引在扩展名前）
        for (int i = 1; i <= 3; ++i) remove_file(rotated(i));
        remove_file(file);
    }
    ~LogFixture() {
        shutdown_logger();
        for (int i = 1; i <= 3; ++i) remove_file(rotated(i));
        remove_file(file);
    }

    // 轮转文件路径：spdlog rotating_file_sink 将索引插在扩展名之前（test.1.log）
    std::string rotated(int i) const {
        return file.substr(0, file.size() - 4) + "." + std::to_string(i) + ".log";
    }

    static std::string read_all(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    }
};
}  // namespace

TEST_CASE("logger/init writes file with buffered pre-init replay",
          "[unit][logger]") {
    LogFixture fx("log_replay");

    // 初始化前的日志进入缓冲，init 后回放进文件
    log_warning("pre-init-buffered-line");
    LogConfig cfg;
    cfg.async = false;  // 同步模式便于确定性断言
    cfg.file_path = fx.file;
    REQUIRE(init_logger(cfg));
    log_info("post-init-line");
    shutdown_logger();

    const std::string content = LogFixture::read_all(fx.file);
    REQUIRE(content.find("pre-init-buffered-line") != std::string::npos);
    REQUIRE(content.find("post-init-line") != std::string::npos);
}

TEST_CASE("logger/double init rejected", "[unit][logger]") {
    LogFixture fx("log_double_init");
    LogConfig cfg;
    cfg.async = false;
    cfg.file_path = fx.file;
    REQUIRE(init_logger(cfg));
    REQUIRE_FALSE(init_logger(cfg));  // 全生命周期仅允许成功一次
}

TEST_CASE("logger/shutdown idempotent and log after shutdown is safe",
          "[unit][logger]") {
    LogFixture fx("log_shutdown");
    LogConfig cfg;
    cfg.async = false;
    cfg.file_path = fx.file;
    REQUIRE(init_logger(cfg));
    shutdown_logger();
    shutdown_logger();  // 幂等：无副作用
    log_info("after-shutdown-line");  // 安全：不崩溃、不写文件
    REQUIRE(LogFixture::read_all(fx.file).find("after-shutdown-line") ==
            std::string::npos);
}

TEST_CASE("logger/level filters below threshold", "[unit][logger]") {
    LogFixture fx("log_level");
    LogConfig cfg;
    cfg.async = false;
    cfg.file_path = fx.file;
    cfg.level = "warning";
    REQUIRE(init_logger(cfg));
    log_info("filtered-info-line");
    log_warning("kept-warning-line");
    shutdown_logger();

    const std::string content = LogFixture::read_all(fx.file);
    REQUIRE(content.find("filtered-info-line") == std::string::npos);
    REQUIRE(content.find("kept-warning-line") != std::string::npos);
}

TEST_CASE("logger/async mode writes after shutdown flush", "[unit][logger]") {
    LogFixture fx("log_async");
    LogConfig cfg;
    cfg.async = true;  // 异步路径覆盖
    cfg.queue_size = 1024;
    cfg.file_path = fx.file;
    REQUIRE(init_logger(cfg));
    log_info("async-flush-line");
    shutdown_logger();  // flush + 线程池排空

    REQUIRE(LogFixture::read_all(fx.file).find("async-flush-line") !=
            std::string::npos);
}

TEST_CASE("logger/rotation keeps bounded number of capped files",
          "[unit][logger]") {
    LogFixture fx("log_rotate");
    LogConfig cfg;
    cfg.async = false;
    cfg.file_path = fx.file;
    cfg.max_file_mb = 1;  // 测试用小阈值触发轮转（语义与 10M 一致）
    cfg.max_files = 3;
    cfg.console = false;  // 避免刷屏
    REQUIRE(init_logger(cfg));
    // 每行约 1KB，写 ~3MB 触发至少 2 次轮转
    const std::string line(1000, 'x');
    for (int i = 0; i < 3000; ++i) log_info("r" + std::to_string(i) + line);
    shutdown_logger();

    REQUIRE(LogFixture::read_all(fx.file).size() > 0);
    REQUIRE(LogFixture::read_all(fx.rotated(1)).size() > 0);
    REQUIRE(LogFixture::read_all(fx.rotated(2)).size() > 0);
    // max_files=3（含当前文件）之外的轮转文件不应存在
    std::ifstream third(fx.rotated(3), std::ios::binary);
    REQUIRE_FALSE(third.good());
}
